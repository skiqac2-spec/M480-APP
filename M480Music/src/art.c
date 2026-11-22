/* art.c - ID3 태그(제목/가수/앨범)와 앨범아트 읽기
 * 커버 우선순위: MP3 안의 APIC(앞표지) → 같은 폴더의 cover/folder/front/AlbumArt*.jpg */
#include <stdlib.h>
#include <string.h>
#include "art.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(x) ((void)0)
#include "stb_image.h"

#define MAX_TAG (4 * 1024 * 1024)
#define MAX_IMG_PIXELS (1600 * 1600)

/* ------------------------------------------------------------ 문자열 */
static void utf8_to_w(const unsigned char *s, int n, wchar_t *o, int max)
{
    int i = 0, k = 0;
    while (i < n && s[i] && k < max - 1) {
        unsigned c = s[i];
        if (c < 0x80) { o[k++] = (wchar_t)c; i++; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < n) { o[k++] = (wchar_t)(((c & 0x1F) << 6) | (s[i + 1] & 0x3F)); i += 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < n) { o[k++] = (wchar_t)(((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F)); i += 3; }
        else { i++; if ((c & 0xF8) == 0xF0) i += 3; o[k++] = L'?'; }
    }
    o[k] = 0;
}

static void id3_text(const unsigned char *d, int n, wchar_t *o, int max)
{
    int enc, k = 0, i;
    o[0] = 0;
    if (n < 2) return;
    enc = d[0]; d++; n--;
    if (enc == 0) {
        /* 한국 MP3는 'ISO-8859-1' 칸에 CP949로 저장된 경우가 대부분 → 시스템 코드페이지로 */
        int len = 0;
        while (len < n && d[len]) len++;
        k = MultiByteToWideChar(CP_ACP, 0, (const char *)d, len, o, max - 1);
        if (k < 0) k = 0;
        o[k] = 0;
    } else if (enc == 3) {
        utf8_to_w(d, n, o, max);
    } else {
        int be = (enc == 2);
        i = 0;
        if (enc == 1 && n >= 2) {
            if (d[0] == 0xFE && d[1] == 0xFF) { be = 1; i = 2; }
            else if (d[0] == 0xFF && d[1] == 0xFE) { be = 0; i = 2; }
        }
        for (; i + 1 < n && k < max - 1; i += 2) {
            wchar_t c = be ? (wchar_t)((d[i] << 8) | d[i + 1]) : (wchar_t)(d[i] | (d[i + 1] << 8));
            if (!c) break;
            if (c == 0xFEFF) continue;
            o[k++] = c;
        }
        o[k] = 0;
    }
    /* 앞뒤 공백 정리 */
    while (k > 0 && (o[k - 1] == L' ' || o[k - 1] == L'\r' || o[k - 1] == L'\n')) o[--k] = 0;
    if (o[0] == L' ') { i = 0; while (o[i] == L' ') i++; memmove(o, o + i, (wcslen(o + i) + 1) * sizeof(wchar_t)); }
}

/* ------------------------------------------------------------ ID3v2 */
static unsigned long ss32(const unsigned char *p)
{
    return ((unsigned long)(p[0] & 0x7F) << 21) | ((p[1] & 0x7F) << 14) | ((p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}
static unsigned long be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | (p[2] << 8) | p[3];
}

static int unsync(unsigned char *d, int n)
{
    int i, k = 0;
    for (i = 0; i < n; i++) {
        d[k++] = d[i];
        if (d[i] == 0xFF && i + 1 < n && d[i + 1] == 0x00) i++;
    }
    return k;
}

/* 그림 프레임에서 이미지 데이터 위치 */
static int pic_data(const unsigned char *d, int n, int v22, int *type, const unsigned char **img)
{
    int enc, i = 1;
    if (n < 4) return 0;
    enc = d[0];
    if (v22) i = 4;
    else { while (i < n && d[i]) i++; i++; }       /* MIME */
    if (i >= n) return 0;
    *type = d[i++];
    if (enc == 1 || enc == 2) {                     /* UTF-16 설명: 00 00 로 끝 */
        while (i + 1 < n && (d[i] || d[i + 1])) i += 2;
        i += 2;
    } else {
        while (i < n && d[i]) i++;
        i++;
    }
    if (i >= n) return 0;
    *img = d + i;
    return n - i;
}

typedef struct { const unsigned char *img; int len; int type; } Pic;

static unsigned char *read_tag(HANDLE h, int *outLen, int *ver)
{
    unsigned char hdr[10];
    DWORD got;
    unsigned long size;
    unsigned char *buf;
    if (!ReadFile(h, hdr, 10, &got, NULL) || got != 10 || memcmp(hdr, "ID3", 3)) return NULL;
    size = ss32(hdr + 6);
    if (size < 10 || size > MAX_TAG) return NULL;
    buf = (unsigned char *)malloc(size + 16);
    if (!buf) return NULL;
    if (!ReadFile(h, buf, size, &got, NULL)) { free(buf); return NULL; }
    size = got;
    *ver = hdr[3];
    if ((hdr[5] & 0x80) && hdr[3] < 4) size = (unsigned long)unsync(buf, (int)size);
    if (hdr[5] & 0x40) {
        /* 확장 헤더 건너뛰기 */
        unsigned long es = (hdr[3] >= 4) ? ss32(buf) : be32(buf) + 4;
        if (es < size) { memmove(buf, buf + es, size - es); size -= es; }
    }
    *outLen = (int)size;
    return buf;
}

static void parse_tag(unsigned char *buf, int n, int ver, ArtResult *r, Pic *pic)
{
    int pos = 0;
    int v22 = (ver == 2);
    int hlen = v22 ? 6 : 10;
    while (pos + hlen <= n) {
        unsigned char *f = buf + pos;
        unsigned long fs;
        int flags = 0;
        unsigned char *d;
        int dn;
        if (f[0] == 0) break;
        if (v22) fs = ((unsigned long)f[3] << 16) | (f[4] << 8) | f[5];
        else {
            fs = (ver >= 4) ? ss32(f + 4) : be32(f + 4);
            /* 일부 프로그램은 v2.4에도 일반 정수 크기를 씀 */
            if (ver >= 4 && pos + hlen + (int)fs < n) {
                unsigned char *nx = f + hlen + fs;
                if (!(nx[0] == 0 || (nx[0] >= 'A' && nx[0] <= 'Z'))) {
                    unsigned long alt = be32(f + 4);
                    if (pos + hlen + (long)alt <= n) fs = alt;
                }
            }
            flags = (f[8] << 8) | f[9];
        }
        if (fs == 0 || pos + hlen + (long)fs > n) break;
        d = f + hlen;
        dn = (int)fs;
        if (ver >= 4) {
            if (flags & 0x0001) { d += 4; dn -= 4; }               /* 데이터 길이 표시 */
            if (flags & 0x0002) dn = unsync(d, dn);
        }
        if (dn > 0) {
            if (!memcmp(f, v22 ? "TT2" : "TIT2", v22 ? 3 : 4)) id3_text(d, dn, r->title, 128);
            else if (!memcmp(f, v22 ? "TP1" : "TPE1", v22 ? 3 : 4)) id3_text(d, dn, r->artist, 128);
            else if (!memcmp(f, v22 ? "TAL" : "TALB", v22 ? 3 : 4)) id3_text(d, dn, r->album, 128);
            else if (!memcmp(f, v22 ? "PIC" : "APIC", v22 ? 3 : 4)) {
                const unsigned char *img;
                int type = 0;
                int len = pic_data(d, dn, v22, &type, &img);
                if (len > 100 && (!pic->img || (type == 3 && pic->type != 3))) {
                    pic->img = img; pic->len = len; pic->type = type;
                }
            }
        }
        pos += hlen + (int)fs;
    }
}

/* ------------------------------------------------------------ 이미지 */
static unsigned short *scale565(const unsigned char *px, int w, int h, int t, unsigned long *sr, unsigned long *sg, unsigned long *sb)
{
    int cw, ox, oy, x, y;
    unsigned short *out = (unsigned short *)malloc((size_t)t * t * 2);
    if (!out) return NULL;
    cw = w < h ? w : h;                 /* 가운데 정사각형으로 자르기 */
    ox = (w - cw) / 2; oy = (h - cw) / 2;
    for (y = 0; y < t; y++) {
        int sy0 = oy + (int)((long)y * cw / t), sy1 = oy + (int)((long)(y + 1) * cw / t);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (x = 0; x < t; x++) {
            int sx0 = ox + (int)((long)x * cw / t), sx1 = ox + (int)((long)(x + 1) * cw / t);
            unsigned r = 0, g = 0, b = 0, c = 0;
            int sx, sy, step = 1;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if ((sx1 - sx0) * (sy1 - sy0) > 64) step = 2;   /* 큰 그림은 샘플 수 줄임 */
            for (sy = sy0; sy < sy1 && sy < h; sy += step)
                for (sx = sx0; sx < sx1 && sx < w; sx += step) {
                    const unsigned char *p = px + ((long)sy * w + sx) * 3;
                    r += p[0]; g += p[1]; b += p[2]; c++;
                }
            if (!c) c = 1;
            r /= c; g /= c; b /= c;
            if (sr) { *sr += r; *sg += g; *sb += b; }
            out[y * t + x] = (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
    return out;
}

static int decode_img(const unsigned char *data, int len, ArtResult *r)
{
    int w, h, comp;
    unsigned char *px;
    unsigned long sr = 0, sg = 0, sb = 0;
    if (!stbi_info_from_memory(data, len, &w, &h, &comp)) return 0;
    if (w < 8 || h < 8 || (long)w * h > MAX_IMG_PIXELS) return 0;
    px = stbi_load_from_memory(data, len, &w, &h, &comp, 3);
    if (!px) return 0;
    r->bigPx = scale565(px, w, h, r->bigSz, &sr, &sg, &sb);
    r->smallPx = scale565(px, w, h, r->smallSz, NULL, NULL, NULL);
    stbi_image_free(px);
    if (r->bigPx) {
        unsigned long n = (unsigned long)r->bigSz * r->bigSz;
        r->avg = RGB(sr / n, sg / n, sb / n);
    }
    return r->bigPx != NULL;
}

static unsigned char *read_whole(const wchar_t *path, int *len)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD sz, got = 0;
    unsigned char *b;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    sz = GetFileSize(h, NULL);
    if (sz < 100 || sz > MAX_TAG) { CloseHandle(h); return NULL; }
    b = (unsigned char *)malloc(sz);
    if (b) ReadFile(h, b, sz, &got, NULL);
    CloseHandle(h);
    if (b && got != sz) { free(b); return NULL; }
    *len = (int)sz;
    return b;
}

/* 같은 폴더의 커버 이미지 */
static int folder_cover(const wchar_t *path, ArtResult *r)
{
    static const wchar_t *names[] = { L"cover.jpg", L"folder.jpg", L"front.jpg", L"cover.png", L"folder.png", L"AlbumArt*Large.jpg", L"AlbumArt*.jpg" };
    wchar_t dir[MAX_PATH], p[MAX_PATH];
    wchar_t *s;
    int i;
    if (wcslen(path) >= MAX_PATH - 32) return 0;
    wcscpy(dir, path);
    s = wcsrchr(dir, L'\\');
    if (!s) return 0;
    s[1] = 0;
    for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
        WIN32_FIND_DATAW fd;
        HANDLE h;
        wcscpy(p, dir); wcscat(p, names[i]);
        h = FindFirstFileW(p, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            int len = 0, ok = 0;
            unsigned char *b;
            FindClose(h);
            wcscpy(p, dir); wcscat(p, fd.cFileName);
            b = read_whole(p, &len);
            if (b) { ok = decode_img(b, len, r); free(b); }
            if (ok) return 1;
        }
    }
    return 0;
}

ArtResult *art_load(const wchar_t *path, int bigSz, int smallSz)
{
    ArtResult *r = (ArtResult *)calloc(1, sizeof(ArtResult));
    HANDLE h;
    if (!r) return NULL;
    r->bigSz = bigSz; r->smallSz = smallSz;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        int n = 0, ver = 3;
        unsigned char *tag = read_tag(h, &n, &ver);
        CloseHandle(h);
        if (tag) {
            Pic pic;
            memset(&pic, 0, sizeof(pic));
            parse_tag(tag, n, ver, r, &pic);
            if (pic.img) decode_img(pic.img, pic.len, r);
            free(tag);
        }
    }
    if (!r->bigPx) folder_cover(path, r);
    return r;
}

void art_free(ArtResult *r)
{
    if (!r) return;
    free(r->bigPx);
    free(r->smallPx);
    free(r);
}
