/*
 * Every wire shape in one place: the JSON bodies of /api/v1/info, /state,
 * the per-dataType payloads, the input events and the health message, as
 * OpenHaunt/node doc 06 (PR "wire-payloads") writes them.
 */
#pragma once

#include "cJSON.h"
#include "oh_effect.h"
#include "oh_labels.h"
#include "oh_module.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *sn;   /* node serial, 8 hex */
    const char *fw;   /* firmware version */
    const char *name; /* resolved friendly name */
    const oh_labels_t *labels; /* what somebody called its ports and heads; NULL for none */
} oh_node_identity_t;

cJSON *oh_json_port_desc(const oh_port_desc_t *p);
/* The `heads` list of `info` (doc 14). */
cJSON *oh_json_heads(const oh_module_t *m);
cJSON *oh_json_info(const oh_module_t *m, const oh_node_identity_t *id);

/* {"outputs":{"<n>":payload}} for every port. `states` is indexed like m->ports. */
cJSON *oh_json_state(const oh_module_t *m, const oh_port_state_t *states);

cJSON *oh_json_value(const oh_value_t *v);

/* Parse a payload for a port of type `t`. Rejects the wrong key, out-of-range
 * color bytes and over-long text. */
bool oh_json_value_parse(oh_datatype_t t, const cJSON *payload, oh_value_t *out);

/* What an input publishes: {state,edge,ts} or {value,unit,ts}. */
cJSON *oh_json_input_event(const oh_port_desc_t *p, const oh_value_t *v, int64_t ts_ms);

cJSON *oh_json_health(uint32_t uptime_s, const float *temp_c, const int *poe_class, const char *const *errors,
                      size_t n_errors);

/* openhaunt/<sn>/net: {"addresses":[{"transport","ip"}, ...]}, the preferred path first. */
cJSON *oh_json_net(const char *const *transports, const char *const *ips, size_t n);

/* Walk {"outputs":{...}} and {"effects":{...}} calling `fn` and `effect_fn` for
 * every entry that names a writable port with a well-formed payload. Returns the
 * number applied, or -1 with `err` filled when the body is malformed or names a
 * port that cannot be written (the whole body is validated before anything is
 * applied — an `effects` map that will not parse rejects the `outputs` beside it).
 *
 * `payload` is the raw entry the value came from, so a caller that wants the
 * timing keys riding beside it can reach them without parsing twice.
 * `effect_fn` may be NULL on a build that cannot render one. */
typedef void (*oh_json_output_fn)(void *ctx, uint8_t port, const oh_value_t *value, const cJSON *payload);
typedef void (*oh_json_effect_fn)(void *ctx, uint8_t port, const oh_effect_t *effect);
int oh_json_apply_outputs(const cJSON *body, const oh_module_t *m, oh_json_output_fn fn, oh_json_effect_fn effect_fn,
                          void *ctx, char *err);

/* Small helpers shared with the config code. */
bool oh_json_copy_string(const cJSON *obj, const char *key, char *out, size_t out_len);
cJSON *oh_json_ok(void);
cJSON *oh_json_error(const char *message);

#ifdef __cplusplus
}
#endif
