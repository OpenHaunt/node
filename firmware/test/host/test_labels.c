#include <string.h>

#include "oh_json.h"
#include "oh_labels.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Two relays on the node itself and a strip as head 1, with its color on port 6. */
static oh_module_t a_module(void)
{
    oh_module_t m;
    oh_module_init_headless(&m);
    oh_port_desc_t p = oh_port_new(0, "Relay 1", OH_ACCESS_READWRITE, OH_DT_BOOLEAN);
    oh_module_add_port(&m, &p);
    p = oh_port_new(4, "Sensor 1", OH_ACCESS_READONLY, OH_DT_BOOLEAN);
    oh_module_add_port(&m, &p);
    oh_module_add_head(&m, 1, "Strip 1", "pixel-strip");
    p = oh_port_new(6, "Color", OH_ACCESS_READWRITE, OH_DT_COLOR);
    p.head = 1;
    oh_module_add_port(&m, &p);
    return m;
}

static bool merge(oh_labels_t *l, const char *json, const oh_module_t *m, char *err, bool *changed)
{
    cJSON *body = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(body);
    bool ok = oh_labels_merge(l, body, m, err, changed);
    cJSON_Delete(body);
    return ok;
}

static void a_port_and_a_head_are_named(void)
{
    oh_module_t m = a_module();
    oh_labels_t l = {0};
    char err[OH_ERR_MAX];
    bool changed;
    TEST_ASSERT_TRUE(merge(&l, "{\"ports\":{\"4\":\" Ausgang PIR \"},\"heads\":{\"1\":\"LED Streifen Tor\"}}", &m, err,
                           &changed));
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_EQUAL_STRING("Ausgang PIR", oh_labels_port(&l, 4));
    TEST_ASSERT_EQUAL_STRING("LED Streifen Tor", oh_labels_head(&l, 1));
    TEST_ASSERT_NULL(oh_labels_port(&l, 0));

    TEST_ASSERT_TRUE(merge(&l, "{\"ports\":{\"4\":\"Ausgang PIR\"}}", &m, err, &changed));
    TEST_ASSERT_FALSE_MESSAGE(changed, "the same name again is no change");

    TEST_ASSERT_TRUE(merge(&l, "{\"ports\":{\"4\":\"\"}}", &m, err, &changed));
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_NULL_MESSAGE(oh_labels_port(&l, 4), "an empty label clears it");
    TEST_ASSERT_EQUAL_STRING("LED Streifen Tor", oh_labels_head(&l, 1));
}

static void a_body_with_anything_wrong_changes_nothing(void)
{
    oh_module_t m = a_module();
    oh_labels_t l = {0};
    char err[OH_ERR_MAX];
    bool changed;
    TEST_ASSERT_TRUE(merge(&l, "{\"ports\":{\"0\":\"Relay Horn\"}}", &m, err, &changed));

    TEST_ASSERT_FALSE(merge(&l, "{\"ports\":{\"0\":\"Horn\",\"9\":\"x\"}}", &m, err, &changed));
    TEST_ASSERT_NOT_NULL(strstr(err, "no port 9"));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Relay Horn", oh_labels_port(&l, 0), "nothing taken from a refused body");

    TEST_ASSERT_FALSE(merge(&l, "{\"heads\":{\"2\":\"x\"}}", &m, err, &changed));
    TEST_ASSERT_FALSE(merge(&l, "{\"ports\":{\"zero\":\"x\"}}", &m, err, &changed));
    TEST_ASSERT_FALSE(merge(&l, "{\"ports\":{\"0\":5}}", &m, err, &changed));
    TEST_ASSERT_FALSE(merge(&l, "{\"ports\":{\"0\":\"a\\nb\"}}", &m, err, &changed));
    TEST_ASSERT_FALSE(merge(&l, "{\"ports\":{\"0\":\"\\u00ff\\u00ff\\u00ff\\u00ff\\u00ff\\u00ff\\u00ff\\u00ff"
                                "\\u00ff\\u00ff\\u00ff\\u00ff\\u00ff\\u00ff\\u00ff\\u00ff\"}}",
                            &m, err, &changed));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(err, "31 bytes"), "sixteen two-byte characters are 32 bytes");
    TEST_ASSERT_EQUAL_STRING("Relay Horn", oh_labels_port(&l, 0));
}

static void what_was_stored_reads_back_without_a_module(void)
{
    oh_module_t m = a_module();
    oh_labels_t l = {0}, back = {0};
    char err[OH_ERR_MAX];
    bool changed;
    TEST_ASSERT_TRUE(merge(&l, "{\"ports\":{\"4\":\"Türöffner\"},\"heads\":{\"1\":\"Tor\"}}", &m, err, &changed));
    cJSON *json = oh_labels_to_json(&l);
    TEST_ASSERT_TRUE(oh_labels_merge(&back, json, NULL, err, &changed));
    cJSON_Delete(json);
    TEST_ASSERT_EQUAL_STRING("Türöffner", oh_labels_port(&back, 4));
    TEST_ASSERT_EQUAL_STRING("Tor", oh_labels_head(&back, 1));
}

static void info_carries_a_label_beside_the_name(void)
{
    oh_module_t m = a_module();
    oh_labels_t l = {0};
    char err[OH_ERR_MAX];
    bool changed;
    TEST_ASSERT_TRUE(merge(&l, "{\"ports\":{\"4\":\"Ausgang PIR\"},\"heads\":{\"1\":\"Tor\"}}", &m, err, &changed));
    oh_node_identity_t id = {.sn = "1a2b3c4d", .fw = "0.3.1", .name = "Hof", .labels = &l};
    cJSON *info = oh_json_info(&m, &id);
    const cJSON *ports = cJSON_GetObjectItem(info, "ports");
    const cJSON *relay = cJSON_GetArrayItem(ports, 0), *sensor = cJSON_GetArrayItem(ports, 1);
    TEST_ASSERT_EQUAL_STRING("Sensor 1", cJSON_GetObjectItem(sensor, "name")->valuestring);
    TEST_ASSERT_EQUAL_STRING("Ausgang PIR", cJSON_GetObjectItem(sensor, "label")->valuestring);
    TEST_ASSERT_NULL_MESSAGE(cJSON_GetObjectItem(relay, "label"), "absent where nobody named it");
    const cJSON *head = cJSON_GetArrayItem(cJSON_GetObjectItem(info, "heads"), 0);
    TEST_ASSERT_EQUAL_STRING("Tor", cJSON_GetObjectItem(head, "label")->valuestring);
    cJSON_Delete(info);

    oh_node_identity_t none = {.sn = "1a2b3c4d", .fw = "0.3.1", .name = "Hof"};
    info = oh_json_info(&m, &none);
    TEST_ASSERT_NULL(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(info, "ports"), 1), "label"));
    cJSON_Delete(info);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(a_port_and_a_head_are_named);
    RUN_TEST(a_body_with_anything_wrong_changes_nothing);
    RUN_TEST(what_was_stored_reads_back_without_a_module);
    RUN_TEST(info_carries_a_label_beside_the_name);
    return UNITY_END();
}
