#include "oh_pixel.h"

#include <stdio.h>
#include <string.h>

#include "oh_json.h"
#include "oh_pixfx.h"

const char *oh_pix_mode_str(oh_pix_mode_t m)
{
    return m == OH_PIX_MODE_SACN ? "sacn" : "effect";
}

bool oh_pix_mode_parse(const char *s, oh_pix_mode_t *out)
{
    if (s && strcmp(s, "effect") == 0) {
        *out = OH_PIX_MODE_EFFECT;
        return true;
    }
    if (s && strcmp(s, "sacn") == 0) {
        *out = OH_PIX_MODE_SACN;
        return true;
    }
    return false;
}

/* ── Color order ─────────────────────────────────────────────────────── */

static const char *const ORDERS[] = {"RGB", "RBG", "GRB", "GBR", "BRG", "BGR", "RGBW", "GRBW"};

const char *const *oh_pix_orders(size_t *n)
{
    *n = sizeof(ORDERS) / sizeof(ORDERS[0]);
    return ORDERS;
}

bool oh_pix_order_parse(const char *s, oh_pix_order_t *out)
{
    if (!s) {
        return false;
    }
    const size_t len = strlen(s);
    if (len != 3 && len != 4) {
        return false;
    }
    const char letters[] = "RGBW";
    int seen[4] = {-1, -1, -1, -1};
    for (size_t i = 0; i < len; i++) {
        const char *at = strchr(letters, s[i]);
        if (!s[i] || !at) {
            return false;
        }
        const int which = (int)(at - letters);
        if (seen[which] >= 0 || (which == 3 && len == 3)) {
            return false;
        }
        seen[which] = (int)i;
    }
    if (seen[0] < 0 || seen[1] < 0 || seen[2] < 0 || (len == 4 && seen[3] < 0)) {
        return false;
    }
    for (int c = 0; c < 4; c++) {
        out->pos[c] = (uint8_t)(seen[c] < 0 ? 3 : seen[c]);
    }
    out->components = (uint8_t)len;
    return true;
}

/* ── Settings ─────────────────────────────────────────────────────────── */

void oh_pix_cfg_default(oh_pix_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->mode = OH_PIX_MODE_EFFECT;
    c->count = 60;
    strcpy(c->order, "GRB");
    c->universe = 0;
    c->address = 1;
    c->hold_ms = OH_PIX_HOLD_DEFAULT_MS;
}

static bool number_in(const cJSON *body, const char *key, double lo, double hi, double *out, bool *present,
                      char *err)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(body, key);
    *present = n != NULL;
    if (!n) {
        return true;
    }
    if (!cJSON_IsNumber(n) || n->valuedouble < lo || n->valuedouble > hi || n->valuedouble != (double)(int64_t)n->valuedouble) {
        snprintf(err, OH_ERR_MAX, "\"%s\" is a whole number %.0f..%.0f", key, lo, hi);
        return false;
    }
    *out = n->valuedouble;
    return true;
}

bool oh_pix_cfg_apply_json(oh_pix_cfg_t *c, const cJSON *body, char *err)
{
    if (!cJSON_IsObject(body)) {
        snprintf(err, OH_ERR_MAX, "a strip's settings are an object");
        return false;
    }
    oh_pix_cfg_t next = *c;
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(body, "mode");
    if (mode && (!cJSON_IsString(mode) || !oh_pix_mode_parse(mode->valuestring, &next.mode))) {
        snprintf(err, OH_ERR_MAX, "\"mode\" is \"effect\" or \"sacn\"");
        return false;
    }
    const cJSON *order = cJSON_GetObjectItemCaseSensitive(body, "order");
    if (order) {
        oh_pix_order_t parsed;
        if (!cJSON_IsString(order) || !oh_pix_order_parse(order->valuestring, &parsed)) {
            snprintf(err, OH_ERR_MAX, "\"order\" is a permutation of RGB, or of RGBW");
            return false;
        }
        strncpy(next.order, order->valuestring, OH_PIX_ORDER_MAX - 1);
        next.order[OH_PIX_ORDER_MAX - 1] = '\0';
    }
    double v;
    bool present;
    if (!number_in(body, "count", 1, OH_PIX_MAX_COUNT, &v, &present, err)) {
        return false;
    }
    if (present) {
        next.count = (uint16_t)v;
    }
    if (!number_in(body, "universe", 0, 63999, &v, &present, err)) {
        return false;
    }
    if (present) {
        next.universe = (uint16_t)v;
    }
    if (!number_in(body, "address", 1, 512, &v, &present, err)) {
        return false;
    }
    if (present) {
        next.address = (uint16_t)v;
    }
    if (!number_in(body, "hold_ms", 0, 3600000, &v, &present, err)) {
        return false;
    }
    if (present) {
        next.hold_ms = (uint32_t)v;
    }

    if (next.mode == OH_PIX_MODE_SACN) {
        if (next.universe == 0) {
            snprintf(err, OH_ERR_MAX, "a strip in sacn mode needs a \"universe\" 1..63999");
            return false;
        }
        oh_pix_map_t map;
        if (!oh_pix_map_build(&next, &map)) {
            snprintf(err, OH_ERR_MAX, "%u pixels from %u/%u need more than %d universes, or run past 63999",
                     next.count, next.universe, next.address, OH_PIX_MAX_UNIVERSES);
            return false;
        }
    }
    *c = next;
    return true;
}

cJSON *oh_pix_cfg_to_json(const oh_pix_cfg_t *c, uint8_t output, uint8_t head)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "output", output);
    cJSON_AddNumberToObject(o, "head", head);
    cJSON_AddStringToObject(o, "mode", oh_pix_mode_str(c->mode));
    cJSON_AddNumberToObject(o, "count", c->count);
    cJSON_AddStringToObject(o, "order", c->order);
    cJSON_AddNumberToObject(o, "universe", c->universe);
    cJSON_AddNumberToObject(o, "address", c->address);
    cJSON_AddNumberToObject(o, "hold_ms", c->hold_ms);
    cJSON *universes = cJSON_AddArrayToObject(o, "universes");
    oh_pix_map_t map;
    if (c->mode == OH_PIX_MODE_SACN && oh_pix_map_build(c, &map)) {
        for (int i = 0; i < map.n; i++) {
            cJSON_AddItemToArray(universes, cJSON_CreateNumber(map.universe[i]));
        }
    }
    cJSON_AddNumberToObject(o, "max_fps", oh_pix_max_fps(c->count));
    return o;
}

cJSON *oh_pix_info_json(const oh_pix_cfg_t *cfgs, const uint8_t *heads, size_t n)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *effects = cJSON_AddArrayToObject(o, "effects");
    for (int i = 0; i < OH_PIXFX_COUNT; i++) {
        cJSON_AddItemToArray(effects, cJSON_CreateString(oh_pixfx_str((oh_pixfx_kind_t)i)));
    }
    cJSON *orders = cJSON_AddArrayToObject(o, "orders");
    size_t n_orders;
    const char *const *list = oh_pix_orders(&n_orders);
    for (size_t i = 0; i < n_orders; i++) {
        cJSON_AddItemToArray(orders, cJSON_CreateString(list[i]));
    }
    cJSON_AddNumberToObject(o, "max_count", OH_PIX_MAX_COUNT);
    cJSON_AddNumberToObject(o, "multicast_universes", OH_PIX_MULTICAST_UNIVERSES);
    cJSON *strips = cJSON_AddArrayToObject(o, "strips");
    for (size_t i = 0; i < n; i++) {
        cJSON_AddItemToArray(strips, oh_pix_cfg_to_json(&cfgs[i], (uint8_t)i, heads[i]));
    }
    return o;
}

/* ── The mapping ──────────────────────────────────────────────────────── */

bool oh_pix_map_build(const oh_pix_cfg_t *c, oh_pix_map_t *out)
{
    memset(out, 0, sizeof(*out));
    oh_pix_order_t order;
    if (!oh_pix_order_parse(c->order, &order) || c->universe == 0 || c->address < 1 || c->address > 512 ||
        c->count == 0) {
        return false;
    }
    const uint16_t ch = order.components;
    out->channels = (uint8_t)ch;
    uint16_t pixel = 0;
    uint16_t slot = (uint16_t)(c->address - 1);
    uint32_t universe = c->universe;
    while (pixel < c->count) {
        const uint16_t room = (uint16_t)((512 - slot) / ch);
        if (room == 0) {
            /* An address too near the end for even one pixel: the strip starts in
             * the next universe, at address 1. */
            if (pixel == 0 && slot != 0) {
                slot = 0;
                universe++;
                continue;
            }
            return false;
        }
        if (out->n >= OH_PIX_MAX_UNIVERSES || universe > 63999) {
            return false;
        }
        const uint16_t take = (uint16_t)(c->count - pixel < room ? c->count - pixel : room);
        out->universe[out->n] = (uint16_t)universe;
        out->first_pixel[out->n] = pixel;
        out->pixels[out->n] = take;
        out->first_slot[out->n] = slot;
        out->n++;
        pixel = (uint16_t)(pixel + take);
        slot = 0;
        universe++;
    }
    return true;
}

int oh_pix_map_find(const oh_pix_map_t *m, uint16_t universe)
{
    for (int i = 0; i < m->n; i++) {
        if (m->universe[i] == universe) {
            return i;
        }
    }
    return -1;
}

void oh_pix_map_apply(const oh_pix_map_t *m, int k, const uint8_t *slots, uint16_t n_slots, uint8_t *rgbw)
{
    if (k < 0 || k >= m->n) {
        return;
    }
    const uint16_t ch = m->channels;
    for (uint16_t p = 0; p < m->pixels[k]; p++) {
        const uint32_t at = (uint32_t)m->first_slot[k] + (uint32_t)p * ch;
        if (at + ch > n_slots) {
            return; /* a short frame: what it does not reach stays as it was */
        }
        uint8_t *px = rgbw + 4u * (m->first_pixel[k] + p);
        px[0] = slots[at];
        px[1] = slots[at + 1];
        px[2] = slots[at + 2];
        px[3] = ch == 4 ? slots[at + 3] : 0;
    }
}

/* ── Rate, memory, and going dark ─────────────────────────────────────── */

int oh_pix_max_fps(uint16_t count)
{
    const uint32_t frame_us = (uint32_t)count * 30u + 300u;
    const uint32_t fps = 1000000u / frame_us;
    return fps > OH_PIX_MAX_FPS ? OH_PIX_MAX_FPS : (int)(fps < 1 ? 1 : fps);
}

int oh_pix_dma_choice(uint16_t count0, uint16_t count1)
{
    if (count0 <= 64 && count1 <= 64) {
        return -1;
    }
    return count1 > count0 ? 1 : 0;
}

float oh_pix_loss_level(uint32_t since_ms, uint32_t hold_ms)
{
    if (hold_ms == 0 || since_ms < hold_ms) {
        return 1.0f;
    }
    const uint32_t into = since_ms - hold_ms;
    if (into >= OH_PIX_FADE_MS) {
        return 0.0f;
    }
    return 1.0f - (float)into / (float)OH_PIX_FADE_MS;
}
