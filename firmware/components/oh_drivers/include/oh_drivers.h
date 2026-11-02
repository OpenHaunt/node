/* Register every built-in port driver with oh_node. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

void oh_drivers_register_all(void);

/* What the strip on `gpio` is doing, for `GET /api/v1/pixels` (doc 14). False
 * when no strip is bound to that pin. */
typedef struct {
    uint32_t frames;  /* refreshes since it was bound */
    float fps;        /* refreshes in the last second */
    int sources;      /* sACN sources on its busiest universe; 0 in effect mode */
    bool holding;     /* every source gone, the last frame held or fading */
} oh_ws2812_status_t;

bool oh_ws2812_status(int gpio, oh_ws2812_status_t *out);

#ifdef __cplusplus
}
#endif
