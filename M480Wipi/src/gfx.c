/* software rasterizer on RGB565 surfaces */
#include "gfx.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------------- surfaces ---------------- */
#define MAXSURF 4096
static Surf *surfs[MAXSURF];

Surf *surf_alloc(int w, int h, int alpha)
{
    Surf *s;
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;
    if (w > 4096 || h > 4096) return NULL;
    s = (Surf *)calloc(1, sizeof(Surf));
    if (!s) return NULL;
    s->w = w; s->h = h;
    s->px = (u16 *)malloc((size_t)w * h * 2);
    if (!s->px) { free(s); return NULL; }
    if (alpha) {
        s->a = (u8 *)calloc((size_t)w * h, 1);
        if (!s->a) { free(s->px); free(s); return NULL; }
    }
    return s;
}

void surf_destroy(Surf *s)
{
    if (!s) return;
    if (!s->ext) free(s->px);
    free(s->a);
    free(s);
}

void surf_ensure_alpha(Surf *s)
{
    if (!s->a) { s->a = (u8 *)malloc((size_t)s->w * s->h); if (s->a) memset(s->a, 255, (size_t)s->w * s->h); }
}

int surf_add(Surf *s)
{
    int i;
    if (!s) return 0;
    for (i = 2; i < MAXSURF; i++) if (!surfs[i]) { surfs[i] = s; return i; }
    surf_destroy(s);
    return 0;
}

int surf_new(int w, int h, int alpha) { return surf_add(surf_alloc(w, h, alpha)); }
Surf *surf_get(int h) { return (h > 0 && h < MAXSURF) ? surfs[h] : NULL; }
void surf_free(int h) { if (h > 1 && h < MAXSURF && surfs[h]) { surf_destroy(surfs[h]); surfs[h] = NULL; } }
void surf_set_screen(Surf *s) { surfs[1] = s; }

/* ---------------- pixel helpers ---------------- */
static inline u16 blend(u16 d, u16 s, int a)
{
    /* a: 0..256 */
    u32 dd = (d | ((u32)d << 16)) & 0x07E0F81F;
    u32 ss = (s | ((u32)s << 16)) & 0x07E0F81F;
    u32 r = ((dd << 5) + (ss - dd) * (u32)(a >> 3)) >> 5;
    r &= 0x07E0F81F;
    return (u16)(r | (r >> 16));
}

static inline void put(GC *g, u16 *p, u8 *ap)
{
    if (g->xor) *p ^= g->c;
    else if (g->alpha >= 255) *p = g->c;
    else *p = blend(*p, g->c, g->alpha + 1);
    if (ap) *ap = 255;
}

static void span(GC *g, int y, int x0, int x1)
{
    Surf *s = g->s;
    u16 *p;
    u8 *ap;
    int n;
    if (y < g->cy0 || y >= g->cy1) return;
    if (x0 < g->cx0) x0 = g->cx0;
    if (x1 > g->cx1) x1 = g->cx1;
    if (x0 >= x1) return;
    p = s->px + (size_t)y * s->w + x0;
    ap = s->a ? s->a + (size_t)y * s->w + x0 : NULL;
    n = x1 - x0;
    if (!g->xor && g->alpha >= 255) {
        u16 c = g->c;
        while (n--) *p++ = c;
        if (ap) memset(ap, 255, x1 - x0);
    } else {
        while (n--) { put(g, p++, ap); if (ap) ap++; }
    }
}

void g_pixel(GC *g, int x, int y)
{
    if (x < g->cx0 || x >= g->cx1 || y < g->cy0 || y >= g->cy1) return;
    put(g, g->s->px + (size_t)y * g->s->w + x, g->s->a ? g->s->a + (size_t)y * g->s->w + x : NULL);
}

void g_fill(GC *g, int x, int y, int w, int h)
{
    int y1 = y + h, yy;
    if (w <= 0 || h <= 0) return;
    if (y < g->cy0) y = g->cy0;
    if (y1 > g->cy1) y1 = g->cy1;
    for (yy = y; yy < y1; yy++) span(g, yy, x, x + w);
}

void g_line(GC *g, int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    if (y0 == y1) { if (x0 > x1) { int t = x0; x0 = x1; x1 = t; } span(g, y0, x0, x1 + 1); return; }
    for (;;) {
        g_pixel(g, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

void g_rect(GC *g, int x, int y, int w, int h)
{
    if (w < 0 || h < 0) return;
    if (w == 0 || h == 0) { g_line(g, x, y, x + w, y + h); return; }
    span(g, y, x, x + w + 1);
    span(g, y + h, x, x + w + 1);
    g_fill(g, x, y + 1, 1, h - 1);
    g_fill(g, x + w, y + 1, 1, h - 1);
}

/* ellipse helpers using integer midpoint: compute half-width for each row */
static int ell_half(int rx, int ry, int dy)
{
    /* returns floor(rx * sqrt(1 - dy^2/ry^2)) */
    long long num, lo = 0, hi = rx;
    if (ry <= 0) return rx;
    num = (long long)rx * rx * ((long long)ry * ry - (long long)dy * dy);
    if (num <= 0) return 0;
    while (lo < hi) {
        long long mid = (lo + hi + 1) / 2;
        if (mid * mid * ry * ry <= num) lo = mid; else hi = mid - 1;
    }
    return (int)lo;
}

void g_round(GC *g, int x, int y, int w, int h, int aw, int ah, int fill)
{
    int rx, ry, yy;
    if (w < 0 || h < 0) return;
    if (aw > w) aw = w;
    if (ah > h) ah = h;
    rx = aw / 2; ry = ah / 2;
    if (rx <= 0 || ry <= 0) { if (fill) g_fill(g, x, y, w, h); else g_rect(g, x, y, w, h); return; }
    if (fill) {
        for (yy = 0; yy < h; yy++) {
            int inset = 0, dy;
            if (yy < ry) dy = ry - yy; else if (yy >= h - ry) dy = yy - (h - ry) + 1; else dy = 0;
            if (dy) inset = rx - ell_half(rx, ry, dy);
            span(g, y + yy, x + inset, x + w - inset);
        }
    } else {
        int prev = rx;
        span(g, y, x + rx, x + w - rx + 1);
        span(g, y + h, x + rx, x + w - rx + 1);
        g_fill(g, x, y + ry, 1, h - 2 * ry + 1);
        g_fill(g, x + w, y + ry, 1, h - 2 * ry + 1);
        for (yy = 1; yy <= ry; yy++) {
            int hw = ell_half(rx, ry, ry - yy), k;
            int x0 = rx - hw, x1 = rx - prev;
            if (x1 > x0) { int t = x0; x0 = x1; x1 = t; }
            for (k = x1; k <= x0; k++) {
                g_pixel(g, x + k, y + yy); g_pixel(g, x + w - k, y + yy);
                g_pixel(g, x + k, y + h - yy); g_pixel(g, x + w - k, y + h - yy);
            }
            prev = hw;
        }
    }
}

/* angle of point relative to ellipse center in degrees (0..359), counter-clockwise, y up */
static int iatan2deg(int y, int x)
{
    /* small-table atan approximation */
    static const u8 at[65] = { 0,1,2,3,4,4,5,6,7,8,9,10,11,11,12,13,14,15,16,17,17,18,19,20,21,21,22,23,24,24,25,26,27,27,28,29,29,30,31,31,32,33,33,34,35,35,36,36,37,37,38,39,39,40,40,41,41,42,42,43,43,44,44,45,45 };
    int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, a;
    if (!ax && !ay) return 0;
    if (ax >= ay) a = at[ay * 64 / ax]; else a = 90 - at[ax * 64 / ay];
    if (x >= 0 && y >= 0) return a;
    if (x < 0 && y >= 0) return 180 - a;
    if (x < 0 && y < 0) return 180 + a;
    return (360 - a) % 360;
}

static int in_arc(int deg, int start, int arc)
{
    int d;
    if (arc >= 360 || arc <= -360) return 1;
    if (arc < 0) { start += arc; arc = -arc; }
    start %= 360; if (start < 0) start += 360;
    d = deg - start; if (d < 0) d += 360;
    return d <= arc;
}

void g_arc(GC *g, int x, int y, int w, int h, int start, int arc, int fill)
{
    int cx2 = 2 * x + w, cy2 = 2 * y + h, yy, xx;
    if (w < 0 || h < 0 || arc == 0) return;
    if (w == 0 || h == 0) { g_line(g, x, y, x + w, y + h); return; }
    if (fill) {
        for (yy = 0; yy <= h; yy++) {
            int py = y + yy, dy2 = 2 * py - cy2;
            int hw = ell_half(w, h, dy2 < 0 ? -dy2 : dy2);
            int x0 = (cx2 - hw + 1) / 2, x1 = (cx2 + hw) / 2;
            if (arc >= 360 || arc <= -360) { span(g, py, x0, x1 + 1); continue; }
            for (xx = x0; xx <= x1; xx++)
                if (in_arc(iatan2deg(-dy2 * w, (2 * xx - cx2) * h), start, arc)) g_pixel(g, xx, py);
        }
    } else {
        int n, i, px = 0, py = 0;
        double a0 = start * 3.14159265358979 / 180.0, da;
        if (arc > 360) arc = 360;
        if (arc < -360) arc = -360;
        n = (arc < 0 ? -arc : arc);
        n = n * (w + h) / 180 + 2;
        if (n > 720) n = 720;
        da = arc * 3.14159265358979 / 180.0 / n;
        for (i = 0; i <= n; i++) {
            double t = a0 + da * i;
            int qx = (int)((cx2 + w * cos(t)) / 2 + 0.5), qy = (int)((cy2 - h * sin(t)) / 2 + 0.5);
            if (i) g_line(g, px, py, qx, qy);
            px = qx; py = qy;
        }
    }
}

void g_poly(GC *g, const int *xs, const int *ys, int n, int fill)
{
    int i, miny, maxy, y;
    if (n <= 0) return;
    if (!fill || n < 3) {
        for (i = 0; i < n; i++) g_line(g, xs[i], ys[i], xs[(i + 1) % n], ys[(i + 1) % n]);
        return;
    }
    miny = maxy = ys[0];
    for (i = 1; i < n; i++) { if (ys[i] < miny) miny = ys[i]; if (ys[i] > maxy) maxy = ys[i]; }
    if (miny < g->cy0) miny = g->cy0;
    if (maxy >= g->cy1) maxy = g->cy1 - 1;
    for (y = miny; y <= maxy; y++) {
        int xi[64], k = 0, a, b;
        for (i = 0; i < n && k < 64; i++) {
            int j = (i + 1) % n;
            int y0 = ys[i], y1 = ys[j];
            if (y0 == y1) continue;
            if ((y >= y0 && y < y1) || (y >= y1 && y < y0)) {
                xi[k++] = xs[i] + (int)((long long)(y - y0) * (xs[j] - xs[i]) / (y1 - y0));
            }
        }
        for (a = 1; a < k; a++) { int v = xi[a]; b = a - 1; while (b >= 0 && xi[b] > v) { xi[b + 1] = xi[b]; b--; } xi[b + 1] = v; }
        for (a = 0; a + 1 < k; a += 2) span(g, y, xi[a], xi[a + 1] + 1);
        if (k == 0) {
            /* horizontal edge rows */
            for (i = 0; i < n; i++) if (ys[i] == y && ys[(i + 1) % n] == y) {
                int x0 = xs[i], x1 = xs[(i + 1) % n];
                if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
                span(g, y, x0, x1 + 1);
            }
        }
    }
}

/* ---------------- blits ---------------- */
void g_blit(GC *g, Surf *src, int sx, int sy, int w, int h, int dx, int dy, int tr)
{
    Surf *d = g->s;
    int dw = (tr & 4) ? h : w, dh = (tr & 4) ? w : h;
    int x0 = dx, y0 = dy, x1 = dx + dw, y1 = dy + dh, u, v;
    int ga = g->alpha >= 255 ? 256 : g->alpha + 1;
    if (!src || w <= 0 || h <= 0) return;
    if (sx < 0 || sy < 0 || sx + w > src->w || sy + h > src->h) return;
    if (x0 < g->cx0) x0 = g->cx0;
    if (y0 < g->cy0) y0 = g->cy0;
    if (x1 > g->cx1) x1 = g->cx1;
    if (y1 > g->cy1) y1 = g->cy1;
    if (x0 >= x1 || y0 >= y1) return;
    if (tr == 0 && !src->a && ga == 256 && src != d) {
        int n = x1 - x0;
        for (v = y0; v < y1; v++) {
            memcpy(d->px + (size_t)v * d->w + x0, src->px + (size_t)(sy + v - dy) * src->w + sx + (x0 - dx), (size_t)n * 2);
            if (d->a) memset(d->a + (size_t)v * d->w + x0, 255, n);
        }
        return;
    }
    for (v = y0; v < y1; v++) {
        u16 *dp = d->px + (size_t)v * d->w + x0;
        u8 *dap = d->a ? d->a + (size_t)v * d->w + x0 : NULL;
        int ly = v - dy;
        for (u = x0; u < x1; u++, dp++) {
            int lx = u - dx, px, py, a;
            size_t si;
            /* map destination-local (lx,ly) to source-local */
            switch (tr) {
            default: px = lx; py = ly; break;                       /* NONE */
            case 2: px = w - 1 - lx; py = ly; break;                /* MIRROR */
            case 1: px = lx; py = h - 1 - ly; break;                /* MIRROR_ROT180 */
            case 3: px = w - 1 - lx; py = h - 1 - ly; break;        /* ROT180 */
            case 5: px = ly; py = h - 1 - lx; break;                /* ROT90 */
            case 6: px = w - 1 - ly; py = lx; break;                /* ROT270 */
            case 4: px = ly; py = lx; break;                        /* MIRROR_ROT270 */
            case 7: px = w - 1 - ly; py = h - 1 - lx; break;        /* MIRROR_ROT90 */
            }
            si = (size_t)(sy + py) * src->w + sx + px;
            a = src->a ? src->a[si] : 255;
            if (!a) { if (dap) dap++; continue; }
            a = (a == 255 ? 256 : a + 1) * ga >> 8;
            if (a >= 256) *dp = src->px[si];
            else *dp = blend(*dp, src->px[si], a);
            if (dap) { int na = *dap + ((255 - *dap) * a >> 8); *dap++ = (u8)(na > 255 ? 255 : na); }
        }
    }
}

void g_rgb(GC *g, const u32 *argb, int off, int scan, int x, int y, int w, int h, int use_alpha)
{
    Surf *d = g->s;
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h, u, v;
    if (x0 < g->cx0) x0 = g->cx0;
    if (y0 < g->cy0) y0 = g->cy0;
    if (x1 > g->cx1) x1 = g->cx1;
    if (y1 > g->cy1) y1 = g->cy1;
    for (v = y0; v < y1; v++) {
        const u32 *sp = argb + off + (v - y) * scan + (x0 - x);
        u16 *dp = d->px + (size_t)v * d->w + x0;
        u8 *dap = d->a ? d->a + (size_t)v * d->w + x0 : NULL;
        for (u = x0; u < x1; u++, sp++, dp++) {
            u32 c = *sp;
            int a = use_alpha ? (int)(c >> 24) : 255;
            u16 c5 = C565(c);
            if (a == 255) *dp = c5;
            else if (a) *dp = blend(*dp, c5, a + 1);
            if (dap) { if (!d->a || a == 255) *dap = 255; else *dap = (u8)(*dap + ((255 - *dap) * a >> 8)); dap++; }
        }
    }
}

void g_copy(GC *g, int sx, int sy, int w, int h, int dx, int dy)
{
    Surf *s = g->s;
    int v;
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > s->w) w = s->w - sx;
    if (sy + h > s->h) h = s->h - sy;
    if (dx < g->cx0) { int k = g->cx0 - dx; w -= k; sx += k; dx += k; }
    if (dy < g->cy0) { int k = g->cy0 - dy; h -= k; sy += k; dy += k; }
    if (dx + w > g->cx1) w = g->cx1 - dx;
    if (dy + h > g->cy1) h = g->cy1 - dy;
    if (w <= 0 || h <= 0) return;
    if (dy <= sy) for (v = 0; v < h; v++) memmove(s->px + (size_t)(dy + v) * s->w + dx, s->px + (size_t)(sy + v) * s->w + sx, (size_t)w * 2);
    else for (v = h - 1; v >= 0; v--) memmove(s->px + (size_t)(dy + v) * s->w + dx, s->px + (size_t)(sy + v) * s->w + sx, (size_t)w * 2);
}

/* nearest-neighbour scale of src into a rectangle of dst (used by the presenter) */
void g_scale_to(Surf *dst, int dx, int dy, int dw, int dh, Surf *src)
{
    int x, y;
    u32 stepx = ((u32)src->w << 16) / (u32)dw, stepy = ((u32)src->h << 16) / (u32)dh, fy = 0;
    for (y = 0; y < dh; y++, fy += stepy) {
        int ty = dy + y;
        u16 *dp, *sp;
        u32 fx = 0;
        if (ty < 0 || ty >= dst->h) continue;
        dp = dst->px + (size_t)ty * dst->w + dx;
        sp = src->px + (size_t)(fy >> 16) * src->w;
        for (x = 0; x < dw; x++, fx += stepx) dp[x] = sp[fx >> 16];
    }
}

/* ---------------- text ---------------- */
int g_text_width(const u16 *s, int n, int fid)
{
    int i, w = 0;
    for (i = 0; i < n; i++) w += font_char_w(fid, s[i]);
    return w;
}

void g_text(GC *g, const u16 *s, int n, int x, int ytop, int fid)
{
    int i;
    int ga = g->alpha >= 255 ? 256 : g->alpha + 1;
    Surf *d = g->s;
    int x_start = x;
    for (i = 0; i < n; i++) {
        int gw, gh, adv, u, v;
        const u8 *bm = font_glyph(fid, s[i], &gw, &gh, &adv);
        if (bm) {
            for (v = 0; v < gh; v++) {
                int py = ytop + v;
                if (py < g->cy0 || py >= g->cy1) continue;
                for (u = 0; u < gw; u++) {
                    int px = x + u, a = bm[v * gw + u];
                    u16 *dp;
                    if (!a || px < g->cx0 || px >= g->cx1) continue;
                    dp = d->px + (size_t)py * d->w + px;
                    a = (a == 255 ? 256 : a + 1) * ga >> 8;
                    if (g->xor) *dp ^= g->c;
                    else *dp = a >= 256 ? g->c : blend(*dp, g->c, a);
                    if (d->a) d->a[(size_t)py * d->w + px] = 255;
                }
            }
        }
        x += adv;
    }
    if (fid & 4) {
        int uy = ytop + font_ascent(fid) + 1;
        span(g, uy, x_start, x);
    }
}
