#ifndef M480_NET_H
#define M480_NET_H

#include "util.h"

typedef struct {
	int   status;
	char *ctype;       /* lower-case mime type, no params */
	char *charset;     /* from Content-Type, may be NULL */
	char *url;         /* final URL after redirects */
	char *refresh;     /* Refresh header */
	char *disposition; /* Content-Disposition */
	Buf   body;
	char  err[256];
	int   certerr;     /* BearSSL error code if certificate problem */
	char  certhost[128];
	char  tlsinfo[96];
	void *sink;        /* FILE* the body was streamed into (NULL = in body) */
	long  streamed;    /* bytes written to sink */
	DWORD date;        /* Date header (unix time), 0 if none */
} HttpResp;

typedef struct {
	volatile int abort;
	volatile unsigned int sock;   /* current socket, for cancel */
	void (*progress)(void *ctx, int got, int total);
	/* called once headers of the final 2xx response are known; may return a
	   FILE* to stream the body into instead of memory */
	void *(*on_headers)(void *ctx, HttpResp *r);
	void *ctx;
	const char *ua;    /* per-request User-Agent (NULL = global) */
	const char *hdrs;  /* extra request header lines, each ending in \r\n */
} NetCtl;

void net_init(void);
void net_shutdown(void);
void net_set_ua(const char *ua);
const char *net_get_ua(void);
void net_allow_insecure(const char *host);
void net_cancel(NetCtl *c);
void net_set_auth(const char *host, const char *token);

/* method: "GET"/"POST"; body may be NULL. returns 1 if got an HTTP response */
int  http_request(const char *url, const char *method, const char *body, int bodylen,
                  const char *ctype, const char *referer, HttpResp *r, NetCtl *ctl, int maxbytes);
void http_resp_free(HttpResp *r);

void cookies_load(void);
void cookies_save(void);
void cookies_clear(void);

/* streaming range reader with keep-alive (for media) */
typedef struct HStream HStream;
HStream *hs_new(NetCtl *ctl);
/* starts a GET for bytes [start, end] (end < 0 = to the end). follows redirects.
   returns HTTP status (0 = network failure, see hs_err) */
int  hs_get(HStream *h, const char *url, long start, long end);
/* reads body of the current response. >0 bytes, 0 = response finished, -1 = error */
int  hs_read(HStream *h, unsigned char *buf, int n);
long hs_total(HStream *h);
/* media fetch quirks: POST with body "x\\0" instead of GET, append &rn=<n> */
void hs_options(HStream *h, int post, int rn);   /* total size from Content-Range, -1 unknown */
const char *hs_err(HStream *h);
void hs_free(HStream *h);

#endif
