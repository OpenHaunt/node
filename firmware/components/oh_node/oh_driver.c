#include "oh_driver.h"

#include <string.h>

#include "esp_log.h"

#define MAX_DRIVERS 16

static const oh_driver_t *s_drivers[MAX_DRIVERS];
static size_t s_count;

esp_err_t oh_driver_register(const oh_driver_t *driver)
{
    if (!driver || !driver->id || !driver->init) {
        return ESP_ERR_INVALID_ARG;
    }
    if (oh_driver_find(driver->id)) {
        return ESP_OK;
    }
    if (s_count >= MAX_DRIVERS) {
        return ESP_ERR_NO_MEM;
    }
    s_drivers[s_count++] = driver;
    return ESP_OK;
}

const oh_driver_t *oh_driver_find(const char *id)
{
    if (!id) {
        return NULL;
    }
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_drivers[i]->id, id) == 0) {
            return s_drivers[i];
        }
    }
    return NULL;
}

size_t oh_driver_list(const char **ids, size_t cap)
{
    size_t n = s_count < cap ? s_count : cap;
    for (size_t i = 0; i < n; i++) {
        ids[i] = s_drivers[i]->id;
    }
    return n;
}

int oh_driver_param_int(const cJSON *params, const char *key, int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(params, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

bool oh_driver_param_bool(const cJSON *params, const char *key, bool fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(params, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    if (cJSON_IsNumber(item)) {
        return item->valuedouble != 0;
    }
    return fallback;
}

const char *oh_driver_param_str(const cJSON *params, const char *key, const char *fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(params, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : fallback;
}
