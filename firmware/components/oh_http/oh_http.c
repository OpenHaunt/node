#include "oh_http.h"

#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "esp_core_dump.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "oh_app.h"
#include "oh_audio.h"
#include "oh_board.h"
#include "oh_clock.h"
#include "oh_config.h"
#include "oh_json.h"
#include "oh_net.h"
#include "oh_node.h"
#include "oh_ota.h"
#include "oh_sacn.h"
#include "oh_status.h"
#include "oh_vmod.h"

static const char *TAG = "oh_http";
static httpd_handle_t s_server;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

/* ── Helpers ──────────────────────────────────────────────────────────── */

static esp_err_t send_json(httpd_req_t *req, cJSON *json, const char *status)
{
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!text) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_status(req, status ? status : "200 OK");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    free(text);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message)
{
    return send_json(req, oh_json_error(message), status);
}

/* Read a JSON body of up to `cap` bytes. Returns NULL (and answers) on trouble. */
static cJSON *read_json(httpd_req_t *req, size_t cap)
{
    if (req->content_len == 0) {
        return cJSON_CreateObject();
    }
    if (req->content_len > cap) {
        send_error(req, "413 Payload Too Large", "body too large");
        return NULL;
    }
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        send_error(req, "500 Internal Server Error", "out of memory");
        return NULL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, buf + got, req->content_len - got);
        if (n <= 0) {
            free(buf);
            send_error(req, "400 Bad Request", "body incomplete");
            return NULL;
        }
        got += n;
    }
    buf[got] = '\0';
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    if (!json) {
        send_error(req, "400 Bad Request", "body is not JSON");
    }
    return json;
}

/* ── The protocol ─────────────────────────────────────────────────────── */

static esp_err_t get_info(httpd_req_t *req)
{
    char name[OH_NAME_MAX];
    oh_app_node_name(name, sizeof(name));
    oh_node_identity_t id = {.sn = oh_app_sn(), .fw = oh_app_fw(), .name = name};
    oh_node_lock();
    cJSON *info = oh_json_info(oh_node_module(), &id);
    oh_node_unlock();
    if (oh_audio_available()) {
        oh_audio_caps_t caps;
        oh_audio_caps(&caps);
        oh_json_audio_caps(info, &caps);
    }
    cJSON *pixels = oh_app_pixels_json(false);
    if (pixels) {
        cJSON_AddItemToObject(info, "pixels", pixels);
    }
    return send_json(req, info, NULL);
}

static esp_err_t get_state(httpd_req_t *req)
{
    oh_node_lock();
    cJSON *state = oh_json_state(oh_node_module(), oh_node_states());
    oh_node_unlock();
    return send_json(req, state, NULL);
}

/*
 * A value, with or without a time to reach it by.
 *
 * The timing rides beside an ordinary payload rather than wrapping one, so a body
 * with no `fade_ms` takes exactly the path it always has: this looks for the keys,
 * finds none, and applies the value.
 */
static void apply_output(void *ctx, uint8_t port, const oh_value_t *value, const cJSON *payload)
{
    (void)ctx;
    oh_port_desc_t desc;
    oh_port_state_t state;
    oh_transition_t timed;

    const bool known = oh_node_get(port, &desc, &state) == ESP_OK;
    const oh_value_t from = known && state.valid ? state.value : *value;
    if (oh_transition_parse(payload, &from, value, oh_clock_console_now_ms(), &timed)) {
        oh_node_set_transition(port, &timed);
        return;
    }
    oh_node_set_output(port, value, OH_SOURCE_HTTP);
}

static void apply_effect(void *ctx, uint8_t port, const oh_effect_t *effect)
{
    (void)ctx;
    oh_node_set_effect(port, effect);
}

static esp_err_t post_state(httpd_req_t *req)
{
    cJSON *body = read_json(req, 4096);
    if (!body) {
        return ESP_FAIL;
    }
    char err[OH_ERR_MAX];
    /* The node lock is recursive: the callback's oh_node_set_output takes it
     * again from this task, and nobody else can rebind the module meanwhile. */
    oh_node_lock();
    int n = oh_json_apply_outputs(body, oh_node_module(), apply_output, apply_effect, NULL, err);
    oh_node_unlock();
    cJSON_Delete(body);
    if (n < 0) {
        return send_error(req, "400 Bad Request", err);
    }
    return send_json(req, oh_json_ok(), NULL);
}

static esp_err_t get_config(httpd_req_t *req)
{
    return send_json(req, oh_config_to_json(), NULL);
}

static esp_err_t post_config(httpd_req_t *req)
{
    cJSON *body = read_json(req, 2048);
    if (!body) {
        return ESP_FAIL;
    }
    char err[OH_ERR_MAX];
    uint32_t changed = 0;
    oh_node_lock();
    esp_err_t rc = oh_config_apply_json(body, oh_node_module(), err, &changed);
    oh_node_unlock();
    cJSON_Delete(body);
    if (rc != ESP_OK) {
        return send_error(req, "400 Bad Request", err);
    }
    cJSON *ok = oh_json_ok();
    cJSON_AddNumberToObject(ok, "changed", changed);
    return send_json(req, ok, NULL);
}

static esp_err_t post_identify(httpd_req_t *req)
{
    oh_node_identify(5000);
    return send_json(req, oh_json_ok(), NULL);
}

/* Raw image body -> the other app slot -> reboot. */
static esp_err_t post_ota(httpd_req_t *req)
{
    if (req->content_len < 64 * 1024) {
        return send_error(req, "400 Bad Request", "that is not a firmware image");
    }
    oh_ota_session_t *ota;
    esp_err_t err = oh_ota_begin(req->content_len, &ota);
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    char *buf = malloc(4096);
    if (!buf) {
        oh_ota_abort(ota);
        return send_error(req, "500 Internal Server Error", "out of memory");
    }
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, buf, 4096);
        if (n <= 0) {
            free(buf);
            oh_ota_abort(ota);
            return send_error(req, "400 Bad Request", "upload interrupted");
        }
        err = oh_ota_write(ota, buf, n);
        if (err != ESP_OK) {
            free(buf);
            oh_ota_abort(ota);
            return send_error(req, "400 Bad Request", err == ESP_ERR_OTA_VALIDATE_FAILED
                                                          ? "not a valid image for this chip"
                                                          : esp_err_to_name(err));
        }
        got += n;
    }
    free(buf);
    char version[32];
    err = oh_ota_end(ota, version, sizeof(version));
    if (err != ESP_OK) {
        return send_error(req, "400 Bad Request",
                          err == ESP_ERR_OTA_VALIDATE_FAILED ? "image failed validation" : esp_err_to_name(err));
    }
    cJSON *ok = oh_json_ok();
    cJSON_AddStringToObject(ok, "version", version);
    cJSON_AddNumberToObject(ok, "size", (double)got);
    err = send_json(req, ok, NULL);
    oh_app_reboot(1000);
    return err;
}

/* ── Audio clips (OpenHaunt/node doc 13) ──────────────────────────────── */

static const char *status_line(int code)
{
    switch (code) {
    case 200:
        return "200 OK";
    case 201:
        return "201 Created";
    case 204:
        return "204 No Content";
    case 400:
        return "400 Bad Request";
    case 404:
        return "404 Not Found";
    case 409:
        return "409 Conflict";
    case 413:
        return "413 Payload Too Large";
    case 503:
        return "503 Service Unavailable";
    default:
        return "500 Internal Server Error";
    }
}

#define CLIPS_PREFIX "/api/v1/audio/clips/"

/* The sha in /api/v1/audio/clips/<sha>, or "" when the URI has none. */
static void clip_sha(const httpd_req_t *req, char out[OH_CLIP_ID_MAX])
{
    out[0] = 0;
    const size_t n = strlen(CLIPS_PREFIX);
    if (strncmp(req->uri, CLIPS_PREFIX, n) == 0) {
        const char *sha = req->uri + n;
        size_t len = strcspn(sha, "?");
        if (len < OH_CLIP_ID_MAX) {
            memcpy(out, sha, len);
            out[len] = 0;
        }
    }
}

static esp_err_t put_clip(httpd_req_t *req)
{
    char sha[OH_CLIP_ID_MAX];
    clip_sha(req, sha);
    const char *msg = "";
    int code = oh_clips_upload_begin(sha, req->content_len, &msg);
    if (code == 200) {
        cJSON *ok = oh_json_ok();
        cJSON_AddStringToObject(ok, "sha", sha);
        cJSON_AddBoolToObject(ok, "stored", false);
        return send_json(req, ok, NULL);
    }
    if (code != 0) {
        return send_error(req, status_line(code), msg);
    }
    char *buf = malloc(4096);
    if (!buf) {
        oh_clips_upload_abort();
        return send_error(req, "500 Internal Server Error", "out of memory");
    }
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, buf, 4096);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0 || oh_clips_upload_write(buf, (size_t)n) != ESP_OK) {
            free(buf);
            oh_clips_upload_abort();
            return send_error(req, n <= 0 ? "400 Bad Request" : "500 Internal Server Error",
                              n <= 0 ? "upload interrupted" : "write to the clip store failed");
        }
        got += (size_t)n;
    }
    free(buf);
    code = oh_clips_upload_finish(&msg);
    if (code != 201) {
        return send_error(req, status_line(code), msg);
    }
    cJSON *ok = oh_json_ok();
    cJSON_AddStringToObject(ok, "sha", sha);
    cJSON_AddBoolToObject(ok, "stored", true);
    return send_json(req, ok, "201 Created");
}

static esp_err_t get_clips(httpd_req_t *req)
{
    cJSON *list = oh_clips_list();
    if (!list) {
        return send_error(req, "404 Not Found", "this node has no audio");
    }
    return send_json(req, list, NULL);
}

static esp_err_t delete_clip(httpd_req_t *req)
{
    char sha[OH_CLIP_ID_MAX];
    clip_sha(req, sha);
    const int code = oh_clips_delete(sha);
    if (code == 204) {
        httpd_resp_set_status(req, "204 No Content");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        return httpd_resp_send(req, NULL, 0);
    }
    return send_error(req, status_line(code), code == 409 ? "a voice is playing it" : "no such clip");
}

/* ── Beyond the protocol: what the web UI and the console need ────────── */

static const char *reset_reason(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt-watchdog";
    case ESP_RST_TASK_WDT: return "task-watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    case ESP_RST_EFUSE: return "efuse";
    case ESP_RST_PWR_GLITCH: return "power-glitch";
    case ESP_RST_CPU_LOCKUP: return "cpu-lockup";
    default: return "unknown";
    }
}

/* Each port's value with its timestamp, for the live table. */
static cJSON *ports_json(void)
{
    cJSON *ports = cJSON_CreateArray();
    oh_node_lock();
    const oh_module_t *m = oh_node_module();
    const oh_port_state_t *st = oh_node_states();
    for (int i = 0; i < m->n_ports; i++) {
        cJSON *p = oh_json_value(&st[i].value);
        cJSON_AddNumberToObject(p, "port", m->ports[i].port);
        cJSON_AddNumberToObject(p, "ts", (double)st[i].ts_ms);
        cJSON_AddItemToArray(ports, p);
    }
    oh_node_unlock();
    return ports;
}

/* What GET /status answers, and the stream's `status` event carries. */
static cJSON *status_json(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "sn", oh_app_sn());
    cJSON_AddStringToObject(o, "fw", oh_app_fw());
    cJSON_AddStringToObject(o, "board", oh_board_name());
    cJSON_AddStringToObject(o, "hostname", oh_app_hostname());
    char name[OH_NAME_MAX];
    oh_app_node_name(name, sizeof(name));
    cJSON_AddStringToObject(o, "name", name);

    char ip[16], ssid[33], ap[24];
    oh_net_ip(ip, sizeof(ip));
    oh_net_ssid(ssid, sizeof(ssid));
    oh_net_ap_ssid(ap, sizeof(ap));
    cJSON *net = cJSON_AddObjectToObject(o, "network");
    static const char *const states[] = {"down", "connecting", "up", "setup-ap"};
    cJSON_AddStringToObject(net, "state", states[oh_net_state()]);
    cJSON_AddStringToObject(net, "transport", oh_net_transport());
    cJSON_AddStringToObject(net, "ip", ip);
    cJSON_AddStringToObject(net, "ssid", ssid);
    cJSON_AddNumberToObject(net, "rssi", oh_net_rssi());
    cJSON_AddStringToObject(net, "setupAp", ap);
    oh_net_address_t addrs[2];
    const size_t n_addrs = oh_net_addresses(addrs, 2);
    cJSON *list = cJSON_AddArrayToObject(net, "addresses");
    for (size_t i = 0; i < n_addrs; i++) {
        cJSON *a = cJSON_CreateObject();
        cJSON_AddStringToObject(a, "transport", addrs[i].transport);
        cJSON_AddStringToObject(a, "ip", addrs[i].ip);
        cJSON_AddItemToArray(list, a);
    }

    cJSON_AddStringToObject(o, "moduleSource", oh_app_module_source());
    /* The BOOT button, live, and the dark mode it toggles. */
    bool pressed;
    if (oh_status_button(&pressed)) {
        cJSON_AddBoolToObject(o, "buttonPressed", pressed);
    }
    cJSON_AddBoolToObject(o, "dark", oh_config_dark());
    cJSON_AddNumberToObject(o, "uptime_s", oh_clock_uptime_s());
    /* Why the last boot happened, and whether a crash left a core dump behind: a node
     * that rebooted on its own says so here, rather than only on a serial console
     * nobody had open. */
    cJSON_AddStringToObject(o, "resetReason", reset_reason());
    cJSON_AddBoolToObject(o, "coredump", esp_core_dump_image_check() == ESP_OK);
    cJSON_AddBoolToObject(o, "clockSynced", oh_clock_is_unix());
    /* What this node thinks the console's clock reads, relative to its own. A shape
     * is timed against this, so a large or missing offset is the first thing to look
     * at when a strip is breathing out of step with the console driving it. */
    cJSON_AddBoolToObject(o, "clockLocked", oh_clock_console_locked());
    cJSON_AddNumberToObject(o, "consoleOffsetMs", oh_clock_console_offset_ms());
    cJSON_AddNumberToObject(o, "freeHeap", esp_get_free_heap_size());
    cJSON_AddStringToObject(o, "partition", oh_ota_running_partition());
    cJSON_AddBoolToObject(o, "pendingVerify", oh_ota_pending_verify());
    /* Which ports are running something of their own, so a glance at /status says
     * whether the node is being driven or is getting on with it. */
    /* On the heap: 5.5 KB would take most of the event stream's stack. */
    oh_module_t *module = malloc(sizeof(*module));
    if (module) {
        oh_node_module_copy(module);
    }
    cJSON *tracing = NULL;
    for (int i = 0; module && i < module->n_ports; i++) {
        char summary[40];
        oh_node_motion_summary(module->ports[i].port, summary, sizeof(summary));
        if (!summary[0]) {
            continue;
        }
        if (!tracing) {
            tracing = cJSON_AddObjectToObject(o, "effects");
        }
        char key[8];
        snprintf(key, sizeof(key), "%u", module->ports[i].port);
        cJSON_AddStringToObject(tracing, key, summary);
    }
    free(module);

    if (oh_node_has_dmx()) {
        cJSON *sacn = cJSON_AddObjectToObject(o, "sacn");
        cJSON_AddBoolToObject(sacn, "listening", oh_sacn_listening());
        cJSON_AddNumberToObject(sacn, "universe", oh_sacn_universe());
        cJSON_AddNumberToObject(sacn, "frames", oh_sacn_frames());
    }

    char codes[OH_MAX_ERRORS][OH_ERROR_CODE_MAX];
    size_t n = oh_node_errors(codes, OH_MAX_ERRORS);
    cJSON *errors = cJSON_AddArrayToObject(o, "errors");
    for (size_t i = 0; i < n; i++) {
        cJSON_AddItemToArray(errors, cJSON_CreateString(codes[i]));
    }
    cJSON_AddItemToObject(o, "ports", ports_json());
    return o;
}

static esp_err_t get_status(httpd_req_t *req)
{
    return send_json(req, status_json(), NULL);
}

static esp_err_t get_wifi_scan(httpd_req_t *req)
{
    cJSON *list = oh_net_wifi_scan();
    /* What the scan left of the stack, so the next time it is too deep a log line
     * says so before a reboot does. */
    ESP_LOGI(TAG, "wifi scan: %u bytes of stack to spare", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    return send_json(req, list, NULL);
}

static esp_err_t get_module(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "source", oh_app_module_source());
    oh_node_lock();
    cJSON_AddItemToObject(o, "module", oh_vmod_to_json(oh_node_module()));
    oh_node_unlock();
    size_t n;
    const char *const *keys = oh_vmod_preset_keys(&n);
    cJSON *presets = cJSON_AddArrayToObject(o, "presets");
    for (size_t i = 0; i < n; i++) {
        cJSON_AddItemToArray(presets, cJSON_CreateString(keys[i]));
    }
    const char *ids[16];
    n = oh_driver_list(ids, 16);
    cJSON *drivers = cJSON_AddArrayToObject(o, "drivers");
    for (size_t i = 0; i < n; i++) {
        cJSON_AddItemToArray(drivers, cJSON_CreateString(ids[i]));
    }
    return send_json(req, o, NULL);
}

/* {"preset":"led"} | {"clear":true} | a whole module description */
static esp_err_t post_module(httpd_req_t *req)
{
    cJSON *body = read_json(req, 4096);
    if (!body) {
        return ESP_FAIL;
    }
    char err[OH_ERR_MAX] = "";
    esp_err_t rc;
    const cJSON *preset = cJSON_GetObjectItemCaseSensitive(body, "preset");
    const cJSON *clear = cJSON_GetObjectItemCaseSensitive(body, "clear");
    if (cJSON_IsString(preset)) {
        rc = oh_app_set_preset(preset->valuestring, err);
    } else if (cJSON_IsTrue(clear)) {
        rc = oh_app_clear_vmod();
    } else {
        char *text = cJSON_PrintUnformatted(body);
        rc = text ? oh_app_set_vmod_json(text, err) : ESP_ERR_NO_MEM;
        free(text);
    }
    cJSON_Delete(body);
    if (rc != ESP_OK) {
        return send_error(req, "400 Bad Request", err[0] ? err : esp_err_to_name(rc));
    }
    return send_json(req, oh_json_ok(), NULL);
}

/* ── Strips (doc 14) ──────────────────────────────────────────────────── */

#define PIXELS_PREFIX "/api/v1/pixels/"

static esp_err_t get_pixels(httpd_req_t *req)
{
    cJSON *pixels = oh_app_pixels_json(true);
    if (!pixels) {
        return send_error(req, "404 Not Found", "this node has no strips of its own");
    }
    return send_json(req, pixels, NULL);
}

static esp_err_t put_pixels(httpd_req_t *req)
{
    const char *tail = req->uri + strlen(PIXELS_PREFIX);
    char *end;
    const long output = strtol(tail, &end, 10);
    if (end == tail || (*end && *end != '?')) {
        return send_error(req, "404 Not Found", "no such strip output");
    }
    cJSON *body = read_json(req, 1024);
    if (!body) {
        return ESP_FAIL;
    }
    char err[OH_ERR_MAX] = "";
    cJSON *strip = NULL;
    const esp_err_t rc = oh_app_set_pixels((int)output, body, err, &strip);
    cJSON_Delete(body);
    if (rc == ESP_ERR_NOT_FOUND) {
        return send_error(req, "404 Not Found", err);
    }
    if (rc == ESP_ERR_INVALID_STATE) {
        return send_error(req, "409 Conflict", err);
    }
    if (rc == ESP_ERR_INVALID_ARG) {
        return send_error(req, "400 Bad Request", err);
    }
    if (!strip) {
        return send_error(req, "500 Internal Server Error", err[0] ? err : esp_err_to_name(rc));
    }
    return send_json(req, strip, NULL);
}

static esp_err_t post_reboot(httpd_req_t *req)
{
    esp_err_t err = send_json(req, oh_json_ok(), NULL);
    oh_app_reboot(500);
    return err;
}

/* The core dump a crash left in flash, as the raw image `esp-coredump info_corefile
 * --core-format raw` reads, so a node that rebooted on its own can be asked why
 * without a USB cable. 404 when there is none. */
static esp_err_t get_coredump(httpd_req_t *req)
{
    size_t addr, size;
    if (esp_core_dump_image_check() != ESP_OK || esp_core_dump_image_get(&addr, &size) != ESP_OK) {
        return send_error(req, "404 Not Found", "no core dump");
    }
    char *buf = malloc(4096);
    if (!buf) {
        return send_error(req, "500 Internal Server Error", "out of memory");
    }
    httpd_resp_set_type(req, "application/octet-stream");
    esp_err_t err = ESP_OK;
    for (size_t at = 0; at < size && err == ESP_OK; at += 4096) {
        const size_t n = MIN(4096, size - at);
        err = esp_flash_read(NULL, buf, addr + at, n);
        if (err == ESP_OK) {
            err = httpd_resp_send_chunk(req, buf, n);
        }
    }
    free(buf);
    if (err != ESP_OK) {
        return err;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t delete_coredump(httpd_req_t *req)
{
    esp_err_t err = esp_core_dump_image_erase();
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_json(req, oh_json_ok(), NULL);
}

static esp_err_t post_reset(httpd_req_t *req)
{
    esp_err_t err = send_json(req, oh_json_ok(), NULL);
    oh_app_factory_reset();
    return err;
}

static esp_err_t get_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start - 1);
}

/* Captive-portal probes from phones and laptops: send them to the page. */
static esp_err_t redirect_root(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t not_found(httpd_req_t *req, httpd_err_code_t code)
{
    (void)code;
    /* Off the setup AP, anything unknown is a captive-portal probe. */
    if (oh_net_state() == OH_NET_SETUP_AP && strncmp(req->uri, "/api/", 5) != 0) {
        return redirect_root(req);
    }
    return send_error(req, "404 Not Found", "no such endpoint");
}

/* ── Live state: server-sent events ───────────────────────────────────── */

/*
 * GET /api/v1/events is a text/event-stream, so the web UI shows a sensor the
 * moment it trips instead of on the next poll. Three events:
 *
 *   status   what GET /status answers: on connect, when the network, the broker,
 *            the errors or the config change, and every 5 s for the counters
 *   ports    the `ports` array alone, within 50 ms of a port changing, and at the
 *            renderer's 4 Hz while a port traces a shape
 *   module   the node bound its I/O again; /info has new ports
 *
 * Each stream is an async request: the server neither reads nor purges its socket,
 * and one task writes every stream. Each holds one of the server's seven sessions,
 * so there are two at most; a third evicts the oldest, which is usually a page that
 * was reloaded and has not been noticed gone yet.
 */
#define SSE_MAX 2
#define SSE_PORTS_EVERY_MS 50
#define SSE_STATUS_EVERY_MS 5000
#define SSE_PORTS BIT0
#define SSE_STATUS BIT1
#define SSE_MODULE BIT2

static struct {
    httpd_req_t *req;
    int64_t opened_ms;
} s_sse[SSE_MAX];
static SemaphoreHandle_t s_sse_lock;
static TaskHandle_t s_sse_task;

/* Under s_sse_lock. */
static void sse_drop(int i)
{
    const int fd = httpd_req_to_sockfd(s_sse[i].req);
    httpd_req_async_handler_complete(s_sse[i].req);
    httpd_sess_trigger_close(s_server, fd);
    s_sse[i].req = NULL;
}

/* One event as one chunk. False when the stream is gone. */
static bool sse_send(httpd_req_t *req, const char *event, const char *data)
{
    const size_t cap = strlen(event) + strlen(data) + 20;
    char *buf = malloc(cap);
    if (!buf) {
        return true;   /* skip this one, the stream is not at fault */
    }
    const int n = snprintf(buf, cap, "event: %s\ndata: %s\n\n", event, data);
    const bool ok = httpd_resp_send_chunk(req, buf, n) == ESP_OK;
    free(buf);
    return ok;
}

/* Send to every stream; takes the JSON. */
static void sse_broadcast(const char *event, cJSON *json)
{
    char *text = json ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    xSemaphoreTake(s_sse_lock, portMAX_DELAY);
    for (int i = 0; i < SSE_MAX; i++) {
        if (s_sse[i].req && !sse_send(s_sse[i].req, event, text ? text : "{}")) {
            ESP_LOGI(TAG, "event stream %d closed", i);
            sse_drop(i);
        }
    }
    xSemaphoreGive(s_sse_lock);
    cJSON_free(text);
}

static bool sse_open(void)
{
    bool any = false;
    xSemaphoreTake(s_sse_lock, portMAX_DELAY);
    for (int i = 0; i < SSE_MAX; i++) {
        any |= s_sse[i].req != NULL;
    }
    xSemaphoreGive(s_sse_lock);
    return any;
}

static void sse_task(void *arg)
{
    (void)arg;
    int64_t last_status = 0, last_ports = 0;
    uint32_t pending = 0;
    for (;;) {
        int64_t now = esp_timer_get_time() / 1000;
        int64_t wait = last_status + SSE_STATUS_EVERY_MS - now;
        if (pending & SSE_PORTS) {
            wait = MIN(wait, last_ports + SSE_PORTS_EVERY_MS - now);
        }
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(MAX(wait, 0)));
        pending |= bits;
        now = esp_timer_get_time() / 1000;
        if (now - last_status >= SSE_STATUS_EVERY_MS) {
            pending |= SSE_STATUS;
        }
        if (!sse_open()) {
            pending = 0;
            last_status = now;
            continue;
        }
        if (pending & SSE_MODULE) {
            sse_broadcast("module", NULL);
        }
        if (pending & SSE_STATUS) {
            /* carries the ports too */
            sse_broadcast("status", status_json());
            static bool logged;
            if (!logged) {
                logged = true;
                ESP_LOGI(TAG, "event stream: %u bytes of stack to spare", (unsigned)uxTaskGetStackHighWaterMark(NULL));
            }
            last_status = last_ports = now;
            pending = 0;
        } else if ((pending & SSE_PORTS) && now - last_ports >= SSE_PORTS_EVERY_MS) {
            sse_broadcast("ports", ports_json());
            last_ports = now;
            pending = 0;
        } else {
            pending &= SSE_PORTS;   /* not yet: wait out the 50 ms */
        }
    }
}

static void sse_on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg, (void)base, (void)data;
    uint32_t bits;
    switch (id) {
    case OH_EVENT_PORT_CHANGED:
    case OH_EVENT_RENDERED:
        bits = SSE_PORTS;
        break;
    case OH_EVENT_MODULE_BOUND:
        bits = SSE_MODULE | SSE_STATUS;
        break;
    case OH_EVENT_IDENTIFY:
        return;
    default:
        bits = SSE_STATUS;
        break;
    }
    xTaskNotify(s_sse_task, bits, eSetBits);
}

static esp_err_t get_events(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    cJSON *status = status_json();
    char *text = cJSON_PrintUnformatted(status);
    cJSON_Delete(status);
    xSemaphoreTake(s_sse_lock, portMAX_DELAY);
    int slot = 0;
    for (int i = 0; i < SSE_MAX; i++) {
        if (!s_sse[i].req) {
            slot = i;
            break;
        }
        if (s_sse[i].opened_ms < s_sse[slot].opened_ms) {
            slot = i;
        }
    }
    if (s_sse[slot].req) {
        ESP_LOGI(TAG, "event stream %d evicted by a newer one", slot);
        sse_drop(slot);
    }
    /* The first chunk carries the headers and the reconnect delay; the async copy
     * takes over from there. */
    esp_err_t err = httpd_resp_send_chunk(req, "retry: 2000\n\n", HTTPD_RESP_USE_STRLEN);
    if (err == ESP_OK && text && !sse_send(req, "status", text)) {
        err = ESP_FAIL;
    }
    httpd_req_t *copy = NULL;
    if (err == ESP_OK) {
        err = httpd_req_async_handler_begin(req, &copy);
    }
    if (err == ESP_OK) {
        s_sse[slot].req = copy;
        s_sse[slot].opened_ms = esp_timer_get_time() / 1000;
    }
    xSemaphoreGive(s_sse_lock);
    cJSON_free(text);
    return err;
}

static void sse_start(void)
{
    s_sse_lock = xSemaphoreCreateMutex();
    xTaskCreate(sse_task, "oh_sse", 8192, NULL, 4, &s_sse_task);
    ESP_ERROR_CHECK(esp_event_handler_register(OH_EVENT, ESP_EVENT_ANY_ID, sse_on_event, NULL));
}

esp_err_t oh_http_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_uri_handlers = 29;
    /* /api/v1/audio/clips/<sha> names its clip in the path. */
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    /* 12 KB, not 8: the setup page's Wi-Fi scan runs on this task, and a blocking
     * scan overflowed 8 KB — the node rebooted, and the setup AP it was serving
     * vanished from under the phone. The serial console runs the same scan on
     * 12 KB and always has. */
    cfg.stack_size = 12288;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;
    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd: %s", esp_err_to_name(err));
        return err;
    }
    if (!s_sse_task) {
        sse_start();
    }
    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, get_index, NULL},
        {"/api/v1/info", HTTP_GET, get_info, NULL},
        {"/api/v1/state", HTTP_GET, get_state, NULL},
        {"/api/v1/state", HTTP_POST, post_state, NULL},
        {"/api/v1/config", HTTP_GET, get_config, NULL},
        {"/api/v1/config", HTTP_POST, post_config, NULL},
        {"/api/v1/identify", HTTP_POST, post_identify, NULL},
        {"/api/v1/ota", HTTP_POST, post_ota, NULL},
        {"/api/v1/status", HTTP_GET, get_status, NULL},
        {"/api/v1/events", HTTP_GET, get_events, NULL},
        {"/api/v1/wifi/scan", HTTP_GET, get_wifi_scan, NULL},
        {"/api/v1/module", HTTP_GET, get_module, NULL},
        {"/api/v1/module", HTTP_POST, post_module, NULL},
        {"/api/v1/reboot", HTTP_POST, post_reboot, NULL},
        {"/api/v1/reset", HTTP_POST, post_reset, NULL},
        {"/api/v1/coredump", HTTP_GET, get_coredump, NULL},
        {"/api/v1/coredump", HTTP_DELETE, delete_coredump, NULL},
        {"/api/v1/audio/clips", HTTP_GET, get_clips, NULL},
        {"/api/v1/audio/clips/*", HTTP_PUT, put_clip, NULL},
        {"/api/v1/audio/clips/*", HTTP_DELETE, delete_clip, NULL},
        {"/api/v1/pixels", HTTP_GET, get_pixels, NULL},
        {"/api/v1/pixels/*", HTTP_PUT, put_pixels, NULL},
        /* Captive-portal detection URLs. */
        {"/generate_204", HTTP_GET, redirect_root, NULL},
        {"/hotspot-detect.html", HTTP_GET, redirect_root, NULL},
        {"/connecttest.txt", HTTP_GET, redirect_root, NULL},
        {"/ncsi.txt", HTTP_GET, redirect_root, NULL},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, not_found);
    ESP_LOGI(TAG, "listening on :80");
    return ESP_OK;
}
