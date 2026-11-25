/* Minimal libretro-common file_stream implementation on top of stdio (Windows CE build).
 * Replaces file_stream.c + vfs_implementation.c, which need far more of a C library than CE has. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <streams/file_stream.h>

struct RFILE { FILE *f; int64_t size; int eof; };

void filestream_vfs_init(const struct retro_vfs_interface_info *vfs_info) { (void)vfs_info; }

RFILE *filestream_open(const char *path, unsigned mode, unsigned hints)
{
    const char *m = "rb";
    RFILE *s;
    FILE *f;
    (void)hints;
    if ((mode & RETRO_VFS_FILE_ACCESS_READ_WRITE) == RETRO_VFS_FILE_ACCESS_READ_WRITE)
        m = (mode & RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING) ? "r+b" : "w+b";
    else if (mode & RETRO_VFS_FILE_ACCESS_WRITE)
        m = "wb";
    f = fopen(path, m);
    if (!f) return NULL;
    s = calloc(1, sizeof(*s));
    if (!s) { fclose(f); return NULL; }
    s->f = f;
    fseek(f, 0, SEEK_END); s->size = ftell(f); fseek(f, 0, SEEK_SET);
    return s;
}

int filestream_close(RFILE *s)
{
    int r;
    if (!s) return -1;
    r = fclose(s->f);
    free(s);
    return r;
}

int64_t filestream_get_size(RFILE *s) { return s ? s->size : -1; }

int64_t filestream_read(RFILE *s, void *data, int64_t len)
{
    size_t n;
    if (!s || len < 0) return -1;
    n = fread(data, 1, (size_t)len, s->f);
    if ((int64_t)n < len) s->eof = 1;
    return (int64_t)n;
}

int64_t filestream_write(RFILE *s, const void *data, int64_t len)
{
    int64_t n, pos;
    if (!s || len < 0) return -1;
    n = (int64_t)fwrite(data, 1, (size_t)len, s->f);
    pos = ftell(s->f);
    if (pos > s->size) s->size = pos;
    return n;
}

int64_t filestream_seek(RFILE *s, int64_t offset, int seek_position)
{
    int w = seek_position == RETRO_VFS_SEEK_POSITION_CURRENT ? SEEK_CUR :
            seek_position == RETRO_VFS_SEEK_POSITION_END ? SEEK_END : SEEK_SET;
    if (!s) return -1;
    s->eof = 0;
    return fseek(s->f, (long)offset, w) == 0 ? 0 : -1;
}

int64_t filestream_tell(RFILE *s) { return s ? (int64_t)ftell(s->f) : -1; }
void filestream_rewind(RFILE *s) { if (s) { fseek(s->f, 0, SEEK_SET); s->eof = 0; } }
int filestream_eof(RFILE *s) { return s ? (s->eof || ftell(s->f) >= s->size) : 1; }
int filestream_flush(RFILE *s) { return s ? fflush(s->f) : -1; }
int filestream_error(RFILE *s) { (void)s; return 0; }

int filestream_getc(RFILE *s)
{
    unsigned char c;
    return filestream_read(s, &c, 1) == 1 ? c : EOF;
}

char *filestream_gets(RFILE *s, char *buf, size_t len)
{
    size_t n = 0;
    int c = EOF;
    if (!s || !len) return NULL;
    while (n + 1 < len && (c = filestream_getc(s)) != EOF) {
        buf[n++] = (char)c;
        if (c == '\n') break;
    }
    buf[n] = 0;
    return (n || c != EOF) ? buf : NULL;
}

int filestream_putc(RFILE *s, int c)
{
    unsigned char b = (unsigned char)c;
    return filestream_write(s, &b, 1) == 1 ? c : EOF;
}

bool filestream_exists(const char *path)
{
    FILE *f = (path && path[0]) ? fopen(path, "rb") : NULL;
    if (f) fclose(f);
    return f != NULL;
}

int64_t filestream_read_file(const char *path, void **buf, int64_t *len)
{
    RFILE *s = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ, 0);
    void *p;
    *buf = NULL;
    if (len) *len = -1;
    if (!s) return 0;
    p = malloc((size_t)s->size + 1);
    if (!p) { filestream_close(s); return 0; }
    if (filestream_read(s, p, s->size) != s->size) { free(p); filestream_close(s); return 0; }
    ((char *)p)[s->size] = 0;
    if (len) *len = s->size;
    *buf = p;
    filestream_close(s);
    return 1;
}

bool filestream_write_file(const char *path, const void *data, int64_t size)
{
    RFILE *s = filestream_open(path, RETRO_VFS_FILE_ACCESS_WRITE, 0);
    int64_t n;
    if (!s) return false;
    n = filestream_write(s, data, size);
    filestream_close(s);
    return n == size;
}

int filestream_delete(const char *path) { (void)path; return -1; }

/* returns a malloc'd line without the line ending, NULL at end of file */
char *filestream_getline(RFILE *s)
{
    size_t cap = 64, n = 0;
    char *line = malloc(cap);
    int c;
    if (!s || !line) { free(line); return NULL; }
    c = filestream_getc(s);
    while (c != EOF && c != '\n') {
        if (n + 2 > cap) {
            char *t = realloc(line, cap * 2);
            if (!t) { free(line); return NULL; }
            line = t; cap *= 2;
        }
        if (c != '\r') line[n++] = (char)c;
        c = filestream_getc(s);
    }
    if (c == EOF && n == 0) { free(line); return NULL; }
    line[n] = 0;
    return line;
}
