/*
 * What a node is told, and remembers across reboots: the broker, the
 * universe, its friendly name, and how to reach the network. NVS-backed.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#include "oh_brokers.h"
#include "oh_labels.h"
#include "oh_module.h"
#include "oh_pixel.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OH_CFG_NAME (1u << 0)
#define OH_CFG_MQTT (1u << 1)
#define OH_CFG_DMX (1u << 2)
#define OH_CFG_WIFI (1u << 3)
#define OH_CFG_NETWORK (1u << 4)
#define OH_CFG_DARK (1u << 5)
#define OH_CFG_LABELS (1u << 6)

typedef struct {
    char name[OH_NAME_MAX];   /* "" = default "<module name> <sn>" */
    char mqtt_broker[64];     /* "host:port", "" = none: the list's first entry */
    oh_brokers_t mqtt_brokers; /* every broker it may use, leader first (doc 15) */
    char dmx_protocol[OH_PROTOCOL_MAX];
    uint16_t dmx_universe;    /* 0 = none */
    char wifi_ssid[33];
    char wifi_psk[65];
    bool static_ip;
    char ip[16], netmask[16], gateway[16], dns[16];
    bool dark;                /* indicator LEDs off, identify still blinks */
} oh_config_t;

esp_err_t oh_config_init(void);
void oh_config_get(oh_config_t *out);

/* Merge a partial /api/v1/config body. The whole body is validated before
 * anything changes; on failure `err` (OH_ERR_MAX) says why and nothing was
 * applied. Changed keys are persisted and announced with
 * OH_EVENT_CONFIG_CHANGED carrying the OH_CFG_* mask; unchanged keys are
 * left alone, so re-sending the same config is free. */
esp_err_t oh_config_apply_json(const cJSON *body, const oh_module_t *module, char *err, uint32_t *changed);

/* GET /api/v1/config: the same shape, with the passphrase redacted. */
cJSON *oh_config_to_json(void);

esp_err_t oh_config_set_name(const char *name);
/* One broker: the list of one a console from before lists sends, or `mqtt set`. */
esp_err_t oh_config_set_mqtt_broker(const char *broker);
/* The whole list, as `mqtt.brokers` or `openhaunt/brokers` carries it. */
esp_err_t oh_config_set_mqtt_brokers(const oh_brokers_t *list);
esp_err_t oh_config_set_dmx(const char *protocol, uint16_t universe);
esp_err_t oh_config_set_wifi(const char *ssid, const char *psk);
bool oh_config_has_wifi(void);
/* Dark mode: the node's indicator LEDs stay off (the BOOT button toggles it on V0). */
esp_err_t oh_config_set_dark(bool dark);
bool oh_config_dark(void);

/* What somebody called the ports and heads (`labels` in /config), a copy. */
void oh_config_labels(oh_labels_t *out);

/* The friendly name as the TXT record and /info carry it. */
void oh_config_node_name(const oh_module_t *module, const char *sn, char *out, size_t cap);

/* The virtual module, as JSON text. *out is malloc'ed; NULL when none. */
esp_err_t oh_config_vmod_get(char **out);
esp_err_t oh_config_vmod_set(const char *json);
esp_err_t oh_config_vmod_clear(void);

/* WS2812 pixel count for an EEPROM-described 0x0003, which the descriptor
 * cannot carry. */
uint16_t oh_config_ws_count(void);
esp_err_t oh_config_set_ws_count(uint16_t count);

/* Everything, gone. */
/*
 * A strip output's mode and settings (doc 14). `get` leaves `out->count` at zero
 * when nothing is stored, which the V0 builder reads as "the defaults". Stored as
 * the strip's own JSON and parsed back through the same validation, so a value
 * that could not have been set over HTTP cannot come back out of flash either.
 */
#define OH_CFG_PIXEL_OUTPUTS 2
esp_err_t oh_config_pixels_get(int output, oh_pix_cfg_t *out);
esp_err_t oh_config_pixels_set(int output, const oh_pix_cfg_t *cfg);

esp_err_t oh_config_erase_all(void);

#ifdef __cplusplus
}
#endif
