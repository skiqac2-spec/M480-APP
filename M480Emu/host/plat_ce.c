/* Windows Mobile 6.x platform layer (GDI + waveOut), tuned for the 320x320 SCH-M480. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "host.h"

/* a few declarations that old SDK headers may lack */
#ifndef SHFS_HIDETASKBAR
#define SHFS_SHOWTASKBAR   0x0001
#define SHFS_HIDETASKBAR   0x0002
#define SHFS_SHOWSIPBUTTON 0x0004
#define SHFS_HIDESIPBUTTON 0x0008
#define SHFS_SHOWSTARTICON 0x0010
#define SHFS_HIDESTARTICON 0x0020
#endif
#ifndef VK_PROCESSKEY
#define VK_PROCESSKEY 0xE5
#endif
BOOL WINAPI SHFullScreen(HWND hwnd, DWORD state);
void WINAPI SystemIdleTimerReset(void);
HANDLE WINAPI ImmAssociateContext(HWND hwnd, HANDLE himc);
UINT WINAPI ImmGetVirtualKey(HWND hwnd);

static WCHAR class_name[40] = L"M480Emu_";
#define CLASS_NAME class_name
char g_start_game[HPATH];
int  g_start_resume;

uint16_t *g_fb;
static HINSTANCE h_inst;
static HWND h_wnd;
static HDC dc_mem, dc_wnd;
static HBITMAP h_bmp, h_bmp_old;
static HFONT fonts[3];
static int scr_w = SCR_W, scr_h = SCR_H;      /* real screen size */
static int active = 1;
static char app_dir[HPATH];
static FILE *log_f;

/* ---- event queue ------------------------------------------------------- */
#define EVQ 64
static struct event evq[EVQ];
static int ev_r, ev_w;

static void ev_push(int type, int key, int x, int y)
{
    int n = (ev_w + 1) % EVQ;
    if (type == EV_TMOVE && ev_w != ev_r) {              /* coalesce moves */
        int last = (ev_w + EVQ - 1) % EVQ;
        if (evq[last].type == EV_TMOVE) { evq[last].x = x; evq[last].y = y; return; }
    }
    if (n == ev_r) return;                               /* full: drop */
    evq[ev_w].type = type; evq[ev_w].key = key; evq[ev_w].x = x; evq[ev_w].y = y;
    ev_w = n;
}

static void go_fullscreen(void)
{
    SHFullScreen(h_wnd, SHFS_HIDETASKBAR | SHFS_HIDESIPBUTTON | SHFS_HIDESTARTICON);
    MoveWindow(h_wnd, 0, 0, scr_w, scr_h, FALSE);
}

static void blit(HDC dc, int x, int y, int w, int h)
{
    if (scr_w == SCR_W && scr_h == SCR_H)
        BitBlt(dc, x, y, w, h, dc_mem, x, y, SRCCOPY);
    else                                                  /* other screen sizes: stretch everything */
        StretchBlt(dc, 0, 0, scr_w, scr_h, dc_mem, 0, 0, SCR_W, SCR_H, SRCCOPY);
}

static int tx(int x) { x = (scr_w == SCR_W) ? x : x * SCR_W / scr_w; return x < 0 ? 0 : x >= SCR_W ? SCR_W - 1 : x; }
static int ty(int y) { y = (scr_h == SCR_H) ? y : y * SCR_H / scr_h; return y < 0 ? 0 : y >= SCR_H ? SCR_H - 1 : y; }

static int real_vk(HWND hwnd, WPARAM wp)
{
    int vk = (int)wp;
    if (vk == VK_PROCESSKEY) vk = (int)ImmGetVirtualKey(hwnd);
    if (vk >= 'a' && vk <= 'z') vk -= 32;
    return vk & 0xff;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN:
        ev_push(EV_KEYDOWN, real_vk(hwnd, wp), 0, 0);
        return 0;
    case WM_KEYUP:
        ev_push(EV_KEYUP, real_vk(hwnd, wp), 0, 0);
        return 0;
    case WM_CHAR:
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        ev_push(EV_TDOWN, 0, tx((short)LOWORD(lp)), ty((short)HIWORD(lp)));
        return 0;
    case WM_MOUSEMOVE:
        if (wp & MK_LBUTTON)
            ev_push(EV_TMOVE, 0, tx((short)LOWORD(lp)), ty((short)HIWORD(lp)));
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        ev_push(EV_TUP, 0, tx((short)LOWORD(lp)), ty((short)HIWORD(lp)));
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) {
            active = 0;
            ev_push(EV_SUSPEND, 0, 0, 0);
        } else {
            active = 1;
            go_fullscreen();
            if (dc_wnd && dc_mem) blit(dc_wnd, 0, 0, SCR_W, SCR_H);   /* show what was drawn meanwhile */
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_SETTINGCHANGE:
        if (active) go_fullscreen();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        blit(dc, 0, 0, SCR_W, SCR_H);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        ev_push(EV_QUIT, 0, 0, 0);
        return 0;
    case WM_DESTROY:
        ev_push(EV_QUIT, 0, 0, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void pump(void)
{
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&m);
}

int plat_poll(struct event *ev)
{
    pump();
    if (ev_r == ev_w) return 0;
    *ev = evq[ev_r];
    ev_r = (ev_r + 1) % EVQ;
    return 1;
}

void plat_wait_event(int ms)
{
    if (ev_r != ev_w) return;
    MsgWaitForMultipleObjectsEx(0, NULL, (DWORD)ms, QS_ALLINPUT, 0);
}

unsigned plat_ticks(void) { return GetTickCount(); }
void plat_sleep(int ms) { Sleep((DWORD)ms); }
void plat_keep_awake(void) { SystemIdleTimerReset(); }
const char *plat_app_dir(void) { return app_dir; }

int plat_free_mem_mb(void)
{
    MEMORYSTATUS ms;
    memset(&ms, 0, sizeof(ms));
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatus(&ms);
    return (int)(ms.dwAvailPhys >> 20);
}

/* ---- strings / files ----------------------------------------------------- */
static void to_w(const char *s, WCHAR *out, int max)
{
    if (!MultiByteToWideChar(CP_ACP, 0, s, -1, out, max)) out[0] = 0;
    out[max - 1] = 0;
}

static void to_a(const WCHAR *s, char *out, int max)
{
    if (!WideCharToMultiByte(CP_ACP, 0, s, -1, out, max, NULL, NULL)) out[0] = 0;
    out[max - 1] = 0;
}

void plat_path_to_wch(const char *path, wch *out, int max)
{
    to_w(path, (WCHAR *)out, max);
}

void plat_remove(const char *path)
{
    WCHAR w[HPATH];
    to_w(path, w, HPATH);
    DeleteFileW(w);
}

int plat_spawn(const char *exe_path, const char *args)
{
    WCHAR we[HPATH], wa[HPATH + 16];
    PROCESS_INFORMATION pi;
    to_w(exe_path, we, HPATH);
    to_w(args, wa, HPATH + 16);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(we, wa, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &pi)) {
        plat_log("CreateProcess failed (%d): %s\n", (int)GetLastError(), exe_path);
        return -1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

void plat_mkdir(const char *path)
{
    WCHAR w[HPATH];
    to_w(path, w, HPATH);
    CreateDirectoryW(w, NULL);
}

int plat_file_exists(const char *path)
{
    WCHAR w[HPATH];
    DWORD a;
    to_w(path, w, HPATH);
    a = GetFileAttributesW(w);
    return a != 0xFFFFFFFF && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

int plat_list_dir(const char *path, struct dirent_h **out)
{
    WCHAR w[HPATH + 4];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    struct dirent_h *v = NULL, *nv;
    int n = 0, cap = 0, l;

    to_w(path, w, HPATH);
    l = lstrlenW(w);
    if (l && w[l - 1] == L'\\') w[--l] = 0;
    w[l] = L'\\'; w[l + 1] = L'*'; w[l + 2] = 0;
    *out = NULL;
    h = FindFirstFileW(w, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return (GetLastError() == ERROR_NO_MORE_FILES || GetLastError() == ERROR_FILE_NOT_FOUND) ? 0 : -1;
    do {
        if (fd.cFileName[0] == L'.' && (!fd.cFileName[1] || (fd.cFileName[1] == L'.' && !fd.cFileName[2])))
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            nv = realloc(v, sizeof(*v) * cap);
            if (!nv) break;
            v = nv;
        }
        memset(&v[n], 0, sizeof(*v));
        to_a(fd.cFileName, v[n].name, HPATH);
        if (!v[n].name[0]) continue;
        v[n].is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        v[n].is_card = v[n].is_dir && (fd.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY) != 0;
        n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    *out = v;
    return n;
}

void plat_log(const char *fmt, ...)
{
    va_list ap;
    if (!log_f) return;
    va_start(ap, fmt);
    vfprintf(log_f, fmt, ap);
    va_end(ap);
    fflush(log_f);
}

extern void *ce_big_alloc(size_t size);
extern void ce_big_free(void *p);
void *plat_alloc_big(size_t size) { return ce_big_alloc(size); }
void plat_free_big(void *p, size_t size) { (void)size; ce_big_free(p); }

/* ---- drawing ---------------------------------------------------------------- */
void plat_present(int x, int y, int w, int h)
{
    if (!active) return;
    blit(dc_wnd, x, y, w, h);
}

void plat_text(int x, int y, int w, int h, const wch *s, uint16_t color, int flags)
{
    RECT rc;
    UINT fmt = DT_NOPREFIX;
    int r = (color >> 11) << 3, g = ((color >> 5) & 63) << 2, b = (color & 31) << 3;

    rc.left = x; rc.top = y; rc.right = x + w; rc.bottom = y + h;
    SelectObject(dc_mem, fonts[(flags & TF_BIG) ? 0 : (flags & TF_SMALL) ? 2 : 1]);
    SetTextColor(dc_mem, (COLORREF)(r | (g << 8) | (b << 16)));
    if (flags & TF_CENTER) fmt |= DT_CENTER;
    else if (flags & TF_RIGHT) fmt |= DT_RIGHT;
    else fmt |= DT_LEFT;
    if (flags & TF_WRAP) fmt |= DT_WORDBREAK;
    else fmt |= DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
    DrawTextW(dc_mem, (const WCHAR *)s, -1, &rc, fmt);
}

int plat_text_width(const wch *s, int n, int flags)
{
    SIZE sz;
    if (n < 0) { n = 0; while (s[n]) n++; }
    if (n == 0) return 0;
    SelectObject(dc_mem, fonts[(flags & TF_BIG) ? 0 : (flags & TF_SMALL) ? 2 : 1]);
    if (!GetTextExtentPoint32W(dc_mem, (const WCHAR *)s, n, &sz)) return n * 8;
    return sz.cx;
}

/* ---- audio: waveOut, polled (no threads) ------------------------------------ */
#define NBUF        12
#define BUF_MAX     1024
#define BUF_FRAMES  buf_frames                 /* ~23 ms per buffer at either rate */
#define RING_FRAMES 16384                      /* power of two */
static int cur_rate = 22050, buf_frames = 512;
static HWAVEOUT h_wo;
static WAVEHDR whdr[NBUF];
static int16_t *wbuf[NBUF];
static int16_t *ring;
static unsigned ring_r, ring_w;                /* in frames, free running */
static int queued[NBUF], audio_started;

int plat_audio_open(int rate)
{
    WAVEFORMATEX wf;
    int i;
    if (h_wo) return 0;
    cur_rate = rate;
    buf_frames = rate >= 44100 ? 1024 : 512;
    memset(&wf, 0, sizeof(wf));
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = (DWORD)rate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4;
    wf.nAvgBytesPerSec = (DWORD)rate * 4;
    if (waveOutOpen(&h_wo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        h_wo = NULL;
        plat_log("waveOutOpen failed\n");
        return -1;
    }
    if (!ring) ring = calloc(RING_FRAMES, 4);
    for (i = 0; i < NBUF; i++) {
        if (!wbuf[i]) wbuf[i] = calloc(BUF_MAX, 4);
        memset(&whdr[i], 0, sizeof(whdr[i]));
        whdr[i].lpData = (LPSTR)wbuf[i];
        whdr[i].dwBufferLength = BUF_FRAMES * 4;
        waveOutPrepareHeader(h_wo, &whdr[i], sizeof(WAVEHDR));
        queued[i] = 0;
    }
    ring_r = ring_w = 0;
    audio_started = 0;
    return 0;
}

void plat_audio_close(void)
{
    int i;
    if (!h_wo) return;
    waveOutReset(h_wo);
    for (i = 0; i < NBUF; i++)
        waveOutUnprepareHeader(h_wo, &whdr[i], sizeof(WAVEHDR));
    waveOutClose(h_wo);
    h_wo = NULL;
}

void plat_audio_pump(void)
{
    int i, nq = 0;
    if (!h_wo || !ring) return;
    for (i = 0; i < NBUF; i++) {
        if (queued[i] && (whdr[i].dwFlags & WHDR_DONE)) queued[i] = 0;
        nq += queued[i];
    }
    if (nq == 0) audio_started = 0;                       /* ran dry: rebuild a little cushion */
    if (!audio_started) {
        if (ring_w - ring_r < BUF_FRAMES * 5) return;     /* ~115ms cushion before playing */
        audio_started = 1;
    }
    for (i = 0; i < NBUF && ring_w - ring_r >= BUF_FRAMES; i++) {
        unsigned k, pos;
        if (queued[i]) continue;
        pos = ring_r & (RING_FRAMES - 1);
        k = RING_FRAMES - pos;
        if (k > BUF_FRAMES) k = BUF_FRAMES;
        memcpy(wbuf[i], ring + pos * 2, k * 4);
        if (k < BUF_FRAMES) memcpy(wbuf[i] + k * 2, ring, (BUF_FRAMES - k) * 4);
        ring_r += BUF_FRAMES;
        whdr[i].dwFlags &= ~WHDR_DONE;
        if (waveOutWrite(h_wo, &whdr[i], sizeof(WAVEHDR)) == MMSYSERR_NOERROR)
            queued[i] = 1;
    }
}

void plat_audio_write(const int16_t *pcm, int frames)
{
    unsigned pos, k;
    if (h_wo && cur_rate != plat_audio_rate()) plat_audio_close();   /* setting changed */
    if (!h_wo) {
        if (plat_audio_open(plat_audio_rate()) != 0) {
            if (plat_audio_open(44100) != 0) return;               /* device refused 22 kHz */
            plat_log("22 kHz output refused, using 44.1 kHz\n");
        }
    }
    if (!ring || frames <= 0) return;
    if (frames > RING_FRAMES / 2) frames = RING_FRAMES / 2;
    if (ring_w - ring_r + (unsigned)frames > RING_FRAMES - BUF_FRAMES)
        ring_r = ring_w - BUF_FRAMES * 2;                 /* too much latency: drop old audio */
    pos = ring_w & (RING_FRAMES - 1);
    k = RING_FRAMES - pos;
    if (k > (unsigned)frames) k = (unsigned)frames;
    memcpy(ring + pos * 2, pcm, k * 4);
    if (k < (unsigned)frames) memcpy(ring, pcm + k * 2, ((unsigned)frames - k) * 4);
    ring_w += (unsigned)frames;
    plat_audio_pump();
}

int plat_audio_fill(void)
{
    int i, nq = 0;
    if (!h_wo || !ring) return 0;
    for (i = 0; i < NBUF; i++)
        if (queued[i] && !(whdr[i].dwFlags & WHDR_DONE)) nq++;
    return (int)((ring_w - ring_r + (unsigned)nq * BUF_FRAMES) * 1000u / (unsigned)cur_rate);
}

int plat_audio_rate(void)
{
    return cfg.audio_hq ? 44100 : 22050;
}

void plat_audio_pause(int on)
{
    int i;
    if (!h_wo) return;
    if (on) {
        waveOutReset(h_wo);
        for (i = 0; i < NBUF; i++) queued[i] = 0;
        ring_r = ring_w = 0;
        audio_started = 0;
    }
}

/* ---- startup ------------------------------------------------------------------ */
static HFONT make_font(int px, int bold)
{
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -px;
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = DEFAULT_QUALITY;
    return CreateFontIndirectW(&lf);
}

int plat_init(void)
{
    WNDCLASSW wc;
    struct { BITMAPINFOHEADER h; DWORD mask[3]; } bi;
    WCHAR wpath[HPATH], *p;
    void *bits = NULL;
    char logp[HPATH];
    HWND prev;

    {   /* one window class per system: "M480Emu_<id>" */
        int i, l = lstrlenW(class_name);
        for (i = 0; sys.id[i] && l + i < 38; i++) class_name[l + i] = (WCHAR)sys.id[i];
        class_name[l + i] = 0;
    }
    prev = g_start_game[0] ? NULL : FindWindowW(CLASS_NAME, NULL);
    if (prev) {                                           /* already running: bring it to front */
        SetForegroundWindow((HWND)((ULONG_PTR)prev | 1));
        return -1;
    }

    GetModuleFileNameW(NULL, wpath, HPATH);
    p = wcsrchr(wpath, L'\\');
    if (p) *p = 0;
    to_a(wpath, app_dir, HPATH);
    snprintf(logp, sizeof(logp), "%s\\log_%s.txt", app_dir, sys.id);
    log_f = fopen(logp, "w");

    scr_w = GetSystemMetrics(SM_CXSCREEN);
    scr_h = GetSystemMetrics(SM_CYSCREEN);
    if (scr_w <= 0 || scr_h <= 0) { scr_w = SCR_W; scr_h = SCR_H; }
    plat_log("screen %dx%d\n", scr_w, scr_h);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = h_inst;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = CLASS_NAME;
    if (!RegisterClassW(&wc)) return -2;

    h_wnd = CreateWindowExW(0, CLASS_NAME, L"Emulator", WS_VISIBLE,
                            0, 0, scr_w, scr_h, NULL, NULL, h_inst, NULL);
    if (!h_wnd) return -3;

    memset(&bi, 0, sizeof(bi));
    bi.h.biSize = sizeof(BITMAPINFOHEADER);
    bi.h.biWidth = SCR_W;
    bi.h.biHeight = -SCR_H;                               /* top-down */
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 16;
    bi.h.biCompression = BI_BITFIELDS;
    bi.mask[0] = 0xF800; bi.mask[1] = 0x07E0; bi.mask[2] = 0x001F;
    dc_wnd = GetDC(h_wnd);
    dc_mem = CreateCompatibleDC(dc_wnd);
    h_bmp = CreateDIBSection(dc_wnd, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dc_mem || !h_bmp || !bits) return -4;
    h_bmp_old = SelectObject(dc_mem, h_bmp);
    g_fb = bits;
    memset(g_fb, 0, SCR_W * SCR_H * 2);
    SetBkMode(dc_mem, TRANSPARENT);

    fonts[0] = make_font(19, 1);
    fonts[1] = make_font(17, 0);
    fonts[2] = make_font(14, 0);

    ImmAssociateContext(h_wnd, NULL);                     /* raw keys, no Hangul composition */
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);   /* fewer hiccups from background programs */
    go_fullscreen();
    ShowWindow(h_wnd, SW_SHOW);
    SetForegroundWindow(h_wnd);
    UpdateWindow(h_wnd);
    return 0;
}

void plat_exit(void)
{
    plat_audio_close();
    if (dc_mem) { SelectObject(dc_mem, h_bmp_old); DeleteDC(dc_mem); }
    if (h_bmp) DeleteObject(h_bmp);
    if (h_wnd) { ReleaseDC(h_wnd, dc_wnd); DestroyWindow(h_wnd); }
    if (log_f) fclose(log_f);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show)
{
    (void)prev; (void)show;
    h_inst = inst;
    if (cmd && cmd[0]) {                                  /* [/r] "game path" */
        WCHAR tmp[HPATH];
        int n = 0;
        while (*cmd == L' ') cmd++;
        if (cmd[0] == L'/' && (cmd[1] == L'r' || cmd[1] == L'R')) { g_start_resume = 1; cmd += 2; }
        while (*cmd == L' ') cmd++;
        if (*cmd == L'"') cmd++;
        while (*cmd && *cmd != L'"' && n < HPATH - 1) tmp[n++] = *cmd++;
        tmp[n] = 0;
        to_a(tmp, g_start_game, HPATH);
    }
    if (plat_init() == 0)
        ui_main();
    plat_exit();
    return 0;
}
