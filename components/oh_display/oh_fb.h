/* A tiny RGB565 framebuffer with the 8x16 font, shared by the ST7789 status
 * display and (as 1 bpp) the SSD1306 driver. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t *px; /* RGB565, byte-swapped for the panel */
    int w, h;
} oh_fb_t;

static inline uint16_t oh_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c << 8) | (c >> 8)); /* big-endian on the wire */
}

void oh_fb_fill(oh_fb_t *fb, int x, int y, int w, int h, uint16_t colour);
/* Draw `text` at (x, y) with the 8x16 font scaled by `scale`; returns the
 * x after the last glyph. Clips at the framebuffer edge. */
int oh_fb_text(oh_fb_t *fb, int x, int y, int scale, uint16_t fg, uint16_t bg, const char *text);
