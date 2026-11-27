/* M480Web - small utilities */
#ifndef M480_UTIL_H
#define M480_UTIL_H

#include <windows.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32_WCE
#define IS_CE 1
#else
#define IS_CE 0
#endif

typedef struct { char *p; int len, cap; } Buf;
typedef struct { WCHAR *p; int len, cap; } WBuf;

void buf_init(Buf *b);
void buf_free(Buf *b);
int  buf_reserve(Buf *b, int extra);
void buf_add(Buf *b, const void *d, int n);
void buf_adds(Buf *b, const char *s);
void buf_addc(Buf *b, char c);
void buf_addf(Buf *b, const char *fmt, ...);
char *buf_detach(Buf *b); /* NUL terminated, caller frees */

void wbuf_init(WBuf *b);
void wbuf_free(WBuf *b);
void wbuf_add(WBuf *b, const WCHAR *d, int n);
void wbuf_addc(WBuf *b, WCHAR c);
void wbuf_adda(WBuf *b, const char *ascii);

char *xstrdup(const char *s);
char *xstrndup(const char *s, int n);
WCHAR *xwcsdup(const WCHAR *s);
int  strieq(const char *a, const char *b);
int  strnieq(const char *a, const char *b, int n);
const char *stristr(const char *hay, const char *needle);
int  wcsieq_a(const WCHAR *w, int wlen, const char *a); /* compare wide (len) with ascii, case-insens */

/* conversions */
WCHAR *utf8_to_w(const char *s, int len, int *outlen);
/* swap characters the phone font can't draw (shown as boxes) for look-alikes, drop emoji */
void glyph_fix(WCHAR *w);
/* one character: the look-alike, the same character, or 0 (dropped) */
WCHAR glyph_map(WCHAR c);
char  *w_to_utf8(const WCHAR *w, int len);
char  *w_to_cp(const WCHAR *w, int len, UINT cp);
WCHAR *cp_to_w(const char *s, int len, UINT cp, int *outlen);

/* misc */
void   app_path(WCHAR *out, const WCHAR *file); /* file next to exe */
DWORD  now_unix(void);

/* entropy */
void   entropy_stir(const void *data, int len);
void   entropy_get(unsigned char out[32]);

#endif
