/*
 * The module ID EEPROM descriptor, as OpenHaunt/node doc 04 lays it out.
 * Little-endian, 0x32 bytes, CRC over 0x00..0x2F; the 24AA02UID's factory
 * serial sits at 0xF8..0xFF.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OH_DESC_MAGIC "OHM1"
#define OH_DESC_VERSION 1
#define OH_DESC_SIZE 0x32
#define OH_DESC_CRC_LEN 0x30
#define OH_DESC_UID_OFFSET 0xF8
#define OH_DESC_UID_LEN 8
#define OH_EEPROM_SIZE 256

typedef struct __attribute__((packed)) {
    char magic[4];
    uint8_t descriptor_version;
    uint16_t module_type;
    uint8_t hw_revision;
    uint16_t power_ma_12v;
    uint16_t power_ma_5v;
    uint16_t power_ma_3v3;
    uint16_t capability_flags;
    char name[16];   /* null-padded, not necessarily terminated */
    char vendor[16]; /* null-padded, not necessarily terminated */
    uint16_t crc16;
} oh_eeprom_descriptor_t;

typedef enum {
    OH_DESC_OK = 0,
    OH_DESC_BAD_MAGIC,
    OH_DESC_BAD_VERSION,
    OH_DESC_BAD_CRC,
} oh_desc_result_t;

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout.
 * Doc 04 does not name the variant; this one is what `oh eeprom write`
 * produces, so the two agree by construction. */
uint16_t oh_crc16(const uint8_t *data, size_t len);

/* Parse the first 0x32 bytes of an EEPROM image. `out` is filled even on
 * failure, so a tool can show what it found. */
oh_desc_result_t oh_descriptor_parse(const uint8_t *raw, size_t len, oh_eeprom_descriptor_t *out);

/* Build a descriptor image (with CRC) into `raw` (>= OH_DESC_SIZE bytes). */
void oh_descriptor_build(const oh_eeprom_descriptor_t *in, uint8_t *raw);

const char *oh_descriptor_result_str(oh_desc_result_t r);

/* Declared draw in milliwatts: 12*mA12 + 5*mA5 + 3.3*mA3v3. */
uint32_t oh_descriptor_power_mw(const oh_eeprom_descriptor_t *d);

/* Copy a null-padded 16-byte field into a terminated string. */
void oh_descriptor_field(const char *field, size_t field_len, char *out, size_t out_len);

/* The factory UID from a full 256-byte image as lower-case hex.
 * `out` needs 2*OH_DESC_UID_LEN + 1 bytes. Returns false when the image is
 * too short or the UID is blank (0x00 or 0xFF throughout). */
bool oh_descriptor_uid_hex(const uint8_t *raw, size_t len, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
