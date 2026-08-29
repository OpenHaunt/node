#include "oh_fb.h"

#include "oh_font.h"

void oh_fb_fill(oh_fb_t *fb, int x, int y, int w, int h, uint16_t colour)
{
    for (int yy = y; yy < y + h && yy < fb->h; yy++) {
        if (yy < 0) {
            continue;
        }
        uint16_t *row = fb->px + yy * fb->w;
        for (int xx = x; xx < x + w && xx < fb->w; xx++) {
            if (xx >= 0) {
                row[xx] = colour;
            }
        }
    }
}

int oh_fb_text(oh_fb_t *fb, int x, int y, int scale, uint16_t fg, uint16_t bg, const char *text)
{
    for (const char *c = text; *c; c++) {
        unsigned ch = (unsigned char)*c;
        const uint8_t *glyph = OH_FONT[(ch >= 0x20 && ch < 0x7F) ? ch - 0x20 : '?' - 0x20];
        for (int gy = 0; gy < OH_FONT_H; gy++) {
            uint8_t bits = glyph[gy];
            for (int gx = 0; gx < OH_FONT_W; gx++) {
                uint16_t colour = (bits & (0x80 >> gx)) ? fg : bg;
                oh_fb_fill(fb, x + gx * scale, y + gy * scale, scale, scale, colour);
            }
        }
        x += OH_FONT_W * scale;
        if (x >= fb->w) {
            break;
        }
    }
    return x;
}
