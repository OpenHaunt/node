#include "oh_board.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#if SOC_TEMP_SENSOR_SUPPORTED
#include "driver/temperature_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
/* One sensor per chip, read by MQTT health and /status alike. */
static temperature_sensor_handle_t s_tsens;
static SemaphoreHandle_t s_tsens_lock;
#endif

static void parse_list(const char *text, int *out, int cap)
{
    for (int i = 0; i < cap; i++) {
        out[i] = -1;
    }
    int n = 0;
    const char *p = text;
    while (*p && n < cap) {
        char *end;
        long v = strtol(p, &end, 10);
        if (end == p) {
            break;
        }
        out[n++] = (int)v;
        p = end;
        while (*p == ',' || *p == ' ') {
            p++;
        }
    }
}

const oh_board_pins_t *oh_board_pins(void)
{
    static oh_board_pins_t pins;
    static bool ready;
    if (!ready) {
        pins.i2c_sda = CONFIG_OH_PIN_I2C_SDA;
        pins.i2c_scl = CONFIG_OH_PIN_I2C_SCL;
        pins.mod_int = CONFIG_OH_PIN_MOD_INT;
        pins.mod_fast0 = CONFIG_OH_PIN_MOD_FAST0;
        pins.mod_nreset = CONFIG_OH_PIN_MOD_NRESET;
        pins.mod_uart_tx = CONFIG_OH_PIN_MOD_UART_TX;
        pins.mod_uart_rx = CONFIG_OH_PIN_MOD_UART_RX;
        pins.mod_uart_de = CONFIG_OH_PIN_MOD_UART_DE;
        pins.button = CONFIG_OH_PIN_BUTTON;
        pins.button_active_low = CONFIG_OH_BUTTON_ACTIVE_LOW;
#ifdef CONFIG_OH_PIN_STATUS_LED
        pins.status_led = CONFIG_OH_PIN_STATUS_LED;
#else
        pins.status_led = -1;
#endif
        parse_list(CONFIG_OH_PRESET_INPUT_GPIOS, pins.preset_inputs, 8);
        parse_list(CONFIG_OH_PRESET_OUTPUT_GPIOS, pins.preset_outputs, 4);
#ifdef CONFIG_OH_BOARD_OH_NODE_V0
        parse_list(CONFIG_OH_PIN_RELAYS, pins.relays, 4);
        parse_list(CONFIG_OH_PIN_SENSORS, pins.sensors, 2);
#ifdef CONFIG_OH_SENSOR_CONTACT_READS_LOW
        pins.sensor_contact_low = true;
#endif
        parse_list(CONFIG_OH_PIN_STRIPS, pins.strips, 2);
        pins.strip_pixels = CONFIG_OH_STRIP_PIXELS;
        parse_list(CONFIG_OH_PIN_RELAY_LEDS, pins.relay_leds, 4);
        parse_list(CONFIG_OH_PIN_SENSOR_LEDS, pins.sensor_leds, 2);
        pins.i2s_dout = CONFIG_OH_PIN_I2S_DOUT;
        pins.i2s_bclk = CONFIG_OH_PIN_I2S_BCLK;
        pins.i2s_ws = CONFIG_OH_PIN_I2S_WS;
        pins.amp_en = CONFIG_OH_PIN_AMP_EN;
        pins.dac_mute = CONFIG_OH_PIN_DAC_MUTE;
#else
        parse_list("", pins.relays, 4);
        parse_list("", pins.sensors, 2);
        parse_list("", pins.strips, 2);
        parse_list("", pins.relay_leds, 4);
        parse_list("", pins.sensor_leds, 2);
        pins.i2s_dout = pins.i2s_bclk = pins.i2s_ws = -1;
        pins.amp_en = pins.dac_mute = -1;
#endif
        pins.error_led = CONFIG_OH_PIN_ERROR_LED;
        ready = true;
    }
    return &pins;
}

const char *oh_board_name(void)
{
    return CONFIG_OH_BOARD_NAME;
}

bool oh_board_has_display(void)
{
#ifdef CONFIG_OH_DISPLAY
    return true;
#else
    return false;
#endif
}

bool oh_board_has_ethernet(void)
{
#ifdef CONFIG_OH_TRANSPORT_ETH
    return true;
#else
    return false;
#endif
}

bool oh_board_has_builtin_io(void)
{
#ifdef CONFIG_OH_BOARD_OH_NODE_V0
    return true;
#else
    return false;
#endif
}

const char *oh_board_rev(void)
{
#if defined(CONFIG_OH_BOARD_OH_NODE_V0_1)
    return "0.1";
#elif defined(CONFIG_OH_BOARD_OH_NODE_V0_2)
    return "0.2";
#else
    return "";
#endif
}

void oh_board_init(void)
{
#if SOC_TEMP_SENSOR_SUPPORTED
    temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&tcfg, &s_tsens) == ESP_OK && temperature_sensor_enable(s_tsens) == ESP_OK) {
        s_tsens_lock = xSemaphoreCreateMutex();
    } else {
        s_tsens = NULL;
    }
#endif
    const oh_board_pins_t *b = oh_board_pins();
    /* Audio stays silent until a driver claims it: the amplifier off and the
     * DAC muted. Both already default that way through their resistors; this
     * makes it so after a soft reset as well. */
    int quiet[] = {b->amp_en, b->dac_mute};
    for (size_t i = 0; i < sizeof(quiet) / sizeof(quiet[0]); i++) {
        if (quiet[i] >= 0) {
            gpio_config_t cfg = {.pin_bit_mask = 1ULL << quiet[i], .mode = GPIO_MODE_OUTPUT};
            gpio_config(&cfg);
            gpio_set_level(quiet[i], 0);
        }
    }
}

bool oh_board_chip_temp(float *celsius)
{
#if SOC_TEMP_SENSOR_SUPPORTED
    if (!s_tsens || !s_tsens_lock) {
        return false;
    }
    xSemaphoreTake(s_tsens_lock, portMAX_DELAY);
    const bool ok = temperature_sensor_get_celsius(s_tsens, celsius) == ESP_OK;
    xSemaphoreGive(s_tsens_lock);
    return ok;
#else
    (void)celsius;
    return false;
#endif
}
