#include "oh_sacn.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "oh_config.h"
#include "oh_net.h"
#include "oh_node.h"

static const char *TAG = "oh_sacn";

static struct {
    int sock;
    uint16_t universe;
    TaskHandle_t task;
    volatile bool stop;
    oh_sacn_sink_t sink;
    void *sink_ctx;
    uint32_t frames;
    uint8_t last_seq;
} s;

static void task(void *arg)
{
    (void)arg;
    uint8_t *buf = malloc(1500);
    while (!s.stop && buf) {
        int n = recv(s.sock, buf, 1500, 0);
        if (n <= 0) {
            continue; /* timeout, or the socket went away */
        }
        oh_e131_frame_t frame;
        if (!oh_e131_parse(buf, (size_t)n, &frame) || frame.universe != s.universe || frame.start_code != 0) {
            continue;
        }
        s.frames++;
        s.last_seq = frame.sequence;
        if (s.sink) {
            s.sink(s.sink_ctx, &frame);
        }
    }
    free(buf);
    s.task = NULL;
    vTaskDelete(NULL);
}

static void listener_stop(void)
{
    if (s.sock < 0) {
        return;
    }
    s.stop = true;
    int sock = s.sock;
    s.sock = -1;
    close(sock);
    for (int i = 0; i < 50 && s.task; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGI(TAG, "stopped listening for universe %u", s.universe);
}

static void listener_start(uint16_t universe)
{
    listener_stop();
    if (!universe || !oh_net_is_up()) {
        return;
    }
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return;
    }
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_port = htons(OH_E131_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind 5568 failed");
        close(sock);
        return;
    }
    /* Join the universe's group on the interface that has our address. */
    char ip[16];
    oh_net_ip(ip, sizeof(ip));
    struct ip_mreq mreq = {.imr_multiaddr.s_addr = htonl(oh_e131_multicast_group(universe)),
                           .imr_interface.s_addr = inet_addr(ip)};
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        ESP_LOGW(TAG, "multicast join failed; unicast still works");
    }
    struct timeval tv = {.tv_sec = 1};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    s.sock = sock;
    s.universe = universe;
    s.stop = false;
    s.frames = 0;
    if (xTaskCreate(task, "oh_sacn", 4096, NULL, 8, &s.task) != pdPASS) {
        close(sock);
        s.sock = -1;
        return;
    }
    ESP_LOGI(TAG, "listening for universe %u on :%d (unicast and 239.255.%u.%u)", universe, OH_E131_PORT,
             universe >> 8, universe & 0xFF);
}

static void reconsider(void)
{
    oh_config_t cfg;
    oh_config_get(&cfg);
    bool want = oh_node_has_dmx() && cfg.dmx_universe && oh_net_is_up() && strcmp(cfg.dmx_protocol, "sacn") == 0;
    if (want && (s.sock < 0 || s.universe != cfg.dmx_universe)) {
        listener_start(cfg.dmx_universe);
    } else if (!want && s.sock >= 0) {
        listener_stop();
    }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    switch (id) {
    case OH_EVENT_NET_UP:
    case OH_EVENT_NET_DOWN:
    case OH_EVENT_MODULE_BOUND:
        reconsider();
        break;
    case OH_EVENT_CONFIG_CHANGED:
        if (*(uint32_t *)data & OH_CFG_DMX) {
            reconsider();
        }
        break;
    default:
        break;
    }
}

void oh_sacn_set_sink(oh_sacn_sink_t sink, void *ctx)
{
    s.sink_ctx = ctx;
    s.sink = sink;
}

bool oh_sacn_listening(void)
{
    return s.sock >= 0;
}

uint16_t oh_sacn_universe(void)
{
    return s.sock >= 0 ? s.universe : 0;
}

uint32_t oh_sacn_frames(void)
{
    return s.frames;
}

esp_err_t oh_sacn_start(void)
{
    s.sock = -1;
    ESP_ERROR_CHECK(esp_event_handler_register(OH_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    reconsider();
    return ESP_OK;
}
