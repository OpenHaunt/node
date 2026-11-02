#include "oh_sacn.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "oh_config.h"
#include "oh_net.h"
#include "oh_node.h"

static const char *TAG = "oh_sacn";

/*
 * One socket on 5568 for everything that wants a universe: the DMX gateway's one,
 * and every universe a strip in `sacn` mode reads (doc 14). Each subscriber is
 * handed the frames of its universe from the receive task, under `lock`, so
 * unsubscribing waits for any delivery in progress and a driver can free what the
 * sink was writing into as soon as it returns.
 *
 * The socket stays open while the network is up and anybody has ever subscribed:
 * a strip rebinding unsubscribes and subscribes again within a millisecond, and
 * closing the socket in between would cost a second waiting for the task to see it.
 */
#define SUBS 20
#define GROUPS 5 /* doc 14: four for strips, one for a gateway */

typedef struct {
    bool used;
    uint16_t universe;
    oh_sacn_sink_t sink;
    void *ctx;
} sub_t;

static struct {
    SemaphoreHandle_t lock;
    int sock;
    TaskHandle_t task;
    volatile bool stop;
    sub_t subs[SUBS];
    uint16_t joined[GROUPS];
    uint32_t iface; /* the address groups were joined on, network order */
    /* The DMX gateway: one universe, from the node's config. */
    int dmx_sub;
    uint16_t dmx_universe;
    oh_sacn_sink_t dmx_sink;
    void *dmx_ctx;
    uint32_t frames;
} s = {.sock = -1, .dmx_sub = -1};

/* The lock exists from the first subscription on, because the module is bound
 * (and its strips subscribe) before the network and this component start. Boot
 * is single-threaded until then, so creating it lazily is not a race. */
static bool ready(void)
{
    if (!s.lock) {
        s.lock = xSemaphoreCreateMutex();
    }
    return s.lock != NULL;
}

static void task(void *arg)
{
    (void)arg;
    uint8_t *buf = malloc(1500);
    while (!s.stop && buf) {
        int sock = s.sock;
        if (sock < 0) {
            break;
        }
        int n = recv(sock, buf, 1500, 0);
        if (n <= 0) {
            continue; /* timeout, or the socket went away */
        }
        oh_e131_frame_t frame;
        if (!oh_e131_parse(buf, (size_t)n, &frame)) {
            continue;
        }
        xSemaphoreTake(s.lock, portMAX_DELAY);
        bool wanted = false;
        for (int i = 0; i < SUBS; i++) {
            if (s.subs[i].used && s.subs[i].universe == frame.universe && s.subs[i].sink) {
                s.subs[i].sink(s.subs[i].ctx, &frame);
                wanted = true;
            }
        }
        if (wanted) {
            s.frames++;
        }
        xSemaphoreGive(s.lock);
    }
    free(buf);
    s.task = NULL;
    vTaskDelete(NULL);
}

/* Under the lock: join what is wanted and is not joined, leave what is not wanted. */
static void sync_groups_locked(void)
{
    if (s.sock < 0) {
        return;
    }
    uint16_t want[GROUPS] = {0};
    int n_want = 0;
    for (int i = 0; i < SUBS && n_want < GROUPS; i++) {
        if (!s.subs[i].used) {
            continue;
        }
        bool dup = false;
        for (int k = 0; k < n_want; k++) {
            dup |= want[k] == s.subs[i].universe;
        }
        if (!dup) {
            want[n_want++] = s.subs[i].universe;
        }
    }
    for (int g = 0; g < GROUPS; g++) {
        if (!s.joined[g]) {
            continue;
        }
        bool keep = false;
        for (int k = 0; k < n_want; k++) {
            keep |= want[k] == s.joined[g];
        }
        if (!keep) {
            struct ip_mreq mreq = {.imr_multiaddr.s_addr = htonl(oh_e131_multicast_group(s.joined[g])),
                                   .imr_interface.s_addr = s.iface};
            setsockopt(s.sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
            s.joined[g] = 0;
        }
    }
    for (int k = 0; k < n_want; k++) {
        bool have = false;
        int free_slot = -1;
        for (int g = 0; g < GROUPS; g++) {
            have |= s.joined[g] == want[k];
            if (!s.joined[g] && free_slot < 0) {
                free_slot = g;
            }
        }
        if (have || free_slot < 0) {
            continue;
        }
        struct ip_mreq mreq = {.imr_multiaddr.s_addr = htonl(oh_e131_multicast_group(want[k])),
                               .imr_interface.s_addr = s.iface};
        if (setsockopt(s.sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            ESP_LOGW(TAG, "multicast join for universe %u failed; unicast still works", want[k]);
            continue;
        }
        s.joined[free_slot] = want[k];
    }
}

static bool any_sub_locked(void)
{
    for (int i = 0; i < SUBS; i++) {
        if (s.subs[i].used) {
            return true;
        }
    }
    return false;
}

static void listener_stop(void)
{
    if (!ready()) {
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    int sock = s.sock;
    s.sock = -1;
    s.stop = true;
    memset(s.joined, 0, sizeof(s.joined));
    xSemaphoreGive(s.lock);
    if (sock < 0) {
        return;
    }
    close(sock);
    for (int i = 0; i < 60 && s.task; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGI(TAG, "stopped listening");
}

/* Open the socket if the network is up and somebody wants a universe. */
static void listener_ensure(void)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const bool open = s.sock >= 0;
    const bool want = any_sub_locked();
    if (open) {
        sync_groups_locked();
    }
    xSemaphoreGive(s.lock);
    if (open || !want || !oh_net_is_up() || s.task) {
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
    struct timeval tv = {.tv_sec = 1};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char ip[16];
    oh_net_ip(ip, sizeof(ip));

    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.iface = inet_addr(ip);
    s.sock = sock;
    s.stop = false;
    sync_groups_locked();
    xSemaphoreGive(s.lock);
    if (xTaskCreate(task, "oh_sacn", 4096, NULL, 8, &s.task) != pdPASS) {
        listener_stop();
        return;
    }
    ESP_LOGI(TAG, "listening on :%d", OH_E131_PORT);
}

int oh_sacn_subscribe(uint16_t universe, oh_sacn_sink_t sink, void *ctx)
{
    if (!universe || !sink || !ready()) {
        return -1;
    }
    int handle = -1;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    for (int i = 0; i < SUBS; i++) {
        if (!s.subs[i].used) {
            s.subs[i] = (sub_t){.used = true, .universe = universe, .sink = sink, .ctx = ctx};
            handle = i;
            break;
        }
    }
    xSemaphoreGive(s.lock);
    if (handle < 0) {
        ESP_LOGE(TAG, "no room for universe %u", universe);
        return -1;
    }
    listener_ensure();
    return handle;
}

void oh_sacn_unsubscribe(int handle)
{
    if (handle < 0 || handle >= SUBS || !s.lock) {
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.subs[handle].used = false;
    s.subs[handle].sink = NULL;
    sync_groups_locked();
    xSemaphoreGive(s.lock);
}

/* ── The DMX gateway's universe ───────────────────────────────────────── */

static void dmx_forward(void *ctx, const oh_e131_frame_t *frame)
{
    (void)ctx;
    /* Called under the lock, which also guards dmx_sink. */
    if (frame->start_code == 0 && s.dmx_sink) {
        s.dmx_sink(s.dmx_ctx, frame);
    }
}

static void reconsider(void)
{
    oh_config_t cfg;
    oh_config_get(&cfg);
    const bool want = oh_node_has_dmx() && cfg.dmx_universe && strcmp(cfg.dmx_protocol, "sacn") == 0;
    const uint16_t universe = want ? cfg.dmx_universe : 0;
    if (universe != s.dmx_universe) {
        oh_sacn_unsubscribe(s.dmx_sub);
        s.dmx_sub = -1;
        s.dmx_universe = universe;
        if (universe) {
            s.dmx_sub = oh_sacn_subscribe(universe, dmx_forward, NULL);
            ESP_LOGI(TAG, "forwarding universe %u (unicast and 239.255.%u.%u)", universe, universe >> 8,
                     universe & 0xFF);
        }
    }
    listener_ensure();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    switch (id) {
    case OH_EVENT_NET_DOWN:
        listener_stop();
        break;
    case OH_EVENT_NET_UP:
        /* The interface may have changed (wifi to Ethernet): join again on the new one. */
        listener_stop();
        reconsider();
        break;
    case OH_EVENT_NET_CHANGED: {
        /* A path came or went with the node staying up: if the preferred one is
         * another now, the joins are on an interface that no longer carries them. */
        char ip[16];
        oh_net_ip(ip, sizeof(ip));
        xSemaphoreTake(s.lock, portMAX_DELAY);
        const bool moved = s.sock >= 0 && s.iface != inet_addr(ip);
        xSemaphoreGive(s.lock);
        if (moved) {
            listener_stop();
            reconsider();
        }
        break;
    }
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
    if (!ready()) {
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.dmx_ctx = ctx;
    s.dmx_sink = sink;
    xSemaphoreGive(s.lock);
}

bool oh_sacn_listening(void)
{
    return s.sock >= 0;
}

uint16_t oh_sacn_universe(void)
{
    return s.sock >= 0 ? s.dmx_universe : 0;
}

uint32_t oh_sacn_frames(void)
{
    return s.frames;
}

esp_err_t oh_sacn_start(void)
{
    if (!ready()) {
        return ESP_ERR_NO_MEM;
    }
    ESP_ERROR_CHECK(esp_event_handler_register(OH_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    reconsider();
    return ESP_OK;
}
