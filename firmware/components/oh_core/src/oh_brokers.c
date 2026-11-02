#include "oh_brokers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oh_port.h"

bool oh_brokers_valid_addr(const char *addr)
{
    if (!addr) {
        return false;
    }
    const size_t len = strlen(addr);
    if (len == 0 || len >= OH_BROKER_ADDR_MAX || strchr(addr, ',') || strchr(addr, ' ')) {
        return false;
    }
    const char *colon = strrchr(addr, ':');
    if (!colon || colon == addr || colon[1] == '\0') {
        return false;
    }
    char *end = NULL;
    const long port = strtol(colon + 1, &end, 10);
    return *end == '\0' && port >= 1 && port <= 65535;
}

static void add(oh_brokers_t *out, const char *addr)
{
    if (out->n == OH_BROKERS_MAX) {
        return;
    }
    for (uint8_t i = 0; i < out->n; i++) {
        if (strcmp(out->entry[i], addr) == 0) {
            return; /* one door, listed once */
        }
    }
    strncpy(out->entry[out->n], addr, OH_BROKER_ADDR_MAX - 1);
    out->entry[out->n][OH_BROKER_ADDR_MAX - 1] = '\0';
    out->n++;
}

bool oh_brokers_parse(const cJSON *obj, oh_brokers_t *out, bool *present, char *err)
{
    memset(out, 0, sizeof(*out));
    *present = false;
    if (err) {
        err[0] = '\0';
    }
    if (!cJSON_IsObject(obj)) {
        if (err) {
            snprintf(err, OH_ERR_MAX, "a broker list is an object");
        }
        return false;
    }
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(obj, "brokers");
    if (list) {
        if (!cJSON_IsArray(list)) {
            if (err) {
                snprintf(err, OH_ERR_MAX, "mqtt.brokers is an array of \"host:port\"");
            }
            return false;
        }
        const cJSON *item;
        cJSON_ArrayForEach(item, list)
        {
            if (!cJSON_IsString(item) || !oh_brokers_valid_addr(item->valuestring)) {
                if (err) {
                    snprintf(err, OH_ERR_MAX, "mqtt.brokers is an array of \"host:port\"");
                }
                memset(out, 0, sizeof(*out));
                return false;
            }
            add(out, item->valuestring);
        }
        *present = true;
        return true;
    }
    const cJSON *one = cJSON_GetObjectItemCaseSensitive(obj, "broker");
    if (one) {
        /* "" is how a node is told to forget its broker: the empty list. */
        if (!cJSON_IsString(one) || (one->valuestring[0] && !oh_brokers_valid_addr(one->valuestring))) {
            if (err) {
                snprintf(err, OH_ERR_MAX, "mqtt.broker is \"host:port\"");
            }
            return false;
        }
        if (one->valuestring[0]) {
            add(out, one->valuestring);
        }
        *present = true;
    }
    return true;
}

bool oh_brokers_equal(const oh_brokers_t *a, const oh_brokers_t *b)
{
    if (a->n != b->n) {
        return false;
    }
    for (uint8_t i = 0; i < a->n; i++) {
        if (strcmp(a->entry[i], b->entry[i]) != 0) {
            return false;
        }
    }
    return true;
}

bool oh_brokers_set(oh_brokers_t *b, const oh_brokers_t *list, const char *connected_to)
{
    if (list->n == 0) {
        return false;
    }
    const bool unchanged = oh_brokers_equal(b, list);
    for (uint8_t i = 0; i < list->n; i++) {
        memcpy(b->entry[i], list->entry[i], OH_BROKER_ADDR_MAX);
    }
    b->n = list->n;
    if (connected_to && strcmp(connected_to, b->entry[0]) == 0) {
        b->current = 0;
        return false;
    }
    if (unchanged && !connected_to) {
        if (b->current >= b->n) {
            b->current = 0;
        }
        return false; /* still walking the same list: carry on where it was */
    }
    b->current = 0;
    b->backoff_ms = 0;
    return true;
}

uint32_t oh_brokers_failed(oh_brokers_t *b)
{
    if (b->n == 0) {
        return OH_BROKERS_MAX_BACKOFF_MS;
    }
    b->current = (uint8_t)((b->current + 1) % b->n);
    if (b->current != 0) {
        return OH_BROKERS_BETWEEN_MS;
    }
    /* A whole pass failed. */
    const uint32_t wait = b->backoff_ms ? b->backoff_ms : OH_BROKERS_FIRST_BACKOFF_MS;
    b->backoff_ms = wait * 2 > OH_BROKERS_MAX_BACKOFF_MS ? OH_BROKERS_MAX_BACKOFF_MS : wait * 2;
    return wait;
}

void oh_brokers_connected(oh_brokers_t *b)
{
    b->backoff_ms = 0;
}

const char *oh_brokers_current(const oh_brokers_t *b)
{
    return b->current < b->n ? b->entry[b->current] : "";
}

void oh_brokers_join(const oh_brokers_t *b, char *out, size_t cap)
{
    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    size_t used = 0;
    for (uint8_t i = 0; i < b->n; i++) {
        const int w = snprintf(out + used, cap - used, "%s%s", i ? "," : "", b->entry[i]);
        if (w < 0 || (size_t)w >= cap - used) {
            break;
        }
        used += (size_t)w;
    }
}

void oh_brokers_split(const char *joined, oh_brokers_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!joined) {
        return;
    }
    char one[OH_BROKER_ADDR_MAX];
    const char *p = joined;
    while (*p) {
        const char *comma = strchr(p, ',');
        const size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len > 0 && len < sizeof(one)) {
            memcpy(one, p, len);
            one[len] = '\0';
            if (oh_brokers_valid_addr(one)) {
                add(out, one);
            }
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }
}

void oh_brokers_one(const char *addr, oh_brokers_t *out)
{
    memset(out, 0, sizeof(*out));
    if (addr && oh_brokers_valid_addr(addr)) {
        add(out, addr);
    }
}

cJSON *oh_brokers_to_json(const oh_brokers_t *b)
{
    cJSON *a = cJSON_CreateArray();
    for (uint8_t i = 0; i < b->n; i++) {
        cJSON_AddItemToArray(a, cJSON_CreateString(b->entry[i]));
    }
    return a;
}
