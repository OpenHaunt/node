#include "oh_hostname.h"

#include <stdbool.h>
#include <string.h>

/* The two-byte UTF-8 spellings worth keeping, after 0xC3. */
static const char *umlaut(unsigned char second)
{
    switch (second) {
    case 0xA4: /* ä */
    case 0x84: /* Ä */
        return "ae";
    case 0xB6: /* ö */
    case 0x96: /* Ö */
        return "oe";
    case 0xBC: /* ü */
    case 0x9C: /* Ü */
        return "ue";
    case 0x9F: /* ß */
        return "ss";
    default:
        return NULL;
    }
}

size_t oh_hostname_from_name(const char *name, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    size_t n = 0;
    bool dash = false; /* a separator is owed before the next character */
    const unsigned char *p = (const unsigned char *)(name ? name : "");
    while (*p && n + 1 < cap) {
        char buf[3] = {0};
        const char *emit = NULL;
        if ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')) {
            buf[0] = (char)*p;
            emit = buf;
            p++;
        } else if (*p >= 'A' && *p <= 'Z') {
            buf[0] = (char)(*p - 'A' + 'a');
            emit = buf;
            p++;
        } else if (*p == 0xC3 && p[1] && umlaut(p[1])) {
            emit = umlaut(p[1]);
            p += 2;
        } else {
            /* Skip a whole UTF-8 sequence, not just its lead byte. */
            p++;
            while ((*p & 0xC0) == 0x80) {
                p++;
            }
            dash = n > 0;
            continue;
        }
        if (dash) {
            if (n + 2 >= cap) {
                break;
            }
            out[n++] = '-';
            dash = false;
        }
        for (const char *e = emit; *e && n + 1 < cap; e++) {
            out[n++] = *e;
        }
    }
    while (n > 0 && out[n - 1] == '-') {
        n--;
    }
    out[n] = '\0';
    return n;
}
