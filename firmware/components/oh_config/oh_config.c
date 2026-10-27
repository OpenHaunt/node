#include "oh_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "oh_json.h"
#include "oh_node.h"
#include "sdkconfig.h"

static const char *TAG = "oh_config";
#define NS_CFG "ohcfg"
#define NS_MOD "ohmod"
#define VMOD_MAX 4096

static oh_config_t s_cfg;
static SemaphoreHandle_t s_lock;

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

static void nvs_read_str(nvs_handle_t h, const char *key, char *out, size_t cap)
{
    size_t len = cap;
    if (nvs_get_str(h, key, out, &len) != ESP_OK) {
        out[0] = '\0';
    }
}

static esp_err_t nvs_write_str(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_CFG, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = value[0] ? nvs_set_str(h, key, value) : nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static esp_err_t nvs_write_u16(const char *key, uint16_t value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_CFG, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u16(h, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t oh_config_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_cfg, 0, sizeof(s_cfg));
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_CFG, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no stored config");
    } else if (err != ESP_OK) {
        return err;
    } else {
        nvs_read_str(h, "name", s_cfg.name, sizeof(s_cfg.name));
        nvs_read_str(h, "mqtt_broker", s_cfg.mqtt_broker, sizeof(s_cfg.mqtt_broker));
        /* The list, where one was ever given; a node from before lists has only the
         * one broker, which is a list of one. */
        char joined[OH_BROKERS_JOINED_MAX] = "";
        nvs_read_str(h, "mqtt_brokers", joined, sizeof(joined));
        if (joined[0]) {
            oh_brokers_split(joined, &s_cfg.mqtt_brokers);
        } else {
            oh_brokers_one(s_cfg.mqtt_broker, &s_cfg.mqtt_brokers);
        }
        nvs_read_str(h, "dmx_proto", s_cfg.dmx_protocol, sizeof(s_cfg.dmx_protocol));
        nvs_get_u16(h, "dmx_univ", &s_cfg.dmx_universe);
        nvs_read_str(h, "wifi_ssid", s_cfg.wifi_ssid, sizeof(s_cfg.wifi_ssid));
        nvs_read_str(h, "wifi_psk", s_cfg.wifi_psk, sizeof(s_cfg.wifi_psk));
        uint8_t st = 0;
        nvs_get_u8(h, "static_ip", &st);
        s_cfg.static_ip = st != 0;
        nvs_read_str(h, "ip", s_cfg.ip, sizeof(s_cfg.ip));
        nvs_read_str(h, "netmask", s_cfg.netmask, sizeof(s_cfg.netmask));
        nvs_read_str(h, "gateway", s_cfg.gateway, sizeof(s_cfg.gateway));
        nvs_read_str(h, "dns", s_cfg.dns, sizeof(s_cfg.dns));
        uint8_t dark = 0;
        nvs_get_u8(h, "dark", &dark);
        s_cfg.dark = dark != 0;
        nvs_close(h);
    }
    if (!s_cfg.wifi_ssid[0] && CONFIG_OH_WIFI_SSID[0]) {
        strncpy(s_cfg.wifi_ssid, CONFIG_OH_WIFI_SSID, sizeof(s_cfg.wifi_ssid) - 1);
        strncpy(s_cfg.wifi_psk, CONFIG_OH_WIFI_PSK, sizeof(s_cfg.wifi_psk) - 1);
    }
    ESP_LOGI(TAG, "name \"%s\" broker \"%s\" dmx %s/%u wifi \"%s\" %s", s_cfg.name, s_cfg.mqtt_broker,
             s_cfg.dmx_protocol, s_cfg.dmx_universe, s_cfg.wifi_ssid, s_cfg.static_ip ? "static" : "dhcp");
    return ESP_OK;
}

void oh_config_get(oh_config_t *out)
{
    lock();
    *out = s_cfg;
    unlock();
}

bool oh_config_has_wifi(void)
{
    lock();
    bool has = s_cfg.wifi_ssid[0] != '\0';
    unlock();
    return has;
}

/* ── Setters ──────────────────────────────────────────────────────────── */

static void announce(uint32_t mask)
{
    oh_node_post(OH_EVENT_CONFIG_CHANGED, &mask, sizeof(mask));
}

esp_err_t oh_config_set_name(const char *name)
{
    lock();
    bool same = strcmp(s_cfg.name, name) == 0;
    if (!same) {
        strncpy(s_cfg.name, name, sizeof(s_cfg.name) - 1);
    }
    unlock();
    if (same) {
        return ESP_OK;
    }
    esp_err_t err = nvs_write_str("name", name);
    announce(OH_CFG_NAME);
    return err;
}

esp_err_t oh_config_set_mqtt_broker(const char *broker)
{
    oh_brokers_t one;
    oh_brokers_one(broker, &one);
    return oh_config_set_mqtt_brokers(&one);
}

esp_err_t oh_config_set_mqtt_brokers(const oh_brokers_t *list)
{
    const char *first = list->n ? list->entry[0] : "";
    lock();
    bool same = oh_brokers_equal(&s_cfg.mqtt_brokers, list);
    if (!same) {
        s_cfg.mqtt_brokers = *list;
        s_cfg.mqtt_brokers.current = 0;
        s_cfg.mqtt_brokers.backoff_ms = 0;
        strncpy(s_cfg.mqtt_broker, first, sizeof(s_cfg.mqtt_broker) - 1);
        s_cfg.mqtt_broker[sizeof(s_cfg.mqtt_broker) - 1] = '\0';
    }
    unlock();
    if (same) {
        return ESP_OK;
    }
    /* Both keys: an older firmware flashed back over this one still finds its broker. */
    char joined[OH_BROKERS_JOINED_MAX];
    oh_brokers_join(list, joined, sizeof(joined));
    esp_err_t err = nvs_write_str("mqtt_broker", first);
    if (err == ESP_OK) {
        err = nvs_write_str("mqtt_brokers", joined);
    }
    announce(OH_CFG_MQTT);
    return err;
}

esp_err_t oh_config_set_dmx(const char *protocol, uint16_t universe)
{
    lock();
    bool same = strcmp(s_cfg.dmx_protocol, protocol) == 0 && s_cfg.dmx_universe == universe;
    if (!same) {
        strncpy(s_cfg.dmx_protocol, protocol, sizeof(s_cfg.dmx_protocol) - 1);
        s_cfg.dmx_universe = universe;
    }
    unlock();
    if (same) {
        return ESP_OK;
    }
    esp_err_t err = nvs_write_str("dmx_proto", protocol);
    if (err == ESP_OK) {
        err = nvs_write_u16("dmx_univ", universe);
    }
    announce(OH_CFG_DMX);
    return err;
}

esp_err_t oh_config_set_dark(bool dark)
{
    lock();
    const bool same = s_cfg.dark == dark;
    s_cfg.dark = dark;
    unlock();
    if (same) {
        return ESP_OK;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_CFG, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "dark", dark);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    announce(OH_CFG_DARK);
    return err;
}

bool oh_config_dark(void)
{
    lock();
    const bool dark = s_cfg.dark;
    unlock();
    return dark;
}

esp_err_t oh_config_set_wifi(const char *ssid, const char *psk)
{
    lock();
    bool same = strcmp(s_cfg.wifi_ssid, ssid) == 0 && strcmp(s_cfg.wifi_psk, psk) == 0;
    if (!same) {
        strncpy(s_cfg.wifi_ssid, ssid, sizeof(s_cfg.wifi_ssid) - 1);
        strncpy(s_cfg.wifi_psk, psk, sizeof(s_cfg.wifi_psk) - 1);
    }
    unlock();
    if (same) {
        return ESP_OK;
    }
    esp_err_t err = nvs_write_str("wifi_ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_write_str("wifi_psk", psk);
    }
    announce(OH_CFG_WIFI);
    return err;
}

static esp_err_t set_network(bool static_ip, const char *ip, const char *mask, const char *gw, const char *dns)
{
    lock();
    bool same = s_cfg.static_ip == static_ip && strcmp(s_cfg.ip, ip) == 0 && strcmp(s_cfg.netmask, mask) == 0 &&
                strcmp(s_cfg.gateway, gw) == 0 && strcmp(s_cfg.dns, dns) == 0;
    if (!same) {
        s_cfg.static_ip = static_ip;
        strncpy(s_cfg.ip, ip, sizeof(s_cfg.ip) - 1);
        strncpy(s_cfg.netmask, mask, sizeof(s_cfg.netmask) - 1);
        strncpy(s_cfg.gateway, gw, sizeof(s_cfg.gateway) - 1);
        strncpy(s_cfg.dns, dns, sizeof(s_cfg.dns) - 1);
    }
    unlock();
    if (same) {
        return ESP_OK;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_CFG, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        nvs_set_u8(h, "static_ip", static_ip);
        nvs_set_str(h, "ip", ip);
        nvs_set_str(h, "netmask", mask);
        nvs_set_str(h, "gateway", gw);
        nvs_set_str(h, "dns", dns);
        err = nvs_commit(h);
        nvs_close(h);
    }
    announce(OH_CFG_NETWORK);
    return err;
}

/* ── /api/v1/config ───────────────────────────────────────────────────── */

static bool valid_ip(const char *s)
{
    esp_ip4_addr_t a;
    return s[0] && esp_netif_str_to_ip4(s, &a) == ESP_OK;
}

esp_err_t oh_config_apply_json(const cJSON *body, const oh_module_t *module, char *err, uint32_t *changed)
{
    err[0] = '\0';
    *changed = 0;
    if (!cJSON_IsObject(body)) {
        snprintf(err, OH_ERR_MAX, "config is a JSON object");
        return ESP_ERR_INVALID_ARG;
    }

    /* Pass 1: validate everything. */
    char name[OH_NAME_MAX] = "", proto[OH_PROTOCOL_MAX] = "", ssid[33] = "", psk[65] = "";
    oh_brokers_t brokers;
    char ip[16] = "", mask[16] = "", gw[16] = "", dns[16] = "";
    bool has_name = false, has_broker = false, has_dmx = false, has_wifi = false, has_net = false, static_ip = false;
    bool has_dark = false, dark = false;
    uint16_t universe = 0;

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(body, "name");
    if (item) {
        if (!cJSON_IsString(item) || strlen(item->valuestring) >= OH_NAME_MAX) {
            snprintf(err, OH_ERR_MAX, "name is a string of up to %d characters", OH_NAME_MAX - 1);
            return ESP_ERR_INVALID_ARG;
        }
        strcpy(name, item->valuestring);
        has_name = true;
    }
    item = cJSON_GetObjectItemCaseSensitive(body, "mqtt");
    if (item) {
        if (!cJSON_IsObject(item)) {
            snprintf(err, OH_ERR_MAX, "mqtt is an object");
            return ESP_ERR_INVALID_ARG;
        }
        /* `brokers` from a console that fails over (doc 15), `broker` alone from one
         * that does not; either way a list. */
        if (!oh_brokers_parse(item, &brokers, &has_broker, err)) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    item = cJSON_GetObjectItemCaseSensitive(body, "dmx");
    if (item) {
        if (!module || !module->has_dmx) {
            snprintf(err, OH_ERR_MAX, "this node forwards no universe; dmx config is not accepted");
            return ESP_ERR_NOT_SUPPORTED;
        }
        if (!cJSON_IsObject(item)) {
            snprintf(err, OH_ERR_MAX, "dmx is an object");
            return ESP_ERR_INVALID_ARG;
        }
        const cJSON *p = cJSON_GetObjectItemCaseSensitive(item, "protocol");
        const cJSON *u = cJSON_GetObjectItemCaseSensitive(item, "universe");
        if (!cJSON_IsString(p) || !oh_module_dmx_accepts(module, p->valuestring)) {
            snprintf(err, OH_ERR_MAX, "dmx.protocol must be one this node listed");
            return ESP_ERR_INVALID_ARG;
        }
        if (!cJSON_IsNumber(u) || u->valuedouble < 1 || u->valuedouble > 63999) {
            snprintf(err, OH_ERR_MAX, "dmx.universe is 1..63999");
            return ESP_ERR_INVALID_ARG;
        }
        strncpy(proto, p->valuestring, sizeof(proto) - 1);
        universe = (uint16_t)u->valuedouble;
        has_dmx = true;
    }
    item = cJSON_GetObjectItemCaseSensitive(body, "wifi");
    if (item) {
        if (!cJSON_IsObject(item)) {
            snprintf(err, OH_ERR_MAX, "wifi is an object");
            return ESP_ERR_INVALID_ARG;
        }
        const cJSON *s = cJSON_GetObjectItemCaseSensitive(item, "ssid");
        const cJSON *p = cJSON_GetObjectItemCaseSensitive(item, "psk");
        if (!cJSON_IsString(s) || strlen(s->valuestring) > 32) {
            snprintf(err, OH_ERR_MAX, "wifi.ssid is a string of up to 32 characters");
            return ESP_ERR_INVALID_ARG;
        }
        if (p && (!cJSON_IsString(p) || strlen(p->valuestring) > 64)) {
            snprintf(err, OH_ERR_MAX, "wifi.psk is a string of up to 64 characters");
            return ESP_ERR_INVALID_ARG;
        }
        strcpy(ssid, s->valuestring);
        if (p) {
            strcpy(psk, p->valuestring);
        } else {
            /* Same network, keep the passphrase we have. */
            lock();
            if (strcmp(s_cfg.wifi_ssid, ssid) == 0) {
                strcpy(psk, s_cfg.wifi_psk);
            }
            unlock();
        }
        has_wifi = true;
    }
    item = cJSON_GetObjectItemCaseSensitive(body, "network");
    if (item) {
        if (!cJSON_IsObject(item)) {
            snprintf(err, OH_ERR_MAX, "network is an object");
            return ESP_ERR_INVALID_ARG;
        }
        const char *mode = "dhcp";
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(item, "mode");
        if (cJSON_IsString(m)) {
            mode = m->valuestring;
        }
        if (strcmp(mode, "static") == 0) {
            static_ip = true;
            oh_json_copy_string(item, "ip", ip, sizeof(ip));
            oh_json_copy_string(item, "netmask", mask, sizeof(mask));
            oh_json_copy_string(item, "gateway", gw, sizeof(gw));
            oh_json_copy_string(item, "dns", dns, sizeof(dns));
            if (!valid_ip(ip) || !valid_ip(mask) || (gw[0] && !valid_ip(gw)) || (dns[0] && !valid_ip(dns))) {
                snprintf(err, OH_ERR_MAX, "network: static mode needs valid ip, netmask and optional gateway, dns");
                return ESP_ERR_INVALID_ARG;
            }
        } else if (strcmp(mode, "dhcp") != 0) {
            snprintf(err, OH_ERR_MAX, "network.mode is dhcp or static");
            return ESP_ERR_INVALID_ARG;
        }
        has_net = true;
    }

    item = cJSON_GetObjectItemCaseSensitive(body, "dark");
    if (item) {
        if (!cJSON_IsBool(item)) {
            snprintf(err, OH_ERR_MAX, "dark is true or false");
            return ESP_ERR_INVALID_ARG;
        }
        dark = cJSON_IsTrue(item);
        has_dark = true;
    }

    /* Pass 2: apply, each setter persisting and announcing only on change. */
    oh_config_t before;
    oh_config_get(&before);
    if (has_name) {
        oh_config_set_name(name);
        *changed |= strcmp(before.name, name) ? OH_CFG_NAME : 0;
    }
    if (has_broker) {
        oh_config_set_mqtt_brokers(&brokers);
        *changed |= oh_brokers_equal(&before.mqtt_brokers, &brokers) ? 0 : OH_CFG_MQTT;
    }
    if (has_dmx) {
        oh_config_set_dmx(proto, universe);
        *changed |= (strcmp(before.dmx_protocol, proto) || before.dmx_universe != universe) ? OH_CFG_DMX : 0;
    }
    if (has_wifi) {
        oh_config_set_wifi(ssid, psk);
        *changed |= (strcmp(before.wifi_ssid, ssid) || strcmp(before.wifi_psk, psk)) ? OH_CFG_WIFI : 0;
    }
    if (has_dark) {
        oh_config_set_dark(dark);
        *changed |= before.dark != dark ? OH_CFG_DARK : 0;
    }
    if (has_net) {
        set_network(static_ip, ip, mask, gw, dns);
        *changed |= (before.static_ip != static_ip || strcmp(before.ip, ip) || strcmp(before.netmask, mask) ||
                     strcmp(before.gateway, gw) || strcmp(before.dns, dns))
                        ? OH_CFG_NETWORK
                        : 0;
    }
    if (*changed) {
        ESP_LOGI(TAG, "config changed: mask 0x%02lx", (unsigned long)*changed);
    }
    return ESP_OK;
}

cJSON *oh_config_to_json(void)
{
    oh_config_t c;
    oh_config_get(&c);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", c.name);
    cJSON *mqtt = cJSON_AddObjectToObject(o, "mqtt");
    cJSON_AddStringToObject(mqtt, "broker", c.mqtt_broker);
    cJSON_AddItemToObject(mqtt, "brokers", oh_brokers_to_json(&c.mqtt_brokers));
    if (c.dmx_universe) {
        cJSON *dmx = cJSON_AddObjectToObject(o, "dmx");
        cJSON_AddStringToObject(dmx, "protocol", c.dmx_protocol);
        cJSON_AddNumberToObject(dmx, "universe", c.dmx_universe);
    }
    cJSON *wifi = cJSON_AddObjectToObject(o, "wifi");
    cJSON_AddStringToObject(wifi, "ssid", c.wifi_ssid);
    cJSON_AddBoolToObject(wifi, "pskSet", c.wifi_psk[0] != '\0');
    cJSON *net = cJSON_AddObjectToObject(o, "network");
    cJSON_AddStringToObject(net, "mode", c.static_ip ? "static" : "dhcp");
    cJSON_AddStringToObject(net, "ip", c.ip);
    cJSON_AddStringToObject(net, "netmask", c.netmask);
    cJSON_AddStringToObject(net, "gateway", c.gateway);
    cJSON_AddStringToObject(net, "dns", c.dns);
    cJSON_AddBoolToObject(o, "dark", c.dark);
    return o;
}

void oh_config_node_name(const oh_module_t *module, const char *sn, char *out, size_t cap)
{
    lock();
    if (s_cfg.name[0]) {
        strncpy(out, s_cfg.name, cap - 1);
        out[cap - 1] = '\0';
    } else {
        snprintf(out, cap, "%s %s", module->module.name, sn);
    }
    unlock();
}

/* ── Virtual module ───────────────────────────────────────────────────── */

esp_err_t oh_config_vmod_get(char **out)
{
    *out = NULL;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_MOD, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : err;
    }
    size_t len = 0;
    err = nvs_get_blob(h, "vmod", NULL, &len);
    if (err == ESP_OK && len > 0) {
        char *buf = malloc(len + 1);
        if (!buf) {
            nvs_close(h);
            return ESP_ERR_NO_MEM;
        }
        err = nvs_get_blob(h, "vmod", buf, &len);
        buf[len] = '\0';
        if (err == ESP_OK) {
            *out = buf;
        } else {
            free(buf);
        }
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_ERR_NOT_FOUND;
    }
    nvs_close(h);
    return err;
}

esp_err_t oh_config_vmod_set(const char *json)
{
    size_t len = strlen(json);
    if (len == 0 || len > VMOD_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_MOD, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, "vmod", json, len);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t oh_config_vmod_clear(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_MOD, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    }
    err = nvs_erase_key(h, "vmod");
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

uint16_t oh_config_ws_count(void)
{
    nvs_handle_t h;
    uint16_t count = 8;
    if (nvs_open(NS_MOD, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u16(h, "ws_count", &count);
        nvs_close(h);
    }
    return count;
}

esp_err_t oh_config_set_ws_count(uint16_t count)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_MOD, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u16(h, "ws_count", count);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static void pixel_key(int output, char *key)
{
    snprintf(key, 8, "pix%d", output);
}

esp_err_t oh_config_pixels_get(int output, oh_pix_cfg_t *out)
{
    memset(out, 0, sizeof(*out));
    if (output < 0 || output >= OH_CFG_PIXEL_OUTPUTS) {
        return ESP_ERR_INVALID_ARG;
    }
    char key[8];
    pixel_key(output, key);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_MOD, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    char text[256];
    size_t len = sizeof(text);
    err = nvs_get_str(h, key, text, &len);
    nvs_close(h);
    if (err != ESP_OK) {
        return err;
    }
    cJSON *body = cJSON_Parse(text);
    oh_pix_cfg_t c;
    oh_pix_cfg_default(&c);
    char why[OH_ERR_MAX];
    const bool ok = body && oh_pix_cfg_apply_json(&c, body, why);
    cJSON_Delete(body);
    if (!ok) {
        ESP_LOGW(TAG, "stored settings for strip %d are unusable; defaults", output);
        return ESP_ERR_INVALID_STATE;
    }
    *out = c;
    return ESP_OK;
}

esp_err_t oh_config_pixels_set(int output, const oh_pix_cfg_t *cfg)
{
    if (output < 0 || output >= OH_CFG_PIXEL_OUTPUTS) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *o = oh_pix_cfg_to_json(cfg, (uint8_t)output, 0);
    /* Only the settings themselves: what is derived (universes, max_fps) is worked
     * out again from them, and stays right if the arithmetic changes. */
    cJSON_DeleteItemFromObject(o, "output");
    cJSON_DeleteItemFromObject(o, "head");
    cJSON_DeleteItemFromObject(o, "universes");
    cJSON_DeleteItemFromObject(o, "max_fps");
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!text) {
        return ESP_ERR_NO_MEM;
    }
    char key[8];
    pixel_key(output, key);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_MOD, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, key, text);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    free(text);
    return err;
}

esp_err_t oh_config_erase_all(void)
{
    esp_err_t err = nvs_flash_erase();
    if (err == ESP_OK) {
        err = nvs_flash_init();
    }
    return err;
}
