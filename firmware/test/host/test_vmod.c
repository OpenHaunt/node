#include <stdlib.h>
#include <string.h>

#include "oh_vmod.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const char *FOGGER =
    "{ \"name\": \"Crypt fogger\", \"serial\": \"9f1c22\","
    "  \"module\": { \"type\": \"0x0100\", \"name\": \"Fogger\", \"rev\": \"a\", \"flags\": 0, \"caps\": \"\" },"
    "  \"ports\": ["
    "    { \"port\": 0, \"name\": \"Fog output\", \"access\": \"readwrite\", \"dataType\": \"number\","
    "      \"unit\": \"percent\", \"minimum\": 0, \"maximum\": 1, \"default\": 0, \"class\": \"intensity\" },"
    "    { \"port\": 1, \"name\": \"Heater\", \"access\": \"readwrite\", \"dataType\": \"boolean\", \"default\": 0,"
    "      \"class\": \"switch\" },"
    "    { \"port\": 2, \"name\": \"Tank level\", \"access\": \"readonly\", \"dataType\": \"number\","
    "      \"unit\": \"percent\", \"minimum\": 0, \"maximum\": 1 },"
    "    { \"port\": 3, \"name\": \"Ready to fog\", \"access\": \"readonly\", \"dataType\": \"boolean\","
    "      \"class\": \"contact\" } ],"
    "  \"bindings\": [ { \"driver\": \"gpio_out\", \"port\": 1, \"gpio\": 18, \"invert\": true },"
    "                  { \"driver\": \"gpio_in\", \"port\": 3, \"gpio\": 9, \"pull\": \"up\" } ],"
    "  \"httpPort\": 8811, \"advertise\": true }";

static void a_sim_config_file_loads(void)
{
    oh_module_t m;
    char err[OH_ERR_MAX];
    TEST_ASSERT_TRUE_MESSAGE(oh_vmod_parse(FOGGER, &m, err), err);
    TEST_ASSERT_EQUAL_STRING("Crypt fogger", m.name);
    TEST_ASSERT_EQUAL_HEX16(0x0100, m.module.type);
    TEST_ASSERT_EQUAL_STRING("Fogger", m.module.name);
    TEST_ASSERT_EQUAL(4, m.n_ports);
    TEST_ASSERT_EQUAL(OH_ACCESS_READONLY, m.ports[2].access);
    TEST_ASSERT_EQUAL(OH_DT_NUMBER, m.ports[2].type);
    TEST_ASSERT_TRUE(m.ports[2].has_max);
    TEST_ASSERT_FALSE(m.ports[2].has_default);
    TEST_ASSERT_EQUAL_STRING("", m.ports[2].class_);
    TEST_ASSERT_FALSE(m.has_dmx);
    TEST_ASSERT_EQUAL(2, m.n_bindings);
    TEST_ASSERT_EQUAL_STRING("gpio_out", m.bindings[0].driver);
    TEST_ASSERT_EQUAL(1, m.bindings[0].ports[0]);
    TEST_ASSERT_NOT_NULL(strstr(m.bindings[0].params_json, "\"gpio\":18"));
    TEST_ASSERT_EQUAL(OH_MOD_SRC_VIRTUAL, m.source);
}

static void a_config_written_by_hand_may_leave_out_everything_optional(void)
{
    oh_module_t m;
    char err[OH_ERR_MAX];
    TEST_ASSERT_TRUE(oh_vmod_parse("{\"module\": {\"type\": 4, \"name\": \"Relay\"}}", &m, err));
    TEST_ASSERT_EQUAL_HEX16(4, m.module.type);
    TEST_ASSERT_EQUAL_STRING("a", m.module.rev);
    TEST_ASSERT_EQUAL(0, m.n_ports);
    TEST_ASSERT_TRUE(oh_module_is_empty(&m));
}

static void problems_are_named(void)
{
    oh_module_t m;
    char err[OH_ERR_MAX];

    TEST_ASSERT_FALSE(oh_vmod_parse("not json", &m, err));
    TEST_ASSERT_NOT_NULL(strstr(err, "not JSON"));

    TEST_ASSERT_FALSE(oh_vmod_parse("{\"module\": {\"type\": \"0x0001\", \"name\": \"X\"}, \"ports\": ["
                                    "{\"port\": 0, \"name\": \"One\", \"access\": \"readwrite\", \"dataType\": \"boolean\"},"
                                    "{\"port\": 0, \"name\": \"Two\", \"access\": \"readwrite\", \"dataType\": \"boolean\"}]}",
                                    &m, err));
    TEST_ASSERT_NOT_NULL(strstr(err, "numbered 0"));

    TEST_ASSERT_FALSE(oh_vmod_parse("{\"module\": {\"type\": \"0x0001\", \"name\": \"X\"}, \"ports\": ["
                                    "{\"port\": 0, \"name\": \"One\", \"access\": \"sideways\", \"dataType\": \"boolean\"}]}",
                                    &m, err));
    TEST_ASSERT_NOT_NULL(strstr(err, "sideways"));

    TEST_ASSERT_FALSE(oh_vmod_parse("{\"module\": {\"type\": \"0x0001\", \"name\": \"X\"}, \"ports\": ["
                                    "{\"port\": 0, \"name\": \"One\", \"access\": \"readonly\", \"dataType\": \"vibes\"}]}",
                                    &m, err));
    TEST_ASSERT_NOT_NULL(strstr(err, "vibes"));

    TEST_ASSERT_FALSE(oh_vmod_parse("{\"module\": {\"type\": \"zzz\", \"name\": \"X\"}}", &m, err));
    TEST_ASSERT_NOT_NULL(strstr(err, "type"));

    TEST_ASSERT_FALSE(oh_vmod_parse("{\"module\": {\"type\": 1, \"name\": \"X\"},"
                                    "\"bindings\": [{\"driver\": \"gpio_in\", \"port\": 4}]}",
                                    &m, err));
    TEST_ASSERT_NOT_NULL(strstr(err, "not declared"));

    TEST_ASSERT_FALSE(oh_vmod_parse("{\"module\": {\"type\": 1, \"name\": \"X\"}, \"ports\": ["
                                    "{\"port\": 0, \"name\": \"One\", \"access\": \"readonly\", \"dataType\": \"boolean\"}],"
                                    "\"bindings\": [{\"driver\": \"gpio_in\", \"port\": 0},"
                                    "{\"driver\": \"gpio_in\", \"ports\": [0]}]}",
                                    &m, err));
    TEST_ASSERT_NOT_NULL(strstr(err, "bound twice"));
}

static void every_preset_numbers_its_ports_from_zero_and_round_trips(void)
{
    size_t n;
    const char *const *keys = oh_vmod_preset_keys(&n);
    oh_vmod_pins_t pins = {.inputs = {9, 10, 11, -1, -1, -1, -1, -1}, .outputs = {18, 19, 20, 21}, .strip = 4,
                           .mod_int = 5};
    for (size_t i = 0; i < n; i++) {
        oh_module_t m;
        TEST_ASSERT_TRUE_MESSAGE(oh_vmod_preset(keys[i], &pins, &m), keys[i]);
        for (int p = 0; p < m.n_ports; p++) {
            TEST_ASSERT_EQUAL_MESSAGE(p, m.ports[p].port, keys[i]);
        }
        cJSON *json = oh_vmod_to_json(&m);
        char *text = cJSON_PrintUnformatted(json);
        cJSON_Delete(json);
        oh_module_t again;
        char err[OH_ERR_MAX];
        TEST_ASSERT_TRUE_MESSAGE(oh_vmod_parse(text, &again, err), err);
        free(text);
        TEST_ASSERT_EQUAL_MESSAGE(m.module.type, again.module.type, keys[i]);
        TEST_ASSERT_EQUAL_MESSAGE(m.n_ports, again.n_ports, keys[i]);
        TEST_ASSERT_EQUAL_MESSAGE(m.n_bindings, again.n_bindings, keys[i]);
        TEST_ASSERT_EQUAL_MESSAGE(m.has_dmx, again.has_dmx, keys[i]);
        for (int p = 0; p < m.n_ports; p++) {
            TEST_ASSERT_EQUAL_MEMORY(&m.ports[p], &again.ports[p], sizeof(oh_port_desc_t));
        }
    }
    TEST_ASSERT_FALSE(oh_vmod_preset("toaster", &pins, NULL));
}

static void only_the_gateway_forwards_a_universe_and_only_the_relay_switches_mains(void)
{
    oh_module_t m;
    oh_vmod_preset("dmx", NULL, &m);
    TEST_ASSERT_TRUE(m.has_dmx);
    TEST_ASSERT_EQUAL(0, m.n_ports);
    TEST_ASSERT_TRUE(oh_module_dmx_accepts(&m, "sacn"));
    TEST_ASSERT_TRUE(oh_module_dmx_accepts(&m, "artnet"));
    TEST_ASSERT_FALSE(oh_module_dmx_accepts(&m, "osc"));
    TEST_ASSERT_FALSE(oh_module_switches_mains(&m));
    oh_vmod_preset("relay", NULL, &m);
    TEST_ASSERT_TRUE(oh_module_switches_mains(&m));
    TEST_ASSERT_FALSE(m.has_dmx);
    TEST_ASSERT_FALSE(oh_module_dmx_accepts(&m, "sacn"));
}

static void presets_bind_the_pins_the_board_offers(void)
{
    oh_vmod_pins_t pins = {.inputs = {9, -1, -1, -1, -1, -1, -1, -1}, .outputs = {18, -1, -1, -1}, .strip = -1,
                           .mod_int = -1};
    oh_module_t m;
    oh_vmod_preset("input", &pins, &m);
    TEST_ASSERT_EQUAL(8, m.n_ports);
    TEST_ASSERT_EQUAL(1, m.n_bindings);
    TEST_ASSERT_EQUAL_STRING("gpio_in", m.bindings[0].driver);
    oh_vmod_preset("led", &pins, &m);
    TEST_ASSERT_EQUAL(2, m.n_ports);
    TEST_ASSERT_EQUAL(0, m.n_bindings); /* no strip pin: software ports only */
    oh_vmod_preset("contact", &pins, &m);
    TEST_ASSERT_EQUAL(4, m.n_ports);
    TEST_ASSERT_EQUAL(1, m.n_bindings);
}

static oh_vmod_board_io_t v0_io(const char *rev, bool contact_low, int strip2)
{
    oh_vmod_board_io_t io = {
        .relays = {4, 5, 6, 7},
        .inputs = {15, 16},
        .input_contact_low = contact_low,
        .strips = {17, strip2},
        .strip_count = 60,
        .rev = rev,
    };
    return io;
}

static void both_v0_revisions_describe_the_same_ports(void)
{
    oh_vmod_board_io_t a = v0_io("0.1", true, 18), b = v0_io("0.2", false, 38);
    oh_module_t one, two;
    TEST_ASSERT_TRUE(oh_vmod_board_v0(&a, &one));
    TEST_ASSERT_TRUE(oh_vmod_board_v0(&b, &two));
    TEST_ASSERT_EQUAL_HEX16(OH_MODULE_TYPE_NODE_V0, one.module.type);
    TEST_ASSERT_EQUAL(OH_MOD_SRC_BOARD, one.source);
    TEST_ASSERT_EQUAL_STRING("0.2", two.module.rev);
    TEST_ASSERT_EQUAL(24, one.n_ports);
    TEST_ASSERT_EQUAL(one.n_ports, two.n_ports);
    /* The console derives one fixture type from the ports, so what the two
     * revisions say about them has to be identical. */
    for (int i = 0; i < one.n_ports; i++) {
        TEST_ASSERT_EQUAL(i, one.ports[i].port);
        TEST_ASSERT_EQUAL_STRING(one.ports[i].name, two.ports[i].name);
        TEST_ASSERT_EQUAL_STRING(one.ports[i].class_, two.ports[i].class_);
        TEST_ASSERT_EQUAL(one.ports[i].type, two.ports[i].type);
        TEST_ASSERT_EQUAL(one.ports[i].access, two.ports[i].access);
        TEST_ASSERT_EQUAL(one.ports[i].head, two.ports[i].head);
    }
    TEST_ASSERT_EQUAL_STRING("switch", one.ports[0].class_);
    TEST_ASSERT_EQUAL_STRING("contact", one.ports[4].class_);
    TEST_ASSERT_EQUAL(0, one.ports[5].head);
    /* Each strip is a head of its own, and both carry the classes: they are unique
     * per head, which is the whole reason heads exist. */
    TEST_ASSERT_EQUAL(2, one.n_heads);
    TEST_ASSERT_EQUAL_STRING("pixel-strip", one.heads[1].kind);
    TEST_ASSERT_EQUAL(OH_DT_COLOR, one.ports[6].type);
    TEST_ASSERT_EQUAL_STRING("color", one.ports[6].class_);
    TEST_ASSERT_EQUAL(1, one.ports[6].head);
    TEST_ASSERT_EQUAL_STRING("color", one.ports[15].class_);
    TEST_ASSERT_EQUAL_STRING("intensity", one.ports[16].class_);
    TEST_ASSERT_EQUAL(2, one.ports[15].head);
    TEST_ASSERT_EQUAL(OH_DT_STRING, one.ports[9].type);
    TEST_ASSERT_EQUAL_STRING("pixel-effect", one.ports[9].class_);
    TEST_ASSERT_EQUAL_STRING("", one.ports[8].class_); /* Color 2: a named parameter */
    TEST_ASSERT_EQUAL(23, one.ports[23].port);
}

static void the_v0_revisions_differ_only_in_their_bindings(void)
{
    oh_vmod_board_io_t a = v0_io("0.1", true, 18), b = v0_io("0.2", false, 38);
    oh_module_t one, two;
    TEST_ASSERT_TRUE(oh_vmod_board_v0(&a, &one));
    TEST_ASSERT_TRUE(oh_vmod_board_v0(&b, &two));
    TEST_ASSERT_EQUAL(8, one.n_bindings);
    TEST_ASSERT_EQUAL(8, two.n_bindings);
    /* Sensor 1 is binding 4: inverted on V0.1, not on V0.2. */
    TEST_ASSERT_EQUAL_STRING("gpio_in", one.bindings[4].driver);
    TEST_ASSERT_NOT_NULL(strstr(one.bindings[4].params_json, "\"invert\":true"));
    TEST_ASSERT_NULL(strstr(two.bindings[4].params_json, "invert"));
    TEST_ASSERT_NOT_NULL(strstr(one.bindings[7].params_json, "\"gpio\":18"));
    TEST_ASSERT_NOT_NULL(strstr(two.bindings[7].params_json, "\"gpio\":38"));
    TEST_ASSERT_NOT_NULL(strstr(two.bindings[7].params_json, "\"ports\":[15,16,17,18,19,20,21,22,23]"));
    TEST_ASSERT_NOT_NULL(strstr(two.bindings[7].params_json, "\"mode\":\"effect\""));
    /* And what the board says is a module the vmod parser accepts back. */
    cJSON *o = oh_vmod_to_json(&two);
    oh_module_t again;
    char err[OH_ERR_MAX];
    TEST_ASSERT_TRUE_MESSAGE(oh_vmod_parse_object(o, &again, err), err);
    cJSON_Delete(o);
    TEST_ASSERT_EQUAL(two.n_ports, again.n_ports);
    TEST_ASSERT_EQUAL(two.n_bindings, again.n_bindings);
    TEST_ASSERT_EQUAL(two.n_heads, again.n_heads);
    TEST_ASSERT_EQUAL(2, again.ports[15].head);
}

static void a_strip_in_sacn_mode_owns_no_ports_but_stays_a_head(void)
{
    oh_vmod_board_io_t io = v0_io("0.2", false, 38);
    oh_pix_cfg_default(&io.strip_cfg[1]);
    io.strip_cfg[1].mode = OH_PIX_MODE_SACN;
    io.strip_cfg[1].universe = 5;
    io.strip_cfg[1].count = 300;
    oh_module_t m;
    TEST_ASSERT_TRUE(oh_vmod_board_v0(&io, &m));
    TEST_ASSERT_EQUAL(15, m.n_ports);
    TEST_ASSERT_EQUAL(2, m.n_heads);
    TEST_ASSERT_NULL(oh_module_find_port(&m, 15));
    TEST_ASSERT_EQUAL(8, m.n_bindings);
    const char *p = m.bindings[7].params_json;
    TEST_ASSERT_NOT_NULL(strstr(p, "\"mode\":\"sacn\""));
    TEST_ASSERT_NOT_NULL(strstr(p, "\"universe\":5"));
    TEST_ASSERT_EQUAL(0, m.bindings[7].n_ports);
    /* 300 pixels against strip 1's 60: the long one gets the DMA channel. */
    TEST_ASSERT_NOT_NULL(strstr(p, "\"dma\":true"));
    TEST_ASSERT_NOT_NULL(strstr(m.bindings[6].params_json, "\"dma\":false"));
    /* And it survives the vmod round trip, empty port list and all. */
    cJSON *o = oh_vmod_to_json(&m);
    oh_module_t again;
    char err[OH_ERR_MAX];
    TEST_ASSERT_TRUE_MESSAGE(oh_vmod_parse_object(o, &again, err), err);
    cJSON_Delete(o);
    TEST_ASSERT_EQUAL(m.n_bindings, again.n_bindings);
}

static void a_port_naming_an_unlisted_head_is_refused(void)
{
    const char *json = "{\"module\":{\"type\":\"0x0003\",\"name\":\"x\"},"
                       "\"ports\":[{\"port\":0,\"name\":\"C\",\"access\":\"readwrite\",\"dataType\":\"color\",\"head\":2}]}";
    oh_module_t m;
    char err[OH_ERR_MAX];
    TEST_ASSERT_FALSE(oh_vmod_parse(json, &m, err));
    TEST_ASSERT_NOT_NULL(strstr(err, "head 2"));
}

static void a_missing_terminal_is_left_unbound(void)
{
    oh_vmod_board_io_t io = v0_io("0.1", true, -1);
    oh_module_t m;
    TEST_ASSERT_TRUE(oh_vmod_board_v0(&io, &m));
    TEST_ASSERT_EQUAL(24, m.n_ports);
    TEST_ASSERT_EQUAL(7, m.n_bindings);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(a_sim_config_file_loads);
    RUN_TEST(a_config_written_by_hand_may_leave_out_everything_optional);
    RUN_TEST(problems_are_named);
    RUN_TEST(every_preset_numbers_its_ports_from_zero_and_round_trips);
    RUN_TEST(only_the_gateway_forwards_a_universe_and_only_the_relay_switches_mains);
    RUN_TEST(presets_bind_the_pins_the_board_offers);
    RUN_TEST(both_v0_revisions_describe_the_same_ports);
    RUN_TEST(the_v0_revisions_differ_only_in_their_bindings);
    RUN_TEST(a_missing_terminal_is_left_unbound);
    RUN_TEST(a_strip_in_sacn_mode_owns_no_ports_but_stays_a_head);
    RUN_TEST(a_port_naming_an_unlisted_head_is_refused);
    return UNITY_END();
}
