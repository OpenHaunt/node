#include <string.h>

#include "oh_descriptor.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void fill(oh_eeprom_descriptor_t *d)
{
    memset(d, 0, sizeof(*d));
    d->module_type = 0x0003;
    d->hw_revision = 2;
    d->power_ma_12v = 100;
    d->power_ma_5v = 500;
    d->power_ma_3v3 = 50;
    d->capability_flags = 1 << 3;
    memcpy(d->name, "WS2812", 6);
    memcpy(d->vendor, "OpenHaunt", 9);
}

static void a_built_descriptor_parses_back(void)
{
    oh_eeprom_descriptor_t in, out;
    uint8_t raw[OH_EEPROM_SIZE] = {0};
    fill(&in);
    oh_descriptor_build(&in, raw);
    TEST_ASSERT_EQUAL(OH_DESC_OK, oh_descriptor_parse(raw, sizeof(raw), &out));
    TEST_ASSERT_EQUAL_HEX16(0x0003, out.module_type);
    TEST_ASSERT_EQUAL(2, out.hw_revision);
    TEST_ASSERT_EQUAL(500, out.power_ma_5v);
    TEST_ASSERT_EQUAL_HEX16(1 << 3, out.capability_flags);
    char name[17];
    oh_descriptor_field(out.name, 16, name, sizeof(name));
    TEST_ASSERT_EQUAL_STRING("WS2812", name);
    TEST_ASSERT_EQUAL_UINT32(12 * 100 + 5 * 500 + 165, oh_descriptor_power_mw(&out));
}

static void a_tampered_byte_is_a_bad_crc(void)
{
    oh_eeprom_descriptor_t in, out;
    uint8_t raw[OH_DESC_SIZE];
    fill(&in);
    oh_descriptor_build(&in, raw);
    raw[0x08] ^= 0x01;
    TEST_ASSERT_EQUAL(OH_DESC_BAD_CRC, oh_descriptor_parse(raw, sizeof(raw), &out));
    /* The fields are still reported so a tool can show them. */
    TEST_ASSERT_EQUAL_HEX16(0x0003, out.module_type);
}

static void a_blank_eeprom_is_no_module(void)
{
    uint8_t raw[OH_DESC_SIZE];
    oh_eeprom_descriptor_t out;
    memset(raw, 0xFF, sizeof(raw));
    TEST_ASSERT_EQUAL(OH_DESC_BAD_MAGIC, oh_descriptor_parse(raw, sizeof(raw), &out));
    memset(raw, 0x00, sizeof(raw));
    TEST_ASSERT_EQUAL(OH_DESC_BAD_MAGIC, oh_descriptor_parse(raw, sizeof(raw), &out));
    TEST_ASSERT_EQUAL(OH_DESC_BAD_MAGIC, oh_descriptor_parse(raw, 10, &out));
}

static void the_crc_is_ccitt_false(void)
{
    /* The well-known check value: "123456789" -> 0x29B1. */
    TEST_ASSERT_EQUAL_HEX16(0x29B1, oh_crc16((const uint8_t *)"123456789", 9));
}

static void a_field_that_uses_all_16_bytes_is_terminated(void)
{
    char field[16];
    memset(field, 'x', 16);
    char out[17];
    oh_descriptor_field(field, 16, out, sizeof(out));
    TEST_ASSERT_EQUAL(16, strlen(out));
    char small[5];
    oh_descriptor_field(field, 16, small, sizeof(small));
    TEST_ASSERT_EQUAL_STRING("xxxx", small);
}

static void the_uid_is_lower_case_hex_or_absent(void)
{
    uint8_t raw[OH_EEPROM_SIZE];
    char hex[17];
    memset(raw, 0xFF, sizeof(raw));
    TEST_ASSERT_FALSE(oh_descriptor_uid_hex(raw, sizeof(raw), hex, sizeof(hex)));
    const uint8_t uid[8] = {0x29, 0xAB, 0x00, 0x01, 0x9f, 0x8e, 0x7d, 0x6c};
    memcpy(raw + OH_DESC_UID_OFFSET, uid, 8);
    TEST_ASSERT_TRUE(oh_descriptor_uid_hex(raw, sizeof(raw), hex, sizeof(hex)));
    TEST_ASSERT_EQUAL_STRING("29ab00019f8e7d6c", hex);
    TEST_ASSERT_FALSE(oh_descriptor_uid_hex(raw, 100, hex, sizeof(hex)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(a_built_descriptor_parses_back);
    RUN_TEST(a_tampered_byte_is_a_bad_crc);
    RUN_TEST(a_blank_eeprom_is_no_module);
    RUN_TEST(the_crc_is_ccitt_false);
    RUN_TEST(a_field_that_uses_all_16_bytes_is_terminated);
    RUN_TEST(the_uid_is_lower_case_hex_or_absent);
    return UNITY_END();
}
