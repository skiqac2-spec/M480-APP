#ifndef PKG_H
#define PKG_H
#include "types.h"
#include "zip.h"

enum { PKG_JAVA = 0, PKG_KTF_NATIVE = 1, PKG_LGT_NATIVE = 2, PKG_CLET = 3, PKG_BAD = 4 };

typedef struct Pkg {
    char path[512];           /* game file or folder */
    char name[128];           /* UTF-8 display name */
    char id[64];              /* save folder id */
    char mainclass[200];
    int carrier_guess;        /* CARRIER_* */
    int kind;                 /* PKG_* */
    int scr_w, scr_h;         /* from descriptor, 0 = unknown */
    Zip *outer;               /* zip given by the user (if any) */
    Zip *jar;                 /* the jar with classes/resources */
    u8 *jarbuf;               /* owned copy of an inner jar */
    char folder[512];         /* folder container (if any) */
    char props[4096];         /* "key\0value\0..." from descriptor/manifest */
    int nprops;
    int uses_wipi, uses_skt;
} Pkg;

int pkg_open(Pkg *p, const char *path);      /* 1 ok */
void pkg_close(Pkg *p);
u8 *pkg_resource(Pkg *p, const char *name, u32 *len);
const char *pkg_kind_text(int kind);
/* quick scan for the launcher list (no class loading) */
int pkg_probe(const char *path, char *name, int namemax, int *carrier, int *kind);
#endif
