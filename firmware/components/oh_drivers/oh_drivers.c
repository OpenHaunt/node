#include "oh_drivers.h"

#include "oh_driver.h"

extern const oh_driver_t oh_driver_gpio_in;
extern const oh_driver_t oh_driver_gpio_out;
extern const oh_driver_t oh_driver_ws2812;
extern const oh_driver_t oh_driver_mcp23017_in;
extern const oh_driver_t oh_driver_mcp23017_out;
extern const oh_driver_t oh_driver_sht4x;
extern const oh_driver_t oh_driver_ssd1306;
extern const oh_driver_t oh_driver_dmx_uart;

void oh_drivers_register_all(void)
{
    oh_driver_register(&oh_driver_gpio_in);
    oh_driver_register(&oh_driver_gpio_out);
    oh_driver_register(&oh_driver_ws2812);
    oh_driver_register(&oh_driver_mcp23017_in);
    oh_driver_register(&oh_driver_mcp23017_out);
    oh_driver_register(&oh_driver_sht4x);
    oh_driver_register(&oh_driver_ssd1306);
    oh_driver_register(&oh_driver_dmx_uart);
}
