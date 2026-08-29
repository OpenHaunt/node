#include "oh_drivers.h"

#include "oh_driver.h"

extern const oh_driver_t oh_driver_gpio_in;
extern const oh_driver_t oh_driver_gpio_out;

void oh_drivers_register_all(void)
{
    oh_driver_register(&oh_driver_gpio_in);
    oh_driver_register(&oh_driver_gpio_out);
}
