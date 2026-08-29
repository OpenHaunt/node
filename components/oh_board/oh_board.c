#include "oh_board.h"

#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"

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
#ifdef CONFIG_OH_TRANSPORT_ETH_RMII
    return true;
#else
    return false;
#endif
}
