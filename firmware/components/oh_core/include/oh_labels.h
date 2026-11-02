/*
 * What somebody called a node's ports and heads: "Ausgang PIR", "LED Streifen Tor".
 *
 * A label is not a port's `name`. The name is the module's word for the port and a
 * console keys the port by it, so changing it would orphan everything patched
 * against the port; a label is only what a person reads. /info carries it beside the
 * name as `ports[].label` and `heads[].label`, present only when set, and
 * POST /api/v1/config takes {"labels":{"ports":{"4":"…"},"heads":{"1":"…"}}} as a
 * partial merge in which an empty string clears one.
 *
 * Kept by port and head number, not tied to a module: a console compares what it
 * has with what the node says and asks when they differ.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"
#include "oh_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Every port and every head a module can have, one label each. */
#define OH_LABELS_MAX (OH_MAX_PORTS + OH_MAX_HEADS)

typedef struct {
    bool head;     /* a head's label; otherwise a port's */
    uint8_t number;
    char text[OH_NAME_MAX];
} oh_label_t;

typedef struct {
    int n;
    oh_label_t entry[OH_LABELS_MAX];
} oh_labels_t;

/* The label of a port or a head, or NULL where nobody named it. */
const char *oh_labels_port(const oh_labels_t *l, uint8_t port);
const char *oh_labels_head(const oh_labels_t *l, uint8_t head);

/*
 * Merge a `labels` object over `l`, whole or not at all. A port or head `module`
 * does not have, a name over OH_NAME_MAX - 1 bytes, one that is not UTF-8 or holds
 * a control character refuses the body with `err` (OH_ERR_MAX) and leaves `l` as it
 * was. `module` NULL skips the check, for reading back what was stored. Leading and
 * trailing spaces are dropped; an empty label clears one. `changed` says whether
 * anything moved.
 */
bool oh_labels_merge(oh_labels_t *l, const cJSON *labels, const oh_module_t *module, char *err, bool *changed);

/* {"ports":{"4":"…"},"heads":{"1":"…"}}, both keys always present. */
cJSON *oh_labels_to_json(const oh_labels_t *l);

/* Put `ports[].label` and `heads[].label` into an /info body. */
void oh_labels_annotate(const oh_labels_t *l, cJSON *info);

#ifdef __cplusplus
}
#endif
