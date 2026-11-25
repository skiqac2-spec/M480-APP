/* force-included into every C/C++ file of the Windows CE build */
#ifndef CE_COMPAT_H
#define CE_COMPAT_H
#ifndef __ASSEMBLER__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
FILE *ce_fopen(const char *path, const char *mode);
#ifdef __cplusplus
}
#endif
#ifndef CE_COMPAT_IMPL
#define fopen ce_fopen
#endif
#include <sys/stat.h>
#include <sys/types.h>
typedef unsigned int u_int;
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned long u_long;
#define ffs __builtin_ffs
#ifndef __cplusplus
/* This compiler turns even memcpy(&word, p, 4) into a call through the OS import table.
 * Emulator inner loops do that millions of times a second, so copy tiny fixed sizes inline. */
static inline __attribute__((always_inline)) void *ce_memcpy_inl(void *d, const void *s, size_t n)
{
    if (__builtin_constant_p(n) && (n == 1 || n == 2 || n == 4 || n == 8)) {
        unsigned char *dd = (unsigned char *)d;
        const unsigned char *ss = (const unsigned char *)s;
        dd[0] = ss[0];
        if (n >= 2) dd[1] = ss[1];
        if (n >= 4) { dd[2] = ss[2]; dd[3] = ss[3]; }
        if (n >= 8) { dd[4] = ss[4]; dd[5] = ss[5]; dd[6] = ss[6]; dd[7] = ss[7]; }
        return d;
    }
    return memcpy(d, s, n);
}
#ifndef CE_COMPAT_IMPL
#define memcpy(d, s, n) ce_memcpy_inl(d, s, n)
#endif
/* Big heap blocks must not live in the 32MB process slot: see ce_compat.c */
void *ce_malloc(size_t size);
void *ce_calloc(size_t n, size_t size);
void *ce_realloc(void *p, size_t size);
void ce_free(void *p);
#ifndef CE_COMPAT_IMPL
#define malloc(n) ce_malloc(n)
#define calloc(n, s) ce_calloc(n, s)
#define realloc(p, n) ce_realloc(p, n)
#define free(p) ce_free(p)
#endif
#endif
#include <errno.h>
#define fstat(fd, st) (-1)
#define ftello ftell
#define fseeko fseek
#ifndef rewind
#define rewind(f) fseek(f, 0, SEEK_SET)
#endif
#endif
#endif
