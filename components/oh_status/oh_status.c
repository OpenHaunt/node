#include "oh_status.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "oh_board.h"
#include "oh_node.h"
#include "sdkconfig.h"

#ifdef CONFIG_OH_STATUS_LED_WS2812
#include "led_strip.h"
static led_strip_handle_t s_strip;
#endif
#ifdef CONFIG_OH_STATUS_LED_MONO_GPIO
#include "driver/gpio.h"
#endif

static const char *TAG __attribute__((unused)) = "oh_status";

typedef enum { ST_BOOT, ST_SETUP_AP, ST_NET_DOWN, ST_NET_UP, ST_MQTT_UP } state_t;

static struct {
    state_t state;
    bool fault;
    int64_t identify_until_us;
    esp_timer_handle_t tick;
    uint32_t n;
} s;

typedef struct {
    uint8_t r, g, b;
} rgb_t;

static void led_set(bool on, rgb_t colour)
{
    (void)colour;
#ifdef CONFIG_OH_STATUS_LED_WS2812
    if (s_strip) {
        if (on) {
            led_strip_set_pixel(s_strip, 0, colour.r, colour.g, colour.b);
        } else {
            led_strip_set_pixel(s_strip, 0, 0, 0, 0);
        }
        led_strip_refresh(s_strip);
    }
#endif
#ifdef CONFIG_OH_STATUS_LED_MONO_GPIO
#ifdef CONFIG_OH_STATUS_LED_ACTIVE_LOW
    on = !on;
#endif
    if (CONFIG_OH_PIN_STATUS_LED >= 0) {
        gpio_set_level(CONFIG_OH_PIN_STATUS_LED, on);
    }
#endif
}

/* 50 ms ticks. Each pattern is "on for these ticks out of this period". */
static void on_tick(void *arg)
{
    (void)arg;
    uint32_t t = s.n++;
    int64_t now = esp_timer_get_time();
    if (now < s.identify_until_us) {
        led_set((t & 1) == 0, (rgb_t){60, 60, 60});
        return;
    }
    rgb_t colour;
    bool on;
    switch (s.state) {
    case ST_BOOT:
        colour = (rgb_t){20, 20, 20};
        on = true;
        break;
    case ST_SETUP_AP:
        colour = (rgb_t){0, 0, 60};
        on = (t % 10) < 5; /* 2 Hz blue */
        break;
    case ST_NET_DOWN:
        colour = (rgb_t){60, 25, 0};
        on = (t % 20) < 2; /* amber blip every second */
        break;
    case ST_NET_UP:
        colour = (rgb_t){0, 40, 0};
        on = (t % 40) < 2; /* green blip every 2 s: on the network, nobody driving it */
        break;
    case ST_MQTT_UP:
    default:
        colour = (rgb_t){0, 12, 0};
        on = true; /* steady dim green: adopted and talking */
        break;
    }
    if (s.fault) {
        /* A double red flash rides on top every 2 s. */
        uint32_t phase = t % 40;
        if (phase < 8) {
            colour = (rgb_t){60, 0, 0};
            on = phase < 2 || (phase >= 4 && phase < 6);
        }
    }
    led_set(on, colour);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    switch (id) {
    case OH_EVENT_NET_SETUP_AP:
        s.state = ST_SETUP_AP;
        break;
    case OH_EVENT_NET_DOWN:
        s.state = ST_NET_DOWN;
        break;
    case OH_EVENT_NET_UP:
        s.state = ST_NET_UP;
        break;
    case OH_EVENT_MQTT_CONNECTED:
        s.state = ST_MQTT_UP;
        break;
    case OH_EVENT_MQTT_DISCONNECTED:
        if (s.state == ST_MQTT_UP) {
            s.state = ST_NET_UP;
        }
        break;
    case OH_EVENT_IDENTIFY:
        s.identify_until_us = esp_timer_get_time() + (int64_t)(*(uint32_t *)data) * 1000;
        break;
    case OH_EVENT_ERRORS_CHANGED: {
        char codes[OH_MAX_ERRORS][OH_ERROR_CODE_MAX];
        s.fault = oh_node_errors(codes, OH_MAX_ERRORS) > 0;
        break;
    }
    default:
        break;
    }
}

esp_err_t oh_status_start(void)
{
#ifdef CONFIG_OH_STATUS_LED_WS2812
    led_strip_config_t strip = {
        .strip_gpio_num = CONFIG_OH_PIN_STATUS_LED,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,
        .flags.with_dma = false,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip, &rmt, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "status LED: %s", esp_err_to_name(err));
        s_strip = NULL;
    }
#endif
#ifdef CONFIG_OH_STATUS_LED_MONO_GPIO
    if (CONFIG_OH_PIN_STATUS_LED >= 0) {
        gpio_config_t cfg = {.pin_bit_mask = 1ULL << CONFIG_OH_PIN_STATUS_LED, .mode = GPIO_MODE_OUTPUT};
        gpio_config(&cfg);
    }
#endif
    char codes[OH_MAX_ERRORS][OH_ERROR_CODE_MAX];
    s.fault = oh_node_errors(codes, OH_MAX_ERRORS) > 0;
    ESP_ERROR_CHECK(esp_event_handler_register(OH_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    esp_timer_create_args_t args = {.callback = on_tick, .name = "status"};
    ESP_ERROR_CHECK(esp_timer_create(&args, &s.tick));
    return esp_timer_start_periodic(s.tick, 50 * 1000);
}
