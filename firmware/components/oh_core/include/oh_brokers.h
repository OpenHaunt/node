/*
 * Which broker a node is on, and which it tries next (OpenHaunt/node doc 15).
 *
 * A console that fails over hands its nodes an ordered list of brokers, leader first:
 * every station of its session. A node that loses the one it is on walks the list in
 * order, and backs off once a whole pass has failed. Only the leader's broker takes a
 * node, so the walk ends at whoever leads now, and the list that leader then publishes
 * puts it first.
 *
 * No ESP-IDF dependency: compiled into the host tests unchanged. The console's
 * simulator keeps the same rule in `tools/openhaunt-node-sim/src/brokers.rs`.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OH_BROKERS_MAX 4
#define OH_BROKER_ADDR_MAX 64
/* "host:port" × OH_BROKERS_MAX, comma-joined, as it is kept in NVS. */
#define OH_BROKERS_JOINED_MAX (OH_BROKERS_MAX * OH_BROKER_ADDR_MAX)

#define OH_BROKERS_BETWEEN_MS 200u
#define OH_BROKERS_FIRST_BACKOFF_MS 1000u
#define OH_BROKERS_MAX_BACKOFF_MS 8000u

typedef struct {
    char entry[OH_BROKERS_MAX][OH_BROKER_ADDR_MAX];
    uint8_t n;
    /* The entry being tried, or connected to. */
    uint8_t current;
    /* What the next whole-pass failure waits; 0 = the first backoff. */
    uint32_t backoff_ms;
} oh_brokers_t;

/*
 * Read the `mqtt` object of a config body, or the body of an `openhaunt/brokers`
 * message: `brokers`, an array of "host:port", where there is one; `broker` alone
 * as a one-entry list otherwise. True with `*present` false when neither is there.
 * False with `err` (OH_ERR_MAX) when what is there is wrong. Entries past
 * OH_BROKERS_MAX are dropped, not refused: a bigger session is not an error.
 */
bool oh_brokers_parse(const cJSON *obj, oh_brokers_t *out, bool *present, char *err);

/* One "host:port" a node could dial: a host, a colon, a port 1..65535, no comma. */
bool oh_brokers_valid_addr(const char *addr);

/*
 * Take a new list. True when the node has to move: the first entry is not the broker
 * it is connected to (`connected_to`, NULL when it is on none). Moving starts from the
 * top again. An empty list changes nothing.
 */
bool oh_brokers_set(oh_brokers_t *b, const oh_brokers_t *list, const char *connected_to);

/* The broker being tried refused or dropped: move on, and say how long to wait. */
uint32_t oh_brokers_failed(oh_brokers_t *b);

/* Connected: the next failure backs off from the start. */
void oh_brokers_connected(oh_brokers_t *b);

/* The broker to try now; "" for a node nobody has told anything. */
const char *oh_brokers_current(const oh_brokers_t *b);

bool oh_brokers_equal(const oh_brokers_t *a, const oh_brokers_t *b);

/* Comma-joined, for NVS, and back. `split` of "" is the empty list. */
void oh_brokers_join(const oh_brokers_t *b, char *out, size_t cap);
void oh_brokers_split(const char *joined, oh_brokers_t *out);

/* A one-entry list, from what a node from before lists stored. */
void oh_brokers_one(const char *addr, oh_brokers_t *out);

/* The entries as a JSON array. */
cJSON *oh_brokers_to_json(const oh_brokers_t *b);

#ifdef __cplusplus
}
#endif
