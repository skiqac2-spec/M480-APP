/* Windows Mobile 6.1 (and desktop Windows test build) platform layer */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#ifdef _WIN32_WCE
#include <aygshell.h>
#endif
#include "game.h"
#include <string.h>

static HWND hwnd;
static HDC memdc;
static HBITMAP dib, olddib;
static u16 *bits;
static HFONT fSmall, fBig;
static int scrW, scrH, dstX, dstY, dstW, dstH, logH;
static const wchar_t *CLS = L"AbyssLabyrinthWnd";

#ifdef _WIN32_WCE
#define GDIFLUSH()
#else
#define GDIFLUSH() GdiFlush()
#endif

static COLORREF cref(u16 c) { return RGB(((c >> 11) & 31) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3); }

int plat_text_w(const wchar_t *s, int fl)
{
    SIZE sz;
    HFONT o = (HFONT)SelectObject(memdc, (fl & T_BIG) ? fBig : fSmall);
    GetTextExtentPoint32W(memdc, s, (int)wcslen(s), &sz);
    SelectObject(memdc, o);
    return sz.cx;
}

void plat_text(int x, int y, const wchar_t *s, u16 c, int fl)
{
    int n = (int)wcslen(s);
    HFONT o;
    if (!n) return;
    o = (HFONT)SelectObject(memdc, (fl & T_BIG) ? fBig : fSmall);
    if (fl & (T_CENTER | T_RIGHT)) {
        SIZE sz;
        GetTextExtentPoint32W(memdc, s, n, &sz);
        x -= (fl & T_CENTER) ? sz.cx / 2 : sz.cx;
    }
    SetBkMode(memdc, TRANSPARENT);
    if (fl & T_SHADOW) {
        SetTextColor(memdc, RGB(0, 0, 0));
        ExtTextOutW(memdc, x + 1, y + 1, 0, NULL, s, n, NULL);
    }
    SetTextColor(memdc, cref(c));
    ExtTextOutW(memdc, x, y, 0, NULL, s, n, NULL);
    SelectObject(memdc, o);
    GDIFLUSH();
}

static void save_path(wchar_t *p)
{
    wchar_t *s;
    GetModuleFileNameW(NULL, p, MAX_PATH);
    s = wcsrchr(p, L'\\');
    if (s) s[1] = 0; else p[0] = 0;
    wcscat(p, L"abyss.sav");
}
int plat_save(const void *d, int n)
{
    wchar_t p[MAX_PATH + 16];
    HANDLE h;
    DWORD w = 0;
    save_path(p);
    h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    WriteFile(h, d, (DWORD)n, &w, NULL);
    CloseHandle(h);
    return (int)w == n;
}
int plat_load(void *d, int n)
{
    wchar_t p[MAX_PATH + 16];
    HANDLE h;
    DWORD r = 0;
    save_path(p);
    h = CreateFileW(p, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    ReadFile(h, d, (DWORD)n, &r, NULL);
    CloseHandle(h);
    return (int)r == n;
}
void plat_delsave(void)
{
    wchar_t p[MAX_PATH + 16];
    save_path(p);
    DeleteFileW(p);
}

void *plat_fopen(const wchar_t *name)
{
    wchar_t p[MAX_PATH + 32], *s;
    HANDLE h;
    GetModuleFileNameW(NULL, p, MAX_PATH);
    s = wcsrchr(p, L'\\');
    if (s) s[1] = 0; else p[0] = 0;
    wcscat(p, name);
    h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    return h == INVALID_HANDLE_VALUE ? NULL : (void *)h;
}
int plat_fread(void *f, void *buf, int n)
{
    DWORD r = 0;
    if (!ReadFile((HANDLE)f, buf, (DWORD)n, &r, NULL)) return 0;
    return (int)r;
}
int plat_fseek(void *f, long off) { return SetFilePointer((HANDLE)f, off, NULL, FILE_BEGIN) != 0xFFFFFFFF; }
void plat_fclose(void *f) { CloseHandle((HANDLE)f); }

/* startup log next to the exe (helps diagnose devices where the app does not start) */
static void plog(const char *msg)
{
    wchar_t p[MAX_PATH + 32], *s;
    HANDLE h;
    DWORD w;
    GetModuleFileNameW(NULL, p, MAX_PATH);
    s = wcsrchr(p, L'\\');
    if (s) s[1] = 0; else p[0] = 0;
    wcscat(p, L"abyss_log.txt");
    h = CreateFileW(p, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, msg, (DWORD)strlen(msg), &w, NULL);
    WriteFile(h, "\r\n", 2, &w, NULL);
    CloseHandle(h);
}

static HFONT mkfont(int h, int bold)
{
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -h;
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfCharSet = HANGEUL_CHARSET;
    lf.lfQuality = DEFAULT_QUALITY;
    return CreateFontIndirectW(&lf);
}

static void calc_dst(void)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    scrW = rc.right - rc.left;
    scrH = rc.bottom - rc.top;
    if (scrW <= 0 || scrH <= 0) return;
    if (scrW * logH <= scrH * GW) {        /* width-limited */
        dstW = scrW; dstH = logH * scrW / GW;
    } else {
        dstH = scrH; dstW = GW * scrH / logH;
    }
    dstX = (scrW - dstW) / 2;
    dstY = (scrH - dstH) / 2;
}

static void create_dib(void)
{
    struct { BITMAPINFOHEADER h; DWORD m[3]; } bi;
    HDC dc = GetDC(hwnd);
    memset(&bi, 0, sizeof(bi));
    bi.h.biSize = sizeof(BITMAPINFOHEADER);
    bi.h.biWidth = GW;
    bi.h.biHeight = -logH;
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 16;
    bi.h.biCompression = BI_BITFIELDS;
    bi.m[0] = 0xF800; bi.m[1] = 0x07E0; bi.m[2] = 0x001F;
    dib = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    memdc = CreateCompatibleDC(dc);
    olddib = (HBITMAP)SelectObject(memdc, dib);
    ReleaseDC(hwnd, dc);
}

static void redraw(void)
{
    game_render();
    GDIFLUSH();
    InvalidateRect(hwnd, NULL, FALSE);
}

static void fullscreen(void)
{
#ifdef _WIN32_WCE
    SHFullScreen(hwnd, SHFS_HIDETASKBAR | SHFS_HIDESIPBUTTON | SHFS_HIDESTARTICON);
    MoveWindow(hwnd, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), TRUE);
#endif
}

static int mapkey(WPARAM vk)
{
    switch (vk) {
    case VK_UP: return K_UP;
    case VK_DOWN: return K_DOWN;
    case VK_LEFT: return K_LEFT;
    case VK_RIGHT: return K_RIGHT;
    case VK_RETURN: case VK_SPACE: return K_OK;
    case VK_ESCAPE: case VK_BACK: case VK_F2: return K_BACK;
    case VK_F1: case 'M': return K_MENU;
    case '1': case '2': case '3': case '4': return 0;
    }
    return 0;
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) fullscreen();
        break;
    case WM_SIZE:
        calc_dst();
        InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        HBRUSH bk = (HBRUSH)GetStockObject(BLACK_BRUSH);
        if (dstY > 0) { SetRect(&r, 0, 0, scrW, dstY); FillRect(dc, &r, bk); SetRect(&r, 0, dstY + dstH, scrW, scrH); FillRect(dc, &r, bk); }
        if (dstX > 0) { SetRect(&r, 0, 0, dstX, scrH); FillRect(dc, &r, bk); SetRect(&r, dstX + dstW, 0, scrW, scrH); FillRect(dc, &r, bk); }
        if (dstW == GW && dstH == logH) BitBlt(dc, dstX, dstY, GW, logH, memdc, 0, 0, SRCCOPY);
        else StretchBlt(dc, dstX, dstY, dstW, dstH, memdc, 0, 0, GW, logH, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        if (dstW > 0 && dstH > 0 && x >= dstX && y >= dstY && x < dstX + dstW && y < dstY + dstH) {
            game_tap((x - dstX) * GW / dstW, (y - dstY) * logH / dstH);
            redraw();
        }
        return 0;
    }
    case WM_KEYDOWN: {
        int k = mapkey(wp);
        if (k) { game_key(k); redraw(); }
        if (game_wants_quit()) { DestroyWindow(h); }
        return 0;
    }
    case WM_TIMER:
        if (game_tick()) redraw();
        if (game_wants_quit()) DestroyWindow(h);
        return 0;
    case WM_CLOSE:
        game_suspend();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

#ifdef _WIN32_WCE
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cmd, int show)
#else
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show)
#endif
{
    WNDCLASSW wc;
    MSG m;
    int sw, sh;
    (void)hp; (void)cmd;

    plog("--- start");
    hwnd = FindWindowW(CLS, NULL);
    if (hwnd) { SetForegroundWindow(hwnd); return 0; }

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = CLS;
#ifndef _WIN32_WCE
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
#endif
    RegisterClassW(&wc);

    sw = GetSystemMetrics(SM_CXSCREEN);
    sh = GetSystemMetrics(SM_CYSCREEN);
#ifdef _WIN32_WCE
    if (sw <= sh) { logH = sh * GW / sw; if (logH < 320) logH = 320; if (logH > 427) logH = 427; }
    else logH = 320;
    hwnd = CreateWindowW(CLS, L"심연의 미궁", WS_VISIBLE, 0, 0, sw, sh, NULL, NULL, hi, NULL);
#else
    {
        RECT r = { 0, 0, GW * 2, 320 * 2 };
        logH = 320;
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        hwnd = CreateWindowW(CLS, L"심연의 미궁 (PC 테스트판)", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, hi, NULL);
    }
#endif
    if (!hwnd) { plog("CreateWindow failed"); return 1; }
    plog("window ok");
    fSmall = mkfont(12, 0);
    fBig = mkfont(22, 1);
    create_dib();
    plog(bits ? "dib ok" : "dib FAILED");
    if (!bits) return 1;
    calc_dst();
    fullscreen();
    plog("game_init...");
    game_init(logH, bits, GetTickCount());
    plog("game_init ok");
    redraw();
    plog("first frame ok");
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    SetTimer(hwnd, 1, 50, NULL);
    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    SelectObject(memdc, olddib);
    DeleteObject(dib);
    DeleteDC(memdc);
    DeleteObject(fSmall);
    DeleteObject(fBig);
    return 0;
}
