/* M480Web - networking: sockets, TLS 1.2 (BearSSL), HTTP/1.1, cookies */
#include <winsock2.h>
#include "util.h"
#include "net.h"
#include "url.h"
#include "bearssl.h"
#include "tubelog.h"
#include <stdio.h>

#ifdef M480_TEST_TA
#include "trust_anchors_test.h"
#else
#include "trust_anchors.h"
#endif

#define STBI_NO_STDIO
#include "stb_image.h"

static char g_auth_host[128], g_auth_token[160];
static char g_ua[256] = "Opera/9.80 (Windows Mobile; Opera Mini/7.1.32052/191.256; U; ko) Presto/2.12.423 Version/12.16";
static CRITICAL_SECTION g_cs;
/* correction for a wrong device clock (seconds), learned from a server's Date header */
static volatile long g_time_ofs;
static volatile int g_time_synced;
static DWORD parse_http_date(const char *s);
#ifndef BUILD_UNIX
#define BUILD_UNIX 1763424000UL /* 2025-11-18 */
#endif
/* time used for certificate checks: device clock + learned correction, but never
   earlier than the build date (a phone with a flat clock battery says 2008/2009) */
static DWORD net_now(void)
{
	DWORD t = now_unix() + g_time_ofs;
	if (!g_time_ofs && t < (DWORD)BUILD_UNIX) t = (DWORD)BUILD_UNIX;
	return t;
}
static int g_inited;

/* ------------------------------------------------------------------ */
/* Connection manager (Windows Mobile data connection)                 */
/* ------------------------------------------------------------------ */
#ifdef _WIN32_WCE
typedef struct {
	DWORD cbSize, dwParams, dwFlags, dwPriority;
	BOOL bExclusive, bDisabled;
	GUID guidDestNet;
	HWND hWnd;
	UINT uMsg;
	LPARAM lParam;
	ULONG ulMaxCost, ulMinRcvBw, ulMaxConnLatency;
} M_CONNMGR_INFO;
typedef HRESULT (WINAPI *PConnMgrEstablishConnectionSync)(M_CONNMGR_INFO *, HANDLE *, DWORD, DWORD *);
typedef HRESULT (WINAPI *PConnMgrReleaseConnection)(HANDLE, LONG);
typedef HRESULT (WINAPI *PConnMgrMapURL)(LPCWSTR, GUID *, DWORD *);
static HANDLE g_conn;
static DWORD g_conn_tick;

static void ensure_connection(const char *url)
{
	static HMODULE h;
	static PConnMgrEstablishConnectionSync est;
	static PConnMgrReleaseConnection rel;
	static PConnMgrMapURL mapurl;
	M_CONNMGR_INFO ci;
	DWORD status = 0;
	static const GUID internet = { 0x436ef144, 0xb4fb, 0x4863, { 0xa0, 0x41, 0x8f, 0x90, 0x5a, 0x62, 0xc5, 0x72 } };
	if (!h) {
		h = LoadLibraryW(L"cellcore.dll");
		if (!h) return;
		est = (PConnMgrEstablishConnectionSync)GetProcAddressW(h, L"ConnMgrEstablishConnectionSync");
		rel = (PConnMgrReleaseConnection)GetProcAddressW(h, L"ConnMgrReleaseConnection");
		mapurl = (PConnMgrMapURL)GetProcAddressW(h, L"ConnMgrMapURL");
	}
	if (!est) return;
	if (g_conn && GetTickCount() - g_conn_tick < 60000) return;
	if (g_conn && rel) { rel(g_conn, 1); g_conn = NULL; }
	memset(&ci, 0, sizeof ci);
	ci.cbSize = sizeof ci;
	ci.dwParams = 1; /* CONNMGR_PARAM_GUIDDESTNET */
	ci.dwPriority = 0x08000; /* CONNMGR_PRIORITY_USERINTERACTIVE */
	ci.guidDestNet = internet;
	if (mapurl && url) {
		WCHAR *w = utf8_to_w(url, -1, NULL);
		DWORD idx = 0;
		GUID g;
		if (w && mapurl(w, &g, &idx) == 0) ci.guidDestNet = g;
		free(w);
	}
	if (est(&ci, &g_conn, 30000, &status) == 0) g_conn_tick = GetTickCount();
	else g_conn = NULL;
}
#else
static void ensure_connection(const char *url) { (void)url; }
#endif

void net_init(void)
{
	WSADATA wd;
	if (g_inited) return;
	g_inited = 1;
	InitializeCriticalSection(&g_cs);
	WSAStartup(MAKEWORD(2, 2), &wd);
	cookies_load();
}

void net_shutdown(void)
{
	cookies_save();
#ifdef _WIN32_WCE
	{
		HMODULE h = LoadLibraryW(L"cellcore.dll");
		if (h && g_conn) {
			PConnMgrReleaseConnection rel = (PConnMgrReleaseConnection)GetProcAddressW(h, L"ConnMgrReleaseConnection");
			if (rel) rel(g_conn, 0);
		}
	}
#endif
	WSACleanup();
}

void net_set_auth(const char *host, const char *token)
{
	strncpy(g_auth_host, host ? host : "", sizeof g_auth_host - 1);
	strncpy(g_auth_token, token ? token : "", sizeof g_auth_token - 1);
}

void net_set_ua(const char *ua) { strncpy(g_ua, ua, sizeof g_ua - 1); g_ua[sizeof g_ua - 1] = 0; }
const char *net_get_ua(void) { return g_ua; }

/* ------------------------------------------------------------------ */
/* insecure host list / TLS session cache                              */
/* ------------------------------------------------------------------ */
static char g_insecure[16][128];
static int g_ninsecure;

void net_allow_insecure(const char *host)
{
	EnterCriticalSection(&g_cs);
	if (g_ninsecure < 16) { strncpy(g_insecure[g_ninsecure], host, 127); g_insecure[g_ninsecure][127] = 0; g_ninsecure++; }
	LeaveCriticalSection(&g_cs);
}
static int is_insecure(const char *host)
{
	int i, r = 0;
	EnterCriticalSection(&g_cs);
	for (i = 0; i < g_ninsecure; i++) if (strcmp(g_insecure[i], host) == 0) r = 1;
	LeaveCriticalSection(&g_cs);
	return r;
}

typedef struct { char host[128]; int port; br_ssl_session_parameters p; int used; DWORD t; } SessEnt;
static SessEnt g_sess[12];

static int sess_get(const char *host, int port, br_ssl_session_parameters *p)
{
	int i, r = 0;
	EnterCriticalSection(&g_cs);
	for (i = 0; i < 12; i++) if (g_sess[i].used && g_sess[i].port == port && strcmp(g_sess[i].host, host) == 0) {
		*p = g_sess[i].p; r = 1; g_sess[i].t = GetTickCount(); break;
	}
	LeaveCriticalSection(&g_cs);
	return r;
}
static void sess_put(const char *host, int port, const br_ssl_session_parameters *p)
{
	int i, best = 0;
	EnterCriticalSection(&g_cs);
	for (i = 0; i < 12; i++) {
		if (g_sess[i].used && g_sess[i].port == port && strcmp(g_sess[i].host, host) == 0) { best = i; break; }
		if (!g_sess[i].used) { best = i; break; }
		if (g_sess[i].t < g_sess[best].t) best = i;
	}
	strncpy(g_sess[best].host, host, 127);
	g_sess[best].host[127] = 0;
	g_sess[best].port = port;
	g_sess[best].p = *p;
	g_sess[best].used = 1;
	g_sess[best].t = GetTickCount();
	LeaveCriticalSection(&g_cs);
}

/* ------------------------------------------------------------------ */
/* X.509 wrapper allowing "continue anyway"                            */
/* ------------------------------------------------------------------ */
typedef struct {
	const br_x509_class *vtable;
	br_x509_minimal_context *inner;
	int insecure;
	int depth;
	int err;
	br_x509_decoder_context dec;
} XWrap;

static void xw_start_chain(const br_x509_class **ctx, const char *server_name)
{
	XWrap *x = (XWrap *)ctx;
	x->depth = 0; x->err = 0;
	x->inner->vtable->start_chain(&x->inner->vtable, server_name);
}
static void xw_start_cert(const br_x509_class **ctx, uint32_t length)
{
	XWrap *x = (XWrap *)ctx;
	if (x->depth == 0) br_x509_decoder_init(&x->dec, NULL, NULL);
	x->inner->vtable->start_cert(&x->inner->vtable, length);
}
static void xw_append(const br_x509_class **ctx, const unsigned char *buf, size_t len)
{
	XWrap *x = (XWrap *)ctx;
	if (x->depth == 0) br_x509_decoder_push(&x->dec, buf, len);
	x->inner->vtable->append(&x->inner->vtable, buf, len);
}
static void xw_end_cert(const br_x509_class **ctx)
{
	XWrap *x = (XWrap *)ctx;
	x->inner->vtable->end_cert(&x->inner->vtable);
	x->depth++;
}
static unsigned xw_end_chain(const br_x509_class **ctx)
{
	XWrap *x = (XWrap *)ctx;
	unsigned r = x->inner->vtable->end_chain(&x->inner->vtable);
	x->err = (int)r;
	if (r && x->insecure && br_x509_decoder_get_pkey(&x->dec)) return 0;
	return r;
}
static const br_x509_pkey *xw_get_pkey(const br_x509_class *const *ctx, unsigned *usages)
{
	XWrap *x = (XWrap *)ctx;
	if (x->err && x->insecure) {
		if (usages) *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
		return br_x509_decoder_get_pkey(&x->dec);
	}
	return x->inner->vtable->get_pkey((const br_x509_class *const *)&x->inner->vtable, usages);
}
static const br_x509_class xw_vtable = {
	sizeof(XWrap), xw_start_chain, xw_start_cert, xw_append, xw_end_cert, xw_end_chain, xw_get_pkey
};

/* ------------------------------------------------------------------ */
/* Connection                                                          */
/* ------------------------------------------------------------------ */
typedef struct {
	SOCKET s;
	NetCtl *ctl;
	int tls;
	br_ssl_client_context sc;
	br_x509_minimal_context xc;
	XWrap xw;
	br_sslio_context io;
	unsigned char iobuf[BR_SSL_BUFSIZE_MONO];
	/* read buffer */
	unsigned char rb[8192];
	int rpos, rlen;
	int eof;
	char err[200];
} Conn;

static int wait_sock(Conn *c, int forwrite, int timeout_ms)
{
	int waited = 0;
	for (;;) {
		fd_set fs;
		struct timeval tv;
		int r;
		if (c->ctl && c->ctl->abort) return -1;
		FD_ZERO(&fs);
		FD_SET(c->s, &fs);
		tv.tv_sec = 0; tv.tv_usec = 300000;
		r = select((int)c->s + 1, forwrite ? NULL : &fs, forwrite ? &fs : NULL, NULL, &tv);
		if (r > 0) return 1;
		if (r < 0) return -1;
		waited += 300;
		if (waited >= timeout_ms) return 0;
	}
}

static int sock_read(void *ctx, unsigned char *buf, size_t len)
{
	Conn *c = (Conn *)ctx;
	int r = wait_sock(c, 0, 30000);
	if (r <= 0) return -1;
	r = recv(c->s, (char *)buf, (int)len, 0);
	if (r <= 0) return -1;
	entropy_stir(NULL, 0);
	return r;
}
static int sock_write(void *ctx, const unsigned char *buf, size_t len)
{
	Conn *c = (Conn *)ctx;
	int r = wait_sock(c, 1, 30000);
	if (r <= 0) return -1;
	r = send(c->s, (const char *)buf, (int)len, 0);
	if (r <= 0) return -1;
	return r;
}

static int is_ip(const char *h)
{
	for (; *h; h++) if (!((*h >= '0' && *h <= '9') || *h == '.')) return 0;
	return 1;
}

static const char *tls_err_text(int e)
{
	switch (e) {
	case BR_ERR_X509_EXPIRED: return "인증서 유효기간 오류 (기기의 날짜/시간이 정확한지 확인하세요)";
	case BR_ERR_X509_NOT_TRUSTED: return "신뢰할 수 없는 인증서 (루트 인증서 없음)";
	case BR_ERR_X509_BAD_SERVER_NAME: return "인증서의 도메인이 주소와 다름";
	case BR_ERR_X509_TIME_UNKNOWN: return "기기 시간을 알 수 없음";
	case BR_ERR_BAD_VERSION: case BR_ERR_UNSUPPORTED_VERSION: return "서버가 TLS 1.2를 지원하지 않음";
	case BR_ERR_BAD_CIPHER_SUITE: return "서버와 공통 암호 방식 없음";
	case BR_ERR_NO_RANDOM: return "난수 생성 실패";
	case BR_ERR_IO: return "연결이 끊어짐";
	default: return NULL;
	}
}

static void conn_close(Conn *c)
{
	if (c->s != INVALID_SOCKET) {
		if (c->ctl) c->ctl->sock = (unsigned int)INVALID_SOCKET;
		closesocket(c->s);
		c->s = INVALID_SOCKET;
	}
}

static int conn_open(Conn *c, const Url *u, NetCtl *ctl, HttpResp *resp)
{
	struct hostent *he;
	struct sockaddr_in sa;
	unsigned long nb = 1;
	unsigned long addr;
	int r;
	memset(c, 0, offsetof(Conn, iobuf));
	c->s = INVALID_SOCKET;
	c->ctl = ctl;
	c->rpos = c->rlen = 0;
	c->eof = 0;
	addr = inet_addr(u->host);
	if (addr == INADDR_NONE) {
		he = gethostbyname(u->host);
		if (!he) {
			/* data connection may be down: bring it up and retry */
			ensure_connection(NULL);
			he = gethostbyname(u->host);
		}
		if (!he) { _snprintf(resp->err, sizeof resp->err, "주소를 찾을 수 없음: %s", u->host); return 0; }
		memcpy(&addr, he->h_addr_list[0], 4);
	}
	if (ctl && ctl->abort) return 0;
	c->s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (c->s == INVALID_SOCKET) { strcpy(resp->err, "소켓 생성 실패"); return 0; }
	if (ctl) ctl->sock = (unsigned int)c->s;
	ioctlsocket(c->s, FIONBIO, &nb);
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)u->port);
	sa.sin_addr.s_addr = addr;
	r = connect(c->s, (struct sockaddr *)&sa, sizeof sa);
	if (r != 0) {
		int e = WSAGetLastError();
		if (e != WSAEWOULDBLOCK && e != WSAEINPROGRESS) { _snprintf(resp->err, sizeof resp->err, "연결 실패 (%d)", e); conn_close(c); return 0; }
		{
			/* wait for connect */
			int waited = 0;
			for (;;) {
				fd_set wf, ef;
				struct timeval tv;
				if (ctl && ctl->abort) { strcpy(resp->err, "취소됨"); conn_close(c); return 0; }
				FD_ZERO(&wf); FD_SET(c->s, &wf);
				FD_ZERO(&ef); FD_SET(c->s, &ef);
				tv.tv_sec = 0; tv.tv_usec = 300000;
				r = select((int)c->s + 1, NULL, &wf, &ef, &tv);
				if (r > 0) {
					if (FD_ISSET(c->s, &ef)) { strcpy(resp->err, "서버에 연결할 수 없음"); conn_close(c); return 0; }
					break;
				}
				if (r < 0) { strcpy(resp->err, "연결 오류"); conn_close(c); return 0; }
				waited += 300;
				if (waited > 25000) { strcpy(resp->err, "연결 시간 초과"); conn_close(c); return 0; }
			}
		}
	}
	if (strcmp(u->scheme, "https") == 0) {
		unsigned char seed[32];
		br_ssl_session_parameters sp;
		DWORD t = net_now();
		int resume;
		c->tls = 1;
		br_ssl_client_init_full(&c->sc, &c->xc, TAs, TAs_NUM);
		/* faster AES on ARM (table based) */
		br_ssl_engine_set_aes_cbc(&c->sc.eng, &br_aes_big_cbcenc_vtable, &br_aes_big_cbcdec_vtable);
		br_ssl_engine_set_aes_ctr(&c->sc.eng, &br_aes_big_ctr_vtable);
		br_x509_minimal_set_time(&c->xc, (uint32_t)(t / 86400) + 719528, (uint32_t)(t % 86400));
		c->xw.vtable = &xw_vtable;
		c->xw.inner = &c->xc;
		c->xw.insecure = is_insecure(u->host);
		br_ssl_engine_set_x509(&c->sc.eng, &c->xw.vtable);
		br_ssl_engine_set_buffer(&c->sc.eng, c->iobuf, sizeof c->iobuf, 0);
		entropy_get(seed);
		br_ssl_engine_inject_entropy(&c->sc.eng, seed, sizeof seed);
		resume = sess_get(u->host, u->port, &sp);
		if (resume) br_ssl_engine_set_session_parameters(&c->sc.eng, &sp);
		if (!br_ssl_client_reset(&c->sc, is_ip(u->host) ? NULL : u->host, resume)) {
			strcpy(resp->err, "TLS 초기화 실패"); conn_close(c); return 0;
		}
		br_sslio_init(&c->io, &c->sc.eng, sock_read, c, sock_write, c);
	}
	return 1;
}

static void tls_fail(Conn *c, HttpResp *resp, const Url *u)
{
	int e = br_ssl_engine_last_error(&c->sc.eng);
	const char *t;
	if (c->xw.err && e >= 32 && e < 64) {
		resp->certerr = c->xw.err;
		strncpy(resp->certhost, u->host, sizeof resp->certhost - 1);
	}
	if (resp->err[0]) return;
	if (c->ctl && c->ctl->abort) { strcpy(resp->err, "취소됨"); return; }
	t = tls_err_text(resp->certerr ? resp->certerr : e);
	if (resp->certerr == BR_ERR_X509_EXPIRED || resp->certerr == BR_ERR_X509_TIME_UNKNOWN) {
		SYSTEMTIME st;
		GetLocalTime(&st);
		_snprintf(resp->err, sizeof resp->err, "보안 연결 실패: 인증서 날짜 오류 (폰 날짜 %04d-%02d-%02d, 보정 %ld초) [%d]",
		          st.wYear, st.wMonth, st.wDay, (long)g_time_ofs, resp->certerr);
		tube_log("%s host=%s", resp->err, u->host);
		return;
	}
	if (t) _snprintf(resp->err, sizeof resp->err, "보안 연결 실패: %s [%d]", t, resp->certerr ? resp->certerr : e);
	else _snprintf(resp->err, sizeof resp->err, "보안 연결 실패 (오류 %d)", e);
}

static int conn_write(Conn *c, const void *d, int n)
{
	if (c->tls) {
		if (br_sslio_write_all(&c->io, d, n) < 0) return 0;
		return 1;
	} else {
		const char *p = (const char *)d;
		while (n > 0) {
			int r = sock_write(c, (const unsigned char *)p, n);
			if (r <= 0) return 0;
			p += r; n -= r;
		}
		return 1;
	}
}
static int conn_flush(Conn *c)
{
	if (c->tls) return br_sslio_flush(&c->io) == 0;
	return 1;
}

static int conn_fill(Conn *c)
{
	int r;
	if (c->eof) return 0;
	if (c->rpos < c->rlen) return 1;
	c->rpos = c->rlen = 0;
	if (c->tls) r = br_sslio_read(&c->io, c->rb, sizeof c->rb);
	else r = sock_read(c, c->rb, sizeof c->rb);
	if (r <= 0) { c->eof = 1; return 0; }
	c->rlen = r;
	return 1;
}
static int conn_getc(Conn *c)
{
	if (c->rpos >= c->rlen && !conn_fill(c)) return -1;
	return c->rb[c->rpos++];
}
static int conn_readline(Conn *c, Buf *line)
{
	int ch;
	line->len = 0;
	if (line->p) line->p[0] = 0;
	for (;;) {
		ch = conn_getc(c);
		if (ch < 0) return line->len > 0;
		if (ch == '\n') break;
		if (ch != '\r') buf_addc(line, (char)ch);
		if (line->len > 16384) break;
	}
	if (!line->p) buf_adds(line, "");
	return 1;
}

/* ------------------------------------------------------------------ */
/* Cookies                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
	char *domain, *path, *name, *value;
	int hostonly, secure;
	DWORD expires; /* 0 = session */
} Cookie;
static Cookie *g_ck;
static int g_nck;

static void ck_free(Cookie *c) { free(c->domain); free(c->path); free(c->name); free(c->value); }

static void ck_add(const char *domain, int hostonly, const char *path, int secure, DWORD expires, const char *name, const char *value)
{
	int i;
	for (i = 0; i < g_nck; i++) {
		Cookie *c = &g_ck[i];
		if (strcmp(c->name, name) == 0 && strieq(c->domain, domain) && strcmp(c->path, path) == 0) {
			ck_free(c);
			memmove(c, c + 1, (g_nck - i - 1) * sizeof(Cookie));
			g_nck--;
			break;
		}
	}
	if (expires && expires < now_unix()) return; /* deletion */
	if (g_nck >= 400) { ck_free(&g_ck[0]); memmove(g_ck, g_ck + 1, (g_nck - 1) * sizeof(Cookie)); g_nck--; }
	g_ck = (Cookie *)realloc(g_ck, (g_nck + 1) * sizeof(Cookie));
	g_ck[g_nck].domain = xstrdup(domain);
	g_ck[g_nck].path = xstrdup(path);
	g_ck[g_nck].name = xstrdup(name);
	g_ck[g_nck].value = xstrdup(value);
	g_ck[g_nck].hostonly = hostonly;
	g_ck[g_nck].secure = secure;
	g_ck[g_nck].expires = expires;
	g_nck++;
}

static DWORD parse_http_date(const char *s)
{
	static const char *mon[] = { "jan","feb","mar","apr","may","jun","jul","aug","sep","oct","nov","dec" };
	int d = 0, m = -1, y = 0, hh = 0, mm = 0, ss = 0, i;
	const char *p = s;
	while (*p) {
		if (*p >= '0' && *p <= '9') {
			int v = atoi(p);
			int n = 0;
			while (p[n] >= '0' && p[n] <= '9') n++;
			if (p[n] == ':') { hh = v; mm = atoi(p + n + 1); { const char *q = strchr(p + n + 1, ':'); if (q) ss = atoi(q + 1); } p += n; while (*p && *p != ' ') p++; continue; }
			if (n == 4) y = v; else if (!d && n <= 2) d = v; else if (n == 2 && d) y = v < 70 ? 2000 + v : 1900 + v;
			p += n;
		} else if ((*p | 32) >= 'a' && (*p | 32) <= 'z') {
			for (i = 0; i < 12; i++) if (strnieq(p, mon[i], 3)) m = i;
			while ((*p | 32) >= 'a' && (*p | 32) <= 'z') p++;
		} else p++;
	}
	if (m < 0 || !y || !d) return 0;
	{
		int yy = y, mo = m + 1;
		long era; unsigned yoe, doy, doe; long days;
		yy -= mo <= 2;
		era = (yy >= 0 ? yy : yy - 399) / 400;
		yoe = (unsigned)(yy - era * 400);
		doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
		doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		days = era * 146097 + (long)doe - 719468;
		if (days < 1) return 1;
		return (DWORD)(days * 86400L + hh * 3600L + mm * 60L + ss);
	}
}

static void set_cookie(const Url *u, const char *hdr)
{
	const char *p = hdr, *eq, *semi;
	char *name, *value, *domain = NULL, *path = NULL;
	int secure = 0;
	DWORD expires = 0;
	int have_maxage = 0;
	semi = strchr(p, ';');
	if (!semi) semi = p + strlen(p);
	eq = memchr(p, '=', semi - p);
	if (!eq) return;
	while (*p == ' ') p++;
	name = xstrndup(p, (int)(eq - p));
	{ int n = (int)strlen(name); while (n > 0 && name[n - 1] == ' ') name[--n] = 0; }
	value = xstrndup(eq + 1, (int)(semi - eq - 1));
	p = semi;
	while (*p == ';') {
		const char *a, *ae, *ve;
		p++;
		while (*p == ' ') p++;
		a = p;
		ve = strchr(p, ';');
		if (!ve) ve = p + strlen(p);
		ae = memchr(a, '=', ve - a);
		if (ae) {
			int kl = (int)(ae - a);
			char *v = xstrndup(ae + 1, (int)(ve - ae - 1));
			if (kl == 6 && strnieq(a, "domain", 6)) { free(domain); domain = xstrdup(v[0] == '.' ? v + 1 : v); { char *c; for (c = domain; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32; } }
			else if (kl == 4 && strnieq(a, "path", 4)) { free(path); path = xstrdup(v); }
			else if (kl == 7 && strnieq(a, "max-age", 7)) { long ma = atol(v); have_maxage = 1; expires = ma <= 0 ? 1 : now_unix() + (DWORD)ma; }
			else if (kl == 7 && strnieq(a, "expires", 7) && !have_maxage) { expires = parse_http_date(v); }
			free(v);
		} else if ((ve - a) >= 6 && strnieq(a, "secure", 6)) secure = 1;
		p = ve;
	}
	if (domain) {
		/* domain must domain-match host */
		int hl = (int)strlen(u->host), dl = (int)strlen(domain);
		if (!(strieq(u->host, domain) || (hl > dl && u->host[hl - dl - 1] == '.' && strieq(u->host + hl - dl, domain))) || !strchr(domain, '.')) {
			free(domain); domain = NULL;
		}
	}
	if (!path || path[0] != '/') {
		/* default path */
		const char *q = u->path;
		const char *last = q, *e = q;
		while (*e && *e != '?') { if (*e == '/') last = e; e++; }
		free(path);
		path = (last == q) ? xstrdup("/") : xstrndup(q, (int)(last - q));
	}
	EnterCriticalSection(&g_cs);
	ck_add(domain ? domain : u->host, domain ? 0 : 1, path, secure, expires, name, value);
	LeaveCriticalSection(&g_cs);
	free(name); free(value); free(domain); free(path);
}

static void cookie_header(const Url *u, Buf *out)
{
	int i, n = 0;
	int https = strcmp(u->scheme, "https") == 0;
	DWORD now = now_unix();
	EnterCriticalSection(&g_cs);
	for (i = 0; i < g_nck; i++) {
		Cookie *c = &g_ck[i];
		int hl = (int)strlen(u->host), dl = (int)strlen(c->domain), pl = (int)strlen(c->path);
		if (c->expires && c->expires < now) continue;
		if (c->secure && !https) continue;
		if (c->hostonly) { if (!strieq(u->host, c->domain)) continue; }
		else if (!(strieq(u->host, c->domain) || (hl > dl && u->host[hl - dl - 1] == '.' && strieq(u->host + hl - dl, c->domain)))) continue;
		if (strncmp(u->path, c->path, pl) != 0) continue;
		if (pl > 1 && c->path[pl - 1] != '/' && u->path[pl] && u->path[pl] != '/' && u->path[pl] != '?') continue;
		buf_adds(out, n ? "; " : "Cookie: ");
		buf_adds(out, c->name);
		buf_addc(out, '=');
		buf_adds(out, c->value);
		n++;
	}
	LeaveCriticalSection(&g_cs);
	if (n) buf_adds(out, "\r\n");
}

void cookies_save(void)
{
	WCHAR path[MAX_PATH];
	FILE *f;
	int i;
	app_path(path, L"cookies.txt");
	f = _wfopen(path, L"wb");
	if (!f) return;
	EnterCriticalSection(&g_cs);
	for (i = 0; i < g_nck; i++) {
		Cookie *c = &g_ck[i];
		if (!c->expires) continue;
		fprintf(f, "%s\t%d\t%s\t%d\t%lu\t%s\t%s\n", c->domain, c->hostonly, c->path, c->secure, (unsigned long)c->expires, c->name, c->value);
	}
	LeaveCriticalSection(&g_cs);
	fclose(f);
}

void cookies_load(void)
{
	WCHAR path[MAX_PATH];
	FILE *f;
	char line[8192];
	app_path(path, L"cookies.txt");
	f = _wfopen(path, L"rb");
	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		char *fld[7];
		int n = 0;
		char *p = line;
		{ int l = (int)strlen(line); while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0; }
		fld[n++] = p;
		while (*p && n < 7) { if (*p == '\t') { *p = 0; fld[n++] = p + 1; } p++; }
		if (n < 7) continue;
		ck_add(fld[0], atoi(fld[1]), fld[2], atoi(fld[3]), (DWORD)strtoul(fld[4], NULL, 10), fld[5], fld[6]);
	}
	fclose(f);
}

void cookies_clear(void)
{
	int i;
	EnterCriticalSection(&g_cs);
	for (i = 0; i < g_nck; i++) ck_free(&g_ck[i]);
	free(g_ck); g_ck = NULL; g_nck = 0;
	LeaveCriticalSection(&g_cs);
	cookies_save();
}

/* ------------------------------------------------------------------ */
/* HTTP                                                                */
/* ------------------------------------------------------------------ */
void net_cancel(NetCtl *c)
{
	c->abort = 1;
}

void http_resp_free(HttpResp *r)
{
	free(r->ctype); free(r->charset); free(r->url); free(r->refresh); free(r->disposition);
	buf_free(&r->body);
	memset(r, 0, sizeof *r);
}

static int decompress(HttpResp *r, const char *enc)
{
	char *out = NULL;
	int outlen = 0;
	const unsigned char *d = (const unsigned char *)r->body.p;
	int n = r->body.len;
	if (!n) return 1;
	if (stristr(enc, "gzip")) {
		int pos = 10;
		int flg;
		if (n < 18 || d[0] != 0x1f || d[1] != 0x8b) return 1; /* not actually gzipped */
		flg = d[3];
		if (flg & 4) { if (pos + 2 > n) return 0; pos += 2 + (d[pos] | (d[pos + 1] << 8)); }
		if (flg & 8) { while (pos < n && d[pos]) pos++; pos++; }
		if (flg & 16) { while (pos < n && d[pos]) pos++; pos++; }
		if (flg & 2) pos += 2;
		if (pos >= n) return 0;
		out = stbi_zlib_decode_noheader_malloc((const char *)d + pos, n - pos, &outlen);
	} else if (stristr(enc, "deflate")) {
		out = stbi_zlib_decode_malloc((const char *)d, n, &outlen);
		if (!out) out = stbi_zlib_decode_noheader_malloc((const char *)d, n, &outlen);
	} else return 1;
	if (!out) return 0;
	buf_free(&r->body);
	buf_add(&r->body, out, outlen);
	free(out);
	return 1;
}

static void body_put(HttpResp *r, const void *d, int n)
{
	if (r->sink) { fwrite(d, 1, n, (FILE *)r->sink); r->streamed += n; }
	else buf_add(&r->body, d, n);
}
#define BODYLEN(r) ((r)->sink ? (r)->streamed : (long)(r)->body.len)

static int read_body(Conn *c, HttpResp *r, int chunked, long clen, NetCtl *ctl, int maxbytes, int *complete)
{
	*complete = 0;
	if (chunked) {
		Buf line;
		buf_init(&line);
		for (;;) {
			long sz;
			if (!conn_readline(c, &line)) break;
			if (line.len == 0) continue;
			sz = strtol(line.p, NULL, 16);
			if (sz <= 0) {
				/* last chunk: consume trailers up to the blank line */
				while (conn_readline(c, &line) && line.len > 0) {}
				*complete = 1;
				break;
			}
			while (sz > 0) {
				int take;
				if (c->rpos >= c->rlen && !conn_fill(c)) { buf_free(&line); return BODYLEN(r) > 0; }
				take = c->rlen - c->rpos;
				if (take > sz) take = (int)sz;
				body_put(r, c->rb + c->rpos, take);
				c->rpos += take;
				sz -= take;
				if (ctl && ctl->progress) ctl->progress(ctl->ctx, (int)BODYLEN(r), -1);
				if (ctl && ctl->abort) { buf_free(&line); return 0; }
				if (!r->sink && r->body.len > maxbytes) { buf_free(&line); strcpy(r->err, "파일이 너무 큼"); return 0; }
			}
		}
		buf_free(&line);
		return 1;
	}
	for (;;) {
		int take;
		if (clen >= 0 && BODYLEN(r) >= clen) break;
		if (c->rpos >= c->rlen && !conn_fill(c)) break;
		take = c->rlen - c->rpos;
		if (clen >= 0 && take > clen - BODYLEN(r)) take = (int)(clen - BODYLEN(r));
		body_put(r, c->rb + c->rpos, take);
		c->rpos += take;
		if (ctl && ctl->progress) ctl->progress(ctl->ctx, (int)BODYLEN(r), (int)clen);
		if (ctl && ctl->abort) return 0;
		if (!r->sink && r->body.len > maxbytes) { strcpy(r->err, "파일이 너무 큼"); return 0; }
	}
	if (clen >= 0 && BODYLEN(r) >= clen) *complete = 1;
	return 1;
}

/* ---- keep-alive pool: idle connections for reuse (saves TCP + TLS setup) ---- */
#define POOL_N 4
typedef struct { Conn *c; char host[128]; int port, tls; DWORD t; } PoolEnt;
static PoolEnt g_pool[POOL_N];

static Conn *pool_take(const Url *u)
{
	int i, tls = !strcmp(u->scheme, "https");
	Conn *c = NULL;
	DWORD now = GetTickCount();
	EnterCriticalSection(&g_cs);
	for (i = 0; i < POOL_N; i++) {
		PoolEnt *e = &g_pool[i];
		if (!e->c) continue;
		if (now - e->t > 45000) { conn_close(e->c); free(e->c); e->c = NULL; continue; } /* server likely closed it */
		if (!c && e->port == u->port && e->tls == tls && strieq(e->host, u->host)) { c = e->c; e->c = NULL; }
	}
	LeaveCriticalSection(&g_cs);
	return c;
}

static void pool_put(Conn *c, const Url *u)
{
	int i, slot = -1;
	DWORD oldest = 0, now = GetTickCount();
	if (strlen(u->host) >= sizeof g_pool[0].host) { conn_close(c); free(c); return; }
	c->ctl = NULL;
	EnterCriticalSection(&g_cs);
	for (i = 0; i < POOL_N; i++) {
		if (!g_pool[i].c) { slot = i; break; }
		if (now - g_pool[i].t >= oldest) { oldest = now - g_pool[i].t; slot = i; }
	}
	if (g_pool[slot].c) { conn_close(g_pool[slot].c); free(g_pool[slot].c); }
	g_pool[slot].c = c;
	strcpy(g_pool[slot].host, u->host);
	g_pool[slot].port = u->port;
	g_pool[slot].tls = !strcmp(u->scheme, "https");
	g_pool[slot].t = now;
	LeaveCriticalSection(&g_cs);
}

static int do_one(const char *url, const char *method, const char *body, int bodylen, const char *ctype,
                  const char *referer, HttpResp *r, NetCtl *ctl, int maxbytes, char **location)
{
	Url u;
	Conn *c;
	Buf req, line;
	int chunked = 0, ok = 0, reused = 0, keep = 0, closehdr = 0, http10 = 0, complete = 0, attempt;
	long clen = -1;
	char *cenc = NULL;
	*location = NULL;
	if (!url_parse(url, &u)) { _snprintf(r->err, sizeof r->err, "잘못된 주소: %.180s", url); return 0; }
	if (strcmp(u.scheme, "http") && strcmp(u.scheme, "https")) { strcpy(r->err, "지원하지 않는 주소 형식"); url_free(&u); return 0; }
	buf_init(&req);
	buf_init(&line);
	c = NULL;
for (attempt = 0; attempt < 2; attempt++) {
	c = attempt == 0 ? pool_take(&u) : NULL;
	if (c) {
		reused = 1;
		c->ctl = ctl;
		if (ctl) ctl->sock = (unsigned int)c->s;
	} else {
		reused = 0;
		c = (Conn *)malloc(sizeof(Conn));
		if (!c) { strcpy(r->err, "메모리 부족"); buf_free(&req); buf_free(&line); url_free(&u); return 0; }
		if (!conn_open(c, &u, ctl, r)) { free(c); buf_free(&req); buf_free(&line); url_free(&u); return 0; }
	}
	req.len = 0;
	buf_addf(&req, "%s %s HTTP/1.1\r\n", method, u.path);
	if ((u.port == 443 && !strcmp(u.scheme, "https")) || (u.port == 80 && !strcmp(u.scheme, "http")))
		buf_addf(&req, "Host: %s\r\n", u.host);
	else buf_addf(&req, "Host: %s:%d\r\n", u.host, u.port);
	buf_addf(&req, "User-Agent: %s\r\n", (ctl && ctl->ua) ? ctl->ua : g_ua);
	buf_adds(&req, "Accept: text/html,application/xhtml+xml,text/plain;q=0.9,image/png,image/jpeg,image/gif;q=0.8,*/*;q=0.5\r\n");
	buf_adds(&req, "Accept-Language: ko-KR,ko;q=0.9,en;q=0.5\r\n");
	buf_adds(&req, "Accept-Encoding: gzip, deflate\r\n");
	buf_adds(&req, "Connection: keep-alive\r\n");
	if (referer && (strnieq(referer, "http:", 5) || strnieq(referer, "https:", 6))) {
		/* don't leak https referer to http */
		if (!(strnieq(referer, "https:", 6) && !strcmp(u.scheme, "http"))) {
			char *rf = url_strip_fragment(referer);
			buf_addf(&req, "Referer: %s\r\n", rf);
			free(rf);
		}
	}
	if (ctl && ctl->hdrs) buf_adds(&req, ctl->hdrs);
	cookie_header(&u, &req);
	if (g_auth_token[0] && !strcmp(u.scheme, "https") && strieq(u.host, g_auth_host))
		buf_addf(&req, "Authorization: token %s\r\n", g_auth_token);
	if (body) {
		buf_addf(&req, "Content-Type: %s\r\n", ctype ? ctype : "application/x-www-form-urlencoded");
		buf_addf(&req, "Content-Length: %d\r\n", bodylen);
	}
	buf_adds(&req, "\r\n");
	if (!conn_write(c, req.p, req.len) || (body && bodylen && !conn_write(c, body, bodylen)) || !conn_flush(c)) {
		if (reused && !(ctl && ctl->abort)) { conn_close(c); free(c); c = NULL; continue; } /* stale: reconnect */
		if (c->tls) tls_fail(c, r, &u);
		else if (!r->err[0]) strcpy(r->err, ctl && ctl->abort ? "취소됨" : "요청 전송 실패");
		goto done;
	}
	/* status line */
	if (!conn_readline(c, &line) || strncmp(line.p, "HTTP/", 5) != 0) {
		if (reused && !(ctl && ctl->abort)) { conn_close(c); free(c); c = NULL; continue; }
		if (c->tls && br_ssl_engine_last_error(&c->sc.eng)) tls_fail(c, r, &u);
		else if (!r->err[0]) strcpy(r->err, ctl && ctl->abort ? "취소됨" : "서버 응답 없음");
		goto done;
	}
	break;
}
	if (!c) { if (!r->err[0]) strcpy(r->err, "서버 응답 없음"); goto done; }
	if (!strncmp(line.p, "HTTP/1.0", 8)) http10 = 1;
	{
		const char *sp = strchr(line.p, ' ');
		r->status = sp ? atoi(sp + 1) : 0;
	}
	if (c->tls) {
		br_ssl_session_parameters sp;
		static const struct { int id; const char *n; } suites[] = {
			{ BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, "ECDHE-ECDSA-AES128-GCM" },
			{ BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256, "ECDHE-RSA-AES128-GCM" },
			{ BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384, "ECDHE-ECDSA-AES256-GCM" },
			{ BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384, "ECDHE-RSA-AES256-GCM" },
			{ BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256, "ECDHE-ECDSA-CHACHA20" },
			{ BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256, "ECDHE-RSA-CHACHA20" },
			{ 0, NULL } };
		int i;
		const char *sn = "기타";
		br_ssl_engine_get_session_parameters(&c->sc.eng, &sp);
		sess_put(u.host, u.port, &sp);
		for (i = 0; suites[i].n; i++) if (suites[i].id == sp.cipher_suite) sn = suites[i].n;
		_snprintf(r->tlsinfo, sizeof r->tlsinfo, "TLS 1.%d, %s%s", (sp.version & 0xff) - 1, sn,
		          c->xw.err ? " (인증서 검증 무시됨)" : "");
	}
	/* headers */
	for (;;) {
		char *colon, *v;
		if (!conn_readline(c, &line)) break;
		if (line.len == 0) break;
		colon = strchr(line.p, ':');
		if (!colon) continue;
		*colon = 0;
		v = colon + 1;
		while (*v == ' ' || *v == '\t') v++;
		if (strieq(line.p, "content-type")) {
			const char *semi = strchr(v, ';');
			const char *cs;
			char *t;
			free(r->ctype);
			r->ctype = semi ? xstrndup(v, (int)(semi - v)) : xstrdup(v);
			for (t = r->ctype; *t; t++) { if (*t >= 'A' && *t <= 'Z') *t += 32; if (*t == ' ') *t = 0; }
			cs = stristr(v, "charset=");
			if (cs) {
				const char *e;
				cs += 8;
				if (*cs == '"' || *cs == '\'') cs++;
				e = cs;
				while (*e && *e != ';' && *e != '"' && *e != '\'' && *e != ' ') e++;
				free(r->charset);
				r->charset = xstrndup(cs, (int)(e - cs));
			}
		} else if (strieq(line.p, "content-length")) clen = atol(v);
		else if (strieq(line.p, "transfer-encoding")) { if (stristr(v, "chunked")) chunked = 1; }
		else if (strieq(line.p, "content-encoding")) { free(cenc); cenc = xstrdup(v); }
		else if (strieq(line.p, "location")) { free(*location); *location = xstrdup(v); }
		else if (strieq(line.p, "set-cookie")) set_cookie(&u, v);
		else if (strieq(line.p, "refresh")) { free(r->refresh); r->refresh = xstrdup(v); }
		else if (strieq(line.p, "content-disposition")) { free(r->disposition); r->disposition = xstrdup(v); }
		else if (strieq(line.p, "date")) r->date = parse_http_date(v);
		else if (strieq(line.p, "connection")) { if (stristr(v, "close")) closehdr = 1; }
	}
	ok = 1;
	if (strcmp(method, "HEAD") == 0 || r->status == 204 || r->status == 304 || (r->status >= 100 && r->status < 200)) {
		keep = 1;
		goto done;
	}
	if (*location && r->status >= 300 && r->status < 400) goto done; /* skip body of redirects (connection is dropped) */
	if (ctl && ctl->on_headers && r->status >= 200 && r->status < 300 && !cenc)
		r->sink = ctl->on_headers(ctl->ctx, r);
	if (!read_body(c, r, chunked, clen, ctl, maxbytes, &complete)) {
		if (ctl && ctl->abort) { strcpy(r->err, "취소됨"); ok = 0; }
		else if (r->err[0]) ok = 0;
	}
	if (ok && r->sink && clen >= 0 && r->streamed < clen) { strcpy(r->err, "\xEB\x8B\xA4\xEC\x9A\xB4\xEB\xA1\x9C\xEB\x93\x9C\xEA\xB0\x80 \xEC\xA4\x91\xEA\xB0\x84\xEC\x97\x90 \xEB\x81\x8A\xEA\xB9\x80"); ok = 0; }
	if (ok && cenc && !decompress(r, cenc)) { strcpy(r->err, "압축 해제 실패"); ok = 0; }
	if (ok && complete) keep = 1;
done:
	buf_free(&req);
	buf_free(&line);
	free(cenc);
	if (c) {
		/* reuse only a cleanly finished response with nothing left over */
		if (keep && ok && !closehdr && !http10 && !(ctl && ctl->abort) && c->rpos >= c->rlen && !c->eof) {
			if (ctl) ctl->sock = (unsigned int)INVALID_SOCKET;
			pool_put(c, &u);
		}
		else { conn_close(c); free(c); }
	}
	url_free(&u);
	return ok;
}

static int is_time_err(int e) { return e == BR_ERR_X509_EXPIRED || e == BR_ERR_X509_TIME_UNKNOWN; }

/* The phone's clock may be wrong (old battery etc.), which makes every certificate
   look expired. Learn the real time from a plain-HTTP Date header and retry. */
static int time_sync_impl(NetCtl *ctl, int force)
{
	static const char *urls[] = { "http://www.google.com/generate_204", "http://clients3.google.com/generate_204", "http://www.msftconnecttest.com/connecttest.txt" };
	int i;
	if (g_time_synced && !force) return 0;
	g_time_synced = 1;
	for (i = 0; i < 3; i++) {
		HttpResp r;
		char *loc = NULL;
		memset(&r, 0, sizeof r);
		if (do_one(urls[i], "GET", NULL, 0, NULL, NULL, &r, ctl, 65536, &loc) && r.date > 1600000000UL) {
			long ofs = (long)(r.date - now_unix());
			/* ignore small differences (keeps the device clock when it is fine) */
			g_time_ofs = (ofs > 600 || ofs < -600) ? ofs : 0;
			tube_log("clock: device %lu, server %lu, offset %ld s", (unsigned long)now_unix(), (unsigned long)r.date, ofs);
			free(loc);
			http_resp_free(&r);
			return 1;
		}
		free(loc);
		http_resp_free(&r);
	}
	tube_log("clock: time sync failed");
	g_time_synced = 0;
	return 0;
}
static int time_sync(NetCtl *ctl) { return time_sync_impl(ctl, 0); }
int net_time_sync(NetCtl *ctl) { return time_sync_impl(ctl, 1); }

int http_request(const char *url, const char *method, const char *body, int bodylen,
                 const char *ctype, const char *referer, HttpResp *r, NetCtl *ctl, int maxbytes)
{
	char *cur = xstrdup(url);
	char *meth = xstrdup(method);
	char *ref = referer ? xstrdup(referer) : NULL;
	int hops;
	static int conn_once;
	memset(r, 0, sizeof *r);
	if (!conn_once) { conn_once = 1; ensure_connection(url); }
	for (hops = 0; hops < 10; hops++) {
		char *loc = NULL;
		int ok;
		HttpResp tmp;
		memset(&tmp, 0, sizeof tmp);
		ok = do_one(cur, meth, body, bodylen, ctype, ref, &tmp, ctl, maxbytes, &loc);
		if (!ok && is_time_err(tmp.certerr) && time_sync(ctl)) {
			free(loc); loc = NULL;
			http_resp_free(&tmp);
			memset(&tmp, 0, sizeof tmp);
			ok = do_one(cur, meth, body, bodylen, ctype, ref, &tmp, ctl, maxbytes, &loc);
		}
		if (ok && loc && tmp.status >= 300 && tmp.status < 400 && tmp.status != 304) {
			char *next = url_resolve(cur, loc);
			free(loc);
			if (tmp.status == 303 || ((tmp.status == 301 || tmp.status == 302) && strcmp(meth, "GET"))) {
				free(meth); meth = xstrdup("GET"); body = NULL; bodylen = 0;
			}
			free(ref); ref = cur;
			cur = next;
			http_resp_free(&tmp);
			continue;
		}
		free(loc);
		*r = tmp;
		r->url = cur;
		free(meth); free(ref);
		return ok;
	}
	strcpy(r->err, "리디렉션이 너무 많음");
	r->url = cur;
	free(meth); free(ref);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Streaming range reader (keep-alive)                                 */
/* ------------------------------------------------------------------ */
struct HStream {
	NetCtl *ctl;
	Conn *c;
	int open;            /* connection usable */
	char host[256];
	int port, tls;
	int reusable;        /* server allows keep-alive */
	long remain;         /* body bytes left (-1 = until close) */
	int chunked;
	long chunk_left;     /* chunked: bytes left in current chunk, -1 = need size line */
	int body_done;
	long total;
	int post, rn, rangeparam;
	long reqno;
	int certerr;
	char ctype[64];
	char err[200];
};

void hs_options(HStream *h, int post, int rn, int rangeparam) { h->post = post; h->rn = rn; h->rangeparam = rangeparam; }
const char *hs_ctype(HStream *h) { return h->ctype; }

HStream *hs_new(NetCtl *ctl)
{
	HStream *h = (HStream *)calloc(1, sizeof(HStream));
	if (!h) return NULL;
	h->ctl = ctl;
	h->total = -1;
	h->body_done = 1;
	return h;
}

static void hs_close_conn(HStream *h)
{
	if (h->c) {
		if (h->open) conn_close(h->c);
		free(h->c);
		h->c = NULL;
	}
	h->open = 0;
}

void hs_free(HStream *h)
{
	if (!h) return;
	hs_close_conn(h);
	free(h);
}

const char *hs_err(HStream *h) { return h->err; }
long hs_total(HStream *h) { return h->total; }

static void fmt_long(char *out, long v)
{
	char t[24];
	int n = 0, i = 0;
	if (v < 0) { out[i++] = '-'; v = -v; }
	do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
	while (n) out[i++] = t[--n];
	out[i] = 0;
}

/* one request on (possibly reused) connection. returns status, 0 = io failure. */
static int hs_one(HStream *h, const Url *u, long start, long end, char **location, int *retry_ok)
{
	Buf req, line;
	int status = 0, conn_close_hdr = 0;
	long clen = -1;
	char a[24], b[24];
	*location = NULL;
	*retry_ok = 0;
	h->chunked = 0;
	h->ctype[0] = 0;
	h->chunk_left = -1;
	h->remain = -1;
	h->body_done = 0;
	if (h->open && (!h->reusable || strcmp(h->host, u->host) || h->port != u->port || h->tls != !strcmp(u->scheme, "https")))
		hs_close_conn(h);
	if (h->open) *retry_ok = 1;   /* reused connection may have been closed by server */
	if (!h->open) {
		HttpResp tmp;
		memset(&tmp, 0, sizeof tmp);
		if (!h->c) h->c = (Conn *)malloc(sizeof(Conn));
		if (!h->c) { strcpy(h->err, "메모리 부족"); return 0; }
		if (!conn_open(h->c, u, h->ctl, &tmp)) {
			strncpy(h->err, tmp.err[0] ? tmp.err : "연결 실패", sizeof h->err - 1);
			free(h->c); h->c = NULL;
			return 0;
		}
		h->open = 1;
		strncpy(h->host, u->host, sizeof h->host - 1);
		h->port = u->port;
		h->tls = !strcmp(u->scheme, "https");
		h->reusable = 1;
	}
	buf_init(&req);
	buf_init(&line);
	buf_adds(&req, h->post ? "POST " : "GET ");
	buf_adds(&req, u->path);
	buf_adds(&req, " HTTP/1.1\r\n");
	if ((u->port == 443 && h->tls) || (u->port == 80 && !h->tls)) buf_addf(&req, "Host: %s\r\n", u->host);
	else buf_addf(&req, "Host: %s:%d\r\n", u->host, u->port);
	buf_addf(&req, "User-Agent: %s\r\n", (h->ctl && h->ctl->ua) ? h->ctl->ua : g_ua);
	buf_adds(&req, "Accept: */*\r\n");
	if (h->ctl && h->ctl->hdrs) buf_adds(&req, h->ctl->hdrs);
	cookie_header(u, &req);
	fmt_long(a, start);
	if (h->rangeparam) {}
	else if (end >= 0) { fmt_long(b, end); buf_addf(&req, "Range: bytes=%s-%s\r\n", a, b); }
	else if (start > 0) buf_addf(&req, "Range: bytes=%s-\r\n", a);
	buf_adds(&req, "Accept-Encoding: identity\r\nTE: trailers\r\n");
	if (h->post) buf_adds(&req, "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: 2\r\n");
	buf_adds(&req, "Connection: keep-alive\r\n\r\n");
	if (h->post) buf_add(&req, "x\0", 2);
	if (!conn_write(h->c, req.p, req.len) || !conn_flush(h->c)) {
		if (h->tls) { HttpResp t; memset(&t, 0, sizeof t); tls_fail(h->c, &t, u); strncpy(h->err, t.err, sizeof h->err - 1); h->certerr = t.certerr; }
		else strcpy(h->err, "요청 전송 실패");
		goto fail;
	}
	if (!conn_readline(h->c, &line) || strncmp(line.p, "HTTP/", 5) != 0) {
		strcpy(h->err, (h->ctl && h->ctl->abort) ? "취소됨" : "서버 응답 없음");
		goto fail;
	}
	{
		const char *sp = strchr(line.p, ' ');
		status = sp ? atoi(sp + 1) : 0;
		if (!strncmp(line.p, "HTTP/1.0", 8)) h->reusable = 0;
	}
	*retry_ok = 0;
	for (;;) {
		char *colon, *v;
		if (!conn_readline(h->c, &line)) { strcpy(h->err, "응답 헤더 오류"); goto fail; }
		if (line.len == 0) break;
		colon = strchr(line.p, ':');
		if (!colon) continue;
		*colon = 0;
		v = colon + 1;
		while (*v == ' ' || *v == '\t') v++;
		if (strieq(line.p, "content-length")) clen = atol(v);
		else if (strieq(line.p, "transfer-encoding")) { if (stristr(v, "chunked")) h->chunked = 1; }
		else if (strieq(line.p, "connection")) { if (stristr(v, "close")) conn_close_hdr = 1; }
		else if (strieq(line.p, "location")) { free(*location); *location = xstrdup(v); }
		else if (strieq(line.p, "content-type")) { strncpy(h->ctype, v, sizeof h->ctype - 1); h->ctype[sizeof h->ctype - 1] = 0; }
		else if (strieq(line.p, "content-range")) {
			const char *sl = strchr(v, '/');
			if (sl && sl[1] != '*') h->total = atol(sl + 1);
		}
	}
	if (conn_close_hdr) h->reusable = 0;
	if (status == 200 && h->total < 0 && clen >= 0 && start == 0) h->total = clen;
	if (h->chunked) h->remain = -1;
	else if (clen >= 0) h->remain = clen;
	else { h->remain = -1; h->reusable = 0; }
	if (status == 204 || status == 304) h->remain = 0;
	if (h->remain == 0) h->body_done = 1;
	buf_free(&req);
	buf_free(&line);
	return status;
fail:
	buf_free(&req);
	buf_free(&line);
	hs_close_conn(h);
	h->body_done = 1;
	return 0;
}

/* discard the rest of a (small) body so the connection can be reused */
static void hs_drain(HStream *h)
{
	unsigned char tmp[1024];
	int guard = 0;
	if (h->body_done) return;
	if (h->remain > 65536 || h->remain < 0) { hs_close_conn(h); h->body_done = 1; return; }
	while (!h->body_done && guard++ < 100) if (hs_read(h, tmp, sizeof tmp) < 0) break;
}

int hs_get(HStream *h, const char *url0, long start, long end)
{
	char *cur;
	int hops;
	cur = (char *)malloc(strlen(url0) + 64);
	strcpy(cur, url0);
	if (h->rn) {
		char n[16];
		fmt_long(n, h->reqno++);
		sprintf(cur + strlen(cur), "%crn=%s", strchr(cur, '?') ? '&' : '?', n);
	}
	if (h->rangeparam) {
		char a[16], b[16];
		fmt_long(a, start);
		if (end >= 0) fmt_long(b, end); else b[0] = 0;
		sprintf(cur + strlen(cur), "%crange=%s-%s", strchr(cur, '?') ? '&' : '?', a, b);
	}
	h->err[0] = 0;
	if (!h->body_done) {
		if (h->remain > 0 && h->remain <= 65536) hs_drain(h);
		else hs_close_conn(h);
	}
	for (hops = 0; hops < 6; hops++) {
		Url u;
		char *loc = NULL;
		int st, retry_ok, attempt;
		if (!url_parse(cur, &u)) { strcpy(h->err, "잘못된 주소"); free(cur); return 0; }
		for (attempt = 0; attempt < 3; attempt++) {
			h->certerr = 0;
			st = hs_one(h, &u, start, end, &loc, &retry_ok);
			if (!st && is_time_err(h->certerr) && time_sync(h->ctl)) { hs_close_conn(h); continue; }
			if (st || !retry_ok || (h->ctl && h->ctl->abort)) break;
			hs_close_conn(h); /* stale keep-alive connection: reconnect once */
		}
		url_free(&u);
		if (st >= 300 && st < 400 && loc) {
			char *next = url_resolve(cur, loc);
			free(loc);
			hs_drain(h);
			free(cur);
			cur = next;
			continue;
		}
		free(loc);
		free(cur);
		if (st >= 400 && !h->err[0]) {
			char s[16];
			fmt_long(s, st);
			_snprintf(h->err, sizeof h->err, "HTTP %s", s);
		}
		return st;
	}
	free(cur);
	strcpy(h->err, "리디렉션이 너무 많음");
	return 0;
}

int hs_read(HStream *h, unsigned char *buf, int n)
{
	Conn *c = h->c;
	int take;
	if (h->body_done) return 0;
	if (!h->open || !c) return -1;
	if (h->chunked) {
		if (h->chunk_left <= 0) {
			Buf line;
			buf_init(&line);
			if (h->chunk_left == 0) { if (!conn_readline(c, &line)) { buf_free(&line); hs_close_conn(h); return -1; } } /* CRLF after chunk */
			if (!conn_readline(c, &line)) { buf_free(&line); hs_close_conn(h); return -1; }
			h->chunk_left = strtol(line.p, NULL, 16);
			if (h->chunk_left <= 0) {
				/* trailers */
				while (conn_readline(c, &line) && line.len > 0) {}
				buf_free(&line);
				h->body_done = 1;
				return 0;
			}
			buf_free(&line);
		}
		if (c->rpos >= c->rlen && !conn_fill(c)) { hs_close_conn(h); return -1; }
		take = c->rlen - c->rpos;
		if (take > n) take = n;
		if (take > h->chunk_left) take = (int)h->chunk_left;
		memcpy(buf, c->rb + c->rpos, take);
		c->rpos += take;
		h->chunk_left -= take;
		return take;
	}
	if (h->remain == 0) { h->body_done = 1; return 0; }
	if (c->rpos >= c->rlen) {
		/* large reads go straight to the caller's buffer */
		if (n >= (int)sizeof c->rb && (h->remain < 0 || h->remain >= n)) {
			int r;
			if (c->eof) r = 0;
			else if (c->tls) r = br_sslio_read(&c->io, buf, n);
			else r = sock_read(c, buf, n);
			if (r <= 0) {
				hs_close_conn(h);
				if (h->remain < 0) { h->body_done = 1; return 0; }
				return -1;
			}
			if (h->remain > 0) { h->remain -= r; if (!h->remain) h->body_done = 1; }
			return r;
		}
		if (!conn_fill(c)) {
			hs_close_conn(h);
			if (h->remain < 0) { h->body_done = 1; return 0; }
			return -1;
		}
	}
	take = c->rlen - c->rpos;
	if (take > n) take = n;
	if (h->remain >= 0 && take > h->remain) take = (int)h->remain;
	memcpy(buf, c->rb + c->rpos, take);
	c->rpos += take;
	if (h->remain > 0) { h->remain -= take; if (!h->remain) h->body_done = 1; }
	return take;
}
