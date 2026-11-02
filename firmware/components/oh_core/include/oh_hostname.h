/*
 * The DHCP hostname a friendly name becomes, so the router's client list (and the
 * switch port view that borrows it) shows "coffin-room-dmx" rather than
 * "openhaunt-3f2a1b0c". DHCP option 12 is a DNS label: a-z, 0-9 and '-', so the
 * name is folded into one.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lowercase ASCII letters and digits are kept, German umlauts and ß spelled out
 * (ä → ae), everything else becomes one '-', with none leading or trailing. The
 * result is cut to cap - 1 characters, and never ends in '-' after the cut either.
 * Returns its length: 0 when nothing usable was left, and the caller keeps its
 * own name then. */
size_t oh_hostname_from_name(const char *name, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
