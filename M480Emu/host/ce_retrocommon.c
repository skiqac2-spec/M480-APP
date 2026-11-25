/* A few libretro-common helpers cores expect, written without that library's file layer.
 * (No libretro-common headers are included so one copy fits every core's version.) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int path_is_valid(const char *path)
{
    FILE *f = (path && path[0]) ? fopen(path, "rb") : NULL;
    if (f) fclose(f);
    return f != NULL;
}

void path_vfs_init(const void *vfs_info) { (void)vfs_info; }
void dirent_vfs_init(const void *vfs_info) { (void)vfs_info; }

size_t fill_pathname_join(char *s, const char *dir, const char *path, size_t len)
{
    size_t n = 0;
    if (!len) return 0;
    while (dir && dir[n] && n + 1 < len) { s[n] = dir[n]; n++; }
    if (n && s[n - 1] != '\\' && s[n - 1] != '/' && n + 1 < len) s[n++] = '\\';
    while (path && *path && n + 1 < len) s[n++] = *path++;
    s[n] = 0;
    return n;
}

char *string_trim_whitespace_left(char *const s)
{
    if (s && *s) {
        char *p = s;
        while (*p && isspace((unsigned char)*p)) p++;
        if (p != s) memmove(s, p, strlen(p) + 1);
    }
    return s;
}

char *string_trim_whitespace_right(char *const s)
{
    if (s && *s) {
        size_t n = strlen(s);
        while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    }
    return s;
}

char *string_trim_whitespace(char *const s)
{
    string_trim_whitespace_right(s);
    string_trim_whitespace_left(s);
    return s;
}

const char *path_get_extension(const char *path)
{
    const char *e = path ? strrchr(path, '.') : NULL;
    const char *s = path ? strrchr(path, '\\') : NULL;
    if (!e || (s && e < s)) return "";
    return e + 1;
}

const char *path_basename(const char *path)
{
    const char *a = strrchr(path, '\\'), *b = strrchr(path, '/');
    if (b > a) a = b;
    return a ? a + 1 : path;
}

char *path_remove_extension(char *path)
{
    char *e = path ? strrchr(path, '.') : NULL;
    char *s = path ? strrchr(path, '\\') : NULL;
    if (!e || (s && e < s)) return NULL;
    *e = 0;
    return path;
}

/* out = dir + separator + last + ext */
size_t fill_pathname_join_special_ext(char *out_path, const char *dir, const char *path,
                                      const char *last, const char *ext, size_t size)
{
    size_t n = fill_pathname_join(out_path, dir, path, size);
    if (n && out_path[n - 1] != '\\' && out_path[n - 1] != '/' && n + 1 < size) { out_path[n++] = '\\'; out_path[n] = 0; }
    while (last && *last && n + 1 < size) out_path[n++] = *last++;
    while (ext && *ext && n + 1 < size) out_path[n++] = *ext++;
    out_path[n] = 0;
    return n;
}

unsigned string_to_unsigned(const char *str)
{
    const char *p;
    if (!str || !*str) return 0;
    for (p = str; *p; p++) if (!isdigit((unsigned char)*p)) return 0;
    return (unsigned)strtoul(str, NULL, 10);
}
