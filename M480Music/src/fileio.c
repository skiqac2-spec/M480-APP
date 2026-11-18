#include <stdlib.h>
#include "fileio.h"

#ifdef _WIN32_WCE

struct MFile { HANDLE h; };

MFile *mf_open(const mchar *path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    MFile *f;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    f = (MFile *)malloc(sizeof(MFile));
    if (!f) { CloseHandle(h); return NULL; }
    f->h = h;
    return f;
}

void mf_close(MFile *f) { if (f) { CloseHandle(f->h); free(f); } }

unsigned long mf_size(MFile *f) { return GetFileSize(f->h, NULL); }

int mf_seek(MFile *f, unsigned long pos)
{
    return SetFilePointer(f->h, (LONG)pos, NULL, FILE_BEGIN) != 0xFFFFFFFF;
}

int mf_read(MFile *f, void *buf, int n)
{
    DWORD got = 0;
    if (!ReadFile(f->h, buf, (DWORD)n, &got, NULL)) return 0;
    return (int)got;
}

#else

#include <stdio.h>
struct MFile { FILE *fp; };

MFile *mf_open(const mchar *path)
{
    FILE *fp = fopen(path, "rb");
    MFile *f;
    if (!fp) return NULL;
    f = (MFile *)malloc(sizeof(MFile));
    f->fp = fp;
    return f;
}
void mf_close(MFile *f) { if (f) { fclose(f->fp); free(f); } }
unsigned long mf_size(MFile *f)
{
    long cur = ftell(f->fp), sz;
    fseek(f->fp, 0, SEEK_END); sz = ftell(f->fp); fseek(f->fp, cur, SEEK_SET);
    return (unsigned long)sz;
}
int mf_seek(MFile *f, unsigned long pos) { return fseek(f->fp, (long)pos, SEEK_SET) == 0; }
int mf_read(MFile *f, void *buf, int n) { return (int)fread(buf, 1, (size_t)n, f->fp); }

#endif
