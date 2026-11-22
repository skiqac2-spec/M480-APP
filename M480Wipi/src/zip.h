#ifndef ZIP_H
#define ZIP_H
#include "types.h"

typedef struct ZipEnt { char *name; u16 method; u32 csize, usize, lhoff; } ZipEnt;
typedef struct Zip { const u8 *data; size_t len; int owned; long shift; ZipEnt *ents; int n; } Zip;

int inflate_raw(const u8 *src, size_t n, u8 *dst, size_t cap, size_t *outlen);
u8 *inflate_alloc(const u8 *src, size_t n, size_t *outlen);
u8 *zlib_alloc(const u8 *src, size_t n, size_t *outlen);

Zip *zip_open_mem(const u8 *data, size_t len, int owned);
void zip_close(Zip *z);
ZipEnt *zip_find(Zip *z, const char *name, int nocase);
const u8 *zip_read(Zip *z, ZipEnt *e, u32 *len, int *own);
u8 *zip_extract(Zip *z, const char *name, u32 *len);
#endif
