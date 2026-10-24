#include "oh_descriptor.h"

#include <stdio.h>
#include <string.h>

uint16_t oh_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

oh_desc_result_t oh_descriptor_parse(const uint8_t *raw, size_t len, oh_eeprom_descriptor_t *out)
{
    memset(out, 0, sizeof(*out));
    if (len < OH_DESC_SIZE) {
        return OH_DESC_BAD_MAGIC;
    }
    memcpy(out->magic, raw, 4);
    out->descriptor_version = raw[0x04];
    out->module_type = rd16(raw + 0x05);
    out->hw_revision = raw[0x07];
    out->power_ma_12v = rd16(raw + 0x08);
    out->power_ma_5v = rd16(raw + 0x0A);
    out->power_ma_3v3 = rd16(raw + 0x0C);
    out->capability_flags = rd16(raw + 0x0E);
    memcpy(out->name, raw + 0x10, 16);
    memcpy(out->vendor, raw + 0x20, 16);
    out->crc16 = rd16(raw + 0x30);

    if (memcmp(raw, OH_DESC_MAGIC, 4) != 0) {
        return OH_DESC_BAD_MAGIC;
    }
    if (out->descriptor_version != OH_DESC_VERSION) {
        return OH_DESC_BAD_VERSION;
    }
    if (oh_crc16(raw, OH_DESC_CRC_LEN) != out->crc16) {
        return OH_DESC_BAD_CRC;
    }
    return OH_DESC_OK;
}

void oh_descriptor_build(const oh_eeprom_descriptor_t *in, uint8_t *raw)
{
    memset(raw, 0, OH_DESC_SIZE);
    memcpy(raw, OH_DESC_MAGIC, 4);
    raw[0x04] = OH_DESC_VERSION;
    wr16(raw + 0x05, in->module_type);
    raw[0x07] = in->hw_revision;
    wr16(raw + 0x08, in->power_ma_12v);
    wr16(raw + 0x0A, in->power_ma_5v);
    wr16(raw + 0x0C, in->power_ma_3v3);
    wr16(raw + 0x0E, in->capability_flags);
    memcpy(raw + 0x10, in->name, 16);
    memcpy(raw + 0x20, in->vendor, 16);
    wr16(raw + 0x30, oh_crc16(raw, OH_DESC_CRC_LEN));
}

const char *oh_descriptor_result_str(oh_desc_result_t r)
{
    switch (r) {
    case OH_DESC_OK:
        return "ok";
    case OH_DESC_BAD_MAGIC:
        return "bad magic";
    case OH_DESC_BAD_VERSION:
        return "unsupported descriptor version";
    case OH_DESC_BAD_CRC:
        return "bad crc";
    }
    return "?";
}

uint32_t oh_descriptor_power_mw(const oh_eeprom_descriptor_t *d)
{
    return 12u * d->power_ma_12v + 5u * d->power_ma_5v + (33u * d->power_ma_3v3) / 10u;
}

void oh_descriptor_field(const char *field, size_t field_len, char *out, size_t out_len)
{
    size_t n = 0;
    while (n < field_len && field[n] != '\0') {
        n++;
    }
    if (n >= out_len) {
        n = out_len - 1;
    }
    memcpy(out, field, n);
    out[n] = '\0';
}

bool oh_descriptor_uid_hex(const uint8_t *raw, size_t len, char *out, size_t out_len)
{
    if (len < OH_DESC_UID_OFFSET + OH_DESC_UID_LEN || out_len < 2 * OH_DESC_UID_LEN + 1) {
        return false;
    }
    const uint8_t *uid = raw + OH_DESC_UID_OFFSET;
    bool all0 = true, allf = true;
    for (int i = 0; i < OH_DESC_UID_LEN; i++) {
        all0 &= uid[i] == 0x00;
        allf &= uid[i] == 0xFF;
    }
    if (all0 || allf) {
        return false;
    }
    for (int i = 0; i < OH_DESC_UID_LEN; i++) {
        snprintf(out + 2 * i, 3, "%02x", uid[i]);
    }
    return true;
}
