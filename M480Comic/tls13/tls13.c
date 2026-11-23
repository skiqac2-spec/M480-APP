/* TLS 1.3 / 1.2 client on top of mbedTLS 3.6 (see tls13.h) */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mbedtls/ssl.h"
#include "mbedtls/threading.h"
#include "mbedtls/error.h"
#include "psa/crypto.h"
#include "tls13.h"

void entropy_get(unsigned char out[32]);   /* util.c */

struct T13 {
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	t13_io send_fn, recv_fn;
	void *ctx;
};

void mbedtls_platform_zeroize(void *buf, size_t len)
{
	volatile unsigned char *p = (volatile unsigned char *)buf;
	while (len--) *p++ = 0;
}

/* ---- threading (PSA key store is shared between download threads) ---- */
static void mx_init(mbedtls_threading_mutex_t *m) { InitializeCriticalSection(&m->cs); m->ok = 1; }
static void mx_free(mbedtls_threading_mutex_t *m) { if (m->ok) { DeleteCriticalSection(&m->cs); m->ok = 0; } }
static int mx_lock(mbedtls_threading_mutex_t *m) { if (!m->ok) return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA; EnterCriticalSection(&m->cs); return 0; }
static int mx_unlock(mbedtls_threading_mutex_t *m) { if (!m->ok) return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA; LeaveCriticalSection(&m->cs); return 0; }

/* ---- randomness from the app's entropy pool ---- */
static void fill_random(unsigned char *out, size_t n)
{
	unsigned char b[32];
	while (n > 0) {
		size_t k = n < 32 ? n : 32;
		entropy_get(b);
		memcpy(out, b, k);
		out += k; n -= k;
	}
	memset(b, 0, sizeof b);
}

psa_status_t mbedtls_psa_external_get_random(mbedtls_psa_external_random_context_t *context,
                                             uint8_t *output, size_t output_size, size_t *output_length)
{
	(void)context;
	fill_random(output, output_size);
	*output_length = output_size;
	return PSA_SUCCESS;
}

static int rng(void *p, unsigned char *out, size_t n) { (void)p; fill_random(out, n); return 0; }

static volatile int g_init_state;  /* 0 none, 2 ok, 3 failed */

/* called once from the main thread (net_init) before any worker thread starts */
int t13_global_init(void)
{
	if (g_init_state == 0) {
		mbedtls_threading_set_alt(mx_init, mx_free, mx_lock, mx_unlock);
		g_init_state = psa_crypto_init() == PSA_SUCCESS ? 2 : 3;
	}
	return g_init_state == 2;
}

/* ---- I/O glue ---- */
static int bio_send(void *p, const unsigned char *buf, size_t len)
{
	T13 *t = (T13 *)p;
	int r = t->send_fn(t->ctx, (unsigned char *)buf, len);
	return r > 0 ? r : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}
static int bio_recv(void *p, unsigned char *buf, size_t len)
{
	T13 *t = (T13 *)p;
	int r = t->recv_fn(t->ctx, buf, len);
	if (r == 0) return 0;
	return r > 0 ? r : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static const char *err_text(int e)
{
	switch (e) {
	case MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE: return "서버가 연결을 거부함";
	case MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE: return "서버와 공통 암호 방식 없음";
	case MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION: return "지원하지 않는 TLS 버전";
	case MBEDTLS_ERR_SSL_CONN_EOF: return "연결이 끊어짐";
	case MBEDTLS_ERR_SSL_INTERNAL_ERROR: return "연결이 끊어짐";
	case MBEDTLS_ERR_SSL_ALLOC_FAILED: return "메모리 부족";
	default: return NULL;
	}
}

T13 *t13_open(const char *host, t13_io send_fn, t13_io recv_fn, void *ctx, char *err, int errlen)
{
	T13 *t;
	int r;
	if (!t13_global_init()) { _snprintf(err, errlen, "TLS 1.3 초기화 실패"); return NULL; }
	t = (T13 *)calloc(1, sizeof(T13));
	if (!t) { _snprintf(err, errlen, "메모리 부족"); return NULL; }
	t->send_fn = send_fn; t->recv_fn = recv_fn; t->ctx = ctx;
	mbedtls_ssl_init(&t->ssl);
	mbedtls_ssl_config_init(&t->conf);
	r = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
	if (!r) {
		mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_NONE);
		mbedtls_ssl_conf_rng(&t->conf, rng, NULL);
		mbedtls_ssl_conf_min_tls_version(&t->conf, MBEDTLS_SSL_VERSION_TLS1_2);
		mbedtls_ssl_conf_max_tls_version(&t->conf, MBEDTLS_SSL_VERSION_TLS1_3);
		r = mbedtls_ssl_setup(&t->ssl, &t->conf);
	}
	if (!r && host) r = mbedtls_ssl_set_hostname(&t->ssl, host);
	if (r) { _snprintf(err, errlen, "TLS 1.3 설정 실패 (-0x%04X)", -r); t13_close(t); return NULL; }
	mbedtls_ssl_set_bio(&t->ssl, t, bio_send, bio_recv, NULL);
	do r = mbedtls_ssl_handshake(&t->ssl);
	while (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE);
	if (r) {
		const char *m = err_text(r);
		if (m) _snprintf(err, errlen, "보안 연결 실패(TLS 1.3): %s [-0x%04X]", m, -r);
		else _snprintf(err, errlen, "보안 연결 실패(TLS 1.3) (오류 -0x%04X)", -r);
		t13_close(t);
		return NULL;
	}
	return t;
}

int t13_write(T13 *t, const void *d, int n)
{
	const unsigned char *p = (const unsigned char *)d;
	while (n > 0) {
		int r = mbedtls_ssl_write(&t->ssl, p, (size_t)n);
		if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
		if (r <= 0) return 0;
		p += r; n -= r;
	}
	return 1;
}

int t13_read(T13 *t, void *d, int n)
{
	for (;;) {
		int r = mbedtls_ssl_read(&t->ssl, (unsigned char *)d, (size_t)n);
		if (r > 0) return r;
		if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
		if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
#endif
		if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
		return -1;
	}
}

void t13_info(T13 *t, char *out, int outlen)
{
	const char *v = mbedtls_ssl_get_version(&t->ssl);
	const char *c = mbedtls_ssl_get_ciphersuite(&t->ssl);
	_snprintf(out, outlen, "%s, %s (인증서 검증 안 함)", v ? v : "TLS", c ? c : "?");
	if (outlen > 0) out[outlen - 1] = 0;
}

void t13_close(T13 *t)
{
	if (!t) return;
	mbedtls_ssl_free(&t->ssl);
	mbedtls_ssl_config_free(&t->conf);
	free(t);
}
