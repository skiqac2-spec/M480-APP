#ifndef CE_SYS_STAT_H
#define CE_SYS_STAT_H
#include <sys/types.h>
#include <wchar.h>              /* the runtime's own "struct stat" lives here */
#ifdef __cplusplus
extern "C" {
#endif
#ifndef S_IFMT
#define S_IFMT  0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
int ce_stat(const char *path, struct stat *st);
#define stat(p, s) ce_stat(p, s)
#ifdef __cplusplus
}
#endif
#endif
