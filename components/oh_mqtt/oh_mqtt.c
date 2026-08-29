#include "oh_mqtt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mqtt_client.h"
#include "oh_app.h"
#include "oh_clock.h"
#include "oh_config.h"
#include "oh_json.h"
#include "oh_net.h"
#include "oh_node.h"
#include "sdkconfig.h"

#if SOC_TEMP_SENSOR_SUPPORTED
#include "driver/temperature_sensor.h"
static temperature_sensor_handle_t s_tsens;
#endif

static const char *TAG = "oh_mqtt";

#define HEALTH_PERIOD_US (10 * 1000000ULL)
#define BACKLOG 32

/* An input event that happened while the broker was out of reach. Kept, in
 * order, up to a bound; a controller that comes back gets the recent past. */
typedef struct {
    uint8_t port;
    char *payload;
} queued_t;

static struct {
    esp_mqtt_client_handle_t client;
    char broker[64];
    char status_topic[48];
    char prefix[32]; /* openhaunt/<sn> */
    bool connected;
    bool net_up;
    esp_timer_handle_t health;
    SemaphoreHandle_t lock;
    queued_t backlog[BACKLOG];
    size_t backlog_n;
} s;

bool oh_mqtt_connected(void)
{
    return s.connected;
}

const char *oh_mqtt_broker(void)
{
    return s.broker;
}

/* ── Publishing ───────────────────────────────────────────────────────── */

static void publish(const char *suffix, const char *payload, int qos, bool retain)
{
    char topic[80];
    snprintf(topic, sizeof(topic), "%s/%s", s.prefix, suffix);
    esp_mqtt_client_publish(s.client, topic, payload, 0, qos, retain);
}

static void backlog_push(uint8_t port, char *payload)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (s.backlog_n == BACKLOG) {
        free(s.backlog[0].payload);
        memmove(&s.backlog[0], &s.backlog[1], (BACKLOG - 1) * sizeof(queued_t));
        s.backlog_n--;
    }
    s.backlog[s.backlog_n++] = (queued_t){.port = port, .payload = payload};
    xSemaphoreGive(s.lock);
}

static void backlog_flush(void)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    for (size_t i = 0; i < s.backlog_n; i++) {
        char suffix[24];
        snprintf(suffix, sizeof(suffix), "input/%u", s.backlog[i].port);
        publish(suffix, s.backlog[i].payload, 1, false);
        free(s.backlog[i].payload);
    }
    if (s.backlog_n) {
        ESP_LOGI(TAG, "flushed %u queued input event(s)", (unsigned)s.backlog_n);
    }
    s.backlog_n = 0;
    xSemaphoreGive(s.lock);
}

static void publish_input(const oh_port_changed_t *c)
{
    oh_port_desc_t desc;
    if (oh_node_get(c->port, &desc, NULL) != ESP_OK) {
        return;
    }
    cJSON *ev = oh_json_input_event(&desc, &c->value, c->ts_ms);
    char *payload = cJSON_PrintUnformatted(ev);
    cJSON_Delete(ev);
    if (!payload) {
        return;
    }
    if (s.client && s.connected) {
        char suffix[24];
        snprintf(suffix, sizeof(suffix), "input/%u", c->port);
        publish(suffix, payload, 1, false);
        free(payload);
    } else if (s.broker[0]) {
        backlog_push(c->port, payload);
    } else {
        free(payload);
    }
}

static void publish_health(void *arg)
{
    (void)arg;
    if (!s.client || !s.connected) {
        return;
    }
    const float *temp_p = NULL;
#if SOC_TEMP_SENSOR_SUPPORTED
    float temp = 0;
    if (s_tsens && temperature_sensor_get_celsius(s_tsens, &temp) == ESP_OK) {
        temp_p = &temp;
    }
#endif
    char codes[OH_MAX_ERRORS][OH_ERROR_CODE_MAX];
    const char *errs[OH_MAX_ERRORS];
    size_t n = oh_node_errors(codes, OH_MAX_ERRORS);
    for (size_t i = 0; i < n; i++) {
        errs[i] = codes[i];
    }
    cJSON *h = oh_json_health(oh_clock_uptime_s(), temp_p, NULL, errs, n);
    char *payload = cJSON_PrintUnformatted(h);
    cJSON_Delete(h);
    if (payload) {
        publish("health", payload, 0, false);
        free(payload);
    }
}

/* ── Receiving ────────────────────────────────────────────────────────── */

/* openhaunt/<sn>/output/<n>/set */
static void on_message(const char *topic, size_t topic_len, const char *data, size_t data_len)
{
    size_t plen = strlen(s.prefix);
    if (topic_len < plen + 12 || strncmp(topic, s.prefix, plen) != 0 || strncmp(topic + plen, "/output/", 8) != 0) {
        return;
    }
    const char *p = topic + plen + 8;
    const char *end = topic + topic_len;
    unsigned port = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        port = port * 10 + (unsigned)(*p - '0');
        p++;
    }
    if (port > 255 || end - p != 4 || strncmp(p, "/set", 4) != 0) {
        return;
    }
    oh_port_desc_t desc;
    if (oh_node_get((uint8_t)port, &desc, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "set on unknown port %u", port);
        return;
    }
    cJSON *json = cJSON_ParseWithLength(data, data_len);
    oh_value_t v;
    if (!json || !oh_json_value_parse(desc.type, json, &v)) {
        ESP_LOGW(TAG, "port %u: payload does not fit a %s port", port, oh_datatype_str(desc.type));
        cJSON_Delete(json);
        return;
    }
    cJSON_Delete(json);
    oh_node_set_output((uint8_t)port, &v, OH_SOURCE_MQTT);
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t e = data;
    switch (id) {
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "connected to %s", s.broker);
        s.connected = true;
        esp_mqtt_client_publish(s.client, s.status_topic, "online", 0, 1, true);
        char topic[80];
        snprintf(topic, sizeof(topic), "%s/output/+/set", s.prefix);
        esp_mqtt_client_subscribe(s.client, topic, 1);
        backlog_flush();
        publish_health(NULL);
        oh_node_post(OH_EVENT_MQTT_CONNECTED, NULL, 0);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        if (s.connected) {
            ESP_LOGW(TAG, "disconnected");
            oh_node_post(OH_EVENT_MQTT_DISCONNECTED, NULL, 0);
        }
        s.connected = false;
        break;
    case MQTT_EVENT_DATA:
        if (e->current_data_offset == 0 && e->total_data_len == e->data_len) {
            on_message(e->topic, e->topic_len, e->data, e->data_len);
        }
        break;
    case MQTT_EVENT_ERROR:
        if (e->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGD(TAG, "transport error (%s)", strerror(e->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        break;
    }
}

/* ── Lifecycle ────────────────────────────────────────────────────────── */

static void client_stop(void)
{
    if (!s.client) {
        return;
    }
    if (s.connected) {
        /* Leaving on purpose: say so, rather than letting the will say it later. */
        esp_mqtt_client_publish(s.client, s.status_topic, "offline", 0, 1, true);
        s.connected = false;
        oh_node_post(OH_EVENT_MQTT_DISCONNECTED, NULL, 0);
    }
    esp_mqtt_client_stop(s.client);
    esp_mqtt_client_destroy(s.client);
    s.client = NULL;
}

static void client_start(void)
{
    if (!s.broker[0] || !s.net_up || s.client) {
        return;
    }
    char uri[80];
    snprintf(uri, sizeof(uri), "mqtt://%s", s.broker);
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "openhaunt-%s", oh_app_sn());
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = uri,
        .credentials.client_id = client_id,
        .session.keepalive = 10,
        .session.last_will = {.topic = s.status_topic, .msg = "offline", .qos = 1, .retain = true},
        .network.reconnect_timeout_ms = 2000,
        .network.disable_auto_reconnect = false,
        .task.priority = 5,
    };
    s.client = esp_mqtt_client_init(&cfg);
    if (!s.client) {
        ESP_LOGE(TAG, "client init failed");
        return;
    }
    esp_mqtt_client_register_event(s.client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    esp_mqtt_client_start(s.client);
    ESP_LOGI(TAG, "connecting to %s as %s", s.broker, client_id);
}

static void reconfigure(void)
{
    oh_config_t cfg;
    oh_config_get(&cfg);
    if (strcmp(cfg.mqtt_broker, s.broker) == 0 && (s.client || !s.net_up)) {
        return; /* same broker, already running: the idempotent case */
    }
    client_stop();
    strncpy(s.broker, cfg.mqtt_broker, sizeof(s.broker) - 1);
    client_start();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    switch (id) {
    case OH_EVENT_NET_UP:
        s.net_up = true;
        reconfigure();
        break;
    case OH_EVENT_NET_DOWN:
    case OH_EVENT_NET_SETUP_AP:
        s.net_up = oh_net_is_up();
        break;
    case OH_EVENT_CONFIG_CHANGED:
        if (*(uint32_t *)data & OH_CFG_MQTT) {
            reconfigure();
        }
        break;
    case OH_EVENT_PORT_CHANGED: {
        const oh_port_changed_t *c = data;
        if (c->is_input) {
            publish_input(c);
        }
        break;
    }
    case OH_EVENT_ERRORS_CHANGED:
        publish_health(NULL);
        break;
    default:
        break;
    }
}

esp_err_t oh_mqtt_start(void)
{
    s.lock = xSemaphoreCreateMutex();
    snprintf(s.prefix, sizeof(s.prefix), "openhaunt/%s", oh_app_sn());
    snprintf(s.status_topic, sizeof(s.status_topic), "%s/status", s.prefix);
    oh_config_t cfg;
    oh_config_get(&cfg);
    strncpy(s.broker, cfg.mqtt_broker, sizeof(s.broker) - 1);
#if SOC_TEMP_SENSOR_SUPPORTED
    temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&tcfg, &s_tsens) == ESP_OK) {
        temperature_sensor_enable(s_tsens);
    }
#endif
    ESP_ERROR_CHECK(esp_event_handler_register(OH_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    esp_timer_create_args_t args = {.callback = publish_health, .name = "health"};
    ESP_ERROR_CHECK(esp_timer_create(&args, &s.health));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s.health, HEALTH_PERIOD_US));
    s.net_up = oh_net_is_up();
    client_start();
    return ESP_OK;
}
