#include <stdlib.h>
#include <string.h>

#include "oh_curve.h"
#include "oh_json.h"
#include "oh_vmod.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static oh_module_t strip(void)
{
    oh_module_t m;
    TEST_ASSERT_TRUE(oh_vmod_preset("led", NULL, &m));
    strcpy(m.module.sn, "9f8e7d6c");
    strcpy(m.module.rev, "2");
    return m;
}

static void info_has_the_documented_shape(void)
{
    oh_module_t m = strip();
    oh_node_identity_t id = {.sn = "1a2b3c4d", .fw = "0.3.1", .name = "Coffin Room LED"};
    cJSON *info = oh_json_info(&m, &id);

    TEST_ASSERT_EQUAL_STRING("1", cJSON_GetObjectItem(info, "v")->valuestring);
    TEST_ASSERT_EQUAL_STRING("0.3.1", cJSON_GetObjectItem(info, "fw")->valuestring);
    TEST_ASSERT_EQUAL_STRING("1a2b3c4d", cJSON_GetObjectItem(info, "sn")->valuestring);
    cJSON *module = cJSON_GetObjectItem(info, "module");
    TEST_ASSERT_EQUAL_STRING("0x0003", cJSON_GetObjectItem(module, "type")->valuestring);
    TEST_ASSERT_EQUAL_STRING("2", cJSON_GetObjectItem(module, "rev")->valuestring);
    TEST_ASSERT_EQUAL(1 << 3, cJSON_GetObjectItem(module, "flags")->valueint);
    TEST_ASSERT_EQUAL_STRING("", cJSON_GetObjectItem(module, "caps")->valuestring);

    cJSON *ports = cJSON_GetObjectItem(info, "ports");
    TEST_ASSERT_EQUAL(2, cJSON_GetArraySize(ports));
    cJSON *colour = cJSON_GetArrayItem(ports, 0);
    TEST_ASSERT_EQUAL_STRING("readwrite", cJSON_GetObjectItem(colour, "access")->valuestring);
    TEST_ASSERT_EQUAL_STRING("color", cJSON_GetObjectItem(colour, "dataType")->valuestring);
    TEST_ASSERT_NULL(cJSON_GetObjectItem(colour, "unit"));
    TEST_ASSERT_NULL(cJSON_GetObjectItem(colour, "minimum"));
    cJSON *bright = cJSON_GetArrayItem(ports, 1);
    TEST_ASSERT_EQUAL_STRING("percent", cJSON_GetObjectItem(bright, "unit")->valuestring);
    TEST_ASSERT_EQUAL(0, cJSON_GetObjectItem(bright, "minimum")->valueint);
    TEST_ASSERT_EQUAL(1, cJSON_GetObjectItem(bright, "maximum")->valueint);
    TEST_ASSERT_EQUAL(0, cJSON_GetObjectItem(bright, "default")->valueint);
    TEST_ASSERT_EQUAL_STRING("intensity", cJSON_GetObjectItem(bright, "class")->valuestring);
    /* No universe: no dmx key at all. */
    TEST_ASSERT_NULL(cJSON_GetObjectItem(info, "dmx"));
    cJSON_Delete(info);
}

static void a_gateway_says_so_in_info(void)
{
    oh_module_t m;
    oh_vmod_preset("dmx", NULL, &m);
    oh_node_identity_t id = {.sn = "1a2b3c4d", .fw = "0.0.0", .name = "gw"};
    cJSON *info = oh_json_info(&m, &id);
    cJSON *dmx = cJSON_GetObjectItem(info, "dmx");
    TEST_ASSERT_NOT_NULL(dmx);
    TEST_ASSERT_EQUAL(1, cJSON_GetObjectItem(dmx, "universes")->valueint);
    TEST_ASSERT_EQUAL_STRING("sacn", cJSON_GetArrayItem(cJSON_GetObjectItem(dmx, "protocols"), 0)->valuestring);
    TEST_ASSERT_EQUAL(0, cJSON_GetArraySize(cJSON_GetObjectItem(info, "ports")));
    cJSON_Delete(info);
}

static void state_lists_every_port_by_number(void)
{
    oh_module_t m = strip();
    oh_port_state_t states[OH_MAX_PORTS] = {0};
    states[1].valid = true;
    states[1].value.type = OH_DT_NUMBER;
    states[1].value.value = 0.5f;
    cJSON *state = oh_json_state(&m, states);
    cJSON *outputs = cJSON_GetObjectItem(state, "outputs");
    cJSON *colour = cJSON_GetObjectItem(outputs, "0");
    TEST_ASSERT_NOT_NULL(colour);
    TEST_ASSERT_EQUAL(0, cJSON_GetObjectItem(colour, "r")->valueint);
    TEST_ASSERT_EQUAL_DOUBLE(0.5, cJSON_GetObjectItem(cJSON_GetObjectItem(outputs, "1"), "value")->valuedouble);
    cJSON_Delete(state);
}

static void payloads_follow_the_data_type(void)
{
    oh_value_t v;
    cJSON *p;

    p = cJSON_Parse("{\"state\": true}");
    TEST_ASSERT_TRUE(oh_json_value_parse(OH_DT_BOOLEAN, p, &v));
    TEST_ASSERT_TRUE(v.state);
    /* Wrong shape for the type. */
    TEST_ASSERT_FALSE(oh_json_value_parse(OH_DT_NUMBER, p, &v));
    cJSON_Delete(p);

    p = cJSON_Parse("{\"value\": 0.25}");
    TEST_ASSERT_TRUE(oh_json_value_parse(OH_DT_NUMBER, p, &v));
    TEST_ASSERT_EQUAL_FLOAT(0.25f, v.value);
    cJSON_Delete(p);

    p = cJSON_Parse("{\"brightness\": 0.25}");
    TEST_ASSERT_FALSE(oh_json_value_parse(OH_DT_NUMBER, p, &v));
    cJSON_Delete(p);

    p = cJSON_Parse("{\"r\": 255, \"g\": 128, \"b\": 300}");
    TEST_ASSERT_TRUE(oh_json_value_parse(OH_DT_COLOR, p, &v));
    TEST_ASSERT_EQUAL(255, v.color.r);
    TEST_ASSERT_EQUAL(128, v.color.g);
    TEST_ASSERT_EQUAL(255, v.color.b); /* clamped */
    cJSON_Delete(p);

    p = cJSON_Parse("{\"r\": 1, \"g\": 2}");
    TEST_ASSERT_FALSE(oh_json_value_parse(OH_DT_COLOR, p, &v));
    cJSON_Delete(p);

    p = cJSON_Parse("{\"text\": \"ACT ONE\"}");
    TEST_ASSERT_TRUE(oh_json_value_parse(OH_DT_STRING, p, &v));
    TEST_ASSERT_EQUAL_STRING("ACT ONE", v.text);
    cJSON_Delete(p);

    TEST_ASSERT_FALSE(oh_json_value_parse(OH_DT_STRING, NULL, &v));
}

static void an_input_event_carries_edge_or_unit_and_a_timestamp(void)
{
    oh_module_t env;
    oh_vmod_preset("env", NULL, &env);
    oh_value_t reading = {.type = OH_DT_NUMBER, .value = 21.5f};
    cJSON *e = oh_json_input_event(&env.ports[0], &reading, 1699999999999LL);
    TEST_ASSERT_EQUAL_STRING("degree-celsius", cJSON_GetObjectItem(e, "unit")->valuestring);
    TEST_ASSERT_EQUAL_DOUBLE(1699999999999.0, cJSON_GetObjectItem(e, "ts")->valuedouble);
    TEST_ASSERT_NULL(cJSON_GetObjectItem(e, "edge"));
    cJSON_Delete(e);

    oh_module_t in;
    oh_vmod_preset("input", NULL, &in);
    oh_value_t closed = {.type = OH_DT_BOOLEAN, .state = true};
    e = oh_json_input_event(&in.ports[3], &closed, 5);
    TEST_ASSERT_EQUAL_STRING("rising", cJSON_GetObjectItem(e, "edge")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(e, "state")));
    cJSON_Delete(e);
    oh_value_t open = {.type = OH_DT_BOOLEAN, .state = false};
    e = oh_json_input_event(&in.ports[3], &open, 6);
    TEST_ASSERT_EQUAL_STRING("falling", cJSON_GetObjectItem(e, "edge")->valuestring);
    cJSON_Delete(e);

    /* A number port with no unit says "unitless" rather than nothing. */
    oh_port_desc_t bare = oh_port_new(0, "Level", OH_ACCESS_READONLY, OH_DT_NUMBER);
    e = oh_json_input_event(&bare, &reading, 1);
    TEST_ASSERT_EQUAL_STRING("unitless", cJSON_GetObjectItem(e, "unit")->valuestring);
    cJSON_Delete(e);
}

static void health_leaves_out_what_it_does_not_know(void)
{
    cJSON *h = oh_json_health(4210, NULL, NULL, NULL, 0);
    TEST_ASSERT_EQUAL(4210, cJSON_GetObjectItem(h, "uptime_s")->valueint);
    TEST_ASSERT_NULL(cJSON_GetObjectItem(h, "temp_c"));
    TEST_ASSERT_EQUAL(0, cJSON_GetArraySize(cJSON_GetObjectItem(h, "errors")));
    cJSON_Delete(h);
    float t = 41.5f;
    int cls = 3;
    const char *errs[] = {"i2c", "module-absent"};
    h = oh_json_health(1, &t, &cls, errs, 2);
    TEST_ASSERT_EQUAL_FLOAT(41.5f, (float)cJSON_GetObjectItem(h, "temp_c")->valuedouble);
    TEST_ASSERT_EQUAL(3, cJSON_GetObjectItem(h, "poe_class")->valueint);
    TEST_ASSERT_EQUAL_STRING("i2c", cJSON_GetArrayItem(cJSON_GetObjectItem(h, "errors"), 0)->valuestring);
    cJSON_Delete(h);
}

typedef struct {
    int calls;
    uint8_t port;
    oh_value_t value;
} capture_t;

static void capture(void *ctx, uint8_t port, const oh_value_t *v, const cJSON *payload)
{
    (void)payload;
    capture_t *c = ctx;
    c->calls++;
    c->port = port;
    c->value = *v;
}

static void a_state_body_is_applied_whole_or_not_at_all(void)
{
    oh_module_t m = strip();
    capture_t c = {0};
    char err[OH_ERR_MAX];

    cJSON *body = cJSON_Parse("{\"outputs\": {\"1\": {\"value\": 2.0}}}");
    TEST_ASSERT_EQUAL(1, oh_json_apply_outputs(body, &m, capture, NULL, &c, err));
    TEST_ASSERT_EQUAL(1, c.calls);
    TEST_ASSERT_EQUAL(1, c.port);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, c.value.value); /* clamped to maximum */
    cJSON_Delete(body);

    /* One good entry and one bad one: nothing is applied. */
    c.calls = 0;
    body = cJSON_Parse("{\"outputs\": {\"0\": {\"r\":1,\"g\":2,\"b\":3}, \"7\": {\"state\": true}}}");
    TEST_ASSERT_EQUAL(-1, oh_json_apply_outputs(body, &m, capture, NULL, &c, err));
    TEST_ASSERT_EQUAL(0, c.calls);
    TEST_ASSERT_EQUAL_STRING("no port 7", err);
    cJSON_Delete(body);

    body = cJSON_Parse("{\"outputs\": {\"1\": {\"state\": true}}}");
    TEST_ASSERT_EQUAL(-1, oh_json_apply_outputs(body, &m, capture, NULL, &c, err));
    TEST_ASSERT_EQUAL_STRING("port 1 takes {\"value\": ...}", err);
    cJSON_Delete(body);

    body = cJSON_Parse("{\"outputs\": {\"x\": {\"state\": true}}}");
    TEST_ASSERT_EQUAL(-1, oh_json_apply_outputs(body, &m, capture, NULL, &c, err));
    cJSON_Delete(body);

    body = cJSON_Parse("{\"nothing\": 1}");
    TEST_ASSERT_EQUAL(-1, oh_json_apply_outputs(body, &m, capture, NULL, &c, err));
    cJSON_Delete(body);

    /* A readonly port cannot be driven. */
    oh_module_t env;
    oh_vmod_preset("env", NULL, &env);
    body = cJSON_Parse("{\"outputs\": {\"0\": {\"value\": 1}}}");
    TEST_ASSERT_EQUAL(-1, oh_json_apply_outputs(body, &env, capture, NULL, &c, err));
    TEST_ASSERT_EQUAL_STRING("port 0 is readonly", err);
    cJSON_Delete(body);
}

/* Effects are validated in the same first pass as outputs, so a body carrying a
 * descriptor this node cannot honour rejects the plain values beside it too. Half
 * of a cue is worse than none of it. */
static void an_effects_map_is_validated_with_the_outputs_beside_it(void)
{
    oh_module_t m = strip();
    capture_t c = {0};
    char err[OH_ERR_MAX];

    cJSON *body = cJSON_Parse("{\"outputs\":{\"1\":{\"value\":0.5}},"
                              "\"effects\":{\"1\":{\"curve\":{\"shape\":\"sine\"},\"rate\":1}}}");
    TEST_ASSERT_EQUAL(2, oh_json_apply_outputs(body, &m, capture, NULL, &c, err));
    TEST_ASSERT_EQUAL_MESSAGE(1, c.calls, "the value still went through");
    cJSON_Delete(body);

    /* A shape this node has never heard of takes the good value down with it. */
    c.calls = 0;
    body = cJSON_Parse("{\"outputs\":{\"1\":{\"value\":0.5}},"
                       "\"effects\":{\"1\":{\"curve\":{\"shape\":\"spiral\"},\"rate\":1}}}");
    TEST_ASSERT_EQUAL(-1, oh_json_apply_outputs(body, &m, capture, NULL, &c, err));
    TEST_ASSERT_EQUAL_MESSAGE(0, c.calls, "and nothing was applied");
    cJSON_Delete(body);

    /* An effect on a port the node reads is refused the same way a value is. */
    oh_module_t env;
    oh_vmod_preset("env", NULL, &env);
    body = cJSON_Parse("{\"outputs\":{},\"effects\":{\"0\":{\"curve\":{\"shape\":\"sine\"},\"rate\":1}}}");
    TEST_ASSERT_EQUAL(-1, oh_json_apply_outputs(body, &env, capture, NULL, &c, err));
    TEST_ASSERT_EQUAL_STRING("port 0 is readonly", err);
    cJSON_Delete(body);
}

/* The block a console reads to decide whether to send a shape or a stream of
 * values. A port that sets nothing is indistinguishable from firmware that has
 * never heard of effects, which is the whole compatibility story. */
static void a_ports_description_says_what_it_can_trace(void)
{
    oh_module_t m;
    oh_vmod_preset("led", NULL, &m);
    const oh_node_identity_t id = {.sn = "1a2b3c4d", .fw = "0.1.0", .name = "Strip"};

    cJSON *info = oh_json_info(&m, &id);
    const cJSON *ports = cJSON_GetObjectItemCaseSensitive(info, "ports");
    const cJSON *brightness = cJSON_GetArrayItem(ports, 1);
    const cJSON *effects = cJSON_GetObjectItemCaseSensitive(brightness, "effects");
    TEST_ASSERT_NOT_NULL(effects);

    const cJSON *shapes = cJSON_GetObjectItemCaseSensitive(effects, "shapes");
    TEST_ASSERT_EQUAL(OH_SHAPE_COUNT, cJSON_GetArraySize(shapes));
    TEST_ASSERT_EQUAL_STRING("sine", cJSON_GetArrayItem(shapes, 0)->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(effects, "steps")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(effects, "transitions")));
    cJSON_Delete(info);

    /* A relay is honest about having two states and nothing in between. */
    oh_vmod_preset("relay", NULL, &m);
    info = oh_json_info(&m, &id);
    effects = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(info, "ports"), 0), "effects");
    shapes = cJSON_GetObjectItemCaseSensitive(effects, "shapes");
    TEST_ASSERT_EQUAL(1, cJSON_GetArraySize(shapes));
    TEST_ASSERT_EQUAL_STRING("square", cJSON_GetArrayItem(shapes, 0)->valuestring);
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(effects, "transitions")));
    cJSON_Delete(info);

    /* And a sensor says nothing at all, because nothing drives an input. */
    oh_vmod_preset("env", NULL, &m);
    info = oh_json_info(&m, &id);
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(
        cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(info, "ports"), 0), "effects"));
    cJSON_Delete(info);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(info_has_the_documented_shape);
    RUN_TEST(a_gateway_says_so_in_info);
    RUN_TEST(state_lists_every_port_by_number);
    RUN_TEST(payloads_follow_the_data_type);
    RUN_TEST(an_input_event_carries_edge_or_unit_and_a_timestamp);
    RUN_TEST(health_leaves_out_what_it_does_not_know);
    RUN_TEST(a_state_body_is_applied_whole_or_not_at_all);
    RUN_TEST(an_effects_map_is_validated_with_the_outputs_beside_it);
    RUN_TEST(a_ports_description_says_what_it_can_trace);
    return UNITY_END();
}
