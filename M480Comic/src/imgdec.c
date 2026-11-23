/* M480Comic - image decoding and scaling to RGB565 */
#ifdef _WIN32
#include <windows.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "imgdec.h"

/* ------------------------------------------------------------------ */
/* allocator: blocks of 1MB+ use VirtualAlloc reservations of 2MB+,    */
/* which Windows CE 5 places in the large shared area (not in the     */
/* 32MB process slot).                                                 */
/* ------------------------------------------------------------------ */
#define BIG_MAGIC_HEAP 0x4D484541u
#define BIG_MAGIC_VIRT 0x4D564952u
#define BIG_HDR 16

typedef struct { unsigned magic; unsigned pad; size_t size; } BigHdr;

void *big_malloc(size_t n)
{
	BigHdr *h;
	size_t tot = n + BIG_HDR;
	if (tot < n) return NULL;
#if defined(_WIN32_WCE)
	if (n >= 1024 * 1024) {
		size_t res = tot < 0x220000 ? 0x220000 : ((tot + 0xFFFF) & ~(size_t)0xFFFF);
		void *p = VirtualAlloc(NULL, res, MEM_RESERVE, PAGE_NOACCESS);
		if (p) {
			if (!VirtualAlloc(p, tot, MEM_COMMIT, PAGE_READWRITE)) { VirtualFree(p, 0, MEM_RELEASE); return NULL; }
			h = (BigHdr *)p;
			h->magic = BIG_MAGIC_VIRT;
			h->size = n;
			return (char *)p + BIG_HDR;
		}
		return NULL;
	}
#endif
	h = (BigHdr *)malloc(tot);
	if (!h) return NULL;
	h->magic = BIG_MAGIC_HEAP;
	h->size = n;
	return (char *)h + BIG_HDR;
}

void big_free(void *p)
{
	BigHdr *h;
	if (!p) return;
	h = (BigHdr *)((char *)p - BIG_HDR);
#if defined(_WIN32_WCE)
	if (h->magic == BIG_MAGIC_VIRT) { h->magic = 0; VirtualFree(h, 0, MEM_RELEASE); return; }
#endif
	h->magic = 0;
	free(h);
}

void *big_realloc(void *p, size_t n)
{
	BigHdr *h;
	void *q;
	if (!p) return big_malloc(n);
	h = (BigHdr *)((char *)p - BIG_HDR);
	if (n <= h->size && (h->magic == BIG_MAGIC_VIRT || n + 4096 > h->size)) return p;
	if (h->magic == BIG_MAGIC_HEAP && n < 1024 * 1024) {
		BigHdr *nh = (BigHdr *)realloc(h, n + BIG_HDR);
		if (!nh) return NULL;
		nh->size = n;
		return (char *)nh + BIG_HDR;
	}
	q = big_malloc(n);
	if (!q) return NULL;
	memcpy(q, p, h->size < n ? h->size : n);
	big_free(p);
	return q;
}

/* ------------------------------------------------------------------ */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(x) ((void)0)
#define STBI_MALLOC(sz) big_malloc(sz)
#define STBI_REALLOC(p, sz) big_realloc(p, sz)
#define STBI_FREE(p) big_free(p)
#define STBI_MAX_DIMENSIONS 32768
#include "stb_image.h"

#define SIMPLEWEBP_IMPLEMENTATION
#include "simplewebp.h"

static void *wp_alloc(void *u, size_t n) { (void)u; return big_malloc(n); }
static void wp_free(void *u, void *p) { (void)u; big_free(p); }
static simplewebp_allocator g_wpa = { wp_alloc, wp_free, NULL };

static int is_webp(const unsigned char *d, int len)
{
	return len > 16 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4);
}

int imgdec_info(const unsigned char *d, int len, int *w, int *h)
{
	int comp;
	if (!d || len < 8) return 0;
	if (is_webp(d, len)) {
		simplewebp *wp = NULL;
		size_t ww, hh;
		if (simplewebp_load_from_memory((void *)d, (size_t)len, &g_wpa, &wp) != SIMPLEWEBP_NO_ERROR) return 0;
		simplewebp_get_dimensions(wp, &ww, &hh);
		simplewebp_unload(wp);
		*w = (int)ww; *h = (int)hh;
		return ww > 0 && hh > 0;
	}
	if (!stbi_info_from_memory(d, len, w, h, &comp)) return 0;
	return *w > 0 && *h > 0;
}

/* decode to packed RGB24 (3 bytes/pixel) */
static unsigned char *decode_rgb(const unsigned char *d, int len, int *w, int *h, char *err, int errlen)
{
	if (is_webp(d, len)) {
		simplewebp *wp = NULL;
		size_t ww, hh;
		unsigned char *px;
		simplewebp_error e = simplewebp_load_from_memory((void *)d, (size_t)len, &g_wpa, &wp);
		if (e != SIMPLEWEBP_NO_ERROR) { _snprintf(err, errlen, "WebP 오류"); return NULL; }
		simplewebp_get_dimensions(wp, &ww, &hh);
		if ((long)ww * (long)hh > IMG_MAX_SRC_PIXELS) { simplewebp_unload(wp); _snprintf(err, errlen, "너무 큰 이미지 (%dx%d)", (int)ww, (int)hh); return NULL; }
		px = (unsigned char *)big_malloc(ww * hh * 3 + 16);
		if (!px) { simplewebp_unload(wp); _snprintf(err, errlen, "메모리 부족"); return NULL; }
		e = simplewebp_decode24(wp, px, NULL);
		simplewebp_unload(wp);
		if (e != SIMPLEWEBP_NO_ERROR) { big_free(px); _snprintf(err, errlen, "WebP 해독 실패"); return NULL; }
		*w = (int)ww; *h = (int)hh;
		return px;
	} else {
		int comp;
		unsigned char *px;
		if (!stbi_info_from_memory(d, len, w, h, &comp)) { _snprintf(err, errlen, "지원하지 않는 형식"); return NULL; }
		if ((long)*w * *h > IMG_MAX_SRC_PIXELS) { _snprintf(err, errlen, "너무 큰 이미지 (%dx%d)", *w, *h); return NULL; }
		px = stbi_load_from_memory(d, len, w, h, &comp, 3);
		if (!px) { _snprintf(err, errlen, "해독 실패 (%s)", stbi_failure_reason() ? stbi_failure_reason() : "?"); return NULL; }
		return px;
	}
}

#define RGB565(r, g, b) (unsigned short)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

unsigned short *imgdec_decode(const unsigned char *d, int len, int tw, int *th_out, char *err, int errlen)
{
	int w, h, th, x, y;
	unsigned char *px;
	unsigned short *out;
	int *xs0, *xs1;
	err[0] = 0;
	px = decode_rgb(d, len, &w, &h, err, errlen);
	if (!px) return NULL;
	if (tw < 1) tw = 1;
	th = (int)(((long long)h * tw + w / 2) / w);
	if (th < 1) th = 1;
	out = (unsigned short *)big_malloc((size_t)tw * th * 2);
	xs0 = (int *)malloc(sizeof(int) * (tw + 1) * 2);
	if (!out || !xs0) {
		big_free(out); free(xs0); big_free(px);
		_snprintf(err, errlen, "메모리 부족");
		return NULL;
	}
	xs1 = xs0 + tw + 1;
	if (tw <= w) {
		/* downscale: box filter (at most 4x4 samples per pixel) */
		for (x = 0; x < tw; x++) {
			xs0[x] = (int)((long long)x * w / tw);
			xs1[x] = (int)((long long)(x + 1) * w / tw);
			if (xs1[x] <= xs0[x]) xs1[x] = xs0[x] + 1;
			if (xs1[x] > w) xs1[x] = w;
		}
		for (y = 0; y < th; y++) {
			int sy0 = (int)((long long)y * h / th), sy1 = (int)((long long)(y + 1) * h / th);
			int stepy;
			unsigned short *o = out + (size_t)y * tw;
			if (sy1 <= sy0) sy1 = sy0 + 1;
			if (sy1 > h) sy1 = h;
			stepy = (sy1 - sy0 + 3) / 4; if (stepy < 1) stepy = 1;
			for (x = 0; x < tw; x++) {
				unsigned r = 0, g = 0, b = 0, n = 0;
				int sx, sy, sx0 = xs0[x], sx1 = xs1[x];
				int stepx = (sx1 - sx0 + 3) / 4;
				if (stepx < 1) stepx = 1;
				for (sy = sy0; sy < sy1; sy += stepy) {
					const unsigned char *p = px + ((size_t)sy * w + sx0) * 3;
					for (sx = sx0; sx < sx1; sx += stepx, p += 3 * stepx) { r += p[0]; g += p[1]; b += p[2]; n++; }
				}
				o[x] = RGB565(r / n, g / n, b / n);
			}
		}
	} else {
		/* upscale: bilinear, 8.8 fixed point */
		for (x = 0; x < tw; x++) {
			long fx = (long)(((long long)x * 256 * w) / tw) + (128 * w) / tw - 128;
			if (fx < 0) fx = 0;
			xs0[x] = (int)(fx >> 8);
			xs1[x] = (int)(fx & 255);
			if (xs0[x] >= w - 1) { xs0[x] = w - 1; xs1[x] = 0; }
		}
		for (y = 0; y < th; y++) {
			long fy = (long)(((long long)y * 256 * h) / th) + (128 * h) / th - 128;
			int y0, y1, wy;
			const unsigned char *r0, *r1;
			unsigned short *o = out + (size_t)y * tw;
			if (fy < 0) fy = 0;
			y0 = (int)(fy >> 8); wy = (int)(fy & 255);
			if (y0 >= h - 1) { y0 = h - 1; wy = 0; }
			y1 = y0 + 1 < h ? y0 + 1 : y0;
			r0 = px + (size_t)y0 * w * 3;
			r1 = px + (size_t)y1 * w * 3;
			for (x = 0; x < tw; x++) {
				int x0 = xs0[x], wx = xs1[x], x1 = x0 + 1 < w ? x0 + 1 : x0, c;
				unsigned v[3];
				for (c = 0; c < 3; c++) {
					int a = r0[x0 * 3 + c] * (256 - wx) + r0[x1 * 3 + c] * wx;
					int b = r1[x0 * 3 + c] * (256 - wx) + r1[x1 * 3 + c] * wx;
					v[c] = (unsigned)((a * (256 - wy) + b * wy) >> 16);
				}
				o[x] = RGB565(v[0], v[1], v[2]);
			}
		}
	}
	free(xs0);
	big_free(px);
	*th_out = th;
	return out;
}
