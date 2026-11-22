/* Linux / desktop implementation of pal.h (tests) */
#include "pal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

u32 pal_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (u32)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000); }
s64 pal_time_ms(void) { struct timeval tv; gettimeofday(&tv, NULL); return (s64)tv.tv_sec * 1000 + tv.tv_usec / 1000; }
void pal_log(const char *s) { fprintf(stderr, "%s\n", s); }
void *pal_fopen(const char *path, int mode)
{
    FILE *f = fopen(path, mode == 'r' ? "rb" : mode == 'w' ? "wb" : "r+b");
    if (!f && mode == 'a') f = fopen(path, "w+b");
    return f;
}
int pal_fread(void *f, void *buf, int n) { return (int)fread(buf, 1, n, (FILE *)f); }
int pal_fwrite(void *f, const void *buf, int n) { return (int)fwrite(buf, 1, n, (FILE *)f); }
int pal_fseek(void *f, long off) { return fseek((FILE *)f, off, SEEK_SET) == 0; }
long pal_fsize(void *f) { long p = ftell((FILE *)f), n; fseek((FILE *)f, 0, SEEK_END); n = ftell((FILE *)f); fseek((FILE *)f, p, SEEK_SET); return n; }
void pal_fclose(void *f) { fclose((FILE *)f); }
int pal_remove(const char *p) { return remove(p) == 0; }
int pal_mkdir(const char *p) { return mkdir(p, 0755) == 0; }
u8 *pal_load(const char *path, u32 *len)
{
    void *f = pal_fopen(path, 'r');
    long n;
    u8 *d;
    if (!f) return NULL;
    n = pal_fsize(f);
    d = (u8 *)malloc(n + 1);
    if (d && pal_fread(f, d, (int)n) != n) { free(d); d = NULL; }
    pal_fclose(f);
    if (d) { d[n] = 0; *len = (u32)n; }
    return d;
}
int pal_save(const char *path, const void *d, u32 len)
{
    void *f = pal_fopen(path, 'w');
    int ok;
    if (!f) return 0;
    ok = pal_fwrite(f, d, (int)len) == (int)len;
    pal_fclose(f);
    return ok;
}
int pal_listdir(const char *path, void (*cb)(void *u, const char *name, int is_dir, u32 size), void *u)
{
    DIR *d = opendir(path);
    struct dirent *e;
    if (!d) return 0;
    while ((e = readdir(d)) != NULL) {
        char full[1024];
        struct stat st;
        if (e->d_name[0] == '.') continue;
        snprintf(full, sizeof full, "%s/%s", path, e->d_name);
        if (stat(full, &st)) continue;
        cb(u, e->d_name, S_ISDIR(st.st_mode), (u32)st.st_size);
    }
    closedir(d);
    return 1;
}
