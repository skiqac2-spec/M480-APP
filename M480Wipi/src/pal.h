/* platform abstraction (pal_ce.c on the phone, pal_host.c for PC tests) */
#ifndef PAL_H
#define PAL_H
#include "types.h"
u32 pal_ms(void);                 /* monotonic ms */
s64 pal_time_ms(void);            /* wall clock, ms since 1970 UTC */
void pal_log(const char *utf8);
/* files: paths are UTF-8; mode 'r', 'w' (create/truncate), 'a' (read/write, create) */
void *pal_fopen(const char *path, int mode);
int pal_fread(void *f, void *buf, int n);
int pal_fwrite(void *f, const void *buf, int n);
int pal_fseek(void *f, long off);
long pal_fsize(void *f);
void pal_fclose(void *f);
int pal_remove(const char *path);
int pal_mkdir(const char *path);
u8 *pal_load(const char *path, u32 *len);     /* whole file, malloc'd */
int pal_save(const char *path, const void *d, u32 len);
/* directory listing: calls cb for each entry (is_dir flag) */
int pal_listdir(const char *path, void (*cb)(void *u, const char *name, int is_dir, u32 size), void *u);
#endif
