/* raw DEFLATE decoder + ZIP (JAR) archive reader */
#include "zip.h"
#include <stdlib.h>
#include <string.h>

/* ---------------- inflate ---------------- */
typedef struct { u16 counts[16]; u16 symbols[288]; } Huff;
typedef struct {
    const u8 *src, *send;
    u32 bitbuf; int bitcnt;
    u8 *dst; size_t dpos, dcap;
    int grow;
    int err;
} Inf;

static int getbit(Inf *s)
{
    if (!s->bitcnt) {
        if (s->src >= s->send) { s->err = 1; return 0; }
        s->bitbuf = *s->src++;
        s->bitcnt = 8;
    }
    s->bitcnt--;
    {
        int b = s->bitbuf & 1;
        s->bitbuf >>= 1;
        return b;
    }
}

static u32 getbits(Inf *s, int n)
{
    u32 v = 0;
    int i;
    for (i = 0; i < n; i++) v |= (u32)getbit(s) << i;
    return v;
}

static void build(Huff *h, const u8 *lens, int n)
{
    u16 offs[16];
    int i, sum = 0;
    memset(h->counts, 0, sizeof h->counts);
    for (i = 0; i < n; i++) h->counts[lens[i]]++;
    h->counts[0] = 0;
    for (i = 0; i < 16; i++) { offs[i] = (u16)sum; sum += h->counts[i]; }
    for (i = 0; i < n; i++) if (lens[i]) h->symbols[offs[lens[i]]++] = (u16)i;
}

static int decode(Inf *s, Huff *h)
{
    int code = 0, first = 0, index = 0, len;
    for (len = 1; len < 16; len++) {
        int count;
        code |= getbit(s);
        count = h->counts[len];
        if (code - count < first) return h->symbols[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        if (s->err) return -1;
    }
    s->err = 1;
    return -1;
}

static int put(Inf *s, u8 b)
{
    if (s->dpos >= s->dcap) {
        if (!s->grow) { s->err = 2; return 0; }
        {
            size_t nc = s->dcap ? s->dcap * 2 : 65536;
            u8 *nd = (u8 *)realloc(s->dst, nc);
            if (!nd) { s->err = 3; return 0; }
            s->dst = nd; s->dcap = nc;
        }
    }
    s->dst[s->dpos++] = b;
    return 1;
}

static const u16 lbase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const u8 lext[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const u16 dbase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const u8 dext[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

static int codes(Inf *s, Huff *lh, Huff *dh)
{
    for (;;) {
        int sym = decode(s, lh);
        if (s->err) return 0;
        if (sym < 256) { if (!put(s, (u8)sym)) return 0; }
        else if (sym == 256) return 1;
        else {
            int len, dist;
            sym -= 257;
            if (sym >= 29) { s->err = 1; return 0; }
            len = lbase[sym] + (int)getbits(s, lext[sym]);
            sym = decode(s, dh);
            if (sym < 0 || sym >= 30) { s->err = 1; return 0; }
            dist = dbase[sym] + (int)getbits(s, dext[sym]);
            if ((size_t)dist > s->dpos) { s->err = 1; return 0; }
            while (len--) { if (!put(s, s->dst[s->dpos - dist])) return 0; }
        }
    }
}

static int inflate_run(Inf *s)
{
    static Huff fl, fd;
    static int fixed_ready;
    int last;
    do {
        int type;
        last = getbit(s);
        type = (int)getbits(s, 2);
        if (s->err) return 0;
        if (type == 0) {
            u32 len;
            s->bitcnt = 0;
            if (s->src + 4 > s->send) return 0;
            len = s->src[0] | (s->src[1] << 8);
            s->src += 4;
            if (s->src + len > s->send) return 0;
            while (len--) if (!put(s, *s->src++)) return 0;
        } else if (type == 1) {
            if (!fixed_ready) {
                u8 l[288];
                int i;
                for (i = 0; i < 144; i++) l[i] = 8;
                for (; i < 256; i++) l[i] = 9;
                for (; i < 280; i++) l[i] = 7;
                for (; i < 288; i++) l[i] = 8;
                build(&fl, l, 288);
                for (i = 0; i < 30; i++) l[i] = 5;
                build(&fd, l, 30);
                fixed_ready = 1;
            }
            if (!codes(s, &fl, &fd)) return 0;
        } else if (type == 2) {
            static const u8 ord[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
            u8 lens[320];
            Huff lh, dh;
            int nlen = (int)getbits(s, 5) + 257, ndist = (int)getbits(s, 5) + 1, ncode = (int)getbits(s, 4) + 4, i;
            if (nlen > 286 || ndist > 30) return 0;
            memset(lens, 0, sizeof lens);
            for (i = 0; i < ncode; i++) lens[ord[i]] = (u8)getbits(s, 3);
            build(&lh, lens, 19);
            for (i = 0; i < nlen + ndist;) {
                int sym = decode(s, &lh), rep = 0;
                u8 v = 0;
                if (s->err || sym < 0) return 0;
                if (sym < 16) { lens[i++] = (u8)sym; continue; }
                if (sym == 16) { if (!i) return 0; v = lens[i - 1]; rep = 3 + (int)getbits(s, 2); }
                else if (sym == 17) rep = 3 + (int)getbits(s, 3);
                else rep = 11 + (int)getbits(s, 7);
                if (i + rep > nlen + ndist) return 0;
                while (rep--) lens[i++] = v;
            }
            build(&lh, lens, nlen);
            build(&dh, lens + nlen, ndist);
            if (!codes(s, &lh, &dh)) return 0;
        } else return 0;
    } while (!last);
    return !s->err;
}

int inflate_raw(const u8 *src, size_t n, u8 *dst, size_t cap, size_t *outlen)
{
    Inf s;
    memset(&s, 0, sizeof s);
    s.src = src; s.send = src + n; s.dst = dst; s.dcap = cap;
    if (!inflate_run(&s)) return 0;
    if (outlen) *outlen = s.dpos;
    return 1;
}

u8 *inflate_alloc(const u8 *src, size_t n, size_t *outlen)
{
    Inf s;
    memset(&s, 0, sizeof s);
    s.src = src; s.send = src + n; s.grow = 1;
    if (!inflate_run(&s)) { free(s.dst); return NULL; }
    *outlen = s.dpos;
    return s.dst;
}

/* zlib stream (PNG) */
u8 *zlib_alloc(const u8 *src, size_t n, size_t *outlen)
{
    if (n < 2 || (src[0] & 15) != 8) return NULL;
    return inflate_alloc(src + 2, n - 2, outlen);
}

/* ---------------- ZIP ---------------- */
static u32 le32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
static u16 le16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }

Zip *zip_open_mem(const u8 *data, size_t len, int owned)
{
    Zip *z;
    long i;
    const u8 *eocd = NULL;
    u32 cdoff, cdn, k;
    const u8 *p;
    if (len < 22) return NULL;
    for (i = (long)len - 22; i >= 0 && i >= (long)len - 65557; i--)
        if (le32(data + i) == 0x06054b50) { eocd = data + i; break; }
    if (!eocd) return NULL;
    cdn = le16(eocd + 10);
    cdoff = le32(eocd + 16);
    /* SKVM jars can carry a header in front: shift offsets so the central directory lines up */
    {
        u32 cdsize = le32(eocd + 12);
        long real = (long)(eocd - data) - (long)cdsize;
        long shift = real - (long)cdoff;
        z = (Zip *)calloc(1, sizeof(Zip));
        z->shift = shift;
    }
    z->data = data; z->len = len; z->owned = owned;
    z->ents = (ZipEnt *)calloc(cdn + 1, sizeof(ZipEnt));
    p = data + cdoff + z->shift;
    for (k = 0; k < cdn; k++) {
        ZipEnt *e;
        u16 nl, xl, cl;
        if (p < data || p + 46 > data + len || le32(p) != 0x02014b50) break;
        e = &z->ents[z->n];
        e->method = le16(p + 10);
        e->csize = le32(p + 20);
        e->usize = le32(p + 24);
        nl = le16(p + 28); xl = le16(p + 30); cl = le16(p + 32);
        e->lhoff = le32(p + 42);
        if (p + 46 + nl > data + len) break;
        e->name = (char *)malloc(nl + 1);
        memcpy(e->name, p + 46, nl);
        e->name[nl] = 0;
        p += 46 + nl + xl + cl;
        if (nl && e->name[nl - 1] == '/') { free(e->name); continue; }
        z->n++;
    }
    return z;
}

void zip_close(Zip *z)
{
    int i;
    if (!z) return;
    for (i = 0; i < z->n; i++) free(z->ents[i].name);
    free(z->ents);
    if (z->owned) free((void *)z->data);
    free(z);
}

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}

ZipEnt *zip_find(Zip *z, const char *name, int nocase)
{
    int i;
    if (!z) return NULL;
    while (*name == '/') name++;
    for (i = 0; i < z->n; i++)
        if (nocase ? ieq(z->ents[i].name, name) : !strcmp(z->ents[i].name, name)) return &z->ents[i];
    return NULL;
}

/* returns pointer to data; *own set when the caller must free it */
const u8 *zip_read(Zip *z, ZipEnt *e, u32 *len, int *own)
{
    const u8 *lh = z->data + e->lhoff + z->shift, *d;
    *own = 0;
    if (lh < z->data || lh + 30 > z->data + z->len || le32(lh) != 0x04034b50) return NULL;
    d = lh + 30 + le16(lh + 26) + le16(lh + 28);
    if (d + e->csize > z->data + z->len) return NULL;
    if (e->method == 0) { *len = e->csize; return d; }
    if (e->method == 8) {
        u8 *out = (u8 *)malloc(e->usize + 1);
        size_t got = 0;
        if (!out) return NULL;
        if (!inflate_raw(d, e->csize, out, e->usize, &got)) { free(out); return NULL; }
        *len = (u32)got;
        *own = 1;
        return out;
    }
    return NULL;
}

u8 *zip_extract(Zip *z, const char *name, u32 *len)
{
    ZipEnt *e = zip_find(z, name, 0);
    const u8 *d;
    int own;
    u8 *r;
    if (!e) e = zip_find(z, name, 1);
    if (!e) return NULL;
    d = zip_read(z, e, len, &own);
    if (!d) return NULL;
    if (own) return (u8 *)d;
    r = (u8 *)malloc(*len + 1);
    if (r) memcpy(r, d, *len);
    return r;
}
