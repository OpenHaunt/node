#include "oh_status.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "oh_board.h"
#include "oh_config.h"
#include "oh_node.h"
#include "sdkconfig.h"

#ifdef CONFIG_OH_STATUS_LED_WS2812
#include "led_strip.h"
static led_strip_handle_t s_strip;
#endif
#include "driver/gpio.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

static const char *TAG __attribute__((unused)) = "oh_status";

typedef enum { ST_BOOT, ST_SETUP_AP, ST_NET_DOWN, ST_NET_UP, ST_MQTT_UP } state_t;

static struct {
    state_t state;
    bool fault;
    int64_t identify_until_us;
    esp_timer_handle_t tick;
    uint32_t n;
    bool dark;        /* oh_config_dark(), kept here so the tick takes no lock */
    bool pressed;     /* the BOOT button, debounced */
    uint8_t press_ticks;
} s;

typedef struct {
    uint8_t r, g, b;
} rgb_t;

static void led_set(bool on, rgb_t color)
{
    (void)color;
#ifdef CONFIG_OH_STATUS_LED_WS2812
    if (s_strip) {
        if (on) {
            led_strip_set_pixel(s_strip, 0, color.r, color.g, color.b);
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

static void out_pin(int gpio)
{
    if (gpio >= 0) {
        gpio_config_t cfg = {.pin_bit_mask = 1ULL << gpio, .mode = GPIO_MODE_OUTPUT};
        gpio_config(&cfg);
        gpio_set_level(gpio, 0);
    }
}

/* What a pin is driving, read back from the output register: an output-only
 * pad has its input buffer off, so gpio_get_level would always say low. */
static bool driven_high(int gpio)
{
#ifdef GPIO_OUT1_REG
    uint32_t reg = gpio < 32 ? REG_READ(GPIO_OUT_REG) : REG_READ(GPIO_OUT1_REG);
#else
    uint32_t reg = REG_READ(GPIO_OUT_REG);
#endif
    return (reg >> (gpio & 31)) & 1;
}

/* The indicator LEDs beside each relay and sensor terminal (V0.2) follow the
 * pins rather than the ports, so they say what the hardware is doing whatever
 * module is bound: a relay LED is lit while its coil is driven, whoever drives
 * it. Read every tick rather than on port events, which would miss a pin a
 * driver toggles without going through a port. */
static void mirror_indicators(void)
{
    const oh_board_pins_t *b = oh_board_pins();
    for (int i = 0; i < 4; i++) {
        if (b->relay_leds[i] >= 0 && b->relays[i] >= 0) {
            gpio_set_level(b->relay_leds[i], driven_high(b->relays[i]));
        }
    }
    for (int i = 0; i < 2; i++) {
        if (b->sensor_leds[i] >= 0 && b->sensors[i] >= 0) {
            bool level = gpio_get_level(b->sensors[i]);
            gpio_set_level(b->sensor_leds[i], level != b->sensor_contact_low);
        }
    }
}

static void indicators_off(void)
{
    const oh_board_pins_t *b = oh_board_pins();
    for (int i = 0; i < 4; i++) {
        if (b->relay_leds[i] >= 0) {
            gpio_set_level(b->relay_leds[i], 0);
        }
    }
    for (int i = 0; i < 2; i++) {
        if (b->sensor_leds[i] >= 0) {
            gpio_set_level(b->sensor_leds[i], 0);
        }
    }
}

/* The BOOT button, read every tick. Two ticks the same way round (100 ms) count, which
 * rides out contact bounce. The press itself is handled on the event loop: toggling
 * dark writes flash, which the timer task is too small and too busy for. */
static void poll_button(void)
{
    const oh_board_pins_t *b = oh_board_pins();
    if (b->button < 0) {
        return;
    }
    const bool down = gpio_get_level(b->button) == (b->button_active_low ? 0 : 1);
    if (down == s.pressed) {
        s.press_ticks = 0;
        return;
    }
    if (++s.press_ticks < 2) {
        return;
    }
    s.press_ticks = 0;
    s.pressed = down;
    oh_node_post(OH_EVENT_BUTTON, &down, sizeof(down));
}

bool oh_status_button(bool *pressed)
{
    *pressed = s.pressed;
    return oh_board_pins()->button >= 0;
}

/* 50 ms ticks. Each pattern is "on for these ticks out of this period". */
static void on_tick(void *arg)
{
    (void)arg;
    uint32_t t = s.n++;
    int64_t now = esp_timer_get_time();
    poll_button();
    /* Dark mode turns every indicator off. Identify still blinks: a node has to be
     * findable, and the person asking for it wants to see it. */
    if (s.dark) {
        indicators_off();
    } else {
        mirror_indicators();
    }
    const int err_led = oh_board_pins()->error_led;
    if (now < s.identify_until_us) {
        led_set((t & 1) == 0, (rgb_t){60, 60, 60});
        if (err_led >= 0) {
            gpio_set_level(err_led, (t & 1) == 0);
        }
        return;
    }
    if (s.dark) {
        led_set(false, (rgb_t){0, 0, 0});
        if (err_led >= 0) {
            gpio_set_level(err_led, 0);
        }
        return;
    }
    rgb_t color;
    bool on;
    switch (s.state) {
    case ST_BOOT:
        color = (rgb_t){20, 20, 20};
        on = true;
        break;
    case ST_SETUP_AP:
        color = (rgb_t){0, 0, 60};
        on = (t % 10) < 5; /* 2 Hz blue */
        break;
    case ST_NET_DOWN:
        color = (rgb_t){60, 25, 0};
        on = (t % 20) < 2; /* amber blip every second */
        break;
    case ST_NET_UP:
        color = (rgb_t){0, 40, 0};
        on = (t % 40) < 2; /* green blip every 2 s: on the network, nobody driving it */
        break;
    case ST_MQTT_UP:
    default:
        color = (rgb_t){0, 12, 0};
        on = true; /* steady dim green: adopted and talking */
        break;
    }
    uint32_t phase = t % 40;
    bool flash = phase < 2 || (phase >= 4 && phase < 6);
    if (err_led >= 0) {
        /* A board with its own red LED double-flashes that and leaves the
         * status pattern alone. */
        gpio_set_level(err_led, s.fault && flash);
    } else if (s.fault && phase < 8) {
        /* A double red flash rides on top every 2 s. */
        color = (rgb_t){60, 0, 0};
        on = flash;
    }
    led_set(on, color);
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
    case OH_EVENT_BUTTON:
        if (*(bool *)data) {
            oh_config_set_dark(!oh_config_dark());
        }
        break;
    case OH_EVENT_CONFIG_CHANGED:
        if (*(uint32_t *)data & OH_CFG_DARK) {
            s.dark = oh_config_dark();
        }
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
    const oh_board_pins_t *b = oh_board_pins();
    out_pin(b->error_led);
    for (int i = 0; i < 4; i++) {
        out_pin(b->relay_leds[i]);
    }
    for (int i = 0; i < 2; i++) {
        out_pin(b->sensor_leds[i]);
    }
    if (b->button >= 0) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << b->button,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = b->button_active_low,
            .pull_down_en = !b->button_active_low,
        };
        gpio_config(&cfg);
    }
    s.dark = oh_config_dark();
    char codes[OH_MAX_ERRORS][OH_ERROR_CODE_MAX];
    s.fault = oh_node_errors(codes, OH_MAX_ERRORS) > 0;
    ESP_ERROR_CHECK(esp_event_handler_register(OH_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    esp_timer_create_args_t args = {.callback = on_tick, .name = "status"};
    ESP_ERROR_CHECK(esp_timer_create(&args, &s.tick));
    return esp_timer_start_periodic(s.tick, 50 * 1000);
}
