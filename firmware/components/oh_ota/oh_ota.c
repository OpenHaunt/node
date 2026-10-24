#include "oh_ota.h"

#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "oh_node.h"

static const char *TAG = "oh_ota";

struct oh_ota_session {
    esp_ota_handle_t handle;
    const esp_partition_t *target;
    size_t written;
    size_t total;
};

esp_err_t oh_ota_begin(size_t total_size, oh_ota_session_t **out)
{
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        return ESP_ERR_NOT_FOUND;
    }
    if (total_size && total_size > target->size) {
        ESP_LOGE(TAG, "image of %u bytes does not fit the %lu-byte slot", (unsigned)total_size,
                 (unsigned long)target->size);
        return ESP_ERR_INVALID_SIZE;
    }
    oh_ota_session_t *s = calloc(1, sizeof(*s));
    if (!s) {
        return ESP_ERR_NO_MEM;
    }
    s->target = target;
    s->total = total_size;
    esp_err_t err = esp_ota_begin(target, total_size ? total_size : OTA_WITH_SEQUENTIAL_WRITES, &s->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "begin: %s", esp_err_to_name(err));
        free(s);
        return err;
    }
    ESP_LOGI(TAG, "writing %u bytes to %s", (unsigned)total_size, target->label);
    oh_node_post(OH_EVENT_OTA_STARTED, NULL, 0);
    *out = s;
    return ESP_OK;
}

esp_err_t oh_ota_write(oh_ota_session_t *s, const void *data, size_t len)
{
    esp_err_t err = esp_ota_write(s->handle, data, len);
    if (err == ESP_OK) {
        s->written += len;
    }
    return err;
}

esp_err_t oh_ota_end(oh_ota_session_t *s, char *version, size_t version_cap)
{
    esp_err_t err = esp_ota_end(s->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "end: %s", esp_err_to_name(err));
        free(s);
        oh_node_post(OH_EVENT_OTA_FINISHED, NULL, 0);
        return err;
    }
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(s->target, &desc) == ESP_OK) {
        strncpy(version, desc.version, version_cap - 1);
        version[version_cap - 1] = '\0';
    } else {
        version[0] = '\0';
    }
    err = esp_ota_set_boot_partition(s->target);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%u bytes written; %s (%s) boots next", (unsigned)s->written, s->target->label, version);
    } else {
        ESP_LOGE(TAG, "set boot partition: %s", esp_err_to_name(err));
    }
    free(s);
    oh_node_post(OH_EVENT_OTA_FINISHED, NULL, 0);
    return err;
}

void oh_ota_abort(oh_ota_session_t *s)
{
    esp_ota_abort(s->handle);
    free(s);
    oh_node_post(OH_EVENT_OTA_FINISHED, NULL, 0);
}

bool oh_ota_pending_verify(void)
{
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    return esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
}

esp_err_t oh_ota_confirm(void)
{
    if (!oh_ota_pending_verify()) {
        return ESP_OK;
    }
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "image confirmed: %s", esp_err_to_name(err));
    return err;
}

const char *oh_ota_running_partition(void)
{
    return esp_ota_get_running_partition()->label;
}

static void on_net_up(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    /* Reaching the network is the bar a new image has to clear. */
    oh_ota_confirm();
}

esp_err_t oh_ota_start(void)
{
    if (oh_ota_pending_verify()) {
        ESP_LOGW(TAG, "running an unconfirmed image from %s; it is confirmed once the network is up",
                 oh_ota_running_partition());
    }
    return esp_event_handler_register(OH_EVENT, OH_EVENT_NET_UP, on_net_up, NULL);
}
