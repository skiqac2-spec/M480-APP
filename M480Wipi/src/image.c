/* PNG / BMP / GIF / baseline JPEG decoders -> RGB565 (+alpha) surfaces */
#include "gfx.h"
#include "zip.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static u32 le32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
static u16 le16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }

static void put_px(Surf *s, int x, int y, u32 r, u32 g, u32 b, u32 a)
{
    size_t i = (size_t)y * s->w + x;
    s->px[i] = RGB565(r, g, b);
    if (s->a) s->a[i] = (u8)a;
}

/* ---------------- PNG ---------------- */
static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (pa <= pb && pa <= pc) ? a : pb <= pc ? b : c;
}

static int unfilter(u8 *d, int rows, int stride, int bpp)
{
    int y, x;
    u8 *prev = NULL;
    for (y = 0; y < rows; y++) {
        u8 *row = d + (size_t)y * (stride + 1);
        int ft = row[0];
        u8 *p = row + 1;
        for (x = 0; x < stride; x++) {
            int a = x >= bpp ? p[x - bpp] : 0, b = prev ? prev[x] : 0, c = (prev && x >= bpp) ? prev[x - bpp] : 0;
            switch (ft) {
            case 0: break;
            case 1: p[x] = (u8)(p[x] + a); break;
            case 2: p[x] = (u8)(p[x] + b); break;
            case 3: p[x] = (u8)(p[x] + ((a + b) >> 1)); break;
            case 4: p[x] = (u8)(p[x] + paeth(a, b, c)); break;
            default: return 0;
            }
        }
        prev = p;
    }
    return 1;
}

Surf *img_png(const u8 *d, u32 len)
{
    const u8 *p = d + 8, *end = d + len;
    int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0, i;
    u8 pal[256 * 4];
    int npal = 0, has_trns = 0;
    u16 trns_g = 0xFFFF, trns_r = 0xFFFF, trns_gg = 0xFFFF, trns_b = 0xFFFF;
    u8 *idat = NULL, *raw;
    size_t idlen = 0, rawlen;
    Surf *s;
    int chans, bpp, alpha;
    for (i = 0; i < 256; i++) { pal[i * 4] = pal[i * 4 + 1] = pal[i * 4 + 2] = 0; pal[i * 4 + 3] = 255; }
    while (p + 12 <= end) {
        u32 cl = be32(p);
        const u8 *ct = p + 4, *cd = p + 8;
        if (cd + cl > end) break;
        if (!memcmp(ct, "IHDR", 4)) {
            w = (int)be32(cd); h = (int)be32(cd + 4); depth = cd[8]; ctype = cd[9]; interlace = cd[12];
        } else if (!memcmp(ct, "PLTE", 4)) {
            npal = (int)cl / 3;
            for (i = 0; i < npal && i < 256; i++) { pal[i * 4] = cd[i * 3]; pal[i * 4 + 1] = cd[i * 3 + 1]; pal[i * 4 + 2] = cd[i * 3 + 2]; }
        } else if (!memcmp(ct, "tRNS", 4)) {
            has_trns = 1;
            if (ctype == 3) for (i = 0; i < (int)cl && i < 256; i++) pal[i * 4 + 3] = cd[i];
            else if (ctype == 0 && cl >= 2) trns_g = (u16)((cd[0] << 8) | cd[1]);
            else if (ctype == 2 && cl >= 6) { trns_r = (u16)((cd[0] << 8) | cd[1]); trns_gg = (u16)((cd[2] << 8) | cd[3]); trns_b = (u16)((cd[4] << 8) | cd[5]); }
        } else if (!memcmp(ct, "IDAT", 4)) {
            u8 *n = (u8 *)realloc(idat, idlen + cl);
            if (!n) { free(idat); return NULL; }
            idat = n;
            memcpy(idat + idlen, cd, cl);
            idlen += cl;
        } else if (!memcmp(ct, "IEND", 4)) break;
        p = cd + cl + 4;
    }
    if (!idat || w <= 0 || h <= 0) { free(idat); return NULL; }
    raw = zlib_alloc(idat, idlen, &rawlen);
    free(idat);
    if (!raw) return NULL;
    chans = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : 4;
    bpp = (chans * depth + 7) / 8;
    alpha = ctype == 4 || ctype == 6 || has_trns;
    s = surf_alloc(w, h, alpha);
    if (!s) { free(raw); return NULL; }
    {
        static const int sx0[7] = { 0, 4, 0, 2, 0, 1, 0 }, sy0[7] = { 0, 0, 4, 0, 2, 0, 1 };
        static const int dx[7] = { 8, 8, 4, 4, 2, 2, 1 }, dy[7] = { 8, 8, 8, 4, 4, 2, 2 };
        int pass, np = interlace ? 7 : 1;
        u8 *src = raw;
        for (pass = 0; pass < np; pass++) {
            int x0 = interlace ? sx0[pass] : 0, y0 = interlace ? sy0[pass] : 0;
            int stx = interlace ? dx[pass] : 1, sty = interlace ? dy[pass] : 1;
            int pw = (w - x0 + stx - 1) / stx, ph = (h - y0 + sty - 1) / sty;
            int stride, y;
            if (pw <= 0 || ph <= 0) continue;
            stride = (pw * chans * depth + 7) / 8;
            if ((size_t)(src - raw) + (size_t)ph * (stride + 1) > rawlen) break;
            if (!unfilter(src, ph, stride, bpp)) break;
            for (y = 0; y < ph; y++) {
                const u8 *row = src + (size_t)y * (stride + 1) + 1;
                int x;
                for (x = 0; x < pw; x++) {
                    u32 r, g, b, a = 255;
                    int ox = x0 + x * stx, oy = y0 + y * sty;
                    if (depth < 8) {
                        int bitpos = x * depth;
                        u32 v = (row[bitpos >> 3] >> (8 - depth - (bitpos & 7))) & ((1 << depth) - 1);
                        if (ctype == 3) { r = pal[v * 4]; g = pal[v * 4 + 1]; b = pal[v * 4 + 2]; a = pal[v * 4 + 3]; }
                        else { u32 gv = v * 255 / ((1 << depth) - 1); r = g = b = gv; if (v == trns_g) a = 0; }
                    } else if (depth == 8) {
                        const u8 *q = row + x * chans;
                        switch (ctype) {
                        case 0: r = g = b = q[0]; if (q[0] == trns_g) a = 0; break;
                        case 2: r = q[0]; g = q[1]; b = q[2]; if (q[0] == trns_r && q[1] == trns_gg && q[2] == trns_b) a = 0; break;
                        case 3: r = pal[q[0] * 4]; g = pal[q[0] * 4 + 1]; b = pal[q[0] * 4 + 2]; a = pal[q[0] * 4 + 3]; break;
                        case 4: r = g = b = q[0]; a = q[1]; break;
                        default: r = q[0]; g = q[1]; b = q[2]; a = q[3]; break;
                        }
                    } else {
                        const u8 *q = row + x * chans * 2;
                        switch (ctype) {
                        case 0: r = g = b = q[0]; break;
                        case 2: r = q[0]; g = q[2]; b = q[4]; break;
                        case 4: r = g = b = q[0]; a = q[2]; break;
                        default: r = q[0]; g = q[2]; b = q[4]; a = q[6]; break;
                        }
                    }
                    put_px(s, ox, oy, r, g, b, a);
                }
            }
            src += (size_t)ph * (stride + 1);
        }
    }
    free(raw);
    return s;
}

/* ---------------- BMP ---------------- */
Surf *img_bmp(const u8 *d, u32 len)
{
    u32 off, hs;
    int w, h, bpp, comp, flip = 1, x, y, ncol;
    const u8 *pal;
    Surf *s;
    u32 rm = 0, gm = 0, bm = 0;
    if (len < 54) return NULL;
    off = le32(d + 10);
    hs = le32(d + 14);
    if (hs < 40) return NULL;
    w = (int)le32(d + 18);
    h = (int)le32(d + 22);
    bpp = le16(d + 28);
    comp = (int)le32(d + 30);
    ncol = (int)le32(d + 46);
    if (h < 0) { h = -h; flip = 0; }
    if (w <= 0 || h <= 0 || off >= len) return NULL;
    pal = d + 14 + hs;
    if (!ncol && bpp <= 8) ncol = 1 << bpp;
    if (comp == 3 && len >= 66) { rm = le32(d + 54); gm = le32(d + 58); bm = le32(d + 62); }
    s = surf_alloc(w, h, 0);
    if (!s) return NULL;
    {
        int stride = ((w * bpp + 31) / 32) * 4;
        for (y = 0; y < h; y++) {
            const u8 *row = d + off + (size_t)(flip ? h - 1 - y : y) * stride;
            if (row + stride > d + len) break;
            for (x = 0; x < w; x++) {
                u32 r = 0, g = 0, b = 0;
                if (bpp == 24) { b = row[x * 3]; g = row[x * 3 + 1]; r = row[x * 3 + 2]; }
                else if (bpp == 32) { b = row[x * 4]; g = row[x * 4 + 1]; r = row[x * 4 + 2]; }
                else if (bpp == 16) {
                    u32 v = le16(row + x * 2);
                    if (comp == 3 && gm == 0x7E0) { r = (v >> 11) << 3; g = ((v >> 5) & 63) << 2; b = (v & 31) << 3; }
                    else { r = ((v >> 10) & 31) << 3; g = ((v >> 5) & 31) << 3; b = (v & 31) << 3; }
                } else if (bpp <= 8) {
                    int bit = x * bpp;
                    u32 idx = (row[bit >> 3] >> (8 - bpp - (bit & 7))) & ((1 << bpp) - 1);
                    if ((int)idx < ncol) { b = pal[idx * 4]; g = pal[idx * 4 + 1]; r = pal[idx * 4 + 2]; }
                }
                put_px(s, x, y, r, g, b, 255);
            }
        }
    }
    (void)rm; (void)bm;
    return s;
}

/* ---------------- GIF (first frame) ---------------- */
Surf *img_gif(const u8 *d, u32 len)
{
    const u8 *p = d + 13, *end = d + len;
    u8 gpal[768], lpal[768], *pal;
    int w = le16(d + 6), h = le16(d + 8), flags = d[10], bg = d[11];
    int trans = -1, i;
    Surf *s;
    (void)bg;
    if (len < 13) return NULL;
    memset(gpal, 0, sizeof gpal);
    if (flags & 0x80) {
        int n = 2 << (flags & 7);
        if (p + n * 3 > end) return NULL;
        memcpy(gpal, p, n * 3);
        p += n * 3;
    }
    while (p < end) {
        if (*p == 0x21) {
            if (p + 2 < end && p[1] == 0xF9 && p + 7 < end && (p[3] & 1)) trans = p[6];
            p += 2;
            while (p < end && *p) p += *p + 1;
            p++;
        } else if (*p == 0x2C) {
            int ix, iy, iw, ih, lf, minc, x = 0, y = 0, pass = 0, inter;
            u8 *buf;
            size_t blen = 0, bcap = 4096;
            if (p + 10 > end) return NULL;
            ix = le16(p + 1); iy = le16(p + 3); iw = le16(p + 5); ih = le16(p + 7); lf = p[9];
            p += 10;
            pal = gpal;
            if (lf & 0x80) {
                int n = 2 << (lf & 7);
                if (p + n * 3 > end) return NULL;
                memcpy(lpal, p, n * 3);
                p += n * 3;
                pal = lpal;
            }
            inter = (lf & 0x40) != 0;
            if (p >= end) return NULL;
            minc = *p++;
            buf = (u8 *)malloc(bcap);
            while (p < end && *p) {
                int n = *p++;
                if (p + n > end) break;
                if (blen + n > bcap) { bcap = (blen + n) * 2; buf = (u8 *)realloc(buf, bcap); }
                memcpy(buf + blen, p, n);
                blen += n;
                p += n;
            }
            s = surf_alloc(w, h, trans >= 0);
            if (!s) { free(buf); return NULL; }
            memset(s->px, 0, (size_t)w * h * 2);
            if (s->a) memset(s->a, 0, (size_t)w * h);
            /* LZW */
            {
                static u16 prefix[4096];
                static u8 suffix[4096], stack[4097];
                int clear = 1 << minc, eoi = clear + 1, avail = clear + 2, csize = minc + 1, cmask = (1 << csize) - 1;
                int old = -1, first = 0;
                u32 bits = 0;
                int nb = 0;
                size_t bp = 0;
                for (i = 0; i < clear; i++) { prefix[i] = 0; suffix[i] = (u8)i; }
                for (;;) {
                    int code, in, sp = 0;
                    while (nb < csize && bp < blen) { bits |= (u32)buf[bp++] << nb; nb += 8; }
                    if (nb < csize) break;
                    code = (int)(bits & cmask);
                    bits >>= csize; nb -= csize;
                    if (code == clear) { csize = minc + 1; cmask = (1 << csize) - 1; avail = clear + 2; old = -1; continue; }
                    if (code == eoi) break;
                    if (old == -1) { stack[sp++] = suffix[code]; old = code; first = code; }
                    else {
                        in = code;
                        if (code >= avail) { stack[sp++] = (u8)first; code = old; }
                        while (code >= clear && sp < 4096) { stack[sp++] = suffix[code]; code = prefix[code]; }
                        stack[sp++] = suffix[code];
                        first = suffix[code];
                        if (avail < 4096) { prefix[avail] = (u16)old; suffix[avail] = (u8)first; avail++; if (!(avail & cmask) && avail < 4096) { csize++; cmask = (1 << csize) - 1; } }
                        old = in;
                    }
                    while (sp > 0) {
                        int c = stack[--sp];
                        int px = ix + x, py = iy + y;
                        if (px < w && py < h && c != trans) put_px(s, px, py, pal[c * 3], pal[c * 3 + 1], pal[c * 3 + 2], 255);
                        if (++x >= iw) {
                            x = 0;
                            if (!inter) y++;
                            else {
                                static const int st[4] = { 8, 8, 4, 2 }, of[4] = { 0, 4, 2, 1 };
                                y += st[pass];
                                while (y >= ih && pass < 3) { pass++; y = of[pass]; }
                            }
                            if (y >= ih) break;
                        }
                    }
                    if (y >= ih) break;
                }
            }
            free(buf);
            return s;
        } else break;
    }
    return NULL;
}

/* ---------------- baseline JPEG ---------------- */
typedef struct { u8 bits[17]; u8 val[256]; u16 code[256]; int n; } JHuff;
typedef struct {
    const u8 *p, *end;
    u32 bb; int bn;
    int w, h, ncomp, hmax, vmax, restart;
    struct { int id, h, v, tq, td, ta, dc; u8 *data; int bw, bh; } c[3];
    u16 q[4][64];
    JHuff hd[4], ha[4];
} Jpg;

static const u8 zz[64] = { 0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63 };

static int jbit(Jpg *j)
{
    if (!j->bn) {
        u8 b;
        if (j->p >= j->end) return 0;
        b = *j->p++;
        if (b == 0xFF) { if (j->p < j->end && *j->p == 0) j->p++; else { j->p--; b = 0; } }
        j->bb = b; j->bn = 8;
    }
    j->bn--;
    return (j->bb >> j->bn) & 1;
}
static int jbits(Jpg *j, int n) { int v = 0; while (n--) v = (v << 1) | jbit(j); return v; }
static int jext(int v, int n) { return n && v < (1 << (n - 1)) ? v - (1 << n) + 1 : v; }
static int jdec(Jpg *j, JHuff *h)
{
    int code = 0, l, k = 0;
    for (l = 1; l <= 16; l++) {
        int i;
        code = (code << 1) | jbit(j);
        for (i = 0; i < h->bits[l]; i++, k++) if (h->code[k] == code) return h->val[k];
        if (k > h->n) break;
    }
    return 0;
}

static void jidct(int *in, u8 *out, int stride)
{
    int i, tmp[64];
    static int cosv[64];
    static int ready;
    if (!ready) {
        int u, x;
        for (x = 0; x < 8; x++) for (u = 0; u < 8; u++) {
            double cu = u ? 1.0 : 0.70710678;
            double v = cu * cos((2 * x + 1) * u * 3.14159265358979 / 16);
            cosv[x * 8 + u] = (int)(v * 2048);
        }
        ready = 1;
    }
    for (i = 0; i < 8; i++) {
        int x, u;
        for (x = 0; x < 8; x++) { int s = 0; for (u = 0; u < 8; u++) s += in[i * 8 + u] * cosv[x * 8 + u]; tmp[i * 8 + x] = s >> 8; }
    }
    for (i = 0; i < 8; i++) {
        int y, v;
        for (y = 0; y < 8; y++) {
            int s = 0, r;
            for (v = 0; v < 8; v++) s += tmp[v * 8 + i] * cosv[y * 8 + v];
            r = (s >> 16) + 128;
            out[y * stride + i] = (u8)(r < 0 ? 0 : r > 255 ? 255 : r);
        }
    }
}

Surf *img_jpeg(const u8 *d, u32 len)
{
    Jpg *j = (Jpg *)calloc(1, sizeof(Jpg));
    const u8 *p = d + 2, *end = d + len;
    Surf *s = NULL;
    int i, progressive = 0;
    if (!j) return NULL;
    while (p + 4 <= end) {
        int m, l;
        if (*p != 0xFF) { p++; continue; }
        m = p[1];
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01 || m == 0xFF) { p += (m == 0xFF) ? 1 : 2; continue; }
        l = (p[2] << 8) | p[3];
        if (p + 2 + l > end) break;
        if (m == 0xDB) {
            const u8 *q = p + 4, *qe = p + 2 + l;
            while (q < qe) { int t = *q & 3, prec = *q >> 4, k; q++; for (k = 0; k < 64; k++) { j->q[t][zz[k]] = prec ? (u16)((q[0] << 8) | q[1]) : q[0]; q += prec ? 2 : 1; } }
        } else if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
            progressive = m == 0xC2;
            j->h = (p[5] << 8) | p[6]; j->w = (p[7] << 8) | p[8]; j->ncomp = p[9];
            if (j->ncomp > 3) j->ncomp = 3;
            for (i = 0; i < j->ncomp; i++) {
                j->c[i].id = p[10 + i * 3]; j->c[i].h = p[11 + i * 3] >> 4; j->c[i].v = p[11 + i * 3] & 15; j->c[i].tq = p[12 + i * 3] & 3;
                if (j->c[i].h > j->hmax) j->hmax = j->c[i].h;
                if (j->c[i].v > j->vmax) j->vmax = j->c[i].v;
            }
        } else if (m == 0xC4) {
            const u8 *q = p + 4, *qe = p + 2 + l;
            while (q + 17 <= qe) {
                int tc = q[0] >> 4, th = q[0] & 3, n = 0, k, code = 0, bl;
                JHuff *h = tc ? &j->ha[th] : &j->hd[th];
                for (k = 1; k <= 16; k++) { h->bits[k] = q[k]; n += q[k]; }
                if (n > 256) n = 256;
                memcpy(h->val, q + 17, n);
                h->n = n;
                k = 0;
                for (bl = 1; bl <= 16; bl++) { int c; for (c = 0; c < h->bits[bl] && k < 256; c++) h->code[k++] = (u16)code++; code <<= 1; }
                q += 17 + n;
            }
        } else if (m == 0xDD) {
            j->restart = (p[4] << 8) | p[5];
        } else if (m == 0xDA) {
            int ns = p[4], mcux, mcuy, mx, my, mcus = 0;
            if (progressive || !j->w || !j->h) break;
            for (i = 0; i < ns && i < 3; i++) {
                int k;
                for (k = 0; k < j->ncomp; k++) if (j->c[k].id == p[5 + i * 2]) { j->c[k].td = p[6 + i * 2] >> 4; j->c[k].ta = p[6 + i * 2] & 15; }
            }
            j->p = p + 2 + l; j->end = end;
            mcux = (j->w + 8 * j->hmax - 1) / (8 * j->hmax);
            mcuy = (j->h + 8 * j->vmax - 1) / (8 * j->vmax);
            for (i = 0; i < j->ncomp; i++) {
                j->c[i].bw = mcux * j->c[i].h * 8; j->c[i].bh = mcuy * j->c[i].v * 8;
                j->c[i].data = (u8 *)malloc((size_t)j->c[i].bw * j->c[i].bh);
                if (!j->c[i].data) goto fail;
            }
            for (my = 0; my < mcuy; my++) for (mx = 0; mx < mcux; mx++) {
                int ci;
                if (j->restart && mcus && mcus % j->restart == 0) {
                    j->bn = 0;
                    while (j->p + 1 < end && !(j->p[0] == 0xFF && j->p[1] >= 0xD0 && j->p[1] <= 0xD7)) j->p++;
                    if (j->p + 1 < end) j->p += 2;
                    for (ci = 0; ci < j->ncomp; ci++) j->c[ci].dc = 0;
                }
                mcus++;
                for (ci = 0; ci < j->ncomp; ci++) {
                    int bx, by;
                    for (by = 0; by < j->c[ci].v; by++) for (bx = 0; bx < j->c[ci].h; bx++) {
                        int blk[64], k, t;
                        memset(blk, 0, sizeof blk);
                        t = jdec(j, &j->hd[j->c[ci].td]);
                        j->c[ci].dc += jext(jbits(j, t), t);
                        blk[0] = j->c[ci].dc * j->q[j->c[ci].tq][0];
                        for (k = 1; k < 64;) {
                            int rs = jdec(j, &j->ha[j->c[ci].ta]), r = rs >> 4, sz = rs & 15;
                            if (!sz) { if (r == 15) { k += 16; continue; } break; }
                            k += r;
                            if (k > 63) break;
                            blk[zz[k]] = jext(jbits(j, sz), sz) * j->q[j->c[ci].tq][zz[k]];
                            k++;
                        }
                        jidct(blk, j->c[ci].data + (size_t)((my * j->c[ci].v + by) * 8) * j->c[ci].bw + (mx * j->c[ci].h + bx) * 8, j->c[ci].bw);
                    }
                }
            }
            s = surf_alloc(j->w, j->h, 0);
            if (s) {
                int x, y;
                for (y = 0; y < j->h; y++) for (x = 0; x < j->w; x++) {
                    int Y, cb = 128, cr = 128, r, g, b;
                    Y = j->c[0].data[(size_t)(y * j->c[0].v / j->vmax) * j->c[0].bw + x * j->c[0].h / j->hmax];
                    if (j->ncomp == 3) {
                        cb = j->c[1].data[(size_t)(y * j->c[1].v / j->vmax) * j->c[1].bw + x * j->c[1].h / j->hmax];
                        cr = j->c[2].data[(size_t)(y * j->c[2].v / j->vmax) * j->c[2].bw + x * j->c[2].h / j->hmax];
                    }
                    r = Y + ((91881 * (cr - 128)) >> 16);
                    g = Y - ((22554 * (cb - 128) + 46802 * (cr - 128)) >> 16);
                    b = Y + ((116130 * (cb - 128)) >> 16);
                    put_px(s, x, y, r < 0 ? 0 : r > 255 ? 255 : r, g < 0 ? 0 : g > 255 ? 255 : g, b < 0 ? 0 : b > 255 ? 255 : b, 255);
                }
            }
            break;
        }
        p += 2 + l;
    }
fail:
    for (i = 0; i < 3; i++) free(j->c[i].data);
    free(j);
    return s;
}

Surf *img_decode(const u8 *d, u32 len)
{
    if (!d || len < 8) return NULL;
    if (d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return img_png(d, len);
    if (d[0] == 'B' && d[1] == 'M') return img_bmp(d, len);
    if (d[0] == 'G' && d[1] == 'I' && d[2] == 'F') return img_gif(d, len);
    if (d[0] == 0xFF && d[1] == 0xD8) return img_jpeg(d, len);
    return NULL;
}
