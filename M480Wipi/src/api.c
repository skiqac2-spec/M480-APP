/* natives for m480.*, WIPI / MIDP Graphics, images, fonts, storage, sound */
#include "jvm.h"
#include "gfx.h"
#include "host.h"
#include "pal.h"
#include "audio.h"
#include <stdlib.h>
#include <stdio.h>

Host host;

/* ---------------- event queue ---------------- */
#define EVQ 256
static int evq[EVQ][4];
static int evh, evt;

void ev_push(int type, int a, int b, int c)
{
    int n = (evt + 1) % EVQ;
    if (n == evh) return;          /* full: drop */
    /* coalesce repaint requests */
    if (type == EV_REPAINT) {
        int i;
        for (i = evh; i != evt; i = (i + 1) % EVQ) if (evq[i][0] == EV_REPAINT) return;
    }
    evq[evt][0] = type; evq[evt][1] = a; evq[evt][2] = b; evq[evt][3] = c;
    evt = n;
    vm_wake_event();
}
int ev_pending(void) { return evh != evt; }
void ev_clear(void) { evh = evt = 0; }

#define F(o) OBJ_FIELDS(o)
#define RET_I(v) do { a[0].i = (v); return NR_OK; } while (0)
#define RET_L(v) do { a[0].l = (Object *)(v); return NR_OK; } while (0)

/* ---------------- m480.Sys ---------------- */
static int s_getEvent(Thread *t, Slot *a)
{
    Array *ev = (Array *)a[0].l;
    if (!ev) return throw_npe(t);
    if (evh == evt) { t->state = TS_EVENTWAIT; t->wake = 0; return NR_RETRY; }
    memcpy(ARR_I(ev), evq[evh], 4 * sizeof(int) < (size_t)ev->length * 4 ? 4 * sizeof(int) : (size_t)ev->length * 4);
    evh = (evh + 1) % EVQ;
    return NR_OK;
}
static int s_postEvent(Thread *t, Slot *a) { (void)t; ev_push(a[0].i, a[1].i, a[2].i, a[3].i); return NR_OK; }
static int s_flush(Thread *t, Slot *a) { (void)t; (void)a; host.dirty = 1; if (host.flush) host.flush(); return NR_OK; }
static int s_width(Thread *t, Slot *a) { (void)t; RET_I(host.screen ? host.screen->w : 240); }
static int s_height(Thread *t, Slot *a) { (void)t; RET_I(host.screen ? host.screen->h : 320); }
static int s_carrier(Thread *t, Slot *a) { (void)t; RET_I(host.carrier); }
static int s_prop(Thread *t, Slot *a)
{
    char k[200];
    const char *v;
    if (!a[0].l) RET_L(NULL);
    str_to_utf8(a[0].l, k, sizeof k);
    v = vm_get_prop(k);
    RET_L(v ? str_new_utf8(t, v) : NULL);
}
static int s_vibrate(Thread *t, Slot *a) { (void)t; if (host.vibrate) host.vibrate(a[0].i); return NR_OK; }
static int s_backlight(Thread *t, Slot *a) { (void)t; if (host.backlight) host.backlight(a[0].i); return NR_OK; }
static int s_quit(Thread *t, Slot *a) { (void)t; (void)a; host.quit_requested = 1; vm_log("app requested exit"); return NR_OK; }
static int s_fail(Thread *t, Slot *a)
{
    (void)t;
    if (a[0].l) str_to_utf8(a[0].l, host.fail, sizeof host.fail);
    return NR_OK;
}

/* ---------------- images ---------------- */
static int i_decode(Thread *t, Slot *a)
{
    Array *b = (Array *)a[0].l;
    s32 off = a[1].i, len = a[2].i;
    Surf *s;
    if (!b) return throw_npe(t);
    if (off < 0 || len < 0 || off + len > b->length) return throw_new(t, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    s = img_decode((const u8 *)ARR_B(b) + off, (u32)len);
    if (!s) { vm_log("image decode failed (%d bytes, %02x %02x %02x %02x)", len, len > 0 ? (u8)ARR_B(b)[off] : 0, len > 1 ? (u8)ARR_B(b)[off + 1] : 0, len > 2 ? (u8)ARR_B(b)[off + 2] : 0, len > 3 ? (u8)ARR_B(b)[off + 3] : 0); RET_I(0); }
    RET_I(surf_add(s));
}
static int i_create(Thread *t, Slot *a)
{
    int h = surf_new(a[0].i, a[1].i, 0);
    Surf *s = surf_get(h);
    if (!s) return throw_new(t, "java/lang/OutOfMemoryError", "image");
    {
        size_t i, n = (size_t)s->w * s->h;
        for (i = 0; i < n; i++) s->px[i] = 0xFFFF;
    }
    RET_I(h);
}
static int i_region(Thread *t, Slot *a)
{
    Surf *src = surf_get(a[0].i), *d;
    int x = a[1].i, y = a[2].i, w = a[3].i, h = a[4].i, tr = a[5].i;
    GC g;
    if (!src) return throw_npe(t);
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > src->w || y + h > src->h) return throw_new(t, "java/lang/IllegalArgumentException", NULL);
    d = (tr & 4) ? surf_alloc(h, w, src->a != NULL) : surf_alloc(w, h, src->a != NULL);
    if (!d) return throw_new(t, "java/lang/OutOfMemoryError", NULL);
    if (d->a) memset(d->a, 0, (size_t)d->w * d->h);
    memset(d->px, 0, (size_t)d->w * d->h * 2);
    memset(&g, 0, sizeof g);
    g.s = d; g.cx1 = d->w; g.cy1 = d->h; g.alpha = 255;
    g_blit(&g, src, x, y, w, h, 0, 0, tr);
    RET_I(surf_add(d));
}
static int i_fromRGB(Thread *t, Slot *a)
{
    Array *rgb = (Array *)a[0].l;
    int w = a[1].i, h = a[2].i, alpha = a[3].i;
    Surf *s;
    GC g;
    if (!rgb) return throw_npe(t);
    s = surf_alloc(w, h, alpha);
    if (!s) return throw_new(t, "java/lang/OutOfMemoryError", NULL);
    if (s->a) memset(s->a, 0, (size_t)w * h);
    memset(&g, 0, sizeof g);
    g.s = s; g.cx1 = w; g.cy1 = h; g.alpha = 255;
    g_rgb(&g, (const u32 *)ARR_I(rgb), 0, w, 0, 0, w, h, alpha);
    if (s->a) {
        /* exact copy of alpha rather than blending onto nothing */
        int i, n = w * h;
        const u32 *p = (const u32 *)ARR_I(rgb);
        for (i = 0; i < n; i++) { s->a[i] = (u8)(p[i] >> 24); s->px[i] = C565(p[i]); }
    }
    RET_I(surf_add(s));
}
static int i_width(Thread *t, Slot *a) { Surf *s = surf_get(a[0].i); (void)t; RET_I(s ? s->w : 0); }
static int i_height(Thread *t, Slot *a) { Surf *s = surf_get(a[0].i); (void)t; RET_I(s ? s->h : 0); }
static int i_hasAlpha(Thread *t, Slot *a) { Surf *s = surf_get(a[0].i); (void)t; RET_I(s && s->a); }
static void get_rgb(Surf *s, s32 *out, int off, int scan, int x, int y, int w, int h, s32 outlen)
{
    int u, v;
    for (v = 0; v < h; v++) for (u = 0; u < w; u++) {
        int sx = x + u, sy = y + v;
        s32 idx = off + v * scan + u;
        u32 c = 0;
        if (idx < 0 || idx >= outlen) continue;
        if (sx >= 0 && sy >= 0 && sx < s->w && sy < s->h) {
            size_t i = (size_t)sy * s->w + sx;
            c = c888(s->px[i]) | ((u32)(s->a ? s->a[i] : 255) << 24);
        }
        out[idx] = (s32)c;
    }
}
static int i_getRGB(Thread *t, Slot *a)
{
    Surf *s = surf_get(a[0].i);
    Array *rgb = (Array *)a[1].l;
    if (!s || !rgb) return throw_npe(t);
    get_rgb(s, ARR_I(rgb), a[2].i, a[3].i, a[4].i, a[5].i, a[6].i, a[7].i, rgb->length);
    return NR_OK;
}
static int i_setTransparent(Thread *t, Slot *a)
{
    Surf *s = surf_get(a[0].i);
    u16 c;
    size_t i, n;
    (void)t;
    if (!s) return NR_OK;
    c = C565((u32)a[1].i);
    surf_ensure_alpha(s);
    if (!s->a) return NR_OK;
    n = (size_t)s->w * s->h;
    for (i = 0; i < n; i++) if (s->px[i] == c) s->a[i] = 0;
    return NR_OK;
}
static void image_fin(Object *o) { surf_free(OBJ_FIELDS(o)[0].i); }
static void clip_fin(Object *o) { if (OBJ_FIELDS(o)[0].i) snd_free(OBJ_FIELDS(o)[0].i); }

/* ---------------- fonts ---------------- */
static int fn_height(Thread *t, Slot *a) { (void)t; RET_I(font_height(a[0].i)); }
static int fn_ascent(Thread *t, Slot *a) { (void)t; RET_I(font_ascent(a[0].i)); }
static int fn_strWidth(Thread *t, Slot *a)
{
    Object *s = a[1].l;
    s32 off = a[2].i, len = a[3].i;
    if (!s) return throw_npe(t);
    if (off < 0 || len < 0 || off + len > str_len(s)) return throw_new(t, "java/lang/StringIndexOutOfBoundsException", NULL);
    RET_I(g_text_width(str_chars(s) + off, len, a[0].i));
}
static int fn_charsWidth(Thread *t, Slot *a)
{
    Array *c = (Array *)a[1].l;
    s32 off = a[2].i, len = a[3].i;
    if (!c) return throw_npe(t);
    if (off < 0 || len < 0 || off + len > c->length) return throw_new(t, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    RET_I(g_text_width(ARR_C(c) + off, len, a[0].i));
}
static int fn_pixelSize(Thread *t, Slot *a)
{
    int sz = a[0].i, small = host.screen && host.screen->w < 200;
    (void)t;
    if (sz == 8) RET_I(12);
    if (sz == 16) RET_I(small ? 16 : 20);
    RET_I(small ? 12 : 16);
}

/* ---------------- graphics ---------------- */
enum { G_SURF, G_TX, G_TY, G_CX, G_CY, G_CW, G_CH, G_COLOR, G_ALPHA, G_XOR, G_STROKE, G_FONT, G_IMG, G_GW, G_GH };

static int g_checked;
static void check_layout(Class *c)
{
    static const char *names[] = { "surf", "tx", "ty", "cx", "cy", "cw", "ch", "color", "alpha", "xor", "stroke", "font", "img", "gw", "gh" };
    int i;
    for (i = 0; i < 15; i++) {
        Field *f = cls_field_n(c, names[i]);
        if (!f || f->off != i) vm_log("Graphics layout mismatch in %s field %s", c->name, names[i]);
    }
}

static int gc_get(Thread *t, Object *go, GC *g)
{
    Slot *f = F(go);
    Surf *s = surf_get(f[G_SURF].i);
    if (!g_checked) { check_layout(go->cls); g_checked = 1; }
    if (!s) { throw_new(t, "java/lang/IllegalStateException", "graphics surface gone"); return 0; }
    g->s = s;
    g->cx0 = f[G_CX].i; g->cy0 = f[G_CY].i;
    g->cx1 = f[G_CX].i + f[G_CW].i; g->cy1 = f[G_CY].i + f[G_CH].i;
    if (g->cx0 < 0) g->cx0 = 0;
    if (g->cy0 < 0) g->cy0 = 0;
    if (g->cx1 > s->w) g->cx1 = s->w;
    if (g->cy1 > s->h) g->cy1 = s->h;
    g->rgb = (u32)f[G_COLOR].i;
    g->c = C565(g->rgb);
    g->alpha = f[G_ALPHA].i;
    g->xor = f[G_XOR].i;
    if (s == host.screen) host.dirty = 1;
    return 1;
}
#define GC_BEGIN GC g; Slot *f; if (!gc_get(t, a[0].l, &g)) return NR_EXC; f = F(a[0].l); (void)f
#define TX f[G_TX].i
#define TY f[G_TY].i

static int gr_fillRect(Thread *t, Slot *a) { GC_BEGIN; g_fill(&g, a[1].i + TX, a[2].i + TY, a[3].i, a[4].i); return NR_OK; }
static int gr_drawRect(Thread *t, Slot *a) { GC_BEGIN; g_rect(&g, a[1].i + TX, a[2].i + TY, a[3].i, a[4].i); return NR_OK; }
static int gr_drawLine(Thread *t, Slot *a) { GC_BEGIN; g_line(&g, a[1].i + TX, a[2].i + TY, a[3].i + TX, a[4].i + TY); return NR_OK; }
static int gr_fillRound(Thread *t, Slot *a) { GC_BEGIN; g_round(&g, a[1].i + TX, a[2].i + TY, a[3].i, a[4].i, a[5].i, a[6].i, 1); return NR_OK; }
static int gr_drawRound(Thread *t, Slot *a) { GC_BEGIN; g_round(&g, a[1].i + TX, a[2].i + TY, a[3].i, a[4].i, a[5].i, a[6].i, 0); return NR_OK; }
static int gr_fillArc(Thread *t, Slot *a) { GC_BEGIN; g_arc(&g, a[1].i + TX, a[2].i + TY, a[3].i, a[4].i, a[5].i, a[6].i, 1); return NR_OK; }
static int gr_drawArc(Thread *t, Slot *a) { GC_BEGIN; g_arc(&g, a[1].i + TX, a[2].i + TY, a[3].i, a[4].i, a[5].i, a[6].i, 0); return NR_OK; }
static int gr_fillTri(Thread *t, Slot *a)
{
    int xs[3], ys[3];
    GC_BEGIN;
    xs[0] = a[1].i + TX; ys[0] = a[2].i + TY; xs[1] = a[3].i + TX; ys[1] = a[4].i + TY; xs[2] = a[5].i + TX; ys[2] = a[6].i + TY;
    g_poly(&g, xs, ys, 3, 1);
    return NR_OK;
}
static int poly(Thread *t, Slot *a, int fill)
{
    Array *xa = (Array *)a[1].l, *ya = (Array *)a[2].l;
    int n, i, *xs, *ys;
    GC_BEGIN;
    if (!xa || !ya) return throw_npe(t);
    n = xa->length < ya->length ? xa->length : ya->length;
    if (n <= 0) return NR_OK;
    xs = (int *)malloc(n * sizeof(int)); ys = (int *)malloc(n * sizeof(int));
    for (i = 0; i < n; i++) { xs[i] = ARR_I(xa)[i] + TX; ys[i] = ARR_I(ya)[i] + TY; }
    g_poly(&g, xs, ys, n, fill);
    free(xs); free(ys);
    return NR_OK;
}
static int gr_fillPoly(Thread *t, Slot *a) { return poly(t, a, 1); }
static int gr_drawPoly(Thread *t, Slot *a) { return poly(t, a, 0); }

static int font_of(Slot *f)
{
    Object *fo = f[G_FONT].l;
    return fo ? F(fo)[0].i : (16 << 4);
}
static void text_at(GC *g, Slot *f, const u16 *s, int n, int x, int y, int anchor)
{
    int fid = font_of(f), w;
    if (!anchor) anchor = 16 | 4;
    w = g_text_width(s, n, fid);
    if (anchor & 1) x -= w / 2; else if (anchor & 8) x -= w;
    if (anchor & 64) y -= font_ascent(fid);
    else if (anchor & 32) y -= font_height(fid);
    else if (anchor & 2) y -= font_height(fid) / 2;
    g_text(g, s, n, x + f[G_TX].i, y + f[G_TY].i, fid);
}
static int gr_drawString(Thread *t, Slot *a)
{
    Object *s = a[1].l;
    GC_BEGIN;
    if (!s) return throw_npe(t);
    text_at(&g, f, str_chars(s), str_len(s), a[2].i, a[3].i, a[4].i);
    return NR_OK;
}
static int gr_drawSubstring(Thread *t, Slot *a)
{
    Object *s = a[1].l;
    s32 off = a[2].i, len = a[3].i;
    GC_BEGIN;
    if (!s) return throw_npe(t);
    if (off < 0 || len < 0 || off + len > str_len(s)) return throw_new(t, "java/lang/StringIndexOutOfBoundsException", NULL);
    text_at(&g, f, str_chars(s) + off, len, a[4].i, a[5].i, a[6].i);
    return NR_OK;
}
static int gr_drawChars(Thread *t, Slot *a)
{
    Array *c = (Array *)a[1].l;
    s32 off = a[2].i, len = a[3].i;
    GC_BEGIN;
    if (!c) return throw_npe(t);
    if (off < 0 || len < 0 || off + len > c->length) return throw_new(t, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    text_at(&g, f, ARR_C(c) + off, len, a[4].i, a[5].i, a[6].i);
    return NR_OK;
}
static void anchor_img(int *x, int *y, int w, int h, int anchor)
{
    if (!anchor) anchor = 16 | 4;
    if (anchor & 1) *x -= w / 2; else if (anchor & 8) *x -= w;
    if (anchor & 2) *y -= h / 2; else if (anchor & (32 | 64)) *y -= h;
}
static int gr_drawImage(Thread *t, Slot *a)
{
    Object *img = a[1].l;
    Surf *s;
    int x = a[2].i, y = a[3].i;
    GC_BEGIN;
    if (!img) return throw_npe(t);
    s = surf_get(F(img)[0].i);
    if (!s) return NR_OK;
    anchor_img(&x, &y, s->w, s->h, a[4].i);
    g_blit(&g, s, 0, 0, s->w, s->h, x + TX, y + TY, 0);
    return NR_OK;
}
static int gr_drawRegion(Thread *t, Slot *a)
{
    Object *img = a[1].l;
    Surf *s;
    int sx = a[2].i, sy = a[3].i, w = a[4].i, h = a[5].i, tr = a[6].i, x = a[7].i, y = a[8].i;
    GC_BEGIN;
    if (!img) return throw_npe(t);
    s = surf_get(F(img)[0].i);
    if (!s) return NR_OK;
    if (sx < 0) { w += sx; x -= sx; sx = 0; }
    if (sy < 0) { h += sy; y -= sy; sy = 0; }
    if (sx + w > s->w) w = s->w - sx;
    if (sy + h > s->h) h = s->h - sy;
    if (w <= 0 || h <= 0) return NR_OK;
    if (tr & 4) anchor_img(&x, &y, h, w, a[9].i); else anchor_img(&x, &y, w, h, a[9].i);
    g_blit(&g, s, sx, sy, w, h, x + TX, y + TY, tr & 7);
    return NR_OK;
}
static int gr_copyArea(Thread *t, Slot *a)
{
    GC_BEGIN;
    g_copy(&g, a[1].i + TX, a[2].i + TY, a[3].i, a[4].i, a[5].i + TX, a[6].i + TY);
    return NR_OK;
}
static int gr_drawRGB(Thread *t, Slot *a)
{
    Array *rgb = (Array *)a[1].l;
    s32 off = a[2].i, scan = a[3].i, w = a[6].i, h = a[7].i;
    GC_BEGIN;
    if (!rgb) return throw_npe(t);
    if (w <= 0 || h <= 0) return NR_OK;
    if (off < 0 || (s64)off + (s64)(h - 1) * scan + w > rgb->length || (s64)off + (s64)(h - 1) * scan < 0)
        return throw_new(t, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    g_rgb(&g, (const u32 *)ARR_I(rgb), off, scan, a[4].i + TX, a[5].i + TY, w, h, a[8].i);
    return NR_OK;
}
static int gr_getRGBPixels(Thread *t, Slot *a)
{
    Array *rgb = (Array *)a[5].l;
    GC_BEGIN;
    if (!rgb) return throw_npe(t);
    get_rgb(g.s, ARR_I(rgb), a[6].i, a[7].i, a[1].i + TX, a[2].i + TY, a[3].i, a[4].i, rgb->length);
    return NR_OK;
}

static int gfx_present(Thread *t, Slot *a)
{
    Object *img = a[0].l;
    Surf *s;
    GC g;
    if (!img) return throw_npe(t);
    s = surf_get(F(img)[0].i);
    if (!s || !host.screen) return NR_OK;
    memset(&g, 0, sizeof g);
    g.s = host.screen; g.cx1 = host.screen->w; g.cy1 = host.screen->h; g.alpha = 255;
    g_blit(&g, s, 0, 0, s->w, s->h, a[1].i, a[2].i, 0);
    host.dirty = 1;
    if (host.flush) host.flush();
    return NR_OK;
}

/* ---------------- storage ---------------- */
static int store_path(Object *kind, Object *name, char *out, int max)
{
    char k[32], n[200];
    int i;
    str_to_utf8(kind, k, sizeof k);
    if (name) {
        str_to_utf8(name, n, sizeof n);
        for (i = 0; n[i]; i++) if (n[i] == '/' || n[i] == '\\' || n[i] == ':' || n[i] == '*' || n[i] == '?' || n[i] == '"' || n[i] == '<' || n[i] == '>' || n[i] == '|') n[i] = '_';
        snprintf(out, max, "%s/%s/%s.dat", host.savedir, k, n);
    } else snprintf(out, max, "%s/%s", host.savedir, k);
    return 1;
}
static int st_load(Thread *t, Slot *a)
{
    char p[700];
    u32 len;
    u8 *d;
    Array *b;
    store_path(a[0].l, a[1].l, p, sizeof p);
    d = pal_load(p, &len);
    if (!d) RET_L(NULL);
    b = arr_newp(t, 'B', (s32)len);
    if (!b) { free(d); return NR_EXC; }
    memcpy(ARR_B(b), d, len);
    free(d);
    RET_L(b);
}
static int st_save(Thread *t, Slot *a)
{
    char p[700], dir[600];
    Array *b = (Array *)a[2].l;
    s32 len = a[3].i;
    if (!b) return throw_npe(t);
    if (len > b->length) len = b->length;
    pal_mkdir(host.savedir);
    store_path(a[0].l, NULL, dir, sizeof dir);
    pal_mkdir(dir);
    store_path(a[0].l, a[1].l, p, sizeof p);
    RET_I(pal_save(p, ARR_B(b), (u32)len));
}
static int st_remove(Thread *t, Slot *a) { char p[700]; (void)t; store_path(a[0].l, a[1].l, p, sizeof p); RET_I(pal_remove(p)); }
static int st_exists(Thread *t, Slot *a)
{
    char p[700];
    void *f;
    (void)t;
    store_path(a[0].l, a[1].l, p, sizeof p);
    f = pal_fopen(p, 'r');
    if (f) pal_fclose(f);
    RET_I(f != NULL);
}
typedef struct { char names[64][64]; int n; } NameList;
static void list_cb(void *u, const char *name, int is_dir, u32 size)
{
    NameList *l = (NameList *)u;
    int n = (int)strlen(name);
    (void)size;
    if (is_dir || l->n >= 64 || n < 5 || strcmp(name + n - 4, ".dat")) return;
    memcpy(l->names[l->n], name, n - 4 < 63 ? n - 4 : 63);
    l->names[l->n][n - 4 < 63 ? n - 4 : 63] = 0;
    l->n++;
}
static int st_list(Thread *t, Slot *a)
{
    char dir[600];
    NameList *l = (NameList *)calloc(1, sizeof(NameList));
    Array *arr;
    int i;
    store_path(a[0].l, NULL, dir, sizeof dir);
    pal_listdir(dir, list_cb, l);
    arr = arr_new(t, cls_array(C_String, 'L'), l->n);
    if (!arr) { free(l); return NR_EXC; }
    troot(&arr->o);
    for (i = 0; i < l->n; i++) {
        Object *s = str_new_utf8(t, l->names[i]);
        if (!s) { free(l); return NR_EXC; }
        ARR_L(arr)[i] = s;
    }
    free(l);
    RET_L(arr);
}

/* ---------------- sound ---------------- */
static int sn_load(Thread *t, Slot *a)
{
    Array *b = (Array *)a[0].l;
    s32 off = a[1].i, len = a[2].i;
    if (!b) return throw_npe(t);
    if (off < 0 || len < 0 || off + len > b->length) return throw_new(t, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    RET_I(snd_load((const u8 *)ARR_B(b) + off, (u32)len));
}
static int sn_play(Thread *t, Slot *a) { (void)t; snd_play(a[0].i, a[1].i); return NR_OK; }
static int sn_stop(Thread *t, Slot *a) { (void)t; snd_stop(a[0].i); return NR_OK; }
static int sn_pause(Thread *t, Slot *a) { (void)t; snd_pause(a[0].i, 1); return NR_OK; }
static int sn_resume(Thread *t, Slot *a) { (void)t; snd_pause(a[0].i, 0); return NR_OK; }
static int sn_volume(Thread *t, Slot *a) { (void)t; snd_volume(a[0].i, a[1].i); return NR_OK; }
static int sn_playing(Thread *t, Slot *a) { (void)t; RET_I(snd_playing(a[0].i)); }
static int sn_position(Thread *t, Slot *a) { (void)t; RET_I(snd_position_ms(a[0].i)); }
static int sn_duration(Thread *t, Slot *a) { (void)t; RET_I(snd_duration_ms(a[0].i)); }
static int sn_free(Thread *t, Slot *a) { (void)t; snd_free(a[0].i); return NR_OK; }
static int sn_tone(Thread *t, Slot *a) { (void)t; snd_tone(a[0].i, a[1].i, a[2].i); return NR_OK; }

void natives_api(void)
{
    static const char *gcls[2] = { "org/kwis/msp/lcdui/Graphics", "javax/microedition/lcdui/Graphics" };
    int i;
#define R(c, n, d, f) native_register(c, n, d, f)
    R("m480/Sys", "getEvent", NULL, s_getEvent);
    R("m480/Sys", "postEvent", NULL, s_postEvent);
    R("m480/Sys", "flush", NULL, s_flush);
    R("m480/Sys", "width", NULL, s_width);
    R("m480/Sys", "height", NULL, s_height);
    R("m480/Sys", "carrier", NULL, s_carrier);
    R("m480/Sys", "prop", NULL, s_prop);
    R("m480/Sys", "vibrate", NULL, s_vibrate);
    R("m480/Sys", "backlight", NULL, s_backlight);
    R("m480/Sys", "quit", NULL, s_quit);
    R("m480/Sys", "fail", NULL, s_fail);
    R("m480/Img", "decode", NULL, i_decode);
    R("m480/Img", "create", NULL, i_create);
    R("m480/Img", "region", NULL, i_region);
    R("m480/Img", "fromRGB", NULL, i_fromRGB);
    R("m480/Img", "width", NULL, i_width);
    R("m480/Img", "height", NULL, i_height);
    R("m480/Img", "getRGB", NULL, i_getRGB);
    R("m480/Img", "setTransparent", NULL, i_setTransparent);
    R("m480/Img", "hasAlpha", NULL, i_hasAlpha);
    R("m480/FontN", "height", NULL, fn_height);
    R("m480/FontN", "ascent", NULL, fn_ascent);
    R("m480/FontN", "strWidth", NULL, fn_strWidth);
    R("m480/FontN", "charsWidth", NULL, fn_charsWidth);
    R("m480/FontN", "pixelSize", NULL, fn_pixelSize);
    R("m480/Gfx", "present", NULL, gfx_present);
    R("m480/Store", "load", NULL, st_load);
    R("m480/Store", "save", NULL, st_save);
    R("m480/Store", "remove", NULL, st_remove);
    R("m480/Store", "exists", NULL, st_exists);
    R("m480/Store", "list", NULL, st_list);
    R("m480/Snd", "load", NULL, sn_load);
    R("m480/Snd", "play", NULL, sn_play);
    R("m480/Snd", "stop", NULL, sn_stop);
    R("m480/Snd", "pause", NULL, sn_pause);
    R("m480/Snd", "resume", NULL, sn_resume);
    R("m480/Snd", "volume", NULL, sn_volume);
    R("m480/Snd", "playing", NULL, sn_playing);
    R("m480/Snd", "position", NULL, sn_position);
    R("m480/Snd", "duration", NULL, sn_duration);
    R("m480/Snd", "free", NULL, sn_free);
    R("m480/Snd", "tone", NULL, sn_tone);
    for (i = 0; i < 2; i++) {
        const char *c = gcls[i];
        R(c, "fillRect", NULL, gr_fillRect);
        R(c, "drawRect", NULL, gr_drawRect);
        R(c, "drawLine", NULL, gr_drawLine);
        R(c, "fillRoundRect", NULL, gr_fillRound);
        R(c, "drawRoundRect", NULL, gr_drawRound);
        R(c, "fillArc", NULL, gr_fillArc);
        R(c, "drawArc", NULL, gr_drawArc);
        R(c, "fillTriangle", NULL, gr_fillTri);
        R(c, "fillPolygon", "([I[I)V", gr_fillPoly);
        R(c, "drawPolygon", "([I[I)V", gr_drawPoly);
        R(c, "drawString", NULL, gr_drawString);
        R(c, "drawSubstring", NULL, gr_drawSubstring);
        R(c, "drawChars", NULL, gr_drawChars);
        R(c, "drawImage0", NULL, gr_drawImage);
        R(c, "drawRegion0", NULL, gr_drawRegion);
        R(c, "copyArea", "(IIIIII)V", gr_copyArea);
        R(c, "drawRGB", NULL, gr_drawRGB);
        R(c, "getRGBPixels", NULL, gr_getRGBPixels);
    }
#undef R
    cls_set_finalizer("org/kwis/msp/lcdui/Image", image_fin);
    cls_set_finalizer("javax/microedition/lcdui/Image", image_fin);
    cls_set_finalizer("org/kwis/msp/media/BaseClip", clip_fin);
}

void api_reset(void)
{
    ev_clear();
    g_checked = 0;
}
