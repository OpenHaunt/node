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
#include "oh_audio.h"
#include "oh_brokers.h"
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
#define AUDIO_STATUS_PERIOD_US (1000000ULL)
#define BACKLOG 32

/* An input event that happened while the broker was out of reach. Kept, in
 * order, up to a bound; a controller that comes back gets the recent past. */
typedef struct {
    uint8_t port;
    char *payload;
} queued_t;

/* Where the leader publishes its broker list, retained (doc 15). */
#define OH_BROKERS_TOPIC "openhaunt/brokers"

static struct {
    esp_mqtt_client_handle_t client;
    /* The entry of `list` being tried or connected to. */
    char broker[OH_BROKER_ADDR_MAX];
    /* Every broker this node may use, leader first, and where in it the walk is. */
    oh_brokers_t list;
    /* Counts clients, so a retry scheduled for one that has since been replaced is
     * dropped rather than pointing the new one somewhere it was not asked to go. */
    uint32_t generation;
    uint32_t retry_generation;
    esp_timer_handle_t retry;
    /* Held while a client is started, stopped or pointed elsewhere. Not `lock`, which
     * the client's own task takes to flush the backlog: stopping a client waits for
     * that task, and the two would wait for each other. */
    SemaphoreHandle_t client_lock;
    /* Given by a new client's own task once it is running (`client_start`). */
    SemaphoreHandle_t running;
    char status_topic[48];
    char prefix[32]; /* openhaunt/<sn> */
    bool connected;
    bool net_up;
    esp_timer_handle_t health;
    esp_timer_handle_t audio_status;
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
    } else if (s.list.n) {
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

/* openhaunt/<sn>/audio/status: once a second while anything plays, once when it stops. */
static void publish_audio_status(void *arg)
{
    (void)arg;
    if (!s.client || !s.connected) {
        return;
    }
    cJSON *st = oh_audio_status_poll();
    if (!st) {
        return;
    }
    char *payload = cJSON_PrintUnformatted(st);
    cJSON_Delete(st);
    if (payload) {
        publish("audio/status", payload, 0, false);
        free(payload);
    }
}

/* ── Receiving ────────────────────────────────────────────────────────── */

/* One `openhaunt/clock` message: what time the console thinks it is. */
static void on_clock(const char *data, size_t data_len, bool retained)
{
    cJSON *json = cJSON_ParseWithLength(data, data_len);
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(json, "t");
    const cJSON *seq = cJSON_GetObjectItemCaseSensitive(json, "seq");
    if (cJSON_IsNumber(t) && cJSON_IsNumber(seq)) {
        oh_clock_console_feed((int64_t)t->valuedouble, (uint64_t)seq->valuedouble, retained);
        ESP_LOGD(TAG, "console offset %lld ms", (long long)oh_clock_console_offset_ms());
    }
    cJSON_Delete(json);
}

/* openhaunt/<sn>/output/<n>/{set,effect} */
static void on_port_message(unsigned port, const char *verb, const char *data, size_t data_len)
{
    oh_port_desc_t desc;
    oh_port_state_t state;
    if (oh_node_get((uint8_t)port, &desc, &state) != ESP_OK) {
        ESP_LOGW(TAG, "%s on unknown port %u", verb, port);
        return;
    }
    cJSON *json = cJSON_ParseWithLength(data, data_len);
    if (!json) {
        ESP_LOGW(TAG, "port %u: payload is not JSON", port);
        return;
    }

    if (strcmp(verb, "effect") == 0) {
        oh_effect_t effect;
        char err[OH_ERR_MAX] = "";
        if (oh_effect_parse(desc.type, json, &effect, err)) {
            oh_node_set_effect((uint8_t)port, &effect);
        } else {
            ESP_LOGW(TAG, "port %u: %.60s", port, err);
        }
        cJSON_Delete(json);
        return;
    }

    oh_value_t v;
    if (!oh_json_value_parse(desc.type, json, &v)) {
        ESP_LOGW(TAG, "port %u: payload does not fit a %s port", port, oh_datatype_str(desc.type));
        cJSON_Delete(json);
        return;
    }
    /* Timing rides beside the value rather than wrapping it, so a `set` with none
     * of the keys takes the path it always has. */
    oh_transition_t timed;
    const oh_value_t from = state.valid ? state.value : v;
    if (oh_transition_parse(json, &from, &v, oh_clock_console_now_ms(), &timed)) {
        oh_node_set_transition((uint8_t)port, &timed);
    } else {
        oh_node_set_output((uint8_t)port, &v, OH_SOURCE_MQTT);
    }
    cJSON_Delete(json);
}

static void on_message(const char *topic, size_t topic_len, const char *data, size_t data_len, bool retained)
{
    /* The clock is not under this node's prefix, so it is matched first and whole. */
    const size_t clock_len = strlen(OH_CLOCK_TOPIC);
    if (topic_len == clock_len && strncmp(topic, OH_CLOCK_TOPIC, clock_len) == 0) {
        on_clock(data, data_len, retained);
        return;
    }
    /* The broker list, likewise one for every node: kept, so a reboot with the leader
     * dead still knows where the others are, and acted on by `reconfigure` on the
     * event loop, which is where a client may be stopped from. */
    const size_t brokers_len = strlen(OH_BROKERS_TOPIC);
    if (topic_len == brokers_len && strncmp(topic, OH_BROKERS_TOPIC, brokers_len) == 0) {
        cJSON *json = cJSON_ParseWithLength(data, data_len);
        oh_brokers_t list;
        bool present = false;
        char err[OH_ERR_MAX];
        if (oh_brokers_parse(json, &list, &present, err) && present && list.n) {
            oh_config_set_mqtt_brokers(&list);
        } else {
            ESP_LOGW(TAG, "%s: %.60s", OH_BROKERS_TOPIC, err[0] ? err : "no list in it");
        }
        cJSON_Delete(json);
        return;
    }

    size_t plen = strlen(s.prefix);
    /* openhaunt/<sn>/audio/<verb>: doc 13. */
    if (topic_len > plen + 7 && strncmp(topic, s.prefix, plen) == 0 && strncmp(topic + plen, "/audio/", 7) == 0) {
        char verb[12];
        const size_t verb_len = topic_len - plen - 7;
        if (verb_len > 0 && verb_len < sizeof(verb)) {
            memcpy(verb, topic + plen + 7, verb_len);
            verb[verb_len] = 0;
            if (strcmp(verb, "status") != 0) {
                oh_audio_command(verb, data, data_len);
            }
        }
        return;
    }
    if (topic_len < plen + 8 || strncmp(topic, s.prefix, plen) != 0 || strncmp(topic + plen, "/output/", 8) != 0) {
        return;
    }
    const char *p = topic + plen + 8;
    const char *end = topic + topic_len;
    unsigned port = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        port = port * 10 + (unsigned)(*p - '0');
        p++;
    }
    if (port > 255 || p == end || *p != '/') {
        return;
    }
    /* The trailing segment decides which of the two this is. Copied out because the
     * topic is not null-terminated. */
    char verb[12];
    const size_t verb_len = (size_t)(end - p - 1);
    if (verb_len == 0 || verb_len >= sizeof(verb)) {
        return;
    }
    memcpy(verb, p + 1, verb_len);
    verb[verb_len] = 0;
    if (strcmp(verb, "set") != 0 && strcmp(verb, "effect") != 0) {
        return;
    }
    on_port_message(port, verb, data, data_len);
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    /* `arg` is the generation of the client this event is from. */
    if ((uint32_t)(uintptr_t)arg != s.generation) {
        return;
    }
    esp_mqtt_event_handle_t e = data;
    switch (id) {
    case MQTT_EVENT_BEFORE_CONNECT:
        xSemaphoreGive(s.running);
        break;
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "connected to %s", s.broker);
        s.connected = true;
        oh_brokers_connected(&s.list);
        esp_mqtt_client_publish(s.client, s.status_topic, "online", 0, 1, true);
        char topic[80];
        /* Both verbs, not just `set`: a port can be handed a shape as well as a
         * value. */
        snprintf(topic, sizeof(topic), "%s/output/+/+", s.prefix);
        esp_mqtt_client_subscribe(s.client, topic, 1);
        if (oh_audio_available()) {
            snprintf(topic, sizeof(topic), "%s/audio/+", s.prefix);
            esp_mqtt_client_subscribe(s.client, topic, 1);
        }
        /* Not under this node's prefix. The console publishes one clock for every
         * node on the broker, because the whole point is that they agree with each
         * other about when a cycle started. Retained, so this subscribe brings the
         * answer with it rather than waiting up to a second for the next tick. */
        esp_mqtt_client_subscribe(s.client, OH_CLOCK_TOPIC, 1);
        /* Retained too: a node that walked here hears the new order at once. */
        esp_mqtt_client_subscribe(s.client, OH_BROKERS_TOPIC, 1);
        backlog_flush();
        publish_health(NULL);
        oh_node_post(OH_EVENT_MQTT_CONNECTED, NULL, 0);
        break;
    }
    case MQTT_EVENT_DISCONNECTED: {
        if (s.connected) {
            ESP_LOGW(TAG, "disconnected from %s", s.broker);
            oh_node_post(OH_EVENT_MQTT_DISCONNECTED, NULL, 0);
        }
        s.connected = false;
        /* Refused or dropped, it is the same: the next entry, after a moment, and a
         * backoff once the whole list has failed. Only the leader's door is open, so
         * the walk ends wherever the show is being run from now. */
        const uint32_t wait_ms = oh_brokers_failed(&s.list);
        s.retry_generation = (uint32_t)(uintptr_t)arg;
        esp_timer_stop(s.retry);
        esp_timer_start_once(s.retry, (uint64_t)wait_ms * 1000ULL);
        break;
    }
    case MQTT_EVENT_DATA:
        if (e->current_data_offset == 0 && e->total_data_len == e->data_len) {
            on_message(e->topic, e->topic_len, e->data, e->data_len, e->retain);
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
    esp_timer_stop(s.retry);
    if (!s.client) {
        return;
    }
    /* Whatever this client says from here on is about a client that is going. */
    s.generation++;
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

static void uri_of(const char *addr, char *uri, size_t cap)
{
    snprintf(uri, cap, "mqtt://%s", addr);
}

static void client_start(void)
{
    if (!s.list.n || !s.net_up || s.client) {
        return;
    }
    strncpy(s.broker, oh_brokers_current(&s.list), sizeof(s.broker) - 1);
    char uri[80];
    uri_of(s.broker, uri, sizeof(uri));
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "openhaunt-%s", oh_app_sn());
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = uri,
        .credentials.client_id = client_id,
        .session.keepalive = 10,
        .session.last_will = {.topic = s.status_topic, .msg = "offline", .qos = 1, .retain = true},
        /* A sixteen-step color chase is a couple of kilobytes of JSON. The
         * default RX buffer is 1 KiB, and the fragment check below drops anything
         * that arrives in pieces — silently, which is the worst way to lose a cue. */
        .buffer.size = 4096,
        /* Reconnecting is this file's job, not the client's: the next attempt goes to
         * the next entry of the list (`retry`), not to the same broker again. */
        .network.disable_auto_reconnect = true,
        /* A dead station's address answers nothing at all; a refusal is immediate. */
        .network.timeout_ms = 3000,
        .task.priority = 5,
    };
    s.client = esp_mqtt_client_init(&cfg);
    if (!s.client) {
        ESP_LOGE(TAG, "client init failed");
        return;
    }
    s.generation++;
    esp_mqtt_client_register_event(s.client, ESP_EVENT_ANY_ID, mqtt_event, (void *)(uintptr_t)s.generation);
    xSemaphoreTake(s.running, 0);
    esp_mqtt_client_start(s.client);
    /* esp-mqtt marks a client running from inside its task, the first time that task
     * is scheduled, and destroy reads the mark without a lock. A stop that comes
     * before then is refused, destroy frees the client anyway, and the task wakes on
     * the other core in freed memory. A console restarting configures a node twice at
     * once, over HTTP and its retained list, and on a V0.2 that did exactly this. So
     * the client is not handed back until its task has said it is running. */
    if (xSemaphoreTake(s.running, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "client task did not start within 1 s");
    }
    ESP_LOGI(TAG, "connecting to %s as %s (%u of %u)", s.broker, client_id, (unsigned)s.list.current + 1,
             (unsigned)s.list.n);
}

/* esp_timer task: the next entry, on the client that asked for it. */
static void retry(void *arg)
{
    (void)arg;
    xSemaphoreTake(s.client_lock, portMAX_DELAY);
    if (s.client && s.retry_generation == s.generation && !s.connected) {
        strncpy(s.broker, oh_brokers_current(&s.list), sizeof(s.broker) - 1);
        char uri[80];
        uri_of(s.broker, uri, sizeof(uri));
        esp_mqtt_client_set_uri(s.client, uri);
        ESP_LOGI(TAG, "trying %s (%u of %u)", s.broker, (unsigned)s.list.current + 1, (unsigned)s.list.n);
        esp_mqtt_client_reconnect(s.client);
    }
    xSemaphoreGive(s.client_lock);
}

/* The event loop: a new list, from HTTP, the console's retained topic or `mqtt set`. */
static void reconfigure(void)
{
    oh_config_t cfg;
    oh_config_get(&cfg);
    xSemaphoreTake(s.client_lock, portMAX_DELAY);
    const bool move = oh_brokers_set(&s.list, &cfg.mqtt_brokers, s.connected ? s.broker : NULL);
    if (cfg.mqtt_brokers.n == 0) {
        /* Told to forget: no broker at all, the way a node comes out of the box. */
        client_stop();
        memset(&s.list, 0, sizeof(s.list));
        s.broker[0] = '\0';
    } else if (move || !s.client) {
        if (move && s.client) {
            ESP_LOGI(TAG, "moving to %s", oh_brokers_current(&s.list));
        }
        client_stop();
        client_start();
    }
    xSemaphoreGive(s.client_lock);
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
    s.client_lock = xSemaphoreCreateMutex();
    s.running = xSemaphoreCreateBinary();
    snprintf(s.prefix, sizeof(s.prefix), "openhaunt/%s", oh_app_sn());
    snprintf(s.status_topic, sizeof(s.status_topic), "%s/status", s.prefix);
    oh_config_t cfg;
    oh_config_get(&cfg);
    oh_brokers_set(&s.list, &cfg.mqtt_brokers, NULL);
#if SOC_TEMP_SENSOR_SUPPORTED
    temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&tcfg, &s_tsens) == ESP_OK) {
        temperature_sensor_enable(s_tsens);
    }
#endif
    ESP_ERROR_CHECK(esp_event_handler_register(OH_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    esp_timer_create_args_t r = {.callback = retry, .name = "mqtt-retry"};
    ESP_ERROR_CHECK(esp_timer_create(&r, &s.retry));
    esp_timer_create_args_t args = {.callback = publish_health, .name = "health"};
    ESP_ERROR_CHECK(esp_timer_create(&args, &s.health));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s.health, HEALTH_PERIOD_US));
    if (oh_audio_available()) {
        esp_timer_create_args_t a = {.callback = publish_audio_status, .name = "audio-status"};
        ESP_ERROR_CHECK(esp_timer_create(&a, &s.audio_status));
        ESP_ERROR_CHECK(esp_timer_start_periodic(s.audio_status, AUDIO_STATUS_PERIOD_US));
    }
    s.net_up = oh_net_is_up();
    client_start();
    return ESP_OK;
}
