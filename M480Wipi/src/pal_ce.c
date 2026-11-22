/* Windows Mobile 6.1 implementation of pal.h + GDI font rasterizer + waveOut audio */
#include <windows.h>
#include <mmsystem.h>
#include "pal.h"
#include "gfx.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- paths ---------------- */
static WCHAR *w16(const char *s, WCHAR *buf, int max)
{
    int i, n = MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, max);
    if (n <= 0) { for (i = 0; s[i] && i < max - 1; i++) buf[i] = (u8)s[i]; buf[i] = 0; }
    for (i = 0; buf[i]; i++) if (buf[i] == '/') buf[i] = '\\';
    return buf;
}
static void u8s(const WCHAR *w, char *out, int max)
{
    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, out, max, NULL, NULL) <= 0) out[0] = 0;
}

u32 pal_ms(void) { return GetTickCount(); }

s64 pal_time_ms(void)
{
    SYSTEMTIME st;
    FILETIME ft;
    s64 t;
    GetSystemTime(&st);
    SystemTimeToFileTime(&st, &ft);
    t = ((s64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (t - 116444736000000000LL) / 10000;
}

static char logpath[300];
static int logn;
void pal_set_logfile(const char *p) { snprintf(logpath, sizeof logpath, "%s", p); }
void pal_log(const char *s)
{
    void *f;
    if (!logpath[0] || logn > 4000) return;
    logn++;
    f = pal_fopen(logpath, 'a');
    if (!f) return;
    SetFilePointer((HANDLE)f, 0, NULL, FILE_END);
    pal_fwrite(f, s, (int)strlen(s));
    pal_fwrite(f, "\r\n", 2);
    pal_fclose(f);
}

void *pal_fopen(const char *path, int mode)
{
    WCHAR w[MAX_PATH];
    HANDLE h;
    w16(path, w, MAX_PATH);
    if (mode == 'r') h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    else if (mode == 'w') h = CreateFileW(w, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    else h = CreateFileW(w, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    return h == INVALID_HANDLE_VALUE ? NULL : (void *)h;
}
int pal_fread(void *f, void *buf, int n) { DWORD r = 0; ReadFile((HANDLE)f, buf, n, &r, NULL); return (int)r; }
int pal_fwrite(void *f, const void *buf, int n) { DWORD r = 0; WriteFile((HANDLE)f, buf, n, &r, NULL); return (int)r; }
int pal_fseek(void *f, long off) { return SetFilePointer((HANDLE)f, off, NULL, FILE_BEGIN) != 0xFFFFFFFF; }
long pal_fsize(void *f) { return (long)GetFileSize((HANDLE)f, NULL); }
void pal_fclose(void *f) { CloseHandle((HANDLE)f); }
int pal_remove(const char *p) { WCHAR w[MAX_PATH]; return DeleteFileW(w16(p, w, MAX_PATH)) != 0; }
int pal_mkdir(const char *p) { WCHAR w[MAX_PATH]; return CreateDirectoryW(w16(p, w, MAX_PATH), NULL) != 0; }

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
    WCHAR w[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    char p2[600];
    snprintf(p2, sizeof p2, "%s/*", path);
    h = FindFirstFileW(w16(p2, w, MAX_PATH), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        char name[400];
        if (fd.cFileName[0] == '.') continue;
        u8s(fd.cFileName, name, sizeof name);
        cb(u, name, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, fd.nFileSizeLow);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 1;
}

/* ---------------- fonts (GDI) ---------------- */
typedef struct Glyph { u16 ch; u16 fid; u8 w, h, adv; u8 *bm; struct Glyph *next; } Glyph;
#define GH 1024
static Glyph *gcache[GH];
static HDC fdc;
static HBITMAP fbmp;
static u32 *fbits;
#define FBW 64
#define FBH 48
typedef struct { int fid; HFONT f; int height, ascent; } FontEnt;
static FontEnt fents[24];
static int nfents;

static int px_of(int fid) { int p = fid >> 4; return p < 8 ? 8 : p > 40 ? 40 : p; }

static FontEnt *font_ent(int fid)
{
    int i;
    LOGFONTW lf;
    TEXTMETRICW tm;
    fid &= ~4;            /* underline drawn separately */
    for (i = 0; i < nfents; i++) if (fents[i].fid == fid) return &fents[i];
    if (!fdc) {
        BITMAPINFO bi;
        fdc = CreateCompatibleDC(NULL);
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = FBW; bi.bmiHeader.biHeight = -FBH;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        fbmp = CreateDIBSection(fdc, &bi, DIB_RGB_COLORS, (void **)&fbits, NULL, 0);
        SelectObject(fdc, fbmp);
        SetBkMode(fdc, OPAQUE);
        SetBkColor(fdc, RGB(0, 0, 0));
        SetTextColor(fdc, RGB(255, 255, 255));
    }
    if (nfents == 24) nfents = 0;
    memset(&lf, 0, sizeof lf);
    lf.lfHeight = -px_of(fid);
    lf.lfWeight = (fid & 1) ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = (fid & 2) ? TRUE : FALSE;
    lf.lfCharSet = HANGEUL_CHARSET;
    lf.lfQuality = ANTIALIASED_QUALITY;
    fents[nfents].fid = fid;
    fents[nfents].f = CreateFontIndirectW(&lf);
    SelectObject(fdc, fents[nfents].f);
    GetTextMetricsW(fdc, &tm);
    fents[nfents].height = tm.tmHeight;
    fents[nfents].ascent = tm.tmAscent;
    return &fents[nfents++];
}

int font_height(int fid) { return font_ent(fid)->height; }
int font_ascent(int fid) { return font_ent(fid)->ascent; }

static Glyph *glyph(int fid, u16 ch)
{
    u32 hsh = ((u32)ch * 31 + (u32)fid) & (GH - 1);
    Glyph *g;
    FontEnt *fe;
    SIZE sz;
    RECT r;
    int x, y, w, h;
    WCHAR c = ch;
    fid &= ~4;
    for (g = gcache[hsh]; g; g = g->next) if (g->ch == ch && g->fid == fid) return g;
    fe = font_ent(fid);
    SelectObject(fdc, fe->f);
    GetTextExtentPoint32W(fdc, &c, 1, &sz);
    w = sz.cx > FBW ? FBW : sz.cx;
    h = fe->height > FBH ? FBH : fe->height;
    g = (Glyph *)calloc(1, sizeof(Glyph));
    g->ch = ch; g->fid = (u16)fid; g->adv = (u8)sz.cx; g->w = (u8)w; g->h = (u8)h;
    if (ch > ' ' && w > 0 && h > 0) {
        int any = 0;
        r.left = 0; r.top = 0; r.right = FBW; r.bottom = FBH;
        ExtTextOutW(fdc, 0, 0, ETO_OPAQUE, &r, &c, 1, NULL);
        g->bm = (u8 *)malloc(w * h);
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
            u32 p = fbits[y * FBW + x];
            u32 a = ((p >> 16) & 255) > ((p >> 8) & 255) ? (p >> 16) & 255 : (p >> 8) & 255;
            g->bm[y * w + x] = (u8)a;
            if (a) any = 1;
        }
        if (!any) { free(g->bm); g->bm = NULL; }
    }
    g->next = gcache[hsh];
    gcache[hsh] = g;
    return g;
}

int font_char_w(int fid, u16 ch) { return glyph(fid, ch)->adv; }
const u8 *font_glyph(int fid, u16 ch, int *gw, int *gh, int *adv)
{
    Glyph *g = glyph(fid, ch);
    *gw = g->w; *gh = g->h; *adv = g->adv;
    return g->bm;
}

/* ---------------- audio (waveOut, worker thread) ---------------- */
#define ABUF 4
#define ALEN 1024
static CRITICAL_SECTION acs;
static int acs_ready;
static HWAVEOUT hwo;
static WAVEHDR whdr[ABUF];
static short abufs[ABUF][ALEN];
static HANDLE aevt, athr;
static volatile int arun;

void pal_audio_lock(void) { if (acs_ready) EnterCriticalSection(&acs); }
void pal_audio_unlock(void) { if (acs_ready) LeaveCriticalSection(&acs); }

static DWORD WINAPI audio_thread(LPVOID p)
{
    int i;
    (void)p;
    for (i = 0; i < ABUF; i++) {
        audio_render(abufs[i], ALEN);
        whdr[i].lpData = (LPSTR)abufs[i]; whdr[i].dwBufferLength = ALEN * 2; whdr[i].dwFlags = 0;
        waveOutPrepareHeader(hwo, &whdr[i], sizeof(WAVEHDR));
        waveOutWrite(hwo, &whdr[i], sizeof(WAVEHDR));
    }
    while (arun) {
        WaitForSingleObject(aevt, 200);
        for (i = 0; i < ABUF && arun; i++) if (whdr[i].dwFlags & WHDR_DONE) {
            audio_render(abufs[i], ALEN);
            whdr[i].dwFlags &= ~WHDR_DONE;
            waveOutWrite(hwo, &whdr[i], sizeof(WAVEHDR));
        }
    }
    return 0;
}

void pal_audio_start(void)
{
    WAVEFORMATEX wf;
    DWORD tid;
    if (!acs_ready) { InitializeCriticalSection(&acs); acs_ready = 1; }
    if (hwo) return;
    memset(&wf, 0, sizeof wf);
    wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 1; wf.nSamplesPerSec = AUDIO_RATE;
    wf.wBitsPerSample = 16; wf.nBlockAlign = 2; wf.nAvgBytesPerSec = AUDIO_RATE * 2;
    aevt = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (waveOutOpen(&hwo, WAVE_MAPPER, &wf, (DWORD)aevt, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) { hwo = 0; pal_log("waveOut failed"); return; }
    arun = 1;
    athr = CreateThread(NULL, 0, audio_thread, NULL, 0, &tid);
    if (athr) SetThreadPriority(athr, THREAD_PRIORITY_ABOVE_NORMAL);
}
void pal_audio_stop(void)
{
    int i;
    if (!hwo) return;
    arun = 0;
    SetEvent(aevt);
    if (athr) { WaitForSingleObject(athr, 1000); CloseHandle(athr); athr = NULL; }
    waveOutReset(hwo);
    for (i = 0; i < ABUF; i++) waveOutUnprepareHeader(hwo, &whdr[i], sizeof(WAVEHDR));
    waveOutClose(hwo);
    hwo = 0;
}
void pal_audio_pause(int p) { if (hwo) { if (p) waveOutPause(hwo); else waveOutRestart(hwo); } }
