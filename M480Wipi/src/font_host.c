/* PC test font: hollow boxes sized like a phone font (glyph shapes are not needed for tests) */
#include "gfx.h"
#include <string.h>
static u8 glyph[32 * 32];
static int px(int fid) { int p = fid >> 4; return p < 8 ? 8 : p > 32 ? 32 : p; }
int font_height(int fid) { return px(fid) + 2; }
int font_ascent(int fid) { return px(fid); }
int font_char_w(int fid, u16 ch) { int p = px(fid); return ch < 0x80 ? p / 2 + 1 : p + 1; }
const u8 *font_glyph(int fid, u16 ch, int *gw, int *gh, int *adv)
{
    int p = px(fid), w = font_char_w(fid, ch) - 1, h = p, x, y;
    *adv = w + 1;
    if (ch == ' ') return NULL;
    memset(glyph, 0, sizeof glyph);
    for (y = 2; y < h; y++) for (x = 0; x < w; x++) if (y == 2 || y == h - 1 || x == 0 || x == w - 1) glyph[y * w + x] = 255;
    *gw = w; *gh = h;
    return glyph;
}
