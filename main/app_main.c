#include <stdio.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "oh_board.h"
#include "oh_vmod.h"

static const char *TAG = "oh";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    ESP_LOGI(TAG, "OpenHaunt node fw %s on %s, sn %02x%02x%02x%02x", OH_FW_VERSION, oh_board_name(), mac[2], mac[3],
             mac[4], mac[5]);
    const oh_board_pins_t *pins = oh_board_pins();
    ESP_LOGI(TAG, "pins: sda %d scl %d int %d fast0 %d button %d led %d", pins->i2c_sda, pins->i2c_scl, pins->mod_int,
             pins->mod_fast0, pins->button, pins->status_led);
    oh_module_t m;
    oh_vmod_preset("contact", NULL, &m);
    ESP_LOGI(TAG, "preset 0x%04x %s with %d ports", m.module.type, m.module.name, m.n_ports);
}
