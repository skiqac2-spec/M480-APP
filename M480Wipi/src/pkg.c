/* game package detection: SKT (.jar + .msd), KTF (__adf__ + jar), LGT (app_info + jar), plain J2ME (.jar/.jad) */
#include "pkg.h"
#include "host.h"
#include "pal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ksc_to_u16(const u8 *in, int n, u16 *out);

static int ends_with(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf), i;
    if (b > a) return 0;
    for (i = 0; i < b; i++) {
        char x = s[a - b + i], y = suf[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return 1;
}
static const char *base_name(const char *p)
{
    const char *b = p, *q;
    for (q = p; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
    return b;
}

/* descriptor text may be EUC-KR or UTF-8; convert values to UTF-8 */
static void to_utf8(const char *in, int n, char *out, int max)
{
    int i, utf8 = 1, o = 0;
    for (i = 0; i < n; i++) {
        u8 c = (u8)in[i];
        if (c < 0x80) continue;
        if ((c & 0xE0) == 0xC0 && i + 1 < n && ((u8)in[i + 1] & 0xC0) == 0x80) { i++; continue; }
        if ((c & 0xF0) == 0xE0 && i + 2 < n && ((u8)in[i + 1] & 0xC0) == 0x80 && ((u8)in[i + 2] & 0xC0) == 0x80) { i += 2; continue; }
        utf8 = 0;
        break;
    }
    if (utf8) { if (n >= max) n = max - 1; memcpy(out, in, n); out[n] = 0; return; }
    {
        u16 *w = (u16 *)malloc((n + 1) * 2);
        int k = ksc_to_u16((const u8 *)in, n, w);
        for (i = 0; i < k; i++) {
            u16 ch = w[i];
            if (ch < 0x80) { if (o + 1 >= max) break; out[o++] = (char)ch; }
            else if (ch < 0x800) { if (o + 2 >= max) break; out[o++] = (char)(0xC0 | (ch >> 6)); out[o++] = (char)(0x80 | (ch & 0x3F)); }
            else { if (o + 3 >= max) break; out[o++] = (char)(0xE0 | (ch >> 12)); out[o++] = (char)(0x80 | ((ch >> 6) & 0x3F)); out[o++] = (char)(0x80 | (ch & 0x3F)); }
        }
        out[o] = 0;
        free(w);
    }
}

static void add_prop(Pkg *p, const char *k, const char *v)
{
    int used = 0, i;
    const char *q = p->props;
    for (i = 0; i < p->nprops; i++) { size_t kl = strlen(q) + 1; size_t vl = strlen(q + kl) + 1; q += kl + vl; }
    used = (int)(q - p->props);
    if (used + (int)strlen(k) + (int)strlen(v) + 2 >= (int)sizeof p->props) return;
    strcpy(p->props + used, k);
    strcpy(p->props + used + strlen(k) + 1, v);
    p->nprops++;
}
static const char *get_prop(Pkg *p, const char *k)
{
    const char *q = p->props;
    int i;
    for (i = 0; i < p->nprops; i++) {
        size_t kl = strlen(q) + 1;
        if (!strcmp(q, k)) return q + kl;
        q += kl + strlen(q + kl) + 1;
    }
    return NULL;
}

/* "Key: value" lines (manifest, jad, msd, adf, app_info). Manifest continuation lines start with a space. */
static void parse_desc(Pkg *p, const u8 *d, u32 len)
{
    char line[1024];
    u32 i = 0;
    while (i < len) {
        int n = 0;
        while (i < len && d[i] != '\n' && d[i] != '\r') { if (n < 1000) line[n++] = (char)d[i]; i++; }
        while (i < len && (d[i] == '\n' || d[i] == '\r')) i++;
        while (i < len && d[i] == ' ' && n < 1000) { i++; while (i < len && d[i] != '\n' && d[i] != '\r') { if (n < 1000) line[n++] = (char)d[i]; i++; } while (i < len && (d[i] == '\n' || d[i] == '\r')) i++; }
        line[n] = 0;
        {
            char *c = strchr(line, ':');
            char key[200], val[800];
            int kl;
            if (!c) continue;
            kl = (int)(c - line);
            if (kl <= 0 || kl > 190) continue;
            memcpy(key, line, kl); key[kl] = 0;
            while (kl > 0 && key[kl - 1] == ' ') key[--kl] = 0;
            c++;
            while (*c == ' ' || *c == '\t') c++;
            to_utf8(c, (int)strlen(c), val, sizeof val);
            add_prop(p, key, val);
        }
    }
}

static void trim(char *s)
{
    char *a = s, *e;
    while (*a == ' ') a++;
    memmove(s, a, strlen(a) + 1);
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\r')) *--e = 0;
}

static void apply_desc(Pkg *p)
{
    const char *v;
    if ((v = get_prop(p, "MClass")) != NULL && !p->mainclass[0]) snprintf(p->mainclass, sizeof p->mainclass, "%s", v);
    if ((v = get_prop(p, "MIDlet-1")) != NULL && !p->mainclass[0]) {
        const char *c1 = strchr(v, ','), *c2 = c1 ? strchr(c1 + 1, ',') : NULL;
        if (c2) { snprintf(p->mainclass, sizeof p->mainclass, "%s", c2 + 1); trim(p->mainclass); }
    }
    if ((v = get_prop(p, "Main-Class")) != NULL && !p->mainclass[0]) snprintf(p->mainclass, sizeof p->mainclass, "%s", v);
    if ((v = get_prop(p, "Name")) != NULL && !p->name[0]) snprintf(p->name, sizeof p->name, "%s", v);
    if ((v = get_prop(p, "MIDlet-Name")) != NULL && !p->name[0]) snprintf(p->name, sizeof p->name, "%s", v);
    if ((v = get_prop(p, "DD-ProgName")) != NULL && !p->id[0]) snprintf(p->id, sizeof p->id, "%s", v);
    if ((v = get_prop(p, "PID")) != NULL && !p->id[0]) snprintf(p->id, sizeof p->id, "%s", v);
    if ((v = get_prop(p, "AID")) != NULL && !p->id[0]) snprintf(p->id, sizeof p->id, "%s", v);
    if ((v = get_prop(p, "DisplaySize")) != NULL || (v = get_prop(p, "MIDxlet-ScreenSize")) != NULL || (v = get_prop(p, "LCD-Size")) != NULL) {
        int w = 0, h = 0;
        if (sscanf(v, "%d*%d", &w, &h) == 2 || sscanf(v, "%dx%d", &w, &h) == 2 || sscanf(v, "%d,%d", &w, &h) == 2 || sscanf(v, "%dX%d", &w, &h) == 2) {
            if (w >= 96 && w <= 480 && h >= 64 && h <= 640) { p->scr_w = w; p->scr_h = h; }
        }
    }
    {
        /* dotted main class -> slash form */
        char *c;
        for (c = p->mainclass; *c; c++) if (*c == '.') *c = '/';
        if (ends_with(p->mainclass, "/class")) p->mainclass[strlen(p->mainclass) - 6] = 0;
    }
}

static void scan_jar(Pkg *p)
{
    int i;
    ZipEnt *e;
    int classes = 0;
    if (!p->jar) return;
    if (zip_find(p->jar, "client.bin", 1)) p->kind = PKG_KTF_NATIVE;
    if (zip_find(p->jar, "binary.mod", 1)) p->kind = PKG_LGT_NATIVE;
    for (i = 0; i < p->jar->n; i++) {
        e = &p->jar->ents[i];
        if (ends_with(e->name, ".class")) {
            classes++;
            if (classes <= 60) {
                u32 len;
                int own;
                const u8 *d = zip_read(p->jar, e, &len, &own);
                if (d) {
                    u32 k;
                    for (k = 0; k + 9 < len; k++) {
                        if (d[k] == 'o' && !memcmp(d + k, "org/kwis/", 9)) p->uses_wipi = 1;
                        else if (d[k] == 'c' && k + 9 < len && !memcmp(d + k, "com/skt/m", 9)) p->uses_skt = 1;
                    }
                    if (own) free((void *)d);
                }
            }
        }
    }
    if (classes > 0 && (p->kind == PKG_KTF_NATIVE || p->kind == PKG_LGT_NATIVE)) p->kind = PKG_JAVA;  /* classes win */
    {
        u32 len;
        u8 *m = zip_extract(p->jar, "META-INF/MANIFEST.MF", &len);
        if (m) { parse_desc(p, m, len); free(m); }
    }
}

static void use_inner_jar(Pkg *p, Zip *z, ZipEnt *e)
{
    u32 len;
    int own;
    const u8 *d = zip_read(z, e, &len, &own);
    u8 *copy;
    if (!d) return;
    copy = (u8 *)malloc(len);
    memcpy(copy, d, len);
    if (own) free((void *)d);
    p->jarbuf = copy;
    p->jar = zip_open_mem(copy, len, 0);
}

static void read_container_descs(Pkg *p, Zip *z, const char *folder)
{
    static const char *names[] = { "__adf__", "app_info", NULL };
    int i;
    for (i = 0; names[i]; i++) {
        u32 len = 0;
        u8 *d = NULL;
        if (z) d = zip_extract(z, names[i], &len);
        else if (folder) { char fp[700]; snprintf(fp, sizeof fp, "%s/%s", folder, names[i]); d = pal_load(fp, &len); }
        if (d) {
            parse_desc(p, d, len);
            p->carrier_guess = i == 0 ? CARRIER_KTF : CARRIER_LGT;
            free(d);
        }
    }
    if (z) {
        for (i = 0; i < z->n; i++) {
            const char *n = z->ents[i].name;
            if (ends_with(n, ".msd") || ends_with(n, ".jad") || ends_with(n, ".adf")) {
                u32 len;
                u8 *d = zip_extract(z, n, &len);
                if (d) { parse_desc(p, d, len); free(d); }
                if (ends_with(n, ".msd")) p->carrier_guess = CARRIER_SKT;
                if (ends_with(n, ".adf")) p->carrier_guess = CARRIER_KTF;
            }
        }
    }
}

typedef struct { Pkg *p; char jar[512]; int has_desc; } DirScan;
static void dir_cb(void *u, const char *name, int is_dir, u32 size)
{
    DirScan *ds = (DirScan *)u;
    char fp[1024];
    (void)size;
    if (is_dir) return;
    snprintf(fp, sizeof fp, "%s/%s", ds->p->folder, name);
    if (ends_with(name, ".jar") && !ds->jar[0]) snprintf(ds->jar, sizeof ds->jar, "%s", fp);
    if (ends_with(name, ".msd") || ends_with(name, ".jad") || ends_with(name, ".adf")) {
        u32 len;
        u8 *d = pal_load(fp, &len);
        if (d) { parse_desc(ds->p, d, len); free(d); }
        if (ends_with(name, ".msd")) ds->p->carrier_guess = CARRIER_SKT;
        if (ends_with(name, ".adf")) ds->p->carrier_guess = CARRIER_KTF;
    }
}

int pkg_open(Pkg *p, const char *path)
{
    memset(p, 0, sizeof *p);
    snprintf(p->path, sizeof p->path, "%s", path);
    p->carrier_guess = -1;
    p->kind = PKG_JAVA;
    if (ends_with(path, ".jar") || ends_with(path, ".zip") || ends_with(path, ".jad") || ends_with(path, ".msd")) {
        char jarpath[512];
        u32 len;
        u8 *d;
        snprintf(jarpath, sizeof jarpath, "%s", path);
        if (ends_with(path, ".jad") || ends_with(path, ".msd")) {
            u8 *dd = pal_load(path, &len);
            if (dd) { parse_desc(p, dd, len); free(dd); }
            if (ends_with(path, ".msd")) p->carrier_guess = CARRIER_SKT;
            strcpy(jarpath + strlen(jarpath) - 4, ".jar");
        } else if (ends_with(path, ".jar")) {
            /* sibling descriptors */
            static const char *ext[] = { ".msd", ".jad", ".adf" };
            int k;
            for (k = 0; k < 3; k++) {
                char sp[512];
                u8 *dd;
                snprintf(sp, sizeof sp, "%s", path);
                strcpy(sp + strlen(sp) - 4, ext[k]);
                dd = pal_load(sp, &len);
                if (dd) {
                    parse_desc(p, dd, len); free(dd);
                    if (k == 0) p->carrier_guess = CARRIER_SKT;
                    if (k == 2) p->carrier_guess = CARRIER_KTF;
                }
            }
        }
        d = pal_load(jarpath, &len);
        if (!d) return 0;
        p->outer = zip_open_mem(d, len, 1);
        if (!p->outer) { free(d); return 0; }
        if (ends_with(path, ".zip")) {
            int i;
            ZipEnt *best = NULL;
            read_container_descs(p, p->outer, NULL);
            for (i = 0; i < p->outer->n; i++) if (ends_with(p->outer->ents[i].name, ".jar")) { best = &p->outer->ents[i]; break; }
            if (best) use_inner_jar(p, p->outer, best);
            else p->jar = p->outer;          /* a zip that is itself the jar */
        } else p->jar = p->outer;
    } else {
        DirScan ds;
        u32 len;
        u8 *d;
        snprintf(p->folder, sizeof p->folder, "%s", path);
        memset(&ds, 0, sizeof ds);
        ds.p = p;
        read_container_descs(p, NULL, path);
        pal_listdir(path, dir_cb, &ds);
        if (!ds.jar[0]) return 0;
        d = pal_load(ds.jar, &len);
        if (!d) return 0;
        p->outer = zip_open_mem(d, len, 1);
        p->jar = p->outer;
        if (!p->jar) { free(d); return 0; }
    }
    scan_jar(p);
    apply_desc(p);
    if (p->kind == PKG_JAVA && !strcmp(p->mainclass, "Clet")) p->kind = PKG_CLET;
    if (p->carrier_guess < 0) p->carrier_guess = p->uses_skt ? CARRIER_SKT : p->uses_wipi ? CARRIER_KTF : CARRIER_SKT;
    if (!p->name[0]) {
        const char *b = base_name(path);
        char *dot;
        snprintf(p->name, sizeof p->name, "%s", b);
        dot = strrchr(p->name, '.');
        if (dot && dot != p->name) *dot = 0;
    }
    if (!p->id[0]) {
        const char *b = base_name(path);
        int i, o = 0;
        for (i = 0; b[i] && o < 60; i++) {
            char c = b[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') p->id[o++] = c;
            else if (c == '.') break;
            else if ((u8)c >= 0x80) { p->id[o++] = "0123456789abcdef"[((u8)c) >> 4]; }
        }
        if (!o) strcpy(p->id, "game");
        else p->id[o] = 0;
    }
    return 1;
}

void pkg_close(Pkg *p)
{
    if (p->jar && p->jar != p->outer) zip_close(p->jar);
    if (p->outer) zip_close(p->outer);
    free(p->jarbuf);
    memset(p, 0, sizeof *p);
}

u8 *pkg_resource(Pkg *p, const char *name, u32 *len)
{
    u8 *d;
    while (*name == '/') name++;
    d = zip_extract(p->jar, name, len);
    if (d) return d;
    if (p->outer && p->outer != p->jar) { d = zip_extract(p->outer, name, len); if (d) return d; }
    if (p->folder[0]) { char fp[800]; snprintf(fp, sizeof fp, "%s/%s", p->folder, name); d = pal_load(fp, len); if (d) return d; }
    /* some games ask for "res/x.png" when the jar has "x.png" or vice versa */
    {
        const char *b = base_name(name);
        if (b != name) { d = zip_extract(p->jar, b, len); if (d) return d; }
    }
    return NULL;
}

const char *pkg_kind_text(int kind)
{
    switch (kind) {
    case PKG_JAVA: return "Java";
    case PKG_KTF_NATIVE: return "KTF native";
    case PKG_LGT_NATIVE: return "LGT native";
    case PKG_CLET: return "WIPI-C";
    }
    return "?";
}

int pkg_probe(const char *path, char *name, int namemax, int *carrier, int *kind)
{
    Pkg *p = (Pkg *)malloc(sizeof(Pkg));
    int ok = pkg_open(p, path);
    if (ok) {
        snprintf(name, namemax, "%s", p->name);
        *carrier = p->carrier_guess;
        *kind = p->kind;
    }
    pkg_close(p);
    free(p);
    return ok;
}

/* exported for app.c */
const char *pkg_prop_first(Pkg *p, int i, const char **val)
{
    const char *q = p->props;
    int k;
    if (i >= p->nprops) return NULL;
    for (k = 0; k < i; k++) { size_t kl = strlen(q) + 1; q += kl + strlen(q + kl) + 1; }
    *val = q + strlen(q) + 1;
    return q;
}
