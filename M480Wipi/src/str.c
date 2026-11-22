/* java.lang.String helpers + EUC-KR (CP949) conversion */
#include "jvm.h"
#include <stdlib.h>
#include <stdio.h>
#include "ksc_table.h"

/* ---------------- CP949 ---------------- */
static u16 *rev;     /* unicode -> cp949 code (lead<<8|trail), 64K entries, built lazily */

int ksc_to_u16(const u8 *in, int n, u16 *out)
{
    int i = 0, o = 0;
    while (i < n) {
        u8 c = in[i];
        if (c < 0x80) { out[o++] = c; i++; continue; }
        if (c >= 0x81 && c <= 0xFE && i + 1 < n && in[i + 1] >= 0x41 && in[i + 1] <= 0xFE) {
            u16 u = ksc_tab[c - 0x81][in[i + 1] - 0x41];
            out[o++] = u ? u : 0xFFFD;
            i += 2;
            continue;
        }
        out[o++] = 0xFFFD;
        i++;
    }
    return o;
}

static void build_rev(void)
{
    int a, b;
    rev = (u16 *)calloc(65536, sizeof(u16));
    if (!rev) return;
    for (a = 125; a >= 0; a--)
        for (b = 189; b >= 0; b--) {
            u16 u = ksc_tab[a][b];
            if (u) rev[u] = (u16)(((a + 0x81) << 8) | (b + 0x41));
        }
}

int u16_to_ksc(const u16 *in, int n, u8 *out)
{
    int i, o = 0;
    for (i = 0; i < n; i++) {
        u16 c = in[i];
        if (c < 0x80) { out[o++] = (u8)c; continue; }
        if (!rev) build_rev();
        if (rev && rev[c]) { out[o++] = (u8)(rev[c] >> 8); out[o++] = (u8)rev[c]; }
        else out[o++] = '?';
    }
    return o;
}

/* ---------------- strings ---------------- */
int str_len(Object *s) { return s ? OBJ_FIELDS(s)[F_String_count].i : 0; }

const u16 *str_chars(Object *s)
{
    Array *v = (Array *)OBJ_FIELDS(s)[F_String_value].l;
    return v ? ARR_C(v) + OBJ_FIELDS(s)[F_String_offset].i : NULL;
}

Object *str_new_utf16(Thread *t, const u16 *s, int n)
{
    Array *a = arr_newp(t, 'C', n);
    Object *o;
    if (!a) return NULL;
    if (n) memcpy(ARR_C(a), s, n * 2);
    troot(&a->o);
    o = obj_new(t, C_String);
    if (!o) return NULL;
    OBJ_FIELDS(o)[F_String_value].l = &a->o;
    OBJ_FIELDS(o)[F_String_offset].i = 0;
    OBJ_FIELDS(o)[F_String_count].i = n;
    return o;
}

/* modified UTF-8 (class files) or plain UTF-8 */
Object *str_new_utf8(Thread *t, const char *s)
{
    const u8 *p = (const u8 *)s;
    int n = (int)strlen(s), o = 0, i = 0;
    u16 sbuf[256], *buf = n <= 256 ? sbuf : (u16 *)malloc(n * 2);
    Object *r;
    while (i < n) {
        u8 c = p[i];
        if (c < 0x80) { buf[o++] = c; i++; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < n) { buf[o++] = (u16)(((c & 0x1F) << 6) | (p[i + 1] & 0x3F)); i += 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < n) { buf[o++] = (u16)(((c & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F)); i += 3; }
        else if ((c & 0xF8) == 0xF0 && i + 3 < n) {
            u32 cp = ((u32)(c & 7) << 18) | ((u32)(p[i + 1] & 0x3F) << 12) | ((p[i + 2] & 0x3F) << 6) | (p[i + 3] & 0x3F);
            cp -= 0x10000;
            buf[o++] = (u16)(0xD800 | (cp >> 10)); buf[o++] = (u16)(0xDC00 | (cp & 0x3FF)); i += 4;
        } else { buf[o++] = c; i++; }
    }
    r = str_new_utf16(t, buf, o);
    if (buf != sbuf) free(buf);
    return r;
}

Object *str_new_ksc(Thread *t, const char *s, int n)
{
    u16 sbuf[256], *buf = n <= 256 ? sbuf : (u16 *)malloc(n * 2 + 2);
    int k = ksc_to_u16((const u8 *)s, n, buf);
    Object *r = str_new_utf16(t, buf, k);
    if (buf != sbuf) free(buf);
    return r;
}

char *str_to_utf8(Object *s, char *buf, int max)
{
    int n = str_len(s), i, o = 0;
    const u16 *c = s ? str_chars(s) : NULL;
    for (i = 0; i < n && c; i++) {
        u16 ch = c[i];
        if (ch < 0x80) { if (o + 1 >= max) break; buf[o++] = (char)ch; }
        else if (ch < 0x800) { if (o + 2 >= max) break; buf[o++] = (char)(0xC0 | (ch >> 6)); buf[o++] = (char)(0x80 | (ch & 0x3F)); }
        else { if (o + 3 >= max) break; buf[o++] = (char)(0xE0 | (ch >> 12)); buf[o++] = (char)(0x80 | ((ch >> 6) & 0x3F)); buf[o++] = (char)(0x80 | (ch & 0x3F)); }
    }
    buf[o] = 0;
    return buf;
}

int str_eq_c(Object *s, const char *c)
{
    int n = str_len(s), i;
    const u16 *p = str_chars(s);
    if ((int)strlen(c) != n) return 0;
    for (i = 0; i < n; i++) if (p[i] != (u8)c[i]) return 0;
    return 1;
}

/* intern table: open hashing on content */
#define INTH 2048
static Object **intern_tab;
static int intern_n, intern_cap;

static u32 str_hash(Object *s)
{
    int n = str_len(s), i;
    const u16 *p = str_chars(s);
    u32 h = 0;
    for (i = 0; i < n; i++) h = h * 31 + p[i];
    return h;
}

static int str_equal(Object *a, Object *b)
{
    int n = str_len(a);
    if (n != str_len(b)) return 0;
    return n == 0 || !memcmp(str_chars(a), str_chars(b), n * 2);
}

static void intern_grow(void)
{
    int oc = intern_cap, i;
    Object **old = intern_tab;
    intern_cap = oc ? oc * 2 : INTH;
    intern_tab = (Object **)calloc(intern_cap, sizeof(Object *));
    for (i = 0; i < oc; i++) {
        if (old[i]) {
            u32 h = str_hash(old[i]) & (intern_cap - 1);
            while (intern_tab[h]) h = (h + 1) & (intern_cap - 1);
            intern_tab[h] = old[i];
        }
    }
    free(old);
}

Object *str_intern(Thread *t, Object *s)
{
    u32 h;
    (void)t;
    if (!s) return NULL;
    if (intern_n * 2 >= intern_cap) intern_grow();
    h = str_hash(s) & (intern_cap - 1);
    while (intern_tab[h]) {
        if (str_equal(intern_tab[h], s)) return intern_tab[h];
        h = (h + 1) & (intern_cap - 1);
    }
    intern_tab[h] = s;
    intern_n++;
    return s;
}

/* called from GC root marking (see native.c) */
void str_intern_mark(void (*mark)(Object *))
{
    int i;
    for (i = 0; i < intern_cap; i++) if (intern_tab[i]) mark(intern_tab[i]);
}
