/* POSIX-ish helpers missing from Windows CE's C runtime */
#define CE_COMPAT_IMPL
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dirent.h>
#include <errno.h>

/* ce_compat.h is force-included before this file's own text, so its redirections are active
 * here too: switch them off, this file implements the real things */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memcpy
#undef fopen

int ce_errno;

const char *ce_strerror(int e)
{
    static char buf[32];
    sprintf(buf, "error %d", e);
    return buf;
}

static void a2w(const char *s, WCHAR *out, int max)
{
    if (!MultiByteToWideChar(CP_ACP, 0, s, -1, out, max)) out[0] = 0;
    out[max - 1] = 0;
}

/* fopen with an explicit ANSI(code page) -> Unicode path conversion; '/' is accepted too */
FILE *ce_fopen(const char *path, const char *mode)
{
    WCHAR wp[MAX_PATH], wm[8];
    FILE *f;
    int i;
    if (!path || !path[0]) { ce_errno = ENOENT; return NULL; }
    a2w(path, wp, MAX_PATH);
    for (i = 0; wp[i]; i++) if (wp[i] == L'/') wp[i] = L'\\';
    for (i = 0; mode[i] && i < 7; i++) wm[i] = (WCHAR)mode[i];
    wm[i] = 0;
    f = _wfopen(wp, wm);
    if (!f) ce_errno = ENOENT;
    return f;
}

int ce_stat(const char *path, struct stat *st)
{
    WCHAR wp[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA fa;
    int i;
    a2w(path, wp, MAX_PATH);
    for (i = 0; wp[i]; i++) if (wp[i] == L'/') wp[i] = L'\\';
    i = lstrlenW(wp);
    if (i > 1 && wp[i - 1] == L'\\') wp[i - 1] = 0;
    if (!GetFileAttributesExW(wp, GetFileExInfoStandard, &fa)) { ce_errno = ENOENT; return -1; }
    memset(st, 0, sizeof(*st));
    st->st_mode = (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? S_IFDIR : S_IFREG;
    st->st_size = (long)fa.nFileSizeLow;
    return 0;
}

struct ce_dir { HANDLE h; WIN32_FIND_DATAW fd; int first; struct dirent ent; };

DIR *opendir(const char *path)
{
    WCHAR wp[MAX_PATH + 4];
    DIR *d = calloc(1, sizeof(*d));
    int l;
    if (!d) return NULL;
    a2w(path, wp, MAX_PATH);
    l = lstrlenW(wp);
    if (l && (wp[l - 1] == L'\\' || wp[l - 1] == L'/')) l--;
    wp[l] = L'\\'; wp[l + 1] = L'*'; wp[l + 2] = 0;
    d->h = FindFirstFileW(wp, &d->fd);
    if (d->h == INVALID_HANDLE_VALUE) { free(d); return NULL; }
    d->first = 1;
    return d;
}

struct dirent *readdir(DIR *d)
{
    if (!d) return NULL;
    if (!d->first && !FindNextFileW(d->h, &d->fd)) return NULL;
    d->first = 0;
    if (!WideCharToMultiByte(CP_ACP, 0, d->fd.cFileName, -1, d->ent.d_name, sizeof(d->ent.d_name), NULL, NULL))
        d->ent.d_name[0] = 0;
    return &d->ent;
}

int closedir(DIR *d)
{
    if (d) { FindClose(d->h); free(d); }
    return 0;
}

/* ---- big allocations --------------------------------------------------------
 * A Windows Mobile 6.x process only has a 32MB address slot (shared with DLLs).
 * Reservations of 2MB or more are placed in the system-wide "large memory area"
 * instead, so every big block reserves at least 2MB of address space there and
 * commits just what it needs.  Returned memory is zero filled. */
#define BIG_MIN (2u * 1024 * 1024)
static struct { void *p; size_t committed; } big_tab[96];

static void *big_alloc(size_t size, DWORD prot)
{
    size_t res = (size + 0xffff) & ~(size_t)0xffff;
    void *p;
    int i;
    if (res < BIG_MIN) res = BIG_MIN;
    p = VirtualAlloc(NULL, res, MEM_RESERVE, PAGE_NOACCESS);
    if (!p) return NULL;
    if (!VirtualAlloc(p, size, MEM_COMMIT, prot)) {
        VirtualFree(p, 0, MEM_RELEASE);
        return NULL;
    }
    for (i = 0; i < (int)(sizeof(big_tab) / sizeof(big_tab[0])); i++)
        if (!big_tab[i].p) { big_tab[i].p = p; big_tab[i].committed = size; break; }
    return p;
}

void *ce_big_alloc(size_t size) { return big_alloc(size, PAGE_READWRITE); }

void ce_big_free(void *p)
{
    int i;
    if (!p) return;
    for (i = 0; i < (int)(sizeof(big_tab) / sizeof(big_tab[0])); i++)
        if (big_tab[i].p == p) {
            VirtualFree(p, big_tab[i].committed, MEM_DECOMMIT);
            big_tab[i].p = NULL;
            break;
        }
    VirtualFree(p, 0, MEM_RELEASE);
}

/* malloc family used by the emulator cores: anything of 192KB or more goes to the large
 * memory area (addresses from 0x42000000 up), everything else to the normal heap. */
#define BIG_THRESHOLD (192u * 1024)
#define IS_BIG(p) ((unsigned long)(p) >= 0x42000000UL)

static size_t big_size(void *p)
{
    int i;
    for (i = 0; i < (int)(sizeof(big_tab) / sizeof(big_tab[0])); i++)
        if (big_tab[i].p == p) return big_tab[i].committed;
    return 0;
}

void *ce_malloc(size_t size)
{
    if (size >= BIG_THRESHOLD) {
        void *p = ce_big_alloc(size);
        if (p) return p;
    }
    return malloc(size);
}

void *ce_calloc(size_t n, size_t size)
{
    size_t total = n * size;
    if (total >= BIG_THRESHOLD) {
        void *p = ce_big_alloc(total);          /* already zero filled */
        if (p) return p;
    }
    return calloc(n, size);
}

void ce_free(void *p)
{
    if (!p) return;
    if (IS_BIG(p)) ce_big_free(p);
    else free(p);
}

void *ce_realloc(void *p, size_t size)
{
    void *q;
    size_t old;
    if (!p) return ce_malloc(size);
    if (!IS_BIG(p) && size < BIG_THRESHOLD) return realloc(p, size);
    old = IS_BIG(p) ? big_size(p) : 0;
    q = ce_malloc(size);
    if (!q) return NULL;
    if (IS_BIG(p)) memcpy(q, p, old < size ? old : size);
    else {
        /* growing a small block into a big one: the old size is unknown, but it is below the
         * threshold and the heap block is readable up to its real size via realloc */
        void *t = realloc(p, BIG_THRESHOLD);
        if (!t) { ce_free(q); return NULL; }
        memcpy(q, t, BIG_THRESHOLD < size ? BIG_THRESHOLD : size);
        p = t;
    }
    ce_free(p);
    return q;
}

/* anonymous mappings only */
void *mmap(void *addr, size_t len, int prot, int flags, int fd, long off)
{
    void *p;
    (void)addr; (void)flags; (void)fd; (void)off;
    p = big_alloc(len, (prot & PROT_EXEC) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE);
    return p ? p : MAP_FAILED;
}

int munmap(void *addr, size_t len)
{
    (void)len;
    ce_big_free(addr);
    return 0;
}

int mprotect(void *addr, size_t len, int prot)
{
    DWORD old;
    return VirtualProtect(addr, len, (prot & PROT_EXEC) ? PAGE_EXECUTE_READWRITE :
                          (prot & PROT_WRITE) ? PAGE_READWRITE : PAGE_READONLY, &old) ? 0 : -1;
}

void ce_flush_icache(void *start, size_t len)
{
    FlushInstructionCache(GetCurrentProcess(), start, len);
}

/* own qsort (heap sort): keeps behaviour identical everywhere and avoids the OS import */
static void q_swap(char *a, char *b, size_t n)
{
    while (n--) { char t = *a; *a++ = *b; *b++ = t; }
}

void qsort(void *base_, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
    char *base = base_;
    size_t start, end, root, child;
    if (n < 2 || sz == 0) return;
    for (start = n / 2; start-- > 0; ) {
        root = start;
        while ((child = 2 * root + 1) < n) {
            if (child + 1 < n && cmp(base + child * sz, base + (child + 1) * sz) < 0) child++;
            if (cmp(base + root * sz, base + child * sz) >= 0) break;
            q_swap(base + root * sz, base + child * sz, sz);
            root = child;
        }
    }
    for (end = n - 1; end > 0; end--) {
        q_swap(base, base + end * sz, sz);
        root = 0;
        while ((child = 2 * root + 1) < end) {
            if (child + 1 < end && cmp(base + child * sz, base + (child + 1) * sz) < 0) child++;
            if (cmp(base + root * sz, base + child * sz) >= 0) break;
            q_swap(base + root * sz, base + child * sz, sz);
            root = child;
        }
    }
}

/* never used at run time (save states go through memory buffers), only referenced */
void perror(const char *s) { (void)s; }
void *gzopen(const char *path, const char *mode) { (void)path; (void)mode; return NULL; }
int gzread(void *f, void *buf, unsigned len) { (void)f; (void)buf; (void)len; return -1; }
int gzwrite(void *f, const void *buf, unsigned len) { (void)f; (void)buf; (void)len; return -1; }
long gzseek(void *f, long off, int whence) { (void)f; (void)off; (void)whence; return -1; }
int gzclose(void *f) { (void)f; return -1; }

/* tiny math helpers kept in the executable so they need not exist in the OS runtime */
float floorf(float x)
{
    int i;
    if (x != x || x >= 2147483520.0f || x <= -2147483520.0f) return x;
    i = (int)x;
    if ((float)i > x) i--;
    return (float)i;
}
int _isnan(double x) { volatile double v = x; return v != v; }
int _isnanf(float x) { volatile float v = x; return v != v; }
