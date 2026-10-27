/* The one place that knows which pin is which on this board. */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int i2c_sda, i2c_scl;
    int mod_int, mod_fast0, mod_nreset;
    int mod_uart_tx, mod_uart_rx, mod_uart_de;
    int button;
    bool button_active_low;
    int status_led;
    /* Free pins a preset may bind, -1 padded. */
    int preset_inputs[8];
    int preset_outputs[4];
    /* Built-in I/O, -1 padded (the V0 prototype; nothing elsewhere). */
    int relays[4];
    int sensors[2];
    bool sensor_contact_low;
    int strips[2];
    int strip_pixels;
    int relay_leds[4];
    int sensor_leds[2];
    int error_led;
    /* Audio, defined here and unused until a driver claims them. */
    int i2s_dout, i2s_bclk, i2s_ws;
    int amp_en, dac_mute;
} oh_board_pins_t;

const oh_board_pins_t *oh_board_pins(void);
/* Board-level pins with a safe state to be put in at boot (audio muted). */
void oh_board_init(void);
const char *oh_board_name(void);
bool oh_board_has_display(void);
bool oh_board_has_ethernet(void);
/* The board carries its I/O itself (no module connector). */
bool oh_board_has_builtin_io(void);
/* "0.1", "0.2" for a board with a revision of its own, else "". */
const char *oh_board_rev(void);
/* The chip's own temperature in °C: the die, which runs above the air around it.
 * False where the chip has no sensor or it would not start. */
bool oh_board_chip_temp(float *celsius);

#ifdef __cplusplus
}
#endif
