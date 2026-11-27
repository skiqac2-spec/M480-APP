#include "util.h"
#include <stdio.h>
#include <stdarg.h>
#include "bearssl.h"

void buf_init(Buf *b) { b->p = NULL; b->len = b->cap = 0; }
void buf_free(Buf *b) { free(b->p); buf_init(b); }

int buf_reserve(Buf *b, int extra)
{
	if (b->len + extra + 1 > b->cap) {
		int nc = b->cap ? b->cap : 256;
		char *np;
		while (nc < b->len + extra + 1) nc *= 2;
		np = (char *)realloc(b->p, nc);
		if (!np) return 0;
		b->p = np; b->cap = nc;
	}
	return 1;
}

void buf_add(Buf *b, const void *d, int n)
{
	if (n <= 0 || !buf_reserve(b, n)) return;
	memcpy(b->p + b->len, d, n);
	b->len += n;
	b->p[b->len] = 0;
}
void buf_adds(Buf *b, const char *s) { buf_add(b, s, (int)strlen(s)); }
void buf_addc(Buf *b, char c) { buf_add(b, &c, 1); }

void buf_addf(Buf *b, const char *fmt, ...)
{
	char tmp[1024];
	va_list ap;
	int n, cap;
	va_start(ap, fmt);
	n = _vsnprintf(tmp, sizeof tmp - 1, fmt, ap);
	va_end(ap);
	if (n >= 0 && n < (int)sizeof tmp - 1) {
		tmp[n] = 0;
		buf_add(b, tmp, n);
		return;
	}
	/* long output (e.g. ~1-2 KB video URLs): retry with a bigger heap buffer */
	for (cap = 8192; cap <= 262144; cap *= 4) {
		char *big = (char *)malloc(cap);
		if (!big) break;
		va_start(ap, fmt);
		n = _vsnprintf(big, cap - 1, fmt, ap);
		va_end(ap);
		if (n >= 0 && n < cap - 1) {
			buf_add(b, big, n);
			free(big);
			return;
		}
		free(big);
	}
	tmp[sizeof tmp - 1] = 0;
	buf_add(b, tmp, (int)strlen(tmp));
}

char *buf_detach(Buf *b)
{
	char *p;
	if (!b->p) { buf_reserve(b, 1); if (b->p) b->p[0] = 0; }
	p = b->p;
	buf_init(b);
	return p;
}

void wbuf_init(WBuf *b) { b->p = NULL; b->len = b->cap = 0; }
void wbuf_free(WBuf *b) { free(b->p); wbuf_init(b); }
void wbuf_add(WBuf *b, const WCHAR *d, int n)
{
	if (n <= 0) return;
	if (b->len + n + 1 > b->cap) {
		int nc = b->cap ? b->cap : 256;
		WCHAR *np;
		while (nc < b->len + n + 1) nc *= 2;
		np = (WCHAR *)realloc(b->p, nc * sizeof(WCHAR));
		if (!np) return;
		b->p = np; b->cap = nc;
	}
	memcpy(b->p + b->len, d, n * sizeof(WCHAR));
	b->len += n;
	b->p[b->len] = 0;
}
void wbuf_addc(WBuf *b, WCHAR c) { wbuf_add(b, &c, 1); }
void wbuf_adda(WBuf *b, const char *a) { while (*a) wbuf_addc(b, (WCHAR)(unsigned char)*a++); }

char *xstrdup(const char *s) { return s ? xstrndup(s, (int)strlen(s)) : NULL; }
char *xstrndup(const char *s, int n)
{
	char *p = (char *)malloc(n + 1);
	if (!p) return NULL;
	memcpy(p, s, n); p[n] = 0;
	return p;
}
WCHAR *xwcsdup(const WCHAR *s)
{
	int n = 0; WCHAR *p;
	if (!s) return NULL;
	while (s[n]) n++;
	p = (WCHAR *)malloc((n + 1) * sizeof(WCHAR));
	if (p) memcpy(p, s, (n + 1) * sizeof(WCHAR));
	return p;
}

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int strieq(const char *a, const char *b)
{
	while (*a && *b) { if (lc((unsigned char)*a) != lc((unsigned char)*b)) return 0; a++; b++; }
	return *a == *b;
}
int strnieq(const char *a, const char *b, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		if (lc((unsigned char)a[i]) != lc((unsigned char)b[i])) return 0;
		if (!a[i]) return 1;
	}
	return 1;
}
const char *stristr(const char *h, const char *n)
{
	int nl = (int)strlen(n);
	for (; *h; h++) if (strnieq(h, n, nl)) return h;
	return NULL;
}
int wcsieq_a(const WCHAR *w, int wlen, const char *a)
{
	int i;
	for (i = 0; i < wlen; i++) {
		if (!a[i]) return 0;
		if (w[i] > 127 || lc(w[i]) != lc((unsigned char)a[i])) return 0;
	}
	return a[wlen] == 0;
}

WCHAR *cp_to_w(const char *s, int len, UINT cp, int *outlen)
{
	int n;
	WCHAR *w;
	if (len < 0) len = (int)strlen(s);
	if (len == 0) { w = (WCHAR *)malloc(2); w[0] = 0; if (outlen) *outlen = 0; return w; }
	n = MultiByteToWideChar(cp, 0, s, len, NULL, 0);
	if (n <= 0) {
		/* fallback: latin1 */
		int i;
		w = (WCHAR *)malloc((len + 1) * sizeof(WCHAR));
		for (i = 0; i < len; i++) w[i] = (unsigned char)s[i];
		w[len] = 0;
		if (outlen) *outlen = len;
		return w;
	}
	w = (WCHAR *)malloc((n + 1) * sizeof(WCHAR));
	if (!w) return NULL;
	MultiByteToWideChar(cp, 0, s, len, w, n);
	w[n] = 0;
	if (outlen) *outlen = n;
	return w;
}
WCHAR *utf8_to_w(const char *s, int len, int *outlen) { return cp_to_w(s, len, CP_UTF8, outlen); }

char *w_to_cp(const WCHAR *w, int len, UINT cp)
{
	int n;
	char *s;
	if (len < 0) { len = 0; while (w[len]) len++; }
	if (len == 0) return xstrdup("");
	n = WideCharToMultiByte(cp, 0, w, len, NULL, 0, NULL, NULL);
	if (n <= 0) return xstrdup("");
	s = (char *)malloc(n + 1);
	if (!s) return NULL;
	WideCharToMultiByte(cp, 0, w, len, s, n, NULL, NULL);
	s[n] = 0;
	return s;
}
char *w_to_utf8(const WCHAR *w, int len) { return w_to_cp(w, len, CP_UTF8); }

void app_path(WCHAR *out, const WCHAR *file)
{
	int i, n;
	GetModuleFileNameW(NULL, out, MAX_PATH);
	n = (int)wcslen(out);
	for (i = n - 1; i >= 0; i--) if (out[i] == L'\\' || out[i] == L'/') break;
	out[i + 1] = 0;
	wcscat(out, file);
}

DWORD now_unix(void)
{
	SYSTEMTIME st;
	int y, m, d;
	long days;
	GetSystemTime(&st);
	y = st.wYear; m = st.wMonth; d = st.wDay;
	/* days from civil (Howard Hinnant) */
	y -= m <= 2;
	{
		long era = (y >= 0 ? y : y - 399) / 400;
		unsigned yoe = (unsigned)(y - era * 400);
		unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
		unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		days = era * 146097 + (long)doe - 719468;
	}
	return (DWORD)(days * 86400L + st.wHour * 3600L + st.wMinute * 60L + st.wSecond);
}

/* ---- entropy pool (SHA-256 based) ---- */
static unsigned char g_pool[32];
static unsigned long g_pool_ctr;
static CRITICAL_SECTION g_pool_cs;
static int g_pool_init;

static void pool_lock(void)
{
	if (!g_pool_init) { InitializeCriticalSection(&g_pool_cs); g_pool_init = 1; }
	EnterCriticalSection(&g_pool_cs);
}

void entropy_stir(const void *data, int len)
{
	br_sha256_context h;
	DWORD t = GetTickCount();
	LARGE_INTEGER pc;
	pool_lock();
	br_sha256_init(&h);
	br_sha256_update(&h, g_pool, 32);
	br_sha256_update(&h, &t, sizeof t);
	if (QueryPerformanceCounter(&pc)) br_sha256_update(&h, &pc, sizeof pc);
	if (data && len > 0) br_sha256_update(&h, data, len);
	br_sha256_out(&h, g_pool);
	LeaveCriticalSection(&g_pool_cs);
}

typedef BOOL (WINAPI *PCryptAcquireContextW)(ULONG_PTR *, LPCWSTR, LPCWSTR, DWORD, DWORD);
typedef BOOL (WINAPI *PCryptGenRandom)(ULONG_PTR, DWORD, BYTE *);
typedef BOOL (WINAPI *PCryptReleaseContext)(ULONG_PTR, DWORD);

static void os_random(void)
{
	static int done;
	HMODULE h;
	PCryptAcquireContextW acq;
	PCryptGenRandom gen;
	PCryptReleaseContext rel;
	ULONG_PTR prov = 0;
	BYTE rnd[32];
	if (done) return;
	done = 1;
#ifdef _WIN32_WCE
	h = LoadLibraryW(L"coredll.dll");
	if (!h) return;
	acq = (PCryptAcquireContextW)GetProcAddressW(h, L"CryptAcquireContextW");
	gen = (PCryptGenRandom)GetProcAddressW(h, L"CryptGenRandom");
	rel = (PCryptReleaseContext)GetProcAddressW(h, L"CryptReleaseContext");
#else
	h = LoadLibraryW(L"advapi32.dll");
	if (!h) return;
	acq = (PCryptAcquireContextW)GetProcAddress(h, "CryptAcquireContextW");
	gen = (PCryptGenRandom)GetProcAddress(h, "CryptGenRandom");
	rel = (PCryptReleaseContext)GetProcAddress(h, "CryptReleaseContext");
#endif
	if (acq && gen && acq(&prov, NULL, NULL, 1 /*PROV_RSA_FULL*/, 0xF0000000 /*CRYPT_VERIFYCONTEXT*/)) {
		if (gen(prov, sizeof rnd, rnd)) entropy_stir(rnd, sizeof rnd);
		if (rel) rel(prov, 0);
	}
}

void entropy_get(unsigned char out[32])
{
	br_sha256_context h;
	MEMORYSTATUS ms;
	SYSTEMTIME st;
	void *stackp = &h;
	os_random();
	ms.dwLength = sizeof ms;
	GlobalMemoryStatus(&ms);
	GetSystemTime(&st);
	entropy_stir(&ms, sizeof ms);
	entropy_stir(&st, sizeof st);
	entropy_stir(&stackp, sizeof stackp);
	pool_lock();
	g_pool_ctr++;
	br_sha256_init(&h);
	br_sha256_update(&h, "out", 3);
	br_sha256_update(&h, g_pool, 32);
	br_sha256_update(&h, &g_pool_ctr, sizeof g_pool_ctr);
	br_sha256_out(&h, out);
	LeaveCriticalSection(&g_pool_cs);
	entropy_stir(out, 8);
}

/* ---------------- characters the phone font lacks ---------------- */
#include "glyphmap.h"
void glyph_fix(WCHAR *w)
{
	WCHAR *o = w;
	if (!w) return;
	for (; *w; w++) {
		unsigned c = *w;
		int lo, hi;
		if (c >= 0xD800 && c <= 0xDFFF) continue;                  /* emoji etc. (outside the BMP) */
		if ((c >= 0xFE00 && c <= 0xFE0F) || (c >= 0x200B && c <= 0x200F) || c == 0x2060 || c == 0xFEFF) continue;   /* invisible joiners */
		if (c >= 0x2600 && c <= 0x27BF) {                          /* symbol/emoji block: keep only the ones mapped */
			int found = 0;
			lo = 0; hi = (int)(sizeof g_glyph_src / sizeof g_glyph_src[0]) - 1;
			while (lo <= hi) { int m = (lo + hi) / 2; if (g_glyph_src[m] == c) { found = 1; c = g_glyph_dst[m]; break; } if (g_glyph_src[m] < c) lo = m + 1; else hi = m - 1; }
			if (!found) continue;
			*o++ = (WCHAR)c;
			continue;
		}
		if (c >= 0x80) {
			lo = 0; hi = (int)(sizeof g_glyph_src / sizeof g_glyph_src[0]) - 1;
			while (lo <= hi) { int m = (lo + hi) / 2; if (g_glyph_src[m] == c) { c = g_glyph_dst[m]; break; } if (g_glyph_src[m] < c) lo = m + 1; else hi = m - 1; }
		}
		*o++ = (WCHAR)c;
	}
	*o = 0;
}
