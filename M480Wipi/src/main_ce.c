/* Windows Mobile front-end: window, DIB frame, keys, touch, main loop */
#include <windows.h>
#include <aygshell.h>
#include "ui.h"
#include "host.h"
#include "pal.h"
#include "audio.h"
#include <stdio.h>
#include <string.h>

void pal_set_logfile(const char *p);
void pal_audio_start(void);
void pal_audio_stop(void);
void pal_audio_pause(int p);
void vm_log(const char *fmt, ...);

#define CLS L"M480WipiEmu"
static HWND hwnd;
static HDC memdc;
static HBITMAP dib;
static u16 *bits;
static Surf frame;
static int scrW, scrH;
static char appdir[400];
static int active = 1;

typedef BOOL (WINAPI *PFN_ALLKEYS)(BOOL);
typedef HANDLE (WINAPI *PFN_IMMASSOC)(HWND, HANDLE);
typedef UINT (WINAPI *PFN_IMMGETVK)(HWND);
static PFN_ALLKEYS pAllKeys;
static PFN_IMMGETVK pImmGetVirtualKey;
static int allkeys_on;

static void keys_setup(HWND h)
{
    HMODULE m = LoadLibraryW(L"coredll.dll");
    if (m) {
        PFN_IMMASSOC a = (PFN_IMMASSOC)GetProcAddressW(m, L"ImmAssociateContext");
        pImmGetVirtualKey = (PFN_IMMGETVK)GetProcAddressW(m, L"ImmGetVirtualKey");
        if (a) a(h, NULL);
        pAllKeys = (PFN_ALLKEYS)GetProcAddressW(m, L"AllKeys");
    }
}
static void softkeys(int on)
{
    if (pAllKeys && on != allkeys_on) { pAllKeys(on ? TRUE : FALSE); allkeys_on = on; }
}

static void fullscreen(void)
{
    SHFullScreen(hwnd, SHFS_HIDETASKBAR | SHFS_HIDESIPBUTTON | SHFS_HIDESTARTICON);
    MoveWindow(hwnd, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), TRUE);
}

static void create_dib(void)
{
    struct { BITMAPINFOHEADER h; DWORD masks[3]; } bi;
    HDC dc = GetDC(hwnd);
    memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof(BITMAPINFOHEADER);
    bi.h.biWidth = scrW; bi.h.biHeight = -scrH;
    bi.h.biPlanes = 1; bi.h.biBitCount = 16; bi.h.biCompression = BI_BITFIELDS;
    bi.masks[0] = 0xF800; bi.masks[1] = 0x07E0; bi.masks[2] = 0x001F;
    memdc = CreateCompatibleDC(dc);
    dib = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    SelectObject(memdc, dib);
    ReleaseDC(hwnd, dc);
    frame.w = scrW; frame.h = scrH; frame.px = bits; frame.a = NULL; frame.ext = 1;
}

static void present(void)
{
    HDC dc;
    if (!bits) return;
    ui_draw(&frame);
    dc = GetDC(hwnd);
    BitBlt(dc, 0, 0, scrW, scrH, memdc, 0, 0, SRCCOPY);
    ReleaseDC(hwnd, dc);
}

static void restart_self(void)
{
    WCHAR exe[MAX_PATH];
    PROCESS_INFORMATION pi;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (CreateProcessW(exe, L"", NULL, NULL, FALSE, 0, NULL, NULL, NULL, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
}

static void vib(int ms) { (void)ms; }
static void light(int on) { (void)on; SystemIdleTimerReset(); }

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) { active = 1; fullscreen(); softkeys(1); pal_audio_pause(0); ui_pause(0); }
        else { active = 0; softkeys(0); pal_audio_pause(1); ui_pause(1); }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (bits) BitBlt(dc, 0, 0, scrW, scrH, memdc, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: SetCapture(h); ui_tap((short)LOWORD(lp), (short)HIWORD(lp), 1, 0); return 0;
    case WM_MOUSEMOVE: if (wp & MK_LBUTTON) ui_tap((short)LOWORD(lp), (short)HIWORD(lp), 1, 1); return 0;
    case WM_LBUTTONUP: ReleaseCapture(); ui_tap((short)LOWORD(lp), (short)HIWORD(lp), 0, 0); return 0;
    case WM_KEYDOWN:
    case WM_KEYUP: {
        int down = msg == WM_KEYDOWN, rep = down && (lp & 0x40000000);
        static int nlog;
        if (wp == 0xE5 && pImmGetVirtualKey) wp = pImmGetVirtualKey(h);
        if (down && !rep && nlog < 30) { vm_log("key 0x%02X", (int)wp); nlog++; }
        ui_key((int)wp, down, rep);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        softkeys(0);
        pal_audio_stop();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void parse_args(LPWSTR cmd, char *game, int max, int *carrier, int *w, int *h)
{
    /* "/carrier N /size WxH <path>" (used by desktop shortcuts) */
    char a[600];
    char *p;
    WideCharToMultiByte(CP_UTF8, 0, cmd, -1, a, sizeof a, NULL, NULL);
    game[0] = 0; *carrier = -1; *w = 0; *h = 0;
    p = a;
    while (*p) {
        while (*p == ' ') p++;
        if (!strncmp(p, "/carrier ", 9)) { *carrier = atoi(p + 9); p += 9; while (*p && *p != ' ') p++; }
        else if (!strncmp(p, "/size ", 6)) { sscanf(p + 6, "%dx%d", w, h); p += 6; while (*p && *p != ' ') p++; }
        else if (*p) {
            char *e = p;
            if (*p == '"') { p++; e = strchr(p, '"'); if (!e) e = p + strlen(p); }
            else e = p + strlen(p);
            snprintf(game, max, "%.*s", (int)(e - p), p);
            break;
        }
    }
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cmd, int show)
{
    WNDCLASSW wc;
    MSG m;
    WCHAR exe[MAX_PATH];
    char game[500], logp[500];
    int carrier, gw, gh, i;
    (void)hp; (void)show;

    hwnd = FindWindowW(CLS, NULL);
    if (hwnd) { SetForegroundWindow((HWND)((ULONG)hwnd | 1)); return 0; }

    GetModuleFileNameW(NULL, exe, MAX_PATH);
    WideCharToMultiByte(CP_UTF8, 0, exe, -1, appdir, sizeof appdir, NULL, NULL);
    for (i = (int)strlen(appdir) - 1; i >= 0; i--) if (appdir[i] == '\\') { appdir[i] = 0; break; }
    for (i = 0; appdir[i]; i++) if (appdir[i] == '\\') appdir[i] = '/';
    snprintf(logp, sizeof logp, "%s/m480wipi_log.txt", appdir);
    pal_remove(logp);
    pal_set_logfile(logp);
    vm_log("M480 WIPI start, dir %s", appdir);

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = CLS;
    RegisterClassW(&wc);
    scrW = GetSystemMetrics(SM_CXSCREEN);
    scrH = GetSystemMetrics(SM_CYSCREEN);
    hwnd = CreateWindowW(CLS, L"M480 WIPI", WS_VISIBLE, 0, 0, scrW, scrH, NULL, NULL, hi, NULL);
    if (!hwnd) return 1;
    keys_setup(hwnd);
    fullscreen();
    softkeys(1);
    create_dib();
    if (!bits) { vm_log("DIB failed"); return 1; }
    host.vibrate = vib;
    host.backlight = light;
    pal_audio_start();
    parse_args(cmd, game, sizeof game, &carrier, &gw, &gh);
    ui_init(scrW, scrH, appdir, game, carrier, gw, gh);
    present();

    for (;;) {
        int idle;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) goto done;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (ui_quit()) break;
        if (!active) { MsgWaitForMultipleObjects(0, NULL, FALSE, 500, QS_ALLINPUT); continue; }
        if (ui_tick()) present();
        if (ui_quit()) break;
        idle = ui_idle_ms();
        if (idle > 0) MsgWaitForMultipleObjects(0, NULL, FALSE, idle, QS_ALLINPUT);
        {
            static u32 last;
            u32 now = GetTickCount();
            if (now - last > 20000) { SystemIdleTimerReset(); last = now; }
        }
    }
done:
    softkeys(0);
    pal_audio_stop();
    {
        char g2[8];
        int c2, w2, h2;
        if (ui_restart(g2, sizeof g2, &c2, &w2, &h2)) {
            DestroyWindow(hwnd);
            restart_self();
        }
    }
    return 0;
}
