#include <string.h>

#include "oh_lldp.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t MAC[6] = {0x24, 0x0a, 0xc4, 0x3f, 0x2a, 0x1b};

static oh_lldp_self_t self(void)
{
    oh_lldp_self_t s = {
        .system_name = "Coffin Room DMX",
        .system_desc = "OpenHaunt node, DMX512 Out, fw 0.9.0",
        .port_desc = "ethernet",
        .ipv4 = 0,
        .ttl_s = 120,
    };
    memcpy(s.mac, MAC, 6);
    return s;
}

static void our_frame_reads_back(void)
{
    oh_lldp_self_t s = self();
    const uint8_t ip[4] = {192, 168, 1, 42};
    memcpy(&s.ipv4, ip, 4);
    uint8_t f[OH_LLDP_FRAME_MAX];
    size_t n = oh_lldp_build(&s, f, sizeof(f));
    TEST_ASSERT_TRUE(n >= 60);
    TEST_ASSERT_EQUAL_MEMORY(OH_LLDP_MULTICAST, f, 6);
    TEST_ASSERT_EQUAL_MEMORY(MAC, f + 6, 6);
    TEST_ASSERT_EQUAL_HEX8(0x88, f[12]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, f[13]);
    /* Chassis ID TLV: type 1, length 7, subtype MAC. */
    TEST_ASSERT_EQUAL_HEX8(0x02, f[14]);
    TEST_ASSERT_EQUAL_HEX8(0x07, f[15]);
    TEST_ASSERT_EQUAL_HEX8(0x04, f[16]);

    oh_lldp_neighbor_t nb;
    TEST_ASSERT_TRUE(oh_lldp_parse(f, n, &nb));
    TEST_ASSERT_EQUAL_STRING("24:0a:c4:3f:2a:1b", nb.chassis_id);
    TEST_ASSERT_EQUAL_STRING("24:0a:c4:3f:2a:1b", nb.port_id);
    TEST_ASSERT_EQUAL_STRING("Coffin Room DMX", nb.system_name);
    TEST_ASSERT_EQUAL_STRING("OpenHaunt node, DMX512 Out, fw 0.9.0", nb.system_desc);
    TEST_ASSERT_EQUAL_STRING("ethernet", nb.port_desc);
    TEST_ASSERT_EQUAL_STRING("192.168.1.42", nb.mgmt_ipv4);
    TEST_ASSERT_EQUAL_UINT16(120, nb.ttl_s);
}

static void without_an_address_there_is_no_management_tlv(void)
{
    oh_lldp_self_t s = self();
    uint8_t f[OH_LLDP_FRAME_MAX];
    size_t n = oh_lldp_build(&s, f, sizeof(f));
    oh_lldp_neighbor_t nb;
    TEST_ASSERT_TRUE(oh_lldp_parse(f, n, &nb));
    TEST_ASSERT_EQUAL_STRING("", nb.mgmt_ipv4);
}

static void a_small_buffer_builds_nothing(void)
{
    oh_lldp_self_t s = self();
    uint8_t f[40];
    TEST_ASSERT_EQUAL_size_t(0, oh_lldp_build(&s, f, sizeof(f)));
}

static void a_long_name_is_cut_on_a_character(void)
{
    char name[300];
    /* ü (two bytes) repeated: byte 255 is the second half of one. */
    size_t i = 0;
    while (i + 2 < sizeof(name)) {
        name[i++] = (char)0xC3;
        name[i++] = (char)0xBC;
    }
    name[i] = '\0';
    oh_lldp_self_t s = self();
    s.system_name = name;
    uint8_t f[OH_LLDP_FRAME_MAX];
    size_t n = oh_lldp_build(&s, f, sizeof(f));
    TEST_ASSERT_TRUE(n > 0);
    /* Port description TLV comes first: 2 + 8 bytes after chassis, port, TTL. */
    size_t off = 14 + 9 + 9 + 4 + 10;
    TEST_ASSERT_EQUAL_HEX8(5 << 1, f[off] & 0xFE);
    const size_t len = (size_t)(f[off] & 1) << 8 | f[off + 1];
    TEST_ASSERT_EQUAL_size_t(254, len);
}

/* What a UniFi switch sends, in shape: chassis by MAC, port by interface name,
 * a name, a description, an IPv4 management address, then LLDP-MED. */
static size_t switch_frame(uint8_t *f)
{
    static const uint8_t bytes[] = {
        0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e, 0x74, 0xac, 0xb9, 0x01, 0x02, 0x03, 0x88, 0xcc,
        /* chassis: MAC */
        0x02, 0x07, 0x04, 0x74, 0xac, 0xb9, 0x01, 0x02, 0x03,
        /* port: interface name "Port 7" */
        0x04, 0x07, 0x05, 'P', 'o', 'r', 't', ' ', '7',
        /* TTL 120 */
        0x06, 0x02, 0x00, 0x78,
        /* port description "Port 7" */
        0x08, 0x06, 'P', 'o', 'r', 't', ' ', '7',
        /* system name "usw-24" */
        0x0a, 0x06, 'u', 's', 'w', '-', '2', '4',
        /* system description "USW-24, 7.1.26" */
        0x0c, 0x0e, 'U', 'S', 'W', '-', '2', '4', ',', ' ', '7', '.', '1', '.', '2', '6',
        /* management address 192.168.1.2 */
        0x10, 0x0c, 0x05, 0x01, 192, 168, 1, 2, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00,
        /* LLDP-MED capabilities */
        0xfe, 0x07, 0x00, 0x12, 0xbb, 0x01, 0x00, 0x0f, 0x04,
        /* end */
        0x00, 0x00,
    };
    memcpy(f, bytes, sizeof(bytes));
    return sizeof(bytes);
}

static void a_switch_frame_names_the_switch_and_the_port(void)
{
    uint8_t f[128];
    size_t n = switch_frame(f);
    oh_lldp_neighbor_t nb;
    TEST_ASSERT_TRUE(oh_lldp_parse(f, n, &nb));
    TEST_ASSERT_EQUAL_STRING("74:ac:b9:01:02:03", nb.chassis_id);
    TEST_ASSERT_EQUAL_STRING("Port 7", nb.port_id);
    TEST_ASSERT_EQUAL_STRING("Port 7", nb.port_desc);
    TEST_ASSERT_EQUAL_STRING("usw-24", nb.system_name);
    TEST_ASSERT_EQUAL_STRING("USW-24, 7.1.26", nb.system_desc);
    TEST_ASSERT_EQUAL_STRING("192.168.1.2", nb.mgmt_ipv4);
    TEST_ASSERT_EQUAL_UINT16(120, nb.ttl_s);
}

static void a_tagged_frame_parses(void)
{
    uint8_t plain[128], f[132];
    size_t n = switch_frame(plain);
    memcpy(f, plain, 12);
    const uint8_t tag[4] = {0x81, 0x00, 0x00, 0x0a};
    memcpy(f + 12, tag, 4);
    memcpy(f + 16, plain + 12, n - 12);
    oh_lldp_neighbor_t nb;
    TEST_ASSERT_TRUE(oh_lldp_parse(f, n + 4, &nb));
    TEST_ASSERT_EQUAL_STRING("usw-24", nb.system_name);
}

static void malformed_frames_are_refused(void)
{
    uint8_t f[128];
    oh_lldp_neighbor_t nb;
    size_t n = switch_frame(f);

    /* Not LLDP. */
    f[13] = 0x00;
    TEST_ASSERT_FALSE(oh_lldp_parse(f, n, &nb));
    f[13] = 0xcc;
    /* A TLV running past the end. */
    TEST_ASSERT_FALSE(oh_lldp_parse(f, 20, &nb));
    /* Port ID before chassis ID. */
    n = switch_frame(f);
    f[14] = 0x04;
    TEST_ASSERT_FALSE(oh_lldp_parse(f, n, &nb));
    /* Too short for a header. */
    TEST_ASSERT_FALSE(oh_lldp_parse(f, 10, &nb));
}

static void control_bytes_and_broken_utf8_are_dropped(void)
{
    uint8_t f[128];
    size_t n = switch_frame(f);
    /* system name "usw-24" → "us\x01-\xff4" */
    size_t off = 14 + 9 + 9 + 4 + 8 + 2;
    TEST_ASSERT_EQUAL_UINT8('u', f[off]);
    f[off + 2] = 0x01;
    f[off + 4] = 0xff;
    oh_lldp_neighbor_t nb;
    TEST_ASSERT_TRUE(oh_lldp_parse(f, n, &nb));
    TEST_ASSERT_EQUAL_STRING("us-4", nb.system_name);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(our_frame_reads_back);
    RUN_TEST(without_an_address_there_is_no_management_tlv);
    RUN_TEST(a_small_buffer_builds_nothing);
    RUN_TEST(a_long_name_is_cut_on_a_character);
    RUN_TEST(a_switch_frame_names_the_switch_and_the_port);
    RUN_TEST(a_tagged_frame_parses);
    RUN_TEST(malformed_frames_are_refused);
    RUN_TEST(control_bytes_and_broken_utf8_are_dropped);
    return UNITY_END();
}
