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
} HttpResp;

typedef struct {
	volatile int abort;
	volatile unsigned int sock;   /* current socket, for cancel */
	void (*progress)(void *ctx, int got, int total);
	/* called once headers of the final 2xx response are known; may return a
	   FILE* to stream the body into instead of memory */
	void *(*on_headers)(void *ctx, HttpResp *r);
	void *ctx;
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

#endif
