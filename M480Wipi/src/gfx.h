#ifndef GFX_H
#define GFX_H
#include "types.h"

typedef struct Surf {
    int w, h;
    u16 *px;        /* RGB565 */
    u8 *a;          /* alpha plane or NULL (opaque) */
    int ext;        /* pixels not owned */
} Surf;

#define RGB565(r, g, b) ((u16)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define C565(rgb) RGB565(((rgb) >> 16) & 255, ((rgb) >> 8) & 255, (rgb) & 255)
static inline u32 c888(u16 c)
{
    u32 r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

/* surface handles (Java side stores ints) */
int surf_new(int w, int h, int alpha);
int surf_add(Surf *s);
Surf *surf_get(int h);
void surf_free(int h);
Surf *surf_alloc(int w, int h, int alpha);
void surf_destroy(Surf *s);
void surf_ensure_alpha(Surf *s);

typedef struct GC {
    Surf *s;
    int cx0, cy0, cx1, cy1;   /* clip, exclusive max, absolute surface coords */
    u32 rgb;
    u16 c;
    int alpha;                /* 0..255 global alpha */
    int xor;
} GC;

void g_fill(GC *g, int x, int y, int w, int h);
void g_pixel(GC *g, int x, int y);
void g_line(GC *g, int x0, int y0, int x1, int y1);
void g_rect(GC *g, int x, int y, int w, int h);
void g_round(GC *g, int x, int y, int w, int h, int aw, int ah, int fill);
void g_arc(GC *g, int x, int y, int w, int h, int start, int arc, int fill);
void g_poly(GC *g, const int *xs, const int *ys, int n, int fill);
void g_blit(GC *g, Surf *src, int sx, int sy, int w, int h, int dx, int dy, int transform);
void g_rgb(GC *g, const u32 *argb, int off, int scan, int x, int y, int w, int h, int use_alpha);
void g_copy(GC *g, int sx, int sy, int w, int h, int dx, int dy);
void g_text(GC *g, const u16 *s, int n, int x, int ytop, int fid);
int g_text_width(const u16 *s, int n, int fid);
void g_scale_to(Surf *dst, int dx, int dy, int dw, int dh, Surf *src);

/* fonts: fid = (pixel size << 4) | style bits (1 bold, 2 italic, 4 underline) */
int font_height(int fid);
int font_ascent(int fid);
int font_char_w(int fid, u16 ch);
/* alpha glyph bitmap; returns NULL for blank */
const u8 *font_glyph(int fid, u16 ch, int *gw, int *gh, int *adv);

/* image decoding */
Surf *img_decode(const u8 *d, u32 len);
Surf *img_png(const u8 *d, u32 len);
Surf *img_bmp(const u8 *d, u32 len);
Surf *img_gif(const u8 *d, u32 len);
Surf *img_jpeg(const u8 *d, u32 len);
#endif
