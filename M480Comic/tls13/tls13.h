/* TLS 1.3 / 1.2 client on top of mbedTLS 3.6, used when BearSSL (TLS 1.2 only)
 * cannot talk to a server. Certificates are not verified. */
#ifndef M480_TLS13_H
#define M480_TLS13_H

#include <stddef.h>

typedef struct T13 T13;
typedef int (*t13_io)(void *ctx, unsigned char *buf, size_t len);  /* >0 bytes, <0 error */

int  t13_global_init(void);
/* connects and performs the handshake; host may be NULL (IP address) */
T13 *t13_open(const char *host, t13_io send_fn, t13_io recv_fn, void *ctx, char *err, int errlen);
int  t13_write(T13 *t, const void *d, int n);   /* 1 ok, 0 error */
int  t13_read(T13 *t, void *d, int n);          /* >0 bytes, 0 end, <0 error */
void t13_info(T13 *t, char *out, int outlen);
void t13_close(T13 *t);

#endif
