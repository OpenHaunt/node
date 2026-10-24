/*
 * ws2812: an addressable strip on an RMT pin, in one of two modes (OpenHaunt/node
 * doc 14).
 *
 * `effect`: the node draws every pixel from nine ports — color, brightness, color
 * 2, effect, speed, size, reverse, mirror, segments — in that local order:
 *
 *   { "driver": "ws2812", "ports": [6,...,14], "gpio": 17, "count": 60, "order": "GRB",
 *     "dma": true, "mode": "effect" }
 *
 * `sacn`: the console draws every pixel and the strip reads its universes. It owns
 * no ports at all:
 *
 *   { "driver": "ws2812", "ports": [], "gpio": 17, "count": 300, "order": "GRB",
 *     "mode": "sacn", "universe": 5, "address": 1, "hold_ms": 10000 }
 *
 * A binding with two ports or fewer is the catalog's LED module — a color and an
 * intensity, told apart by type — and is drawn as `solid`, which is what that
 * driver always did.
 *
 * Each strip has a task of its own. The pixel arithmetic is in `oh_pixfx` and
 * `oh_pixel`, where the host tests can reach it; this file is the RMT, the clock and
 * the locking.
 */
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "oh_clock.h"
#include "oh_driver.h"
#include "oh_drivers.h"
#include "oh_node.h"
#include "oh_pixel.h"
#include "oh_pixfx.h"
#include "oh_sacn.h"
#include "oh_sacn_merge.h"

static const char *TAG = "ws2812";

enum { P_COLOR = 0, P_BRIGHTNESS, P_COLOR2, P_EFFECT, P_SPEED, P_SIZE, P_REVERSE, P_MIRROR, P_SEGMENTS, P_COUNT };

/* How long a strip in `sacn` mode waits for its other universes once one has
 * arrived, before refreshing with what it has. */
#define COALESCE_MS 8

typedef struct {
    led_strip_handle_t strip;
    uint16_t count;
    uint8_t components;
    oh_pix_mode_t mode;
    int gpio;
    int max_fps;

    SemaphoreHandle_t lock;
    TaskHandle_t task;
    volatile bool stop;

    /* effect mode, and the legacy two-port binding */
    bool legacy;
    int color_local, intensity_local;
    oh_value_t values[P_COUNT];
    oh_pixfx_params_t params;
    bool unknown_effect;
    oh_pixfx_clock_t clock;

    /* sacn mode */
    oh_pix_map_t map;
    oh_sacn_merge_t *merge; /* one per universe of the map */
    int subs[OH_PIX_MAX_UNIVERSES];
    uint32_t hold_ms;
    uint8_t fresh;          /* universes with a frame since the last refresh, as bits */
    int64_t first_fresh_ms; /* when the first of them arrived */
    int64_t last_refresh_ms;
    int64_t lost_ms;        /* when the last source went, 0 while there is one */
    bool ever_driven;
    float level;            /* what the loss fade last drew at */
    uint32_t frames;

    uint32_t refreshes;
    uint32_t window_refreshes;
    int64_t window_ms;
    float fps;

    uint8_t *rgbw; /* 4 bytes a pixel, R G B W */
    uint8_t *sent; /* what was last put on the wire, the same shape */
    bool sent_valid;
} inst_t;

/* ── Putting a frame on the wire ──────────────────────────────────────── */

/* Every strip there is, so `oh_ws2812_status` can find one by its pin. */
#define MAX_STRIPS 4
static inst_t *s_strips[MAX_STRIPS];
static portMUX_TYPE s_strips_mux = portMUX_INITIALIZER_UNLOCKED;

static void count_rate(inst_t *i, int64_t now)
{
    if (now - i->window_ms >= 1000) {
        i->fps = (float)(i->refreshes - i->window_refreshes) * 1000.0f / (float)(now - i->window_ms);
        i->window_refreshes = i->refreshes;
        i->window_ms = now;
    }
}

static void push(inst_t *i, float k)
{
    uint8_t frame[4];
    bool changed = !i->sent_valid;
    for (uint16_t n = 0; n < i->count; n++) {
        const uint8_t *px = i->rgbw + 4 * n;
        for (int c = 0; c < 4; c++) {
            frame[c] = (uint8_t)((float)px[c] * k + 0.5f);
        }
        if (!changed && memcmp(i->sent + 4 * n, frame, 4) != 0) {
            changed = true;
        }
        memcpy(i->sent + 4 * n, frame, 4);
    }
    if (!changed) {
        return; /* a frame the strip is already showing costs nothing */
    }
    for (uint16_t n = 0; n < i->count; n++) {
        const uint8_t *px = i->sent + 4 * n;
        if (i->components == 4) {
            led_strip_set_pixel_rgbw(i->strip, n, px[0], px[1], px[2], px[3]);
        } else {
            led_strip_set_pixel(i->strip, n, px[0], px[1], px[2]);
        }
    }
    led_strip_refresh(i->strip);
    i->sent_valid = true;
    i->refreshes++;
}

/* ── effect mode ──────────────────────────────────────────────────────── */

/* Under the lock: the parameters, from the port values. */
static void read_params_locked(inst_t *i)
{
    oh_pixfx_params_t *p = &i->params;
    if (i->legacy) {
        oh_pixfx_defaults(p);
        if (i->color_local >= 0) {
            p->color = i->values[P_COLOR].color;
        }
        p->brightness = i->intensity_local >= 0 ? i->values[P_BRIGHTNESS].value : 1.0f;
        return;
    }
    p->color = i->values[P_COLOR].color;
    p->brightness = i->values[P_BRIGHTNESS].value;
    p->color2 = i->values[P_COLOR2].color;
    i->unknown_effect = !oh_pixfx_parse(i->values[P_EFFECT].text, &p->kind) && i->values[P_EFFECT].text[0];
    p->speed = i->values[P_SPEED].value;
    p->size = i->values[P_SIZE].value;
    p->reverse = i->values[P_REVERSE].state;
    p->mirror = i->values[P_MIRROR].state;
    p->segments = (int)(i->values[P_SEGMENTS].value + 0.5f);
    oh_pixfx_clamp(p);
}

static void effect_task(inst_t *i)
{
    const TickType_t period = pdMS_TO_TICKS(1000 / i->max_fps) ? pdMS_TO_TICKS(1000 / i->max_fps) : 1;
    uint8_t *rgb = malloc((size_t)i->count * 3);
    bool flagged = false;
    while (!i->stop && rgb) {
        xSemaphoreTake(i->lock, portMAX_DELAY);
        read_params_locked(i);
        const oh_pixfx_params_t p = i->params;
        const bool unknown = i->unknown_effect;
        xSemaphoreGive(i->lock);

        if (unknown != flagged) {
            flagged = unknown;
            if (unknown) {
                oh_node_error_set("pixel-effect");
            } else {
                oh_node_error_clear("pixel-effect");
            }
        }

        const int64_t now = oh_clock_console_now_ms();
        oh_pixfx_advance(&i->clock, p.speed, now);
        oh_pixfx_render(&p, i->clock.phase, now, i->count, rgb);
        for (uint16_t n = 0; n < i->count; n++) {
            memcpy(i->rgbw + 4 * n, rgb + 3 * n, 3);
            i->rgbw[4 * n + 3] = 0;
        }
        push(i, 1.0f);
        count_rate(i, oh_clock_now_ms());
        /* A solid strip changes only when a port does; anything else moves on its own. */
        ulTaskNotifyTake(pdTRUE, p.kind == OH_PIXFX_SOLID ? pdMS_TO_TICKS(1000) : period);
    }
    if (flagged) {
        oh_node_error_clear("pixel-effect");
    }
    free(rgb);
}

/* ── sacn mode ────────────────────────────────────────────────────────── */

static void on_frame(void *ctx, const oh_e131_frame_t *frame)
{
    inst_t *i = ctx;
    const int k = oh_pix_map_find(&i->map, frame->universe);
    if (k < 0) {
        return;
    }
    const int64_t now = oh_clock_now_ms();
    xSemaphoreTake(i->lock, portMAX_DELAY);
    if (oh_sacn_merge_feed(&i->merge[k], frame, now)) {
        if (!i->fresh) {
            i->first_fresh_ms = now;
        }
        i->fresh |= (uint8_t)(1u << k);
    }
    xSemaphoreGive(i->lock);
    xTaskNotifyGive(i->task);
}

static void sacn_task(inst_t *i)
{
    const int64_t min_gap = 1000 / i->max_fps;
    uint8_t *slots = malloc(512);
    while (!i->stop && slots) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(COALESCE_MS));
        const int64_t now = oh_clock_now_ms();
        bool draw = false;
        float k = 1.0f;

        xSemaphoreTake(i->lock, portMAX_DELAY);
        uint8_t live = 0;
        for (int u = 0; u < i->map.n; u++) {
            oh_sacn_merge_expire(&i->merge[u], now);
            if (oh_sacn_merge_sources(&i->merge[u])) {
                live |= (uint8_t)(1u << u);
            }
        }
        if (live) {
            const bool all_in = (i->fresh & live) == live;
            if (i->fresh && (all_in || now - i->first_fresh_ms >= COALESCE_MS) &&
                now - i->last_refresh_ms >= min_gap) {
                for (int u = 0; u < i->map.n; u++) {
                    if (live & (1u << u)) {
                        const uint16_t n = oh_sacn_merge_output(&i->merge[u], slots);
                        oh_pix_map_apply(&i->map, u, slots, n, i->rgbw);
                    }
                }
                i->fresh = 0;
                i->last_refresh_ms = now;
                i->frames++;
                draw = true;
            }
            i->lost_ms = 0;
            i->ever_driven = true;
            i->level = 1.0f;
        } else if (i->ever_driven) {
            /* Nobody is driving any universe of this strip: hold, then fade. */
            if (!i->lost_ms) {
                i->lost_ms = now;
                ESP_LOGW(TAG, "gpio %d: every sACN source has gone; holding for %lu ms", i->gpio,
                         (unsigned long)i->hold_ms);
            }
            k = oh_pix_loss_level((uint32_t)(now - i->lost_ms), i->hold_ms);
            draw = k != i->level;
            i->level = k;
            i->fresh = 0;
        }
        if (draw) {
            push(i, live ? 1.0f : k);
        }
        count_rate(i, now);
        xSemaphoreGive(i->lock);
    }
    free(slots);
}

static void task(void *arg)
{
    inst_t *i = arg;
    if (i->mode == OH_PIX_MODE_SACN) {
        sacn_task(i);
    } else {
        effect_task(i);
    }
    i->task = NULL;
    vTaskDelete(NULL);
}

/* ── The driver ───────────────────────────────────────────────────────── */

static void *alloc(size_t n, size_t size)
{
    void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : calloc(n, size);
}

static void destroy(inst_t *i)
{
    portENTER_CRITICAL(&s_strips_mux);
    for (int n = 0; n < MAX_STRIPS; n++) {
        if (s_strips[n] == i) {
            s_strips[n] = NULL;
        }
    }
    portEXIT_CRITICAL(&s_strips_mux);
    for (int u = 0; u < OH_PIX_MAX_UNIVERSES; u++) {
        oh_sacn_unsubscribe(i->subs[u]);
        i->subs[u] = -1;
    }
    if (i->task) {
        i->stop = true;
        xTaskNotifyGive(i->task);
        for (int n = 0; n < 100 && i->task; n++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (i->strip) {
        led_strip_clear(i->strip);
        led_strip_del(i->strip);
    }
    if (i->lock) {
        vSemaphoreDelete(i->lock);
    }
    free(i->merge);
    free(i->rgbw);
    free(i->sent);
    free(i);
}

static esp_err_t init(const oh_driver_bind_t *bind, void **out)
{
    const int gpio = oh_driver_param_int(bind->params, "gpio", -1);
    if (gpio < 0) {
        ESP_LOGE(TAG, "no gpio");
        return ESP_ERR_INVALID_ARG;
    }
    inst_t *i = calloc(1, sizeof(*i));
    if (!i) {
        return ESP_ERR_NO_MEM;
    }
    for (int u = 0; u < OH_PIX_MAX_UNIVERSES; u++) {
        i->subs[u] = -1;
    }
    i->gpio = gpio;
    int count = oh_driver_param_int(bind->params, "count", 8);
    i->count = (uint16_t)(count < 1 ? 1 : count > OH_PIX_MAX_COUNT ? OH_PIX_MAX_COUNT : count);
    i->max_fps = oh_pix_max_fps(i->count);
    if (!oh_pix_mode_parse(oh_driver_param_str(bind->params, "mode", "effect"), &i->mode)) {
        i->mode = OH_PIX_MODE_EFFECT;
    }
    const char *order_s = oh_driver_param_str(bind->params, "order", "GRB");
    oh_pix_order_t order;
    if (!oh_pix_order_parse(order_s, &order)) {
        ESP_LOGW(TAG, "order \"%s\" is not one this driver knows; GRB", order_s);
        oh_pix_order_parse("GRB", &order);
    }
    i->components = order.components;
    i->lock = xSemaphoreCreateMutex();
    i->rgbw = alloc(i->count, 4);
    i->sent = alloc(i->count, 4);
    if (!i->lock || !i->rgbw || !i->sent) {
        destroy(i);
        return ESP_ERR_NO_MEM;
    }

    /* The ports, in effect mode. */
    i->color_local = i->intensity_local = -1;
    i->legacy = i->mode == OH_PIX_MODE_EFFECT && bind->n_ports <= 2;
    oh_pixfx_params_t d;
    oh_pixfx_defaults(&d);
    i->values[P_SPEED] = (oh_value_t){.type = OH_DT_NUMBER, .value = d.speed};
    i->values[P_SIZE] = (oh_value_t){.type = OH_DT_NUMBER, .value = d.size};
    i->values[P_SEGMENTS] = (oh_value_t){.type = OH_DT_NUMBER, .value = (float)d.segments};
    for (int n = 0; n < bind->n_ports && i->mode == OH_PIX_MODE_EFFECT; n++) {
        const oh_port_desc_t *pd = bind->ports[n];
        if (!pd) {
            continue;
        }
        if (i->legacy) {
            if (pd->type == OH_DT_COLOR) {
                i->color_local = n;
            } else if (pd->type == OH_DT_NUMBER) {
                i->intensity_local = n;
                i->values[P_BRIGHTNESS] =
                    (oh_value_t){.type = OH_DT_NUMBER, .value = pd->has_default ? pd->default_value : 0};
            }
        } else if (n < P_COUNT) {
            oh_value_default(pd, &i->values[n]);
        }
    }
    if (!i->legacy && i->mode == OH_PIX_MODE_EFFECT) {
        strncpy(i->values[P_EFFECT].text, "solid", OH_TEXT_MAX - 1);
    }

    /* The RMT, with the one DMA channel if the board gave it to this strip. */
    const bool dma = oh_driver_param_bool(bind->params, "dma", false);
    led_strip_config_t strip = {
        .strip_gpio_num = gpio,
        .max_leds = i->count,
        .led_model = order.components == 4 ? LED_MODEL_SK6812 : LED_MODEL_WS2812,
        .color_component_format = {.format = {.r_pos = order.pos[0],
                                              .g_pos = order.pos[1],
                                              .b_pos = order.pos[2],
                                              .w_pos = order.pos[3],
                                              .bytes_per_color = 1,
                                              .num_components = order.components}},
        .flags.invert_out = oh_driver_param_bool(bind->params, "invert", false),
    };
    led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        /* Doc 14: a DMA strip gets a buffer big enough to need no refills; the other
         * two memory blocks, which halves its refill interrupts. */
        .mem_block_symbols = dma ? 1024 : (i->count > 64 ? 96 : 0),
#if SOC_RMT_SUPPORT_DMA
        .flags.with_dma = dma,
#endif
    };
    esp_err_t err = led_strip_new_rmt_device(&strip, &rmt, &i->strip);
    if (err != ESP_OK && rmt.mem_block_symbols == 96) {
        rmt.mem_block_symbols = 0; /* no second block free: refill every pixel */
        err = led_strip_new_rmt_device(&strip, &rmt, &i->strip);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio %d: %s", gpio, esp_err_to_name(err));
        destroy(i);
        return err;
    }
    led_strip_clear(i->strip);

    /* The universes, in sacn mode. */
    if (i->mode == OH_PIX_MODE_SACN) {
        oh_pix_cfg_t cfg;
        oh_pix_cfg_default(&cfg);
        cfg.mode = OH_PIX_MODE_SACN;
        cfg.count = i->count;
        strncpy(cfg.order, order_s, OH_PIX_ORDER_MAX - 1);
        cfg.universe = (uint16_t)oh_driver_param_int(bind->params, "universe", 0);
        cfg.address = (uint16_t)oh_driver_param_int(bind->params, "address", 1);
        i->hold_ms = (uint32_t)oh_driver_param_int(bind->params, "hold_ms", OH_PIX_HOLD_DEFAULT_MS);
        if (!oh_pix_map_build(&cfg, &i->map)) {
            ESP_LOGE(TAG, "gpio %d: %u pixels from %u/%u do not map", gpio, i->count, cfg.universe, cfg.address);
            destroy(i);
            return ESP_ERR_INVALID_ARG;
        }
        i->merge = alloc(i->map.n, sizeof(oh_sacn_merge_t));
        if (!i->merge) {
            destroy(i);
            return ESP_ERR_NO_MEM;
        }
    }

    if (xTaskCreate(task, "ws2812", 4096, i, 7, &i->task) != pdPASS) {
        destroy(i);
        return ESP_ERR_NO_MEM;
    }
    if (i->mode == OH_PIX_MODE_SACN) {
        for (int u = 0; u < i->map.n; u++) {
            i->subs[u] = oh_sacn_subscribe(i->map.universe[u], on_frame, i);
        }
        ESP_LOGI(TAG, "%u pixels on gpio %d (%s%s), sACN %u/%u over %u universes", i->count, gpio, order_s,
                 dma ? ", DMA" : "", i->map.universe[0], (unsigned)(i->map.first_slot[0] + 1), i->map.n);
    } else {
        ESP_LOGI(TAG, "%u pixels on gpio %d (%s%s), effects at up to %d fps", i->count, gpio, order_s,
                 dma ? ", DMA" : "", i->max_fps);
    }
    portENTER_CRITICAL(&s_strips_mux);
    for (int n = 0; n < MAX_STRIPS; n++) {
        if (!s_strips[n]) {
            s_strips[n] = i;
            break;
        }
    }
    portEXIT_CRITICAL(&s_strips_mux);
    *out = i;
    return ESP_OK;
}

static esp_err_t write(void *arg, uint8_t local, const oh_value_t *in)
{
    inst_t *i = arg;
    int slot = -1;
    if (i->mode != OH_PIX_MODE_EFFECT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (i->legacy) {
        if ((int)local == i->color_local && in->type == OH_DT_COLOR) {
            slot = P_COLOR;
        } else if ((int)local == i->intensity_local && in->type == OH_DT_NUMBER) {
            slot = P_BRIGHTNESS;
        }
    } else if (local < P_COUNT) {
        static const oh_datatype_t TYPES[P_COUNT] = {OH_DT_COLOR,  OH_DT_NUMBER,  OH_DT_COLOR,
                                                     OH_DT_STRING, OH_DT_NUMBER,  OH_DT_NUMBER,
                                                     OH_DT_BOOLEAN, OH_DT_BOOLEAN, OH_DT_NUMBER};
        if (in->type == TYPES[local]) {
            slot = local;
        }
    }
    if (slot < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(i->lock, portMAX_DELAY);
    i->values[slot] = *in;
    xSemaphoreGive(i->lock);
    if (i->task) {
        xTaskNotifyGive(i->task);
    }
    return ESP_OK;
}

static esp_err_t read(void *arg, uint8_t local, oh_value_t *out)
{
    inst_t *i = arg;
    int slot = -1;
    if (i->mode != OH_PIX_MODE_EFFECT) {
        return ESP_ERR_NOT_FOUND;
    }
    if (i->legacy) {
        slot = (int)local == i->color_local ? P_COLOR : (int)local == i->intensity_local ? P_BRIGHTNESS : -1;
    } else if (local < P_COUNT) {
        slot = local;
    }
    if (slot < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    xSemaphoreTake(i->lock, portMAX_DELAY);
    *out = i->values[slot];
    xSemaphoreGive(i->lock);
    return ESP_OK;
}

static void deinit(void *arg)
{
    destroy(arg);
}

const oh_driver_t oh_driver_ws2812 = {
    .id = "ws2812",
    .init = init,
    .read = read,
    .write = write,
    .deinit = deinit,
};

bool oh_ws2812_status(int gpio, oh_ws2812_status_t *out)
{
    /* Taken under the node lock by the caller, so no strip is bound or unbound
     * while this looks: the registry can only lose an entry inside a rebind. */
    inst_t *i = NULL;
    portENTER_CRITICAL(&s_strips_mux);
    for (int n = 0; n < MAX_STRIPS; n++) {
        if (s_strips[n] && s_strips[n]->gpio == gpio) {
            i = s_strips[n];
        }
    }
    portEXIT_CRITICAL(&s_strips_mux);
    if (!i) {
        return false;
    }
    xSemaphoreTake(i->lock, portMAX_DELAY);
    out->frames = i->refreshes;
    out->fps = i->fps;
    out->sources = 0;
    for (int u = 0; u < i->map.n && i->merge; u++) {
        const int n = oh_sacn_merge_sources(&i->merge[u]);
        out->sources = n > out->sources ? n : out->sources;
    }
    out->holding = i->lost_ms != 0 && i->level > 0.0f;
    xSemaphoreGive(i->lock);
    return true;
}
