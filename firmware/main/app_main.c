/*
 * Boot, in the order OpenHaunt/node doc 04 gives it: storage, the module
 * (held in reset until it is known), then the network and everything that
 * talks over it.
 */
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "oh_app.h"
#include "oh_audio.h"
#include "oh_console.h"
#include "oh_http.h"
#include "oh_mqtt.h"
#include "oh_node.h"
#include "oh_status.h"
#include "sdkconfig.h"
#ifdef CONFIG_OH_DISPLAY
#include "oh_display.h"
#endif

static const char *TAG = "main";

static void on_port_changed(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    const oh_port_changed_t *c = data;
    switch (c->value.type) {
    case OH_DT_BOOLEAN:
        ESP_LOGI(TAG, "port %u %s %s", c->port, c->is_input ? "->" : "<-", c->value.state ? "on" : "off");
        break;
    case OH_DT_NUMBER:
        ESP_LOGI(TAG, "port %u %s %.3f", c->port, c->is_input ? "->" : "<-", c->value.value);
        break;
    case OH_DT_COLOR:
        ESP_LOGI(TAG, "port %u <- rgb(%u,%u,%u)", c->port, c->value.color.r, c->value.color.g, c->value.color.b);
        break;
    case OH_DT_STRING:
        ESP_LOGI(TAG, "port %u <- \"%s\"", c->port, c->value.text);
        break;
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(oh_app_init());
    ESP_ERROR_CHECK(esp_event_handler_register(OH_EVENT, OH_EVENT_PORT_CHANGED, on_port_changed, NULL));
    ESP_ERROR_CHECK(oh_status_start());
#ifdef CONFIG_OH_DISPLAY
    if (oh_display_start() != ESP_OK) {
        oh_node_error_set("display");
    }
#endif

    /* Before the network, so /info and the MQTT subscriptions know there is audio.
     * A board without it answers ESP_OK and does nothing; a failure is a node error
     * and the rest of the node carries on. */
    if (oh_audio_start() != ESP_OK) {
        oh_node_error_set("audio");
    }
    ESP_ERROR_CHECK(oh_app_start_network());
    ESP_ERROR_CHECK(oh_http_start());
    ESP_ERROR_CHECK(oh_mqtt_start());
    ESP_ERROR_CHECK(oh_console_start());
}
