/*
 * M480Home - SCH-M480(WM6.1)용 손가락으로 쓰기 쉬운 홈 화면
 *
 *  - 홈: 큰 시계·날짜·배터리·인터넷 상태 + 큰 앱 아이콘 칸 (좌우로 밀어 페이지 넘김)
 *  - 전체 앱: 시작 메뉴의 모든 프로그램을 큰 글씨 목록으로 (꾹 누르면 홈에 추가/빼기)
 *  - 실행 중: 켜져 있는 앱 목록, 눌러서 전환, ✕로 닫기, 모두 닫기, 남은 메모리
 *  - 설정: 소리 크기, 화면 끄기, 오늘 화면 대신 쓰기, 자동 시작, 아이콘 크기, 시계 형식
 *  - 배경화면: 기본 색 배경 9가지 또는 내 사진(JPG/PNG/BMP), 어둡게 조절
 *  - 아이콘 자리: 꾹 누른 채 끌어서 아무 칸에나 (빈칸 허용, 화면 끝에 대면 페이지 넘김)
 *  - 오늘(Today) 화면이 앞으로 나오면 자동으로 M480Home을 대신 보여 줌 (설정에서 끌 수 있음)
 *
 * 키: 방향키 이동, 가운데(확인) 실행, 왼쪽 소프트키 전체 앱, 오른쪽 소프트키 실행 중, 뒤로 = 홈
 */
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(x) ((void)0)
#include "stb_image.h"

#define APP_CLASS L"M480HomeWnd"
#define APP_TITLE L"M480 홈"
#define APP_VER   L"1.1"

#ifndef VK_OFF
#define VK_OFF 0xDF
#endif
#ifndef KEYEVENTF_SILENT
#define KEYEVENTF_SILENT 0x0004
#endif

/* ------------------------------------------------------------------ */
/* 색 (M480Music과 같은 어두운 테마)                                    */
/* ------------------------------------------------------------------ */
#define C_BG      RGB(12, 12, 14)
#define C_CARD    RGB(30, 30, 34)
#define C_PRESS   RGB(58, 58, 66)
#define C_TEXT    RGB(246, 246, 248)
#define C_SUB     RGB(150, 150, 158)
#define C_DIM     RGB(90, 90, 98)
#define C_SEP     RGB(40, 40, 44)
#define C_ACCENT  RGB(255, 112, 84)
#define C_OK      RGB(90, 210, 120)
#define C_WARN    RGB(255, 196, 60)
#define C_BAD     RGB(240, 70, 70)
#define C_FOCUS   RGB(255, 150, 120)

static const COLORREF PAL[] = {
    RGB(220, 70, 96), RGB(240, 128, 50), RGB(70, 140, 240), RGB(60, 170, 110),
    RGB(150, 90, 230), RGB(220, 170, 40), RGB(40, 170, 200), RGB(220, 90, 160),
    RGB(110, 130, 150), RGB(200, 100, 70)
};

/* ------------------------------------------------------------------ */
/* 전역                                                                */
/* ------------------------------------------------------------------ */
static HWND g_hwnd;
static HINSTANCE g_inst;
static wchar_t g_exeDir[MAX_PATH], g_exePath[MAX_PATH];
static int g_scale = 1;
#define S(x) ((x) * g_scale)
static int W, H;

static HFONT fClock, fHead, fBold, fNorm, fSmall, fLetter, fLetterB;

typedef struct {
    wchar_t path[MAX_PATH];   /* 실행할 파일 (.exe 또는 .lnk) */
    wchar_t key[MAX_PATH];    /* 중복 확인용 (실제 대상 경로, 소문자) */
    wchar_t name[64];
    HICON icon;
    int iconTried;
    int pos;                  /* 홈 칸 번호 (페이지*칸수 + 칸) */
} App;

#define MAX_FAV 60
#define MAX_ALL 200
static App g_fav[MAX_FAV];
static int g_favN;
static App g_all[MAX_ALL];
static int g_allN;

typedef struct { HWND h; wchar_t title[64]; } RunWin;
#define MAX_RUN 40
static RunWin g_run[MAX_RUN];
static int g_runN;

/* 설정 */
static struct {
    int replaceToday;   /* 오늘 화면 대신 쓰기 */
    int grid;           /* 0: 3x3 보통, 1: 4x3 많이, 2: 3x2 크게 */
    int clock24;
    int wallType;       /* 0 없음, 1..: 기본 배경, WALL_PHOTO: 사진 */
    int wallDim;        /* 0 그대로, 1 조금, 2 많이 어둡게 */
    wchar_t wallPath[MAX_PATH];
} g_set = { 1, 0, 1, 1, 1, L"" };
#define WALL_PHOTO 100

/* 화면 */
enum { SCR_HOME, SCR_ALL, SCR_RUN, SCR_SET, SCR_WALL, SCR_N };
static int g_scr = SCR_HOME;
static int g_page;
static int g_scroll[SCR_N], g_contentH[SCR_N];
static int g_addMode;       /* 전체 앱 화면을 '홈에 추가' 용도로 열었음 */
static int g_focus = -1;    /* 방향키 선택 (홈: 칸 번호, 목록: 줄 번호) */
static int g_keyMode;

/* 터치 */
static int t_down, t_moved, t_long, t_x0, t_y0, t_lx, t_ly, t_scroll0, t_vel;
static DWORD t_lt;
static int t_hit = -1;      /* 눌린 영역 번호 */
static int g_swipeX;        /* 홈 페이지 밀기 중 가로 이동량 */
static int g_lift = -1;     /* 꾹 눌러 들어 올린 아이콘 (g_fav 번호) */
static int g_dragging, g_dx, g_dy, g_hover = -1, g_liftX, g_liftY;
static int g_addFrom;       /* 새로 넣는 앱이 들어갈 첫 칸 */
static int g_gTop, g_gW, g_gH, g_gCols, g_gRows;
static void grid_dims(int *cols, int *rows);

/* 눌림 영역 */
enum {
    H_NONE, H_TILE, H_EMPTY, H_WDIM, H_WPRESET, H_WPIC, H_NAV_ALL, H_NAV_RUN, H_NAV_SET, H_BACK, H_ROW, H_CLOSE, H_CLOSEALL,
    H_VOLDN, H_VOLUP, H_OFF, H_OPT, H_MENU, H_MENU_BG, H_DOT, H_DONE
};
typedef struct { RECT r; int id, arg; } Hit;
#define MAX_HIT 96
static Hit g_hit[MAX_HIT];
static int g_hitN;

/* 꾹 누르면 나오는 메뉴 */
enum { M_FAV_REMOVE = 1, M_ALL_ADD, M_ALL_REMOVE, M_RUN_CLOSE, M_CANCEL };
static struct {
    int on, target;
    wchar_t title[64];
    const wchar_t *hint;
    int n, act[6];
    const wchar_t *label[6];
} g_menu;

/* 알림 한 줄 */
static wchar_t g_toast[80];
static DWORD g_toastUntil;
static DWORD g_lastLaunch;

/* 상태 */
static int g_batt = -1, g_charging, g_net;
static int g_vol = -1;          /* 0..10 */
static int g_lastMin = -1;

/* ------------------------------------------------------------------ */
/* 동적으로 불러오는 함수 (기기마다 없을 수도 있음)                      */
/* ------------------------------------------------------------------ */
typedef struct {
    BYTE ACLineStatus, BatteryFlag, BatteryLifePercent, Reserved1;
    DWORD BatteryLifeTime, BatteryFullLifeTime;
    BYTE Reserved2, BackupBatteryFlag, BackupBatteryLifePercent, Reserved3;
    DWORD BackupBatteryLifeTime, BackupBatteryFullLifeTime;
} MyPowerEx;
typedef BOOL (WINAPI *PFN_PowerEx)(MyPowerEx *, BOOL);
typedef BOOL (WINAPI *PFN_ShellEx)(SHELLEXECUTEINFOW *);
typedef DWORD_PTR (WINAPI *PFN_FileInfo)(LPCWSTR, DWORD, SHFILEINFOW *, UINT, UINT);
typedef BOOL (WINAPI *PFN_Special)(HWND, LPWSTR, int, BOOL);
static PFN_PowerEx pPowerEx;
static PFN_ShellEx pShellEx;
static PFN_FileInfo pFileInfo;
static PFN_Special pSpecial;

static void load_funcs(void)
{
    HMODULE h = LoadLibraryW(L"coredll.dll");
    if (!h) return;
    pPowerEx = (PFN_PowerEx)GetProcAddressW(h, L"GetSystemPowerStatusEx");
    pShellEx = (PFN_ShellEx)GetProcAddressW(h, L"ShellExecuteEx");
    pFileInfo = (PFN_FileInfo)GetProcAddressW(h, L"SHGetFileInfo");
    pSpecial = (PFN_Special)GetProcAddressW(h, L"SHGetSpecialFolderPath");
}

/* ------------------------------------------------------------------ */
/* 작은 도우미                                                          */
/* ------------------------------------------------------------------ */
static int file_exists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != 0xFFFFFFFF && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static int dir_exists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != 0xFFFFFFFF && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static void lower(wchar_t *s) { for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s += 32; }
static int ends_with(const wchar_t *s, const wchar_t *e)
{
    size_t a = wcslen(s), b = wcslen(e);
    return a >= b && _wcsicmp(s + a - b, e) == 0;
}
static const wchar_t *base_name(const wchar_t *p)
{
    const wchar_t *s = wcsrchr(p, L'\\');
    return s ? s + 1 : p;
}
static unsigned hash_str(const wchar_t *s)
{
    unsigned h = 5381;
    for (; *s; s++) h = h * 33 + *s;
    return h;
}
static void toast(const wchar_t *s)
{
    wcsncpy(g_toast, s, 79); g_toast[79] = 0;
    g_toastUntil = GetTickCount() + 1800;
    SetTimer(g_hwnd, 4, 300, NULL);
}
static void redraw(void) { InvalidateRect(g_hwnd, NULL, FALSE); }

/* ------------------------------------------------------------------ */
/* 바로 가기(.lnk) 읽기: CE의 lnk는  25#"\경로\앱.exe" 인자  형태의 글자 파일 */
/* ------------------------------------------------------------------ */
static int lnk_target(const wchar_t *lnk, wchar_t *out)
{
    HANDLE f = CreateFileW(lnk, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    char buf[600];
    DWORD n = 0;
    char *p, *e;
    wchar_t w[MAX_PATH * 2];
    out[0] = 0;
    if (f == INVALID_HANDLE_VALUE) return 0;
    ReadFile(f, buf, sizeof(buf) - 1, &n, NULL);
    CloseHandle(f);
    buf[n] = 0;
    p = strchr(buf, '#');
    if (!p) return 0;
    p++;
    if (*p == '"') {
        p++;
        e = strchr(p, '"');
        if (!e) return 0;
        *e = 0;
    } else {
        for (e = p; *e && *e != '\r' && *e != '\n'; e++) ;
        *e = 0;
    }
    if (!MultiByteToWideChar(CP_ACP, 0, p, -1, w, MAX_PATH * 2)) return 0;
    /* 따옴표 없는 경로에 인자가 붙어 있으면 앞부분만 */
    if (!file_exists(w)) {
        wchar_t *sp = wcschr(w, L' ');
        while (sp) {
            *sp = 0;
            if (file_exists(w)) break;
            *sp = L' ';
            sp = wcschr(sp + 1, L' ');
        }
    }
    wcsncpy(out, w, MAX_PATH - 1);
    out[MAX_PATH - 1] = 0;
    return out[0] != 0;
}

/* 알려진 앱은 한글 이름으로 */
static const struct { const wchar_t *exe, *name; } KNOWN[] = {
    { L"m480web.exe", L"웹 브라우저" },
    { L"m480tube.exe", L"유튜브" },
    { L"m480music.exe", L"음악" },
    { L"m480comic.exe", L"만화" },
    { L"emulator.exe", L"게임 에뮬" },
    { L"m480wipi.exe", L"피처폰 게임" },
    { L"abyss_wm61.exe", L"심연의 미궁" },
    { L"m480call.exe", L"통화" },
    { L"d2m480.exe", L"디아블로2" },
    { L"fexplore.exe", L"파일 탐색기" },
    { L"ctlpnl.exe", L"설정" },
    { L"calc.exe", L"계산기" },
    { L"calendar.exe", L"일정" },
    { L"tmail.exe", L"메시지" },
    { L"pimg.exe", L"사진" },
    { L"wmplayer.exe", L"미디어 재생" },
    { L"notes.exe", L"메모" },
    { L"taskmgr.exe", L"작업 관리자" },
    { NULL, NULL }
};
static const wchar_t *known_name(const wchar_t *path)
{
    const wchar_t *b = base_name(path);
    int i;
    for (i = 0; KNOWN[i].exe; i++) if (_wcsicmp(b, KNOWN[i].exe) == 0) return KNOWN[i].name;
    return NULL;
}

static void app_set(App *a, const wchar_t *path, const wchar_t *target, const wchar_t *name)
{
    const wchar_t *kn = known_name(target && target[0] ? target : path);
    memset(a, 0, sizeof(*a));
    wcsncpy(a->path, path, MAX_PATH - 1);
    wcsncpy(a->key, target && target[0] ? target : path, MAX_PATH - 1);
    lower(a->key);
    if (kn) wcsncpy(a->name, kn, 63);
    else if (name) wcsncpy(a->name, name, 63);
    else {
        const wchar_t *b = base_name(path);
        wchar_t *dot;
        wcsncpy(a->name, b, 63);
        dot = wcsrchr(a->name, L'.');
        if (dot) *dot = 0;
    }
}

static void app_free_icon(App *a)
{
    if (a->icon) DestroyIcon(a->icon);
    a->icon = NULL;
    a->iconTried = 0;
}

static HICON app_icon(App *a)
{
    if (!a->iconTried) {
        a->iconTried = 1;
        if (pFileInfo) {
            SHFILEINFOW fi;
            memset(&fi, 0, sizeof(fi));
            if (pFileInfo(a->path, 0, &fi, sizeof(fi), SHGFI_ICON | SHGFI_LARGEICON) && fi.hIcon)
                a->icon = fi.hIcon;
        }
    }
    return a->icon;
}

static int is_self(const wchar_t *key)
{
    return wcsstr(key, L"m480home") != NULL;
}

/* ------------------------------------------------------------------ */
/* 전체 앱: 시작 메뉴 훑기                                              */
/* ------------------------------------------------------------------ */
static int all_find(const wchar_t *key)
{
    int i;
    for (i = 0; i < g_allN; i++) if (wcscmp(g_all[i].key, key) == 0) return i;
    return -1;
}

static void scan_dir(const wchar_t *dir, int depth)
{
    wchar_t pat[MAX_PATH], full[MAX_PATH], tgt[MAX_PATH], name[64];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    _snwprintf(pat, MAX_PATH, L"%s\\*", dir);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.') continue;
        _snwprintf(full, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < 1) scan_dir(full, depth + 1);
            continue;
        }
        if (!ends_with(fd.cFileName, L".lnk") || g_allN >= MAX_ALL) continue;
        wcsncpy(name, fd.cFileName, 63); name[63] = 0;
        if (wcslen(name) > 4) name[wcslen(name) - 4] = 0;
        lnk_target(full, tgt);
        {
            App a;
            app_set(&a, full, tgt, name);
            if (is_self(a.key) || all_find(a.key) >= 0) continue;
            g_all[g_allN++] = a;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static int cmp_app(const void *x, const void *y)
{
    return wcscmp(((const App *)x)->name, ((const App *)y)->name);
}

static void special_dir(int csidl, wchar_t *out, const wchar_t *const *fallbacks)
{
    int i;
    out[0] = 0;
    if (pSpecial && pSpecial(NULL, out, csidl, FALSE) && dir_exists(out)) return;
    for (i = 0; fallbacks[i]; i++) if (dir_exists(fallbacks[i])) { wcscpy(out, fallbacks[i]); return; }
    wcscpy(out, fallbacks[0]);
}

static const wchar_t *const FB_PROGRAMS[] = { L"\\Windows\\Start Menu\\Programs", L"\\Windows\\시작 메뉴\\프로그램", NULL };
static const wchar_t *const FB_START[] = { L"\\Windows\\Start Menu", L"\\Windows\\시작 메뉴", NULL };
static const wchar_t *const FB_STARTUP[] = { L"\\Windows\\StartUp", L"\\Windows\\시작프로그램", L"\\Windows\\시작 프로그램", NULL };

static void scan_all(void)
{
    wchar_t d[MAX_PATH], pat[MAX_PATH], full[MAX_PATH], tgt[MAX_PATH], name[64];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int i;
    for (i = 0; i < g_allN; i++) app_free_icon(&g_all[i]);
    g_allN = 0;
    special_dir(2 /* CSIDL_PROGRAMS */, d, FB_PROGRAMS);
    scan_dir(d, 0);
    /* 시작 메뉴 맨 위 칸 (하위 폴더는 프로그램 폴더라 이미 봤음) */
    special_dir(0x0b /* CSIDL_STARTMENU */, d, FB_START);
    _snwprintf(pat, MAX_PATH, L"%s\\*.lnk", d);
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            App a;
            if (g_allN >= MAX_ALL) break;
            _snwprintf(full, MAX_PATH, L"%s\\%s", d, fd.cFileName);
            wcsncpy(name, fd.cFileName, 63); name[63] = 0;
            if (wcslen(name) > 4) name[wcslen(name) - 4] = 0;
            lnk_target(full, tgt);
            app_set(&a, full, tgt, name);
            if (is_self(a.key) || all_find(a.key) >= 0) continue;
            g_all[g_allN++] = a;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
#ifdef PREVIEW_W
    if (g_allN == 0) {
        static const wchar_t *demo[] = { L"계산기", L"게임 에뮬", L"만화", L"메시지", L"사진", L"설정",
            L"심연의 미궁", L"웹 브라우저", L"유튜브", L"음악", L"인터넷 익스플로러", L"일정", L"파일 탐색기",
            L"피처폰 게임", L"통화", NULL };
        for (i = 0; demo[i]; i++) {
            wchar_t p[64];
            _snwprintf(p, 64, L"\\demo\\%d.lnk", i);
            app_set(&g_all[g_allN], p, NULL, demo[i]);
            g_allN++;
        }
    }
#endif
    qsort(g_all, g_allN, sizeof(App), cmp_app);
}

/* ------------------------------------------------------------------ */
/* 홈(즐겨찾기)                                                         */
/* ------------------------------------------------------------------ */
static int fav_find(const wchar_t *key)
{
    int i;
    for (i = 0; i < g_favN; i++) if (wcscmp(g_fav[i].key, key) == 0) return i;
    return -1;
}
static int fav_at(int pos)
{
    int i;
    for (i = 0; i < g_favN; i++) if (g_fav[i].pos == pos) return i;
    return -1;
}
static int first_free(int from)
{
    int p = from < 0 ? 0 : from;
    while (fav_at(p) >= 0) p++;
    return p;
}
static int per_page(void)
{
    int c, r;
    grid_dims(&c, &r);
    return c * r;
}
static int fav_add(const App *a)
{
    if (g_favN >= MAX_FAV || fav_find(a->key) >= 0) return 0;
    g_fav[g_favN] = *a;
    g_fav[g_favN].icon = NULL;
    g_fav[g_favN].iconTried = 0;
    g_fav[g_favN].pos = first_free(g_addFrom);
    g_favN++;
    return 1;
}
static void fav_remove(int i)
{
    if (i < 0 || i >= g_favN) return;
    if (g_lift == i) g_lift = -1;
    app_free_icon(&g_fav[i]);
    memmove(&g_fav[i], &g_fav[i + 1], sizeof(App) * (g_favN - i - 1));
    g_favN--;
}

/* 아이콘 크기(칸 수)를 바꿀 때: 같은 페이지·줄·칸 자리는 그대로, 새 칸 밖으로 나간 것만 빈칸으로 */
static void regrid(int oldGrid, int newGrid)
{
    static const int gc[3] = { 3, 4, 3 }, gr[3] = { 3, 3, 2 };
    int oc = gc[oldGrid % 3], orr = gr[oldGrid % 3], nc = gc[newGrid % 3], nr = gr[newGrid % 3];
    int i, lost[MAX_FAV], nl = 0;
    for (i = 0; i < g_favN; i++) {
        int pg = g_fav[i].pos / (oc * orr), cell = g_fav[i].pos % (oc * orr), r = cell / oc, c = cell % oc;
        if (r < nr && c < nc) g_fav[i].pos = pg * nc * nr + r * nc + c;
        else { g_fav[i].pos = -1000 - i; lost[nl++] = i; }
    }
    for (i = 0; i < nl; i++) g_fav[lost[i]].pos = first_free(0);
}

/* 기본 홈: M480 앱을 찾아서 + 파일 탐색기·설정 */
static const struct { const wchar_t *dir, *exe; } M480APPS[] = {
    { L"M480Web", L"M480Web.exe" },
    { L"M480Tube", L"M480Tube.exe" },
    { L"M480Music", L"M480Music.exe" },
    { L"M480Comic", L"M480Comic.exe" },
    { L"M480Emu", L"Emulator.exe" },
    { L"M480Wipi", L"M480Wipi.exe" },
    { L"AbyssLabyrinth", L"Abyss_WM61.exe" },
    { L"M480Call", L"M480Call.exe" },
    { L"D2M480", L"D2M480.exe" },
    { NULL, NULL }
};

static int find_m480(int k, wchar_t *out)
{
    static const wchar_t *roots[] = { L"", L"\\Storage Card", L"\\SD Card", L"\\Storage Card2", L"\\SDMMC", L"\\My Documents", NULL };
    static const wchar_t *mids[] = { L"\\Program Files\\", L"\\M480-APP\\", L"\\", NULL };
    int r, m, i;
    /* 1) 시작 메뉴에 있는 바로 가기 (CAB으로 설치한 앱) */
    for (i = 0; i < g_allN; i++)
        if (_wcsicmp(base_name(g_all[i].key), M480APPS[k].exe) == 0) { wcscpy(out, g_all[i].path); return 1; }
    /* 2) 폴더째 복사했거나 내 파일함으로 받은 exe */
    for (m = 0; mids[m]; m++)
        for (r = 0; roots[r]; r++) {
            _snwprintf(out, MAX_PATH, L"%s%s%s\\%s", roots[r], mids[m], M480APPS[k].dir, M480APPS[k].exe);
            if (file_exists(out)) return 1;
        }
    return 0;
}

static void fav_defaults(void)
{
    wchar_t p[MAX_PATH];
    int k, i;
    static const wchar_t *sys[] = { L"\\Windows\\fexplore.exe", L"\\Windows\\ctlpnl.exe", NULL };
    for (i = 0; i < g_favN; i++) app_free_icon(&g_fav[i]);
    g_favN = 0;
    g_addFrom = 0;
    for (k = 0; M480APPS[k].dir; k++) {
        if (find_m480(k, p)) {
            App a;
            wchar_t tgt[MAX_PATH];
            if (ends_with(p, L".lnk")) lnk_target(p, tgt); else wcscpy(tgt, p);
            app_set(&a, p, tgt, NULL);
            fav_add(&a);
        }
    }
    for (i = 0; sys[i]; i++) {
        if (file_exists(sys[i])) {
            App a;
            app_set(&a, sys[i], sys[i], NULL);
            fav_add(&a);
        }
    }
#ifdef PREVIEW_W
    if (g_favN == 0) {
        static const int pick[] = { 7, 8, 9, 2, 1, 13, 6, 14, 12, 5, -1 };
        for (i = 0; pick[i] >= 0; i++) fav_add(&g_all[pick[i] < g_allN ? pick[i] : 0]);
    }
#endif
}

/* ------------------------------------------------------------------ */
/* 설정 파일 (exe 옆 M480Home.ini, UTF-8)                               */
/* ------------------------------------------------------------------ */
static void ini_path(wchar_t *p) { wcscpy(p, g_exeDir); wcscat(p, L"M480Home.ini"); }

static void save_settings(void)
{
    wchar_t p[MAX_PATH], line[MAX_PATH * 3 + 120];
    char u8[MAX_PATH * 9 + 360];
    HANDLE f;
    DWORD wr;
    int i, n;
    ini_path(p);
    f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    for (i = -1; i < g_favN; i++) {
        if (i < 0)
            _snwprintf(line, sizeof(line) / sizeof(wchar_t),
                       L"today=%d\r\ngrid=%d\r\nclock24=%d\r\nwall=%d\r\nwalldim=%d\r\nwallpath=%s\r\nfavinit=2\r\n",
                       g_set.replaceToday, g_set.grid, g_set.clock24, g_set.wallType, g_set.wallDim, g_set.wallPath);
        else
            _snwprintf(line, sizeof(line) / sizeof(wchar_t), L"fav=%d\t%s\t%s\t%s\r\n", g_fav[i].pos, g_fav[i].path, g_fav[i].key, g_fav[i].name);
        n = WideCharToMultiByte(CP_UTF8, 0, line, -1, u8, sizeof(u8), NULL, NULL);
        if (n > 1) WriteFile(f, u8, n - 1, &wr, NULL);
    }
    CloseHandle(f);
}

static int load_settings(void)
{
    wchar_t p[MAX_PATH];
    HANDLE f;
    DWORD sz, rd = 0;
    char *buf;
    wchar_t *w, *line, *nl;
    int n, inited = 0;
    ini_path(p);
    f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    sz = GetFileSize(f, NULL);
    if (sz == 0xFFFFFFFF || sz > 200000) { CloseHandle(f); return 0; }
    buf = (char *)malloc(sz + 1);
    ReadFile(f, buf, sz, &rd, NULL);
    CloseHandle(f);
    buf[rd] = 0;
    n = MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
    w = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, w, n);
    free(buf);
    for (line = w; line && *line; line = nl) {
        nl = wcschr(line, L'\n');
        if (nl) *nl++ = 0;
        if (wcslen(line) && line[wcslen(line) - 1] == L'\r') line[wcslen(line) - 1] = 0;
        if (!wcsncmp(line, L"today=", 6)) g_set.replaceToday = (int)wcstol(line + 6, NULL, 10);
        else if (!wcsncmp(line, L"grid=", 5)) g_set.grid = (int)wcstol(line + 5, NULL, 10) % 3;
        else if (!wcsncmp(line, L"clock24=", 8)) g_set.clock24 = (int)wcstol(line + 8, NULL, 10);
        else if (!wcsncmp(line, L"wall=", 5)) g_set.wallType = (int)wcstol(line + 5, NULL, 10);
        else if (!wcsncmp(line, L"walldim=", 8)) g_set.wallDim = (int)wcstol(line + 8, NULL, 10) % 3;
        else if (!wcsncmp(line, L"wallpath=", 9)) { wcsncpy(g_set.wallPath, line + 9, MAX_PATH - 1); g_set.wallPath[MAX_PATH - 1] = 0; }
        else if (!wcsncmp(line, L"favinit=", 8)) inited = 1;
        else if (!wcsncmp(line, L"fav=", 4) && g_favN < MAX_FAV) {
            /* 1.1: fav=칸<TAB>경로<TAB>키<TAB>이름   1.0: fav=경로<TAB>키<TAB>이름 */
            wchar_t *f[4], *q = line + 4;
            int nf = 0;
            App *ap;
            while (nf < 4) {
                f[nf++] = q;
                q = wcschr(q, L'\t');
                if (!q) break;
                *q++ = 0;
            }
            if (nf < 3) continue;
            ap = &g_fav[g_favN];
            memset(ap, 0, sizeof(*ap));
            if (nf == 4) {
                ap->pos = (int)wcstol(f[0], NULL, 10);
                f[0] = f[1]; f[1] = f[2]; f[2] = f[3];
            } else ap->pos = g_favN;
            wcsncpy(ap->path, f[0], MAX_PATH - 1);
            wcsncpy(ap->key, f[1], MAX_PATH - 1);
            wcsncpy(ap->name, f[2], 63);
            if (fav_at(ap->pos) >= 0) ap->pos = first_free(0);
            g_favN++;
        }
    }
    free(w);
    return inited;
}

/* ------------------------------------------------------------------ */
/* 자동 시작 (시작프로그램 폴더의 바로 가기)                             */
/* ------------------------------------------------------------------ */
static void startup_lnk(wchar_t *p)
{
    wchar_t d[MAX_PATH];
    special_dir(7 /* CSIDL_STARTUP */, d, FB_STARTUP);
    _snwprintf(p, MAX_PATH, L"%s\\M480Home.lnk", d);
}
static int autostart_on(void)
{
    wchar_t p[MAX_PATH];
    startup_lnk(p);
    return file_exists(p);
}
static void autostart_set(int on)
{
    wchar_t p[MAX_PATH];
    startup_lnk(p);
    if (on) {
        char a[MAX_PATH * 2], s[MAX_PATH * 2 + 16];
        HANDLE f;
        DWORD wr;
        int n;
        WideCharToMultiByte(CP_ACP, 0, g_exePath, -1, a, sizeof(a), NULL, NULL);
        n = (int)strlen(a) + 2;
        _snprintf(s, sizeof(s), "%d#\"%s\"", n, a);
        f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) { WriteFile(f, s, (DWORD)strlen(s), &wr, NULL); CloseHandle(f); }
    } else {
        DeleteFileW(p);
    }
}

/* ------------------------------------------------------------------ */
/* 실행 / 전환                                                          */
/* ------------------------------------------------------------------ */
static void launch(App *a)
{
    DWORD now = GetTickCount();
    BOOL ok = FALSE;
    wchar_t msg[96];
    if (now - g_lastLaunch < 1500) return;     /* 두 번 눌러 두 번 켜지는 것 방지 */
    g_lastLaunch = now;
    if (pShellEx) {
        SHELLEXECUTEINFOW se;
        memset(&se, 0, sizeof(se));
        se.cbSize = sizeof(se);
        se.hwnd = g_hwnd;
        se.lpFile = a->path;
        se.lpVerb = L"open";
        se.nShow = SW_SHOWNORMAL;
        ok = pShellEx(&se);
    }
    if (!ok && ends_with(a->path, L".exe")) {
        PROCESS_INFORMATION pi;
        if (CreateProcessW(a->path, NULL, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &pi)) {
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            ok = TRUE;
        }
    }
    if (ok) _snwprintf(msg, 96, L"%s 여는 중…", a->name);
    else _snwprintf(msg, 96, L"%s 을(를) 열 수 없어요 (파일이 옮겨졌을 수 있음)", a->name);
    msg[95] = 0;
    toast(msg);
    redraw();
}

static int run_skip_class(const wchar_t *c)
{
    static const wchar_t *skip[] = { L"HHTaskBar", L"DesktopExplorerWindow", L"menu_worker", L"MS_SIPBUTTON",
        L"SipWndClass", L"Tray", L"static", L"Shell_TrayWnd", L"tooltips_class32", L"Explore_WClass_HiddenProxy",
        APP_CLASS, NULL };
    int i;
    for (i = 0; skip[i]; i++) if (_wcsicmp(c, skip[i]) == 0) return 1;
    return 0;
}

static BOOL CALLBACK enum_proc(HWND h, LPARAM lp)
{
    wchar_t cls[64], title[64];
    (void)lp;
    if (g_runN >= MAX_RUN || h == g_hwnd) return TRUE;
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    if (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    if (!GetWindowTextW(h, title, 64) || !title[0]) return TRUE;
    cls[0] = 0;
    GetClassNameW(h, cls, 64);
    if (run_skip_class(cls)) return TRUE;
    g_run[g_runN].h = h;
    wcscpy(g_run[g_runN].title, title);
    g_runN++;
    return TRUE;
}

static void scan_running(void)
{
    g_runN = 0;
    EnumWindows(enum_proc, 0);
#ifdef PREVIEW_W
    if (g_runN == 0) {
        static const wchar_t *demo[] = { L"M480 뮤직", L"M480Web", L"메시지", NULL };
        int i;
        for (i = 0; demo[i]; i++) { g_run[i].h = NULL; wcscpy(g_run[i].title, demo[i]); }
        g_runN = i;
    }
#endif
}

static void run_switch(int i)
{
    if (i < 0 || i >= g_runN || !IsWindow(g_run[i].h)) return;
    SetForegroundWindow((HWND)((ULONG_PTR)g_run[i].h | 1));
}

static void run_close(int i)
{
    if (i < 0 || i >= g_runN) return;
    if (g_run[i].h) PostMessageW(g_run[i].h, WM_CLOSE, 0, 0);
    memmove(&g_run[i], &g_run[i + 1], sizeof(RunWin) * (g_runN - i - 1));
    g_runN--;
    redraw();
    SetTimer(g_hwnd, 5, 900, NULL);   /* 잠시 뒤 다시 확인 (안 닫힌 앱 다시 보임) */
}

/* ------------------------------------------------------------------ */
/* 배터리 / 인터넷 / 소리                                               */
/* ------------------------------------------------------------------ */
static void read_status(void)
{
    HKEY k;
    g_batt = -1; g_charging = 0;
    if (pPowerEx) {
        MyPowerEx ps;
        memset(&ps, 0, sizeof(ps));
        if (pPowerEx(&ps, TRUE)) {
            if (ps.BatteryLifePercent <= 100) g_batt = ps.BatteryLifePercent;
            g_charging = ps.ACLineStatus == 1;
        }
    }
#ifdef PREVIEW_W
    g_batt = 76;
#endif
    g_net = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\State\\Connections\\Network", 0, 0, &k) == ERROR_SUCCESS) {
        DWORD v = 0, t, sz = sizeof(v);
        if (RegQueryValueExW(k, L"Count", NULL, &t, (BYTE *)&v, &sz) == ERROR_SUCCESS) g_net = v > 0;
        RegCloseKey(k);
    }
#ifdef PREVIEW_W
    g_net = 1;
#endif
}

static void read_volume(void)
{
    DWORD v = 0;
    if (waveOutGetVolume((HWAVEOUT)0, &v) == MMSYSERR_NOERROR) g_vol = ((v & 0xFFFF) * 10 + 0x7FFF) / 0xFFFF;
    else g_vol = 5;
}

static void set_volume(int lv)
{
    DWORD v, one;
    HKEY k;
    if (lv < 0) lv = 0;
    if (lv > 10) lv = 10;
    g_vol = lv;
    one = (DWORD)lv * 0xFFFF / 10;
    v = one | (one << 16);
    waveOutSetVolume((HWAVEOUT)0, v);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"ControlPanel\\Volume", 0, 0, &k) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"Volume", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
        RegCloseKey(k);
    }
    /* 확인음 */
    MessageBeep(MB_OK);
    redraw();
}

static void screen_off(void)
{
    keybd_event(VK_OFF, 0, KEYEVENTF_SILENT, 0);
    keybd_event(VK_OFF, 0, KEYEVENTF_SILENT | KEYEVENTF_KEYUP, 0);
}

/* ------------------------------------------------------------------ */
/* 그리기 도우미                                                         */
/* ------------------------------------------------------------------ */
static HDC g_dc;
static HBITMAP g_wall;
static int g_shadow;

static void hit_add(int x, int y, int w, int h, int id, int arg)
{
    if (g_hitN >= MAX_HIT) return;
    g_hit[g_hitN].r.left = x; g_hit[g_hitN].r.top = y;
    g_hit[g_hitN].r.right = x + w; g_hit[g_hitN].r.bottom = y + h;
    g_hit[g_hitN].id = id; g_hit[g_hitN].arg = arg;
    g_hitN++;
}
static int pressed(int id, int arg)
{
    return t_down && !t_moved && t_hit >= 0 && g_hit[t_hit].id == id && g_hit[t_hit].arg == arg;
}

static void fill(int x, int y, int w, int h, COLORREF c)
{
    RECT r;
    HBRUSH b = CreateSolidBrush(c);
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    FillRect(g_dc, &r, b);
    DeleteObject(b);
}
static void rrect(int x, int y, int w, int h, int rad, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c), ob;
    HPEN op = (HPEN)SelectObject(g_dc, GetStockObject(NULL_PEN));
    ob = (HBRUSH)SelectObject(g_dc, b);
    RoundRect(g_dc, x, y, x + w + 1, y + h + 1, rad * 2, rad * 2);
    SelectObject(g_dc, ob);
    SelectObject(g_dc, op);
    DeleteObject(b);
}
static void frame(int x, int y, int w, int h, int rad, int th, COLORREF c)
{
    HPEN p = CreatePen(PS_SOLID, th, c), op;
    HBRUSH ob = (HBRUSH)SelectObject(g_dc, GetStockObject(NULL_BRUSH));
    op = (HPEN)SelectObject(g_dc, p);
    RoundRect(g_dc, x, y, x + w, y + h, rad * 2, rad * 2);
    SelectObject(g_dc, op);
    SelectObject(g_dc, ob);
    DeleteObject(p);
}
static void circle(int cx, int cy, int r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c), ob;
    HPEN op = (HPEN)SelectObject(g_dc, GetStockObject(NULL_PEN));
    ob = (HBRUSH)SelectObject(g_dc, b);
    Ellipse(g_dc, cx - r, cy - r, cx + r + 1, cy + r + 1);
    SelectObject(g_dc, ob);
    SelectObject(g_dc, op);
    DeleteObject(b);
}
static void poly(const POINT *p, int n, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c), ob;
    HPEN op = (HPEN)SelectObject(g_dc, GetStockObject(NULL_PEN));
    ob = (HBRUSH)SelectObject(g_dc, b);
    Polygon(g_dc, p, n);
    SelectObject(g_dc, ob);
    SelectObject(g_dc, op);
    DeleteObject(b);
}

/* 글자: 넘치면 … 로 줄임. align 0 왼쪽 1 가운데 2 오른쪽 */
static int text(HFONT f, COLORREF c, int x, int y, int w, int h, const wchar_t *s, int align)
{
    wchar_t buf[160];
    SIZE sz;
    int n = (int)wcslen(s);
    RECT r;
    UINT fl = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX;
    SelectObject(g_dc, f);
    SetTextColor(g_dc, c);
    if (n > 150) n = 150;
    wcsncpy(buf, s, n); buf[n] = 0;
    GetTextExtentPoint32W(g_dc, buf, n, &sz);
    if (sz.cx > w) {
        while (n > 0) {
            n--;
            buf[n] = 0x2026; buf[n + 1] = 0;
            GetTextExtentPoint32W(g_dc, buf, n + 1, &sz);
            if (sz.cx <= w) break;
        }
    }
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    fl |= align == 1 ? DT_CENTER : align == 2 ? DT_RIGHT : DT_LEFT;
    if (g_shadow) {
        RECT r2 = r;
        OffsetRect(&r2, 1, 1);
        SetTextColor(g_dc, RGB(0, 0, 0));
        DrawTextW(g_dc, buf, -1, &r2, fl);
        SetTextColor(g_dc, c);
    }
    DrawTextW(g_dc, buf, -1, &r, fl);
    return sz.cx;
}

/* 앱 그림: 아이콘이 있으면 색 사각형 위에, 없으면 첫 글자 */
static void draw_app_badge(App *a, int x, int y, int sz)
{
    HICON ic = app_icon(a);
    COLORREF c = PAL[hash_str(a->name) % (sizeof(PAL) / sizeof(PAL[0]))];
    rrect(x, y, sz, sz, sz / 4, c);
    if (ic) {
        int is = sz >= S(40) ? S(32) : sz - S(6);
        if (is > S(32)) is = S(32);
        DrawIconEx(g_dc, x + (sz - is) / 2, y + (sz - is) / 2, ic, is, is, 0, NULL, DI_NORMAL);
    } else {
        wchar_t l[2];
        l[0] = a->name[0]; l[1] = 0;
        text(sz >= S(44) ? fLetterB : fLetter, C_TEXT, x, y, sz, sz, l, 1);
    }
}

/* 간단한 그림 글자들 */
static void glyph_grid(int cx, int cy, COLORREF c)
{
    int s = S(5), g = S(2);
    fill(cx - s - g / 2 - 0, cy - s - g / 2, s, s, c); fill(cx + g / 2, cy - s - g / 2, s, s, c);
    fill(cx - s - g / 2, cy + g / 2, s, s, c);      fill(cx + g / 2, cy + g / 2, s, s, c);
}
static void glyph_windows(int cx, int cy, COLORREF c, COLORREF bg)
{
    fill(cx - S(7), cy - S(6), S(10), S(8), c);
    fill(cx - S(3), cy - S(2), S(10), S(8), bg);
    fill(cx - S(2), cy - S(1), S(8), S(6), c);
}
static void glyph_sliders(int cx, int cy, COLORREF c)
{
    int i;
    for (i = -1; i <= 1; i++) {
        fill(cx - S(7), cy + i * S(5) - S(1) / 2, S(14), S(1) > 0 ? S(1) + 1 : 2, c);
        circle(cx + (i == 0 ? S(3) : -S(3)), cy + i * S(5), S(2), c);
    }
}
static void glyph_back(int cx, int cy, COLORREF c)
{
    POINT p[3];
    p[0].x = cx + S(3); p[0].y = cy - S(6);
    p[1].x = cx - S(4); p[1].y = cy;
    p[2].x = cx + S(3); p[2].y = cy + S(6);
    poly(p, 3, c);
}
static void glyph_x(int cx, int cy, COLORREF c)
{
    HPEN p = CreatePen(PS_SOLID, S(2), c), op = (HPEN)SelectObject(g_dc, p);
    MoveToEx(g_dc, cx - S(4), cy - S(4), NULL); LineTo(g_dc, cx + S(5), cy + S(5));
    MoveToEx(g_dc, cx + S(4), cy - S(4), NULL); LineTo(g_dc, cx - S(5), cy + S(5));
    SelectObject(g_dc, op);
    DeleteObject(p);
}
static void glyph_plus(int cx, int cy, int s, COLORREF c)
{
    fill(cx - s, cy - S(1), s * 2 + 1, S(2) + 1, c);
    fill(cx - S(1), cy - s, S(2) + 1, s * 2 + 1, c);
}
static void glyph_check(int cx, int cy, COLORREF c)
{
    HPEN p = CreatePen(PS_SOLID, S(2) + 1, c), op = (HPEN)SelectObject(g_dc, p);
    MoveToEx(g_dc, cx - S(6), cy, NULL); LineTo(g_dc, cx - S(2), cy + S(4)); LineTo(g_dc, cx + S(6), cy - S(5));
    SelectObject(g_dc, op);
    DeleteObject(p);
}
static void glyph_chev(int cx, int cy, COLORREF c)
{
    HPEN p = CreatePen(PS_SOLID, S(2), c), op = (HPEN)SelectObject(g_dc, p);
    MoveToEx(g_dc, cx - S(3), cy - S(5), NULL); LineTo(g_dc, cx + S(2), cy); LineTo(g_dc, cx - S(3), cy + S(5));
    SelectObject(g_dc, op);
    DeleteObject(p);
}
static void glyph_speaker(int cx, int cy, COLORREF c)
{
    POINT p[4];
    fill(cx - S(6), cy - S(3), S(4), S(6), c);
    p[0].x = cx - S(3); p[0].y = cy - S(3);
    p[1].x = cx + S(3); p[1].y = cy - S(7);
    p[2].x = cx + S(3); p[2].y = cy + S(7);
    p[3].x = cx - S(3); p[3].y = cy + S(3);
    poly(p, 4, c);
}

/* ------------------------------------------------------------------ */
/* 화면 그리기                                                           */
/* ------------------------------------------------------------------ */
#define NAV_H  S(44)
#define HEAD_H S(50)

static void grid_dims(int *cols, int *rows)
{
    static const int gc[3] = { 3, 4, 3 }, gr[3] = { 3, 3, 2 };
    *cols = gc[g_set.grid % 3];
    *rows = gr[g_set.grid % 3];
}
static int home_pages(void)
{
    int per = per_page(), mx = first_free(0), i, n;
    for (i = 0; i < g_favN; i++) if (g_fav[i].pos > mx) mx = g_fav[i].pos;
    n = mx / per + 1;
    if (g_dragging && g_page >= n - 1) n = g_page + 2;   /* 끌고 있을 때는 빈 페이지 하나 더 */
    return n;
}

static void paint_header(void)
{
    SYSTEMTIME st;
    wchar_t clk[16], ap[8], d1[24], d2[16], bt[16];
    static const wchar_t *wd[7] = { L"일요일", L"월요일", L"화요일", L"수요일", L"목요일", L"금요일", L"토요일" };
    int cw, x, bx, by, bw, bh, lv;
    COLORREF bc;
    GetLocalTime(&st);
    ap[0] = 0;
    g_shadow = g_wall != NULL;
    if (g_set.clock24) _snwprintf(clk, 16, L"%02d:%02d", st.wHour, st.wMinute);
    else {
        int h12 = st.wHour % 12;
        if (h12 == 0) h12 = 12;
        _snwprintf(clk, 16, L"%d:%02d", h12, st.wMinute);
        wcscpy(ap, st.wHour < 12 ? L"오전" : L"오후");
    }
    _snwprintf(d1, 24, L"%d월 %d일", st.wMonth, st.wDay);
    wcscpy(d2, wd[st.wDayOfWeek % 7]);
    x = S(12);
    if (ap[0]) { text(fBold, C_SUB, x, S(8), S(30), HEAD_H - S(16), ap, 0); x += S(30); }
    cw = text(fClock, C_TEXT, x, S(4), S(120), HEAD_H - S(8), clk, 0);
    x += cw + S(10);
    text(fBold, C_TEXT, x, S(10), S(90), S(18), d1, 0);
    text(fSmall, C_SUB, x, S(28), S(90), S(16), d2, 0);

    /* 배터리 */
    bw = S(26); bh = S(13);
    bx = W - S(12) - bw - S(3);
    by = S(12);
    lv = g_batt < 0 ? 0 : g_batt;
    bc = g_charging ? C_OK : lv <= 15 ? C_BAD : lv <= 30 ? C_WARN : C_TEXT;
    frame(bx, by, bw, bh, S(2), S(1) > 1 ? S(1) : 1, C_SUB);
    fill(bx + bw, by + S(4), S(3), bh - S(8), C_SUB);
    if (g_batt >= 0) fill(bx + S(2), by + S(2), (bw - S(4)) * lv / 100, bh - S(4), bc);
    if (g_batt >= 0) _snwprintf(bt, 16, g_charging ? L"충전 %d%%" : L"%d%%", g_batt);
    else wcscpy(bt, L"--");
    text(fBold, bc, bx - S(80), by - S(2), S(74), bh + S(4), bt, 2);

    /* 인터넷 */
    circle(W - S(18), S(37), S(4), g_net ? C_OK : C_DIM);
    text(fSmall, g_net ? C_SUB : C_DIM, W - S(130), S(29), S(104), S(16), g_net ? L"인터넷 연결됨" : L"인터넷 안 됨", 2);
    g_shadow = 0;
    if (!g_wall) fill(0, HEAD_H - 1, W, 1, C_SEP);
}

static void paint_nav(void)
{
    int y = H - NAV_H, bw = W / 3, i;
    static const wchar_t *lab[3] = { L"전체 앱", L"실행 중", L"설정" };
    fill(0, y, W, NAV_H, C_BG);
    fill(0, y, W, 1, C_SEP);
    for (i = 0; i < 3; i++) {
        int x = i * bw, w = (i == 2) ? W - x : bw;
        int id = H_NAV_ALL + i;
        int cx = x + S(22), cy = y + NAV_H / 2;
        int sel = (g_scr == SCR_ALL && i == 0) || (g_scr == SCR_RUN && i == 1) || ((g_scr == SCR_SET || g_scr == SCR_WALL) && i == 2);
        COLORREF fg = sel ? C_ACCENT : C_TEXT;
        if (pressed(id, 0)) rrect(x + S(3), y + S(4), w - S(6), NAV_H - S(8), S(8), C_PRESS);
        else if (sel) rrect(x + S(3), y + S(4), w - S(6), NAV_H - S(8), S(8), C_CARD);
        if (i == 0) glyph_grid(cx, cy, fg);
        else if (i == 1) glyph_windows(cx, cy, fg, pressed(id, 0) ? C_PRESS : sel ? C_CARD : C_BG);
        else glyph_sliders(cx, cy, fg);
        text(fBold, fg, x + S(34), y, w - S(38), NAV_H, lab[i], 0);
        if (i == 1 && g_runN > 0) {
            wchar_t n[8];
            _snwprintf(n, 8, L"%d", g_runN);
            circle(x + w - S(16), y + S(14), S(8), C_ACCENT);
            text(fSmall, C_TEXT, x + w - S(24), y + S(6), S(16), S(16), n, 1);
        }
        hit_add(x, y, w, NAV_H, id, 0);
    }
}


static void tile_label(int x, int y, int w, const wchar_t *name, COLORREF c)
{
    text(g_set.grid == 1 ? fSmall : fBold, c, x, y, w, S(16), name, 1);
}

static void paint_home(void)
{
    int cols, rows, per, top = HEAD_H + S(2), bottom = H - NAV_H - S(12);
    int gw, gh, pg, i, pages, badge, freeSlot;
    grid_dims(&cols, &rows);
    per = cols * rows;
    pages = home_pages();
    if (g_page >= pages) g_page = pages - 1;
    if (g_page < 0) g_page = 0;
    gw = (W - S(8)) / cols;
    gh = (bottom - top) / rows;
    g_gTop = top; g_gW = gw; g_gH = gh; g_gCols = cols; g_gRows = rows;
    badge = gh - S(22);
    if (badge > gw - S(24)) badge = gw - S(24);
    if (badge > S(58)) badge = S(58);
    if (badge < S(24)) badge = S(24);
    freeSlot = first_free(0);
    g_shadow = g_wall != NULL;
    for (pg = g_page - 1; pg <= g_page + 1; pg++) {
        int ox;
        if (pg < 0 || pg >= pages) continue;
        ox = (pg - g_page) * W + g_swipeX;
        if (ox <= -W || ox >= W) continue;
        for (i = 0; i < per; i++) {
            int pos = pg * per + i, idx = fav_at(pos);
            int x = ox + S(4) + (i % cols) * gw, y = top + (i / cols) * gh;
            int bx = x + (gw - badge) / 2, by = y + (gh - badge - S(16)) / 2;
            if (g_dragging && pos == g_hover)
                frame(x + S(3), y + S(3), gw - S(6), gh - S(6), S(10), S(2), C_ACCENT);
            if (g_keyMode && g_focus == pos && g_scr == SCR_HOME && !g_menu.on && g_lift < 0)
                frame(x + S(2), y + S(2), gw - S(4), gh - S(4), S(10), S(2), C_FOCUS);
            if (idx >= 0 && !(g_dragging && idx == g_lift)) {
                if (pressed(H_TILE, idx) || (idx == g_lift && !g_dragging))
                    rrect(x + S(2), y + S(2), gw - S(4), gh - S(4), S(10), C_PRESS);
                draw_app_badge(&g_fav[idx], bx, by, badge);
                tile_label(x + S(2), by + badge + S(1), gw - S(4), g_fav[idx].name, C_TEXT);
                if (pg == g_page) hit_add(x, y, gw, gh, H_TILE, idx);
            } else if (idx < 0) {
                if (pressed(H_EMPTY, pos)) rrect(x + S(2), y + S(2), gw - S(4), gh - S(4), S(10), C_PRESS);
                if (pos == freeSlot && !g_dragging) {
                    frame(bx, by, badge, badge, badge / 4, S(2), C_DIM);
                    glyph_plus(bx + badge / 2, by + badge / 2, badge / 5, C_SUB);
                    tile_label(x + S(2), by + badge + S(1), gw - S(4), L"앱 추가", C_SUB);
                }
                if (pg == g_page) hit_add(x, y, gw, gh, H_EMPTY, pos);
            }
        }
    }
    /* 페이지 점 */
    if (pages > 1) {
        int dy = H - NAV_H - S(6), dx = W / 2 - (pages - 1) * S(8);
        for (i = 0; i < pages; i++) {
            circle(dx + i * S(16), dy, i == g_page ? S(4) : S(3), i == g_page ? C_TEXT : C_DIM);
            if (!g_dragging) hit_add(dx + i * S(16) - S(8), dy - S(8), S(16), S(16), H_DOT, i);
        }
    }
    /* 끌고 있는 아이콘은 손가락 위에 */
    if (g_dragging && g_lift >= 0 && g_lift < g_favN) {
        int b2 = badge + S(8), bx = g_dx - b2 / 2, by = g_dy - b2 / 2 - S(14);
        draw_app_badge(&g_fav[g_lift], bx, by, b2);
        tile_label(bx - S(20), by + b2 + S(1), b2 + S(40), g_fav[g_lift].name, C_TEXT);
        if (g_dx < S(18) || g_dx > W - S(18)) {
            /* 화면 끝: 페이지 넘김 표시 */
            fill(g_dx < S(18) ? 0 : W - S(4), top, S(4), bottom - top, C_ACCENT);
        }
    }
    g_shadow = 0;
}

/* 목록 화면 윗부분: 뒤로 + 제목 */
static void paint_title(const wchar_t *title, const wchar_t *right)
{
    fill(0, 0, W, S(44), C_BG);
    if (pressed(H_BACK, 0)) rrect(S(4), S(4), S(40), S(36), S(8), C_PRESS);
    glyph_back(S(23), S(22), C_TEXT);
    hit_add(0, 0, S(50), S(44), H_BACK, 0);
    text(fHead, C_TEXT, S(50), 0, W - S(60), S(44), title, 0);
    if (right && right[0]) {
        int w = S(84);
        if (pressed(H_DONE, 0)) rrect(W - w - S(6), S(6), w, S(32), S(8), C_PRESS);
        else rrect(W - w - S(6), S(6), w, S(32), S(8), C_ACCENT);
        text(fBold, C_TEXT, W - w - S(6), S(6), w, S(32), right, 1);
        hit_add(W - w - S(10), 0, w + S(10), S(44), H_DONE, 0);
    }
    fill(0, S(44) - 1, W, 1, C_SEP);
}

#define LIST_TOP S(44)
#define ROW_H    S(46)

static void clamp_scroll(int scr)
{
    int vis = H - NAV_H - LIST_TOP, mx = g_contentH[scr] - vis;
    if (mx < 0) mx = 0;
    if (g_scroll[scr] > mx) g_scroll[scr] = mx;
    if (g_scroll[scr] < 0) g_scroll[scr] = 0;
}

static void list_clip(int on)
{
    if (on) {
        HRGN r = CreateRectRgn(0, LIST_TOP, W, H - NAV_H);
        SelectClipRgn(g_dc, r);
        DeleteObject(r);
    } else SelectClipRgn(g_dc, NULL);
}

static int row_visible(int y, int h) { return y + h > LIST_TOP && y < H - NAV_H; }

static void hit_row(int x, int y, int w, int h, int id, int arg)
{
    /* 목록 칸 안에 보이는 부분만 */
    int t = y < LIST_TOP ? LIST_TOP : y, b = y + h > H - NAV_H ? H - NAV_H : y + h;
    if (b > t) hit_add(x, t, w, b - t, id, arg);
}

static void paint_all_apps(void)
{
    int i, y0 = LIST_TOP - g_scroll[SCR_ALL];
    wchar_t t[48];
    if (g_addMode) wcscpy(t, L"홈에 넣을 앱 고르기");
    else _snwprintf(t, 48, L"전체 앱 (%d)", g_allN);
    g_contentH[SCR_ALL] = g_allN * ROW_H + S(8);
    list_clip(1);
    for (i = 0; i < g_allN; i++) {
        int y = y0 + i * ROW_H, inFav = fav_find(g_all[i].key) >= 0;
        if (!row_visible(y, ROW_H)) continue;
        if (pressed(H_ROW, i)) fill(0, y, W, ROW_H, C_PRESS);
        if (g_keyMode && g_focus == i && !g_menu.on) frame(S(2), y + S(1), W - S(4), ROW_H - S(2), S(6), S(2), C_FOCUS);
        draw_app_badge(&g_all[i], S(10), y + (ROW_H - S(36)) / 2, S(36));
        text(fBold, C_TEXT, S(56), y, W - S(56) - S(70), ROW_H, g_all[i].name, 0);
        if (g_addMode) {
            int bx = W - S(44), by = y + (ROW_H - S(28)) / 2;
            if (inFav) { circle(bx + S(14), by + S(14), S(13), C_ACCENT); glyph_check(bx + S(14), by + S(14), C_TEXT); }
            else { frame(bx, by, S(28), S(28), S(14), S(2), C_DIM); glyph_plus(bx + S(14), by + S(14), S(6), C_SUB); }
        } else if (inFav) {
            rrect(W - S(52), y + (ROW_H - S(20)) / 2, S(40), S(20), S(10), C_CARD);
            text(fSmall, C_SUB, W - S(52), y + (ROW_H - S(20)) / 2, S(40), S(20), L"홈", 1);
        }
        fill(S(56), y + ROW_H - 1, W - S(56), 1, C_SEP);
        hit_row(0, y, W, ROW_H, H_ROW, i);
    }
    if (g_allN == 0) text(fNorm, C_SUB, 0, LIST_TOP + S(40), W, S(30), L"시작 메뉴에 프로그램이 없어요", 1);
    list_clip(0);
    paint_title(t, g_addMode ? L"다 했음" : NULL);
    if (!g_addMode)
        text(fSmall, C_DIM, S(150), 0, W - S(160), S(44), L"꾹 누르면 홈에 추가", 2);
}

static void paint_running(void)
{
    int i, y0 = LIST_TOP - g_scroll[SCR_RUN], y;
    MEMORYSTATUS ms;
    wchar_t mem[64];
    memset(&ms, 0, sizeof(ms));
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatus(&ms);
    _snwprintf(mem, 64, L"남은 메모리 %luMB / %luMB", (unsigned long)(ms.dwAvailPhys >> 20), (unsigned long)(ms.dwTotalPhys >> 20));
    list_clip(1);
    y = y0 + S(6);
    text(fNorm, C_SUB, S(14), y, W - S(28), S(24), mem, 0);
    {
        int used = ms.dwTotalPhys ? (int)((ms.dwTotalPhys - ms.dwAvailPhys) / (ms.dwTotalPhys / 100 + 1)) : 0;
        if (used > 100) used = 100;
        rrect(S(14), y + S(26), W - S(28), S(6), S(3), C_CARD);
        rrect(S(14), y + S(26), (W - S(28)) * used / 100, S(6), S(3), used > 85 ? C_BAD : used > 70 ? C_WARN : C_OK);
    }
    y += S(42);
    for (i = 0; i < g_runN; i++, y += ROW_H) {
        if (!row_visible(y, ROW_H)) continue;
        if (pressed(H_ROW, i)) fill(0, y, W, ROW_H, C_PRESS);
        if (g_keyMode && g_focus == i && !g_menu.on) frame(S(2), y + S(1), W - S(4), ROW_H - S(2), S(6), S(2), C_FOCUS);
        {
            App tmp;
            memset(&tmp, 0, sizeof(tmp));
            wcscpy(tmp.name, g_run[i].title);
            tmp.iconTried = 1;
            draw_app_badge(&tmp, S(10), y + (ROW_H - S(34)) / 2, S(34));
        }
        text(fBold, C_TEXT, S(54), y, W - S(54) - S(54), ROW_H, g_run[i].title, 0);
        if (pressed(H_CLOSE, i)) circle(W - S(28), y + ROW_H / 2, S(16), C_PRESS);
        else circle(W - S(28), y + ROW_H / 2, S(16), C_CARD);
        glyph_x(W - S(28), y + ROW_H / 2, C_TEXT);
        fill(S(54), y + ROW_H - 1, W - S(54), 1, C_SEP);
        hit_row(0, y, W - S(52), ROW_H, H_ROW, i);
        hit_row(W - S(52), y, S(52), ROW_H, H_CLOSE, i);
    }
    if (g_runN == 0) { text(fNorm, C_SUB, 0, y + S(10), W, S(30), L"켜져 있는 앱이 없어요", 1); y += S(50); }
    else {
        y += S(10);
        if (row_visible(y, S(42))) {
            rrect(S(14), y, W - S(28), S(42), S(10), pressed(H_CLOSEALL, 0) ? C_PRESS : C_ACCENT);
            text(fBold, C_TEXT, S(14), y, W - S(28), S(42), L"모두 닫기", 1);
            hit_row(S(14), y, W - S(28), S(42), H_CLOSEALL, 0);
        }
        y += S(52);
    }
    g_contentH[SCR_RUN] = y - y0;
    list_clip(0);
    paint_title(L"실행 중인 앱", NULL);
}

/* ------------------------------------------------------------------ */
/* 배경화면                                                             */
/* ------------------------------------------------------------------ */
static const struct { const wchar_t *name; COLORREF a, b; } PRESET[] = {
    { L"없음", RGB(12, 12, 14), RGB(12, 12, 14) },
    { L"밤하늘", RGB(22, 34, 84), RGB(8, 8, 24) },
    { L"노을", RGB(236, 112, 72), RGB(52, 24, 70) },
    { L"숲", RGB(40, 110, 70), RGB(6, 24, 18) },
    { L"바다", RGB(30, 130, 190), RGB(6, 24, 60) },
    { L"보라", RGB(140, 70, 200), RGB(26, 10, 56) },
    { L"분홍", RGB(230, 110, 150), RGB(70, 24, 60) },
    { L"모래", RGB(200, 160, 110), RGB(60, 44, 30) },
    { L"회색", RGB(80, 84, 94), RGB(16, 16, 20) },
};
#define N_PRESET ((int)(sizeof(PRESET) / sizeof(PRESET[0])))
static int g_wallW, g_wallH;

/* 사진 목록 */
#define MAX_PIC 300
static wchar_t (*g_pic)[MAX_PATH];
static int g_picN;

static DWORD *new_dib(int w, int h, HBITMAP *out)
{
    BITMAPINFO bi;
    void *bits = NULL;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h;          /* 아래에서 위로 쌓이는 보통 DIB */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    *out = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    return *out ? (DWORD *)bits : NULL;
}
#define DIBROW(bits, w, h, y) ((bits) + (size_t)((h) - 1 - (y)) * (w))

/* 사진을 화면 크기로 (가운데 맞춰 꽉 채움, 상자 평균으로 줄임) */
static int load_photo(DWORD *bits, int w, int h, const wchar_t *path, int *err)
{
    HANDLE f;
    DWORD sz, rd = 0;
    unsigned char *buf, *px;
    int iw, ih, comp, sx0, sy0, sw, sh, x, y;
    *err = 1;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    sz = GetFileSize(f, NULL);
    if (sz == 0xFFFFFFFF || sz < 16 || sz > 12 * 1024 * 1024) { CloseHandle(f); return 0; }
    buf = (unsigned char *)malloc(sz);
    if (!buf) { CloseHandle(f); *err = 3; return 0; }
    ReadFile(f, buf, sz, &rd, NULL);
    CloseHandle(f);
    if (!stbi_info_from_memory(buf, (int)rd, &iw, &ih, &comp)) { free(buf); return 0; }
    if ((double)iw * ih > 4200000.0) { free(buf); *err = 2; return 0; }   /* 420만 화소까지 */
    px = stbi_load_from_memory(buf, (int)rd, &iw, &ih, &comp, 3);
    free(buf);
    if (!px) { *err = 3; return 0; }
    if ((double)iw * h > (double)ih * w) { sh = ih; sw = (int)((double)ih * w / h); }
    else { sw = iw; sh = (int)((double)iw * h / w); }
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;
    sx0 = (iw - sw) / 2; sy0 = (ih - sh) / 2;
    for (y = 0; y < h; y++) {
        DWORD *row = DIBROW(bits, w, h, y);
        int y0 = sy0 + (int)((double)y * sh / h), y1 = sy0 + (int)((double)(y + 1) * sh / h), ys;
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > ih) y1 = ih;
        ys = (y1 - y0) / 4 + 1;
        for (x = 0; x < w; x++) {
            int x0 = sx0 + (int)((double)x * sw / w), x1 = sx0 + (int)((double)(x + 1) * sw / w), xs, xx, yy;
            unsigned r = 0, g = 0, b = 0, n = 0;
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > iw) x1 = iw;
            xs = (x1 - x0) / 4 + 1;
            for (yy = y0; yy < y1; yy += ys)
                for (xx = x0; xx < x1; xx += xs) {
                    const unsigned char *p = px + ((size_t)yy * iw + xx) * 3;
                    r += p[0]; g += p[1]; b += p[2]; n++;
                }
            if (!n) n = 1;
            row[x] = ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    }
    stbi_image_free(px);
    *err = 0;
    return 1;
}

static void build_wall(void)
{
    DWORD *bits;
    int x, y, f;
    static const int dimF[3] = { 256, 176, 112 };
    if (g_wall) { DeleteObject(g_wall); g_wall = NULL; }
    g_wallW = W; g_wallH = H;
    if (W <= 0 || H <= 0 || g_set.wallType == 0) return;
    if (g_set.wallType != WALL_PHOTO && (g_set.wallType < 0 || g_set.wallType >= N_PRESET)) return;
    bits = new_dib(W, H, &g_wall);
    if (!bits) { g_wall = NULL; return; }
    if (g_set.wallType == WALL_PHOTO) {
        int err;
        if (!load_photo(bits, W, H, g_set.wallPath, &err)) {
            DeleteObject(g_wall); g_wall = NULL;
            toast(err == 2 ? L"사진이 너무 커요 (420만 화소 이하로 줄여 주세요)" :
                  err == 3 ? L"메모리가 모자라 사진을 못 열었어요" : L"사진을 열 수 없어요");
            return;
        }
    } else {
        COLORREF a = PRESET[g_set.wallType].a, b = PRESET[g_set.wallType].b;
        for (y = 0; y < H; y++) {
            DWORD *row = DIBROW(bits, W, H, y);
            for (x = 0; x < W; x++) {
                /* 위→아래 + 살짝 대각선 그라데이션 */
                int t = (y * 220 / H) + (x * 36 / W);
                int r = GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t / 256;
                int g = GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t / 256;
                int bl = GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t / 256;
                row[x] = (r << 16) | (g << 8) | bl;
            }
        }
    }
    f = dimF[g_set.wallDim % 3];
    if (f != 256) {
        for (y = 0; y < W * H; y++) {
            DWORD v = bits[y];
            bits[y] = ((((v >> 16) & 255) * f >> 8) << 16) | ((((v >> 8) & 255) * f >> 8) << 8) | (((v & 255) * f) >> 8);
        }
    }
}

static int is_pic(const wchar_t *n)
{
    return ends_with(n, L".jpg") || ends_with(n, L".jpeg") || ends_with(n, L".png") || ends_with(n, L".bmp");
}

static void scan_pics_dir(const wchar_t *dir, int depth)
{
    wchar_t pat[MAX_PATH], full[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    _snwprintf(pat, MAX_PATH, L"%s\\*", dir);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (g_picN >= MAX_PIC) break;
        if (fd.cFileName[0] == L'.') continue;
        _snwprintf(full, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            /* 프로그램·에뮬 폴더는 건너뜀 (게임 그림이 많음) */
            if (depth < 3 && _wcsicmp(fd.cFileName, L"Program Files") && _wcsnicmp(fd.cFileName, L"M480", 4) &&
                _wcsicmp(fd.cFileName, L"AbyssLabyrinth") && _wcsicmp(fd.cFileName, L"Windows"))
                scan_pics_dir(full, depth + 1);
        } else if (is_pic(fd.cFileName) && fd.nFileSizeLow > 1024) {
            wcscpy(g_pic[g_picN++], full);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static int cmp_pic(const void *a, const void *b)
{
    return _wcsicmp(base_name((const wchar_t *)a), base_name((const wchar_t *)b));
}

static void scan_pics(void)
{
    static const wchar_t *roots[] = { L"\\My Documents", L"\\Storage Card", L"\\SD Card", L"\\Storage Card2", L"\\SDMMC", NULL };
    int i;
    if (!g_pic) g_pic = (wchar_t (*)[MAX_PATH])malloc(sizeof(*g_pic) * MAX_PIC);
    g_picN = 0;
    if (!g_pic) return;
    for (i = 0; roots[i]; i++) if (dir_exists(roots[i])) scan_pics_dir(roots[i], 0);
#ifdef PREVIEW_W
    if (g_picN == 0) {
        wcscpy(g_pic[0], L"\\My Documents\\My Pictures\\바다.jpg");
        wcscpy(g_pic[1], L"\\Storage Card\\DCIM\\100SSCAM\\SAM_0012.JPG");
        g_picN = 2;
    }
#endif
    qsort(g_pic, g_picN, sizeof(*g_pic), cmp_pic);
}

static void set_wall(int type, const wchar_t *path)
{
    int oldT = g_set.wallType;
    wchar_t oldP[MAX_PATH];
    wcscpy(oldP, g_set.wallPath);
    g_set.wallType = type;
    if (path) wcsncpy(g_set.wallPath, path, MAX_PATH - 1);
    if (type == WALL_PHOTO) {
        toast(L"사진을 불러오는 중…");
        InvalidateRect(g_hwnd, NULL, FALSE);
        UpdateWindow(g_hwnd);
    }
    build_wall();
    if (type == WALL_PHOTO && !g_wall) {          /* 실패: 원래대로 */
        g_set.wallType = oldT;
        wcscpy(g_set.wallPath, oldP);
        {
            wchar_t keep[80];
            wcscpy(keep, g_toast);
            build_wall();
            toast(keep);
        }
    } else if (type == WALL_PHOTO) toast(L"배경화면을 바꿨어요");
    save_settings();
    redraw();
}

static void paint_wallpick(void)
{
    int i, y0 = LIST_TOP - g_scroll[SCR_WALL], y = y0, sw, sh = S(52);
    static const wchar_t *dimName[3] = { L"그대로", L"조금 어둡게", L"많이 어둡게" };
    wchar_t t[48];
    list_clip(1);
    /* 어둡게 */
    if (row_visible(y, ROW_H)) {
        if (pressed(H_WDIM, 0)) fill(0, y, W, ROW_H, C_PRESS);
        text(fBold, C_TEXT, S(14), y, W, ROW_H, L"글자 잘 보이게", 0);
        text(fNorm, C_SUB, W - S(150), y, S(120), ROW_H, dimName[g_set.wallDim % 3], 2);
        glyph_chev(W - S(18), y + ROW_H / 2, C_DIM);
        hit_row(0, y, W, ROW_H, H_WDIM, 0);
    }
    y += ROW_H;
    text(fSmall, C_ACCENT, S(14), y + S(4), W, S(18), L"기본 배경", 0);
    y += S(24);
    sw = (W - S(28) - S(16)) / 3;
    for (i = 0; i < N_PRESET; i++) {
        int x = S(14) + (i % 3) * (sw + S(8)), yy = y + (i / 3) * (sh + S(8));
        int sel = g_set.wallType == i;
        if (!row_visible(yy, sh)) continue;
        {
            /* 견본: 위·아래 색 두 칸 */
            int k;
            for (k = 0; k < 6; k++) {
                COLORREF a = PRESET[i].a, b = PRESET[i].b;
                COLORREF c = RGB(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * k / 5,
                                 GetGValue(a) + (GetGValue(b) - GetGValue(a)) * k / 5,
                                 GetBValue(a) + (GetBValue(b) - GetBValue(a)) * k / 5);
                fill(x, yy + k * sh / 6, sw, sh / 6 + 1, c);
            }
            if (i == 0) frame(x, yy, sw, sh, S(2), 1, C_DIM);
        }
        g_shadow = 1;
        text(fBold, C_TEXT, x, yy, sw, sh, PRESET[i].name, 1);
        g_shadow = 0;
        if (sel) frame(x - S(2), yy - S(2), sw + S(4), sh + S(4), S(4), S(3), C_ACCENT);
        else if (pressed(H_WPRESET, i)) frame(x - S(2), yy - S(2), sw + S(4), sh + S(4), S(4), S(3), C_TEXT);
        hit_row(x, yy, sw, sh, H_WPRESET, i);
    }
    y += ((N_PRESET + 2) / 3) * (sh + S(8));
    _snwprintf(t, 48, L"내 사진 (%d장)", g_picN);
    text(fSmall, C_ACCENT, S(14), y + S(4), W, S(18), t, 0);
    y += S(24);
    if (g_picN == 0) {
        text(fNorm, C_SUB, S(14), y, W - S(28), S(22), L"JPG·PNG 사진을 My Documents나", 0);
        text(fNorm, C_SUB, S(14), y + S(20), W - S(28), S(22), L"메모리카드에 넣으면 여기 나와요", 0);
        y += S(50);
    }
    for (i = 0; i < g_picN; i++, y += ROW_H) {
        int sel = g_set.wallType == WALL_PHOTO && _wcsicmp(g_set.wallPath, g_pic[i]) == 0;
        wchar_t folder[MAX_PATH], *sl;
        if (!row_visible(y, ROW_H)) continue;
        if (pressed(H_WPIC, i)) fill(0, y, W, ROW_H, C_PRESS);
        wcscpy(folder, g_pic[i]);
        sl = wcsrchr(folder, L'\\');
        if (sl) *sl = 0;
        text(fBold, C_TEXT, S(14), y + S(4), W - S(60), S(20), base_name(g_pic[i]), 0);
        text(fSmall, C_SUB, S(14), y + S(24), W - S(60), S(18), folder[0] ? folder : L"\\", 0);
        if (sel) { circle(W - S(28), y + ROW_H / 2, S(12), C_ACCENT); glyph_check(W - S(28), y + ROW_H / 2, C_TEXT); }
        fill(S(14), y + ROW_H - 1, W - S(28), 1, C_SEP);
        hit_row(0, y, W, ROW_H, H_WPIC, i);
    }
    y += S(16);
    g_contentH[SCR_WALL] = y - y0;
    list_clip(0);
    paint_title(L"배경화면", NULL);
}

/* 설정 항목 */
enum { O_VOL, O_OFF, O_WALL, O_GRID, O_TODAY, O_AUTO, O_CLOCK, O_RESET, O_RESCAN, O_QUIT, O_N };

static void paint_settings(void)
{
    int i, y = LIST_TOP - g_scroll[SCR_SET] + S(6);
    int y0 = LIST_TOP - g_scroll[SCR_SET];
    static const wchar_t *gridName[3] = { L"보통 (3×3)", L"작게 (4×3)", L"크게 (3×2)" };
    list_clip(1);
    for (i = 0; i < O_N; i++) {
        int h = (i == O_VOL) ? S(60) : ROW_H;
        if (i == O_WALL || i == O_RESET) {   /* 묶음 제목 */
            text(fSmall, C_ACCENT, S(14), y + S(4), W, S(18), i == O_WALL ? L"홈 화면" : L"관리", 0);
            y += S(24);
        }
        if (!row_visible(y, h)) { y += h; continue; }
        if (g_keyMode && g_focus == i && !g_menu.on) frame(S(2), y + S(1), W - S(4), h - S(2), S(6), S(2), C_FOCUS);
        if (i == O_VOL) {
            int bx = S(64), bw = W - S(64) - S(64), k;
            text(fBold, C_TEXT, S(14), y + S(2), W, S(20), L"소리 크기", 0);
            if (pressed(H_VOLDN, 0)) circle(S(34), y + S(38), S(17), C_PRESS); else circle(S(34), y + S(38), S(17), C_CARD);
            fill(S(34) - S(6), y + S(37), S(13), S(2) + 1, C_TEXT);
            if (pressed(H_VOLUP, 0)) circle(W - S(34), y + S(38), S(17), C_PRESS); else circle(W - S(34), y + S(38), S(17), C_CARD);
            glyph_plus(W - S(34), y + S(38), S(6), C_TEXT);
            for (k = 0; k < 10; k++) {
                int sw = bw / 10;
                rrect(bx + k * sw + S(1), y + S(31), sw - S(3), S(14), S(3), k < g_vol ? C_ACCENT : C_CARD);
            }
            hit_row(0, y + S(18), S(64), S(42), H_VOLDN, 0);
            hit_row(W - S(64), y + S(18), S(64), S(42), H_VOLUP, 0);
        } else {
            const wchar_t *lab = L"", *val = NULL;
            int sw = -1;
            switch (i) {
            case O_OFF:   lab = L"화면 끄기 (잠자기)"; break;
            case O_TODAY: lab = L"오늘 화면 대신 M480 홈"; sw = g_set.replaceToday; break;
            case O_AUTO:  lab = L"켤 때 자동으로 시작"; sw = autostart_on(); break;
            case O_GRID:  lab = L"아이콘 크기"; val = gridName[g_set.grid % 3]; break;
            case O_WALL:  lab = L"배경화면"; val = g_set.wallType == WALL_PHOTO ? base_name(g_set.wallPath) :
                                 (g_set.wallType >= 0 && g_set.wallType < N_PRESET) ? PRESET[g_set.wallType].name : L"없음"; break;
            case O_CLOCK: lab = L"시계"; val = g_set.clock24 ? L"24시간" : L"오전/오후"; break;
            case O_RESET: lab = L"홈 앱 처음 상태로"; break;
            case O_RESCAN:lab = L"앱 목록 다시 읽기"; break;
            case O_QUIT:  lab = L"M480 홈 끝내기"; break;
            }
            if (pressed(H_OPT, i)) fill(0, y, W, h, C_PRESS);
            if (i == O_OFF) circle(S(26), y + h / 2, S(5), C_SUB);
            text(fBold, i == O_QUIT ? C_BAD : C_TEXT, i == O_OFF ? S(40) : S(14), y, W - S(140), h, lab, 0);
            if (sw >= 0) {
                int tx = W - S(62), ty = y + (h - S(26)) / 2;
                rrect(tx, ty, S(48), S(26), S(13), sw ? C_ACCENT : C_DIM);
                circle(sw ? tx + S(35) : tx + S(13), ty + S(13), S(10), C_TEXT);
            } else if (val) {
                text(fNorm, C_SUB, W - S(140), y, S(110), h, val, 2);
                glyph_chev(W - S(18), y + h / 2, C_DIM);
            }
            fill(S(14), y + h - 1, W - S(28), 1, C_SEP);
            hit_row(0, y, W, h, H_OPT, i);
        }
        y += h;
    }
    text(fSmall, C_DIM, 0, y + S(6), W, S(20), L"M480 홈 " APP_VER, 1);
    y += S(32);
    g_contentH[SCR_SET] = y - y0;
    list_clip(0);
    paint_title(L"설정", NULL);
}

static void paint_menu(void)
{
    int i, w = W - S(40), rh = S(42), hh = g_menu.hint ? S(18) : 0, h = S(44) + hh + g_menu.n * rh + S(8);
    int x = S(20), y = (H - h) / 2;
    /* 뒤를 어둡게: 줄무늬로 흉내 (CE GDI에는 반투명이 없음) */
    for (i = 0; i < H; i += 2) fill(0, i, W, 1, RGB(0, 0, 0));
    hit_add(0, 0, W, H, H_MENU_BG, 0);
    rrect(x, y, w, h, S(12), RGB(40, 40, 46));
    text(fHead, C_TEXT, x + S(14), y, w - S(28), S(40), g_menu.title, 0);
    if (g_menu.hint) text(fSmall, C_SUB, x + S(14), y + S(34), w - S(28), S(18), g_menu.hint, 0);
    for (i = 0; i < g_menu.n; i++) {
        int ry = y + S(44) + hh + i * rh;
        int isCancel = g_menu.act[i] == M_CANCEL;
        if (pressed(H_MENU, i)) rrect(x + S(6), ry, w - S(12), rh - S(4), S(8), C_PRESS);
        else if (g_keyMode && g_focus == i) frame(x + S(6), ry, w - S(12), rh - S(4), S(8), S(2), C_FOCUS);
        text(fBold, isCancel ? C_SUB : (g_menu.act[i] == M_FAV_REMOVE || g_menu.act[i] == M_ALL_REMOVE || g_menu.act[i] == M_RUN_CLOSE) ? C_ACCENT : C_TEXT,
             x + S(20), ry, w - S(40), rh - S(4), g_menu.label[i], 0);
        hit_add(x, ry, w, rh, H_MENU, i);
    }
}

static void paint_toast(void)
{
    int w, h = S(34), y;
    if (!g_toast[0] || GetTickCount() > g_toastUntil) return;
    w = W - S(24);
    y = H - NAV_H - h - S(20);
    rrect(S(12), y, w, h, S(17), RGB(60, 60, 68));
    text(fNorm, C_TEXT, S(24), y, w - S(24), h, g_toast, 1);
}

static void paint_all(HDC hdc)
{
    HDC mdc = CreateCompatibleDC(hdc);
    HBITMAP bm = CreateCompatibleBitmap(hdc, W, H), ob = (HBITMAP)SelectObject(mdc, bm);
    g_dc = mdc;
    g_hitN = 0;
    SetBkMode(mdc, TRANSPARENT);
    fill(0, 0, W, H, C_BG);
    if (g_scr == SCR_HOME && g_wall) {
        HDC wdc = CreateCompatibleDC(hdc);
        HBITMAP ow = (HBITMAP)SelectObject(wdc, g_wall);
        BitBlt(mdc, 0, 0, W, H, wdc, 0, 0, SRCCOPY);
        SelectObject(wdc, ow);
        DeleteDC(wdc);
    }
    switch (g_scr) {
    case SCR_HOME: paint_header(); paint_home(); break;
    case SCR_ALL:  paint_all_apps(); break;
    case SCR_RUN:  paint_running(); break;
    case SCR_SET:  paint_settings(); break;
    case SCR_WALL: paint_wallpick(); break;
    }
    paint_nav();
    paint_toast();
    if (g_menu.on) paint_menu();
    BitBlt(hdc, 0, 0, W, H, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, ob);
    DeleteObject(bm);
    DeleteDC(mdc);
}

/* ------------------------------------------------------------------ */
/* 동작                                                                 */
/* ------------------------------------------------------------------ */
static void go(int scr)
{
    if (scr == SCR_ALL && g_scr != SCR_ALL) scan_all();
    if (scr == SCR_RUN) scan_running();
    if (scr == SCR_SET) read_volume();
    if (scr == SCR_WALL) scan_pics();
    if (scr != g_scr) { g_scroll[scr] = 0; g_focus = -1; }
    if (scr != SCR_ALL) g_addMode = 0;
    g_scr = scr;
    g_menu.on = 0;
    g_swipeX = 0;
    g_lift = -1; g_dragging = 0;
    redraw();
}

static void menu_open(const wchar_t *title, int target)
{
    memset(&g_menu, 0, sizeof(g_menu));
    g_menu.on = 1;
    g_menu.target = target;
    wcsncpy(g_menu.title, title, 63);
    g_focus = 0;
}
static void menu_item(int act, const wchar_t *label)
{
    if (g_menu.n >= 6) return;
    g_menu.act[g_menu.n] = act;
    g_menu.label[g_menu.n] = label;
    g_menu.n++;
}

static void long_press(int id, int arg)
{
    if (id == H_TILE && arg < g_favN) {
        /* 들어 올림: 이대로 끌면 옮기기, 그냥 떼면 메뉴 */
        g_lift = arg;
        g_dragging = 0;
        g_liftX = g_dx = t_lx; g_liftY = g_dy = t_ly;
        g_hover = -1;
        MessageBeep(MB_OK);
        redraw();
        return;
    } else if (id == H_ROW && g_scr == SCR_ALL && arg < g_allN) {
        menu_open(g_all[arg].name, arg);
        if (fav_find(g_all[arg].key) >= 0) menu_item(M_ALL_REMOVE, L"홈에서 빼기");
        else menu_item(M_ALL_ADD, L"홈에 추가");
        menu_item(M_CANCEL, L"취소");
    } else if (id == H_ROW && g_scr == SCR_RUN && arg < g_runN) {
        menu_open(g_run[arg].title, arg);
        menu_item(M_RUN_CLOSE, L"닫기");
        menu_item(M_CANCEL, L"취소");
    } else return;
    t_down = 0;
    redraw();
}

static void menu_do(int i)
{
    int t = g_menu.target, act;
    wchar_t m[96];
    if (i < 0 || i >= g_menu.n) return;
    act = g_menu.act[i];
    g_menu.on = 0;
    g_focus = -1;
    switch (act) {
    case M_FAV_REMOVE:
        _snwprintf(m, 96, L"%s 을(를) 홈에서 뺐어요", g_fav[t].name);
        fav_remove(t); save_settings(); toast(m);
        break;
    case M_ALL_ADD:
        g_addFrom = 0;
        fav_add(&g_all[t]); save_settings();
        _snwprintf(m, 96, L"%s 을(를) 홈에 넣었어요", g_all[t].name); toast(m);
        break;
    case M_ALL_REMOVE:
        fav_remove(fav_find(g_all[t].key)); save_settings();
        _snwprintf(m, 96, L"%s 을(를) 홈에서 뺐어요", g_all[t].name); toast(m);
        break;
    case M_RUN_CLOSE: run_close(t); break;
    }
    redraw();
}

static void option_do(int i)
{
    switch (i) {
    case O_OFF: screen_off(); break;
    case O_TODAY: g_set.replaceToday = !g_set.replaceToday; save_settings(); break;
    case O_AUTO: autostart_set(!autostart_on()); break;
    case O_GRID: {
        int og = g_set.grid;
        g_set.grid = (g_set.grid + 1) % 3;
        regrid(og, g_set.grid);
        g_page = 0;
        save_settings();
        break;
    }
    case O_WALL: go(SCR_WALL); return;
    case O_CLOCK: g_set.clock24 = !g_set.clock24; save_settings(); break;
    case O_RESET:
        scan_all(); fav_defaults(); save_settings(); g_page = 0;
        toast(L"홈을 처음 상태로 되돌렸어요");
        break;
    case O_RESCAN: {
        int k;
        wchar_t m[64];
        scan_all();
        for (k = 0; k < g_favN; k++) app_free_icon(&g_fav[k]);
        _snwprintf(m, 64, L"앱 %d개를 찾았어요", g_allN);
        toast(m);
        break;
    }
    case O_QUIT: DestroyWindow(g_hwnd); return;
    }
    redraw();
}

static void activate(int id, int arg)
{
    switch (id) {
    case H_TILE: if (arg < g_favN) launch(&g_fav[arg]); break;
    case H_EMPTY: g_addFrom = arg; scan_all(); g_scroll[SCR_ALL] = 0; g_scr = SCR_ALL; g_addMode = 1; g_focus = -1; redraw(); break;
    case H_NAV_ALL: go(g_scr == SCR_ALL ? SCR_HOME : SCR_ALL); break;
    case H_NAV_RUN: go(g_scr == SCR_RUN ? SCR_HOME : SCR_RUN); break;
    case H_NAV_SET: go(g_scr == SCR_SET ? SCR_HOME : SCR_SET); break;
    case H_BACK: case H_DONE: go(g_scr == SCR_WALL ? SCR_SET : SCR_HOME); break;
    case H_ROW:
        if (g_scr == SCR_ALL && arg < g_allN) {
            if (g_addMode) {
                int f = fav_find(g_all[arg].key);
                if (f >= 0) fav_remove(f); else fav_add(&g_all[arg]);
                save_settings();
                redraw();
            } else launch(&g_all[arg]);
        } else if (g_scr == SCR_RUN) run_switch(arg);
        break;
    case H_CLOSE: run_close(arg); break;
    case H_CLOSEALL: {
        int i;
        for (i = 0; i < g_runN; i++) if (g_run[i].h) PostMessageW(g_run[i].h, WM_CLOSE, 0, 0);
        g_runN = 0;
        toast(L"모든 앱을 닫았어요");
        SetTimer(g_hwnd, 5, 1200, NULL);
        redraw();
        break;
    }
    case H_VOLDN: set_volume(g_vol - 1); break;
    case H_VOLUP: set_volume(g_vol + 1); break;
    case H_OPT: option_do(arg); break;
    case H_MENU: menu_do(arg); break;
    case H_MENU_BG: g_menu.on = 0; redraw(); break;
    case H_DOT: g_page = arg; redraw(); break;
    case H_WDIM: g_set.wallDim = (g_set.wallDim + 1) % 3; build_wall(); save_settings(); redraw(); break;
    case H_WPRESET: set_wall(arg, NULL); break;
    case H_WPIC: if (arg < g_picN) set_wall(WALL_PHOTO, g_pic[arg]); break;
    }
}

/* ------------------------------------------------------------------ */
/* 터치                                                                 */
/* ------------------------------------------------------------------ */
static int hit_test(int x, int y)
{
    int i;
    /* 나중에 그린 것이 위 (메뉴가 맨 위) */
    for (i = g_hitN - 1; i >= 0; i--) {
        RECT *r = &g_hit[i].r;
        if (x >= r->left && x < r->right && y >= r->top && y < r->bottom) {
            if (g_menu.on && g_hit[i].id != H_MENU && g_hit[i].id != H_MENU_BG) continue;
            return i;
        }
    }
    return -1;
}

static int scrollable(void) { return g_scr == SCR_ALL || g_scr == SCR_RUN || g_scr == SCR_SET || g_scr == SCR_WALL; }

static void on_down(int x, int y)
{
    KillTimer(g_hwnd, 2);
    t_down = 1; t_moved = 0; t_long = 0;
    t_x0 = t_lx = x; t_y0 = t_ly = y;
    t_lt = GetTickCount();
    t_vel = 0;
    t_scroll0 = g_scroll[g_scr];
    t_hit = hit_test(x, y);
    g_keyMode = 0;
    SetCapture(g_hwnd);
    if (t_hit >= 0) {
        int id = g_hit[t_hit].id;
        if (id == H_TILE || id == H_ROW) SetTimer(g_hwnd, 3, 550, NULL);
    }
    redraw();
}

static int pos_from_xy(int x, int y)
{
    int c, r;
    if (g_gW <= 0 || g_gH <= 0) return -1;
    if (y < g_gTop || y >= g_gTop + g_gH * g_gRows || y >= H - NAV_H) return -1;
    c = (x - S(4)) / g_gW;
    if (c < 0) c = 0;
    if (c >= g_gCols) c = g_gCols - 1;
    r = (y - g_gTop) / g_gH;
    return g_page * g_gCols * g_gRows + r * g_gCols + c;
}

static void fav_drop(void)
{
    int to = pos_from_xy(g_dx, g_dy), o, from;
    if (to < 0 || g_lift < 0 || g_lift >= g_favN) return;
    from = g_fav[g_lift].pos;
    if (to == from) return;
    o = fav_at(to);
    if (o >= 0) g_fav[o].pos = from;      /* 다른 아이콘이 있으면 자리 바꾸기 */
    g_fav[g_lift].pos = to;
    save_settings();
}

static void on_move(int x, int y)
{
    int dx, dy;
    DWORD now;
    if (!t_down) return;
    if (g_lift >= 0) {                     /* 들어 올린 아이콘 끌기 */
        int mx = x - g_liftX, my = y - g_liftY;
        if (!g_dragging && (mx > S(8) || mx < -S(8) || my > S(8) || my < -S(8))) g_dragging = 1;
        if (g_dragging) {
            g_dx = x; g_dy = y;
            g_hover = pos_from_xy(x, y);
            if (x < S(18) || x > W - S(18)) SetTimer(g_hwnd, 6, 650, NULL);
            else KillTimer(g_hwnd, 6);
            redraw();
        }
        return;
    }
    dx = x - t_x0; dy = y - t_y0;
    if (!t_moved && (dx > S(10) || dx < -S(10) || dy > S(10) || dy < -S(10)) && !g_menu.on) {
        t_moved = 1;
        KillTimer(g_hwnd, 3);
    }
    if (!t_moved) return;
    now = GetTickCount();
    if (g_scr == SCR_HOME && t_y0 < H - NAV_H) {
        g_swipeX = dx;
        if (g_page == 0 && g_swipeX > 0) g_swipeX /= 3;
        if (g_page == home_pages() - 1 && g_swipeX < 0) g_swipeX /= 3;
    } else if (scrollable() && t_y0 >= LIST_TOP && t_y0 < H - NAV_H) {
        g_scroll[g_scr] = t_scroll0 - dy;
        clamp_scroll(g_scr);
        if (now != t_lt) t_vel = (t_ly - y) * 1000 / (int)(now - t_lt + 1) / 4 + t_vel / 2;
    }
    t_lx = x; t_ly = y; t_lt = now;
    redraw();
}

static void on_up(int x, int y)
{
    int h = t_hit;
    (void)x; (void)y;
    if (!t_down) return;
    KillTimer(g_hwnd, 3);
    KillTimer(g_hwnd, 6);
    ReleaseCapture();
    t_down = 0;
    if (g_lift >= 0) {
        int li = g_lift;
        if (g_dragging) {
            fav_drop();
            g_lift = -1; g_dragging = 0; g_hover = -1;
        } else {
            g_lift = -1;
            menu_open(g_fav[li].name, li);
            g_menu.hint = L"꾹 누른 채 끌면 원하는 칸으로 옮겨져요";
            menu_item(M_FAV_REMOVE, L"홈에서 빼기");
            menu_item(M_CANCEL, L"닫기");
        }
        t_hit = -1;
        redraw();
        return;
    }
    if (t_moved) {
        if (g_scr == SCR_HOME) {
            int pages = home_pages();
            if (g_swipeX < -W / 6 && g_page < pages - 1) g_page++;
            else if (g_swipeX > W / 6 && g_page > 0) g_page--;
            g_swipeX = 0;
        } else if (scrollable() && (t_vel > 3 || t_vel < -3)) {
            SetTimer(g_hwnd, 2, 30, NULL);
        }
    } else if (!t_long && h >= 0 && h < g_hitN) {
        int id = g_hit[h].id, arg = g_hit[h].arg;
        t_hit = -1;
        activate(id, arg);
    }
    t_hit = -1;
    redraw();
}

static void on_inertia(void)
{
    g_scroll[g_scr] += t_vel;
    clamp_scroll(g_scr);
    t_vel = t_vel * 9 / 10;
    if (t_vel < 2 && t_vel > -2) KillTimer(g_hwnd, 2);
    redraw();
}

/* ------------------------------------------------------------------ */
/* 키                                                                   */
/* ------------------------------------------------------------------ */
static void list_ensure(int row, int rowTop, int rowH)
{
    int vis = H - NAV_H - LIST_TOP, y = rowTop + row * rowH;
    if (y < g_scroll[g_scr]) g_scroll[g_scr] = y;
    if (y + rowH > g_scroll[g_scr] + vis) g_scroll[g_scr] = y + rowH - vis;
    clamp_scroll(g_scr);
}

static int on_key(WPARAM k)
{
    int cols, rows, n;
    if (k == VK_F1) { go(g_scr == SCR_ALL ? SCR_HOME : SCR_ALL); return 1; }
    if (k == VK_F2) { go(g_scr == SCR_RUN ? SCR_HOME : SCR_RUN); return 1; }
    if (k == VK_ESCAPE || k == VK_BACK) {
        if (g_menu.on) { g_menu.on = 0; redraw(); return 1; }
        if (g_scr == SCR_WALL) { go(SCR_SET); return 1; }
        if (g_scr != SCR_HOME) { go(SCR_HOME); return 1; }
        return 1;
    }
    if (k != VK_UP && k != VK_DOWN && k != VK_LEFT && k != VK_RIGHT && k != VK_RETURN) return 0;
    if (!g_keyMode) {          /* 첫 키는 선택 표시만 */
        g_keyMode = 1;
        if (g_focus < 0) {
            if (g_menu.on) g_focus = 0;
            else if (g_scr == SCR_HOME) { grid_dims(&cols, &rows); g_focus = g_page * cols * rows; }
            else g_focus = 0;
        }
        redraw();
        if (k != VK_RETURN) return 1;
    }
    if (g_menu.on) {
        if (k == VK_UP && g_focus > 0) g_focus--;
        else if (k == VK_DOWN && g_focus < g_menu.n - 1) g_focus++;
        else if (k == VK_RETURN) menu_do(g_focus);
        redraw();
        return 1;
    }
    switch (g_scr) {
    case SCR_HOME:
        grid_dims(&cols, &rows);
        n = home_pages() * cols * rows;      /* 모든 칸 (빈칸 포함) */
        if (g_focus < 0) g_focus = 0;
        if (k == VK_LEFT && g_focus > 0) g_focus--;
        else if (k == VK_RIGHT && g_focus < n - 1) g_focus++;
        else if (k == VK_UP && g_focus - cols >= 0) g_focus -= cols;
        else if (k == VK_DOWN && g_focus + cols < n) g_focus += cols;
        else if (k == VK_RETURN) {
            int f = fav_at(g_focus);
            if (f >= 0) activate(H_TILE, f); else activate(H_EMPTY, g_focus);
            return 1;
        }
        g_page = g_focus / (cols * rows);
        break;
    case SCR_ALL:
    case SCR_RUN:
        n = g_scr == SCR_ALL ? g_allN : g_runN;
        if (n == 0) break;
        if (k == VK_UP && g_focus > 0) g_focus--;
        else if (k == VK_DOWN && g_focus < n - 1) g_focus++;
        else if (k == VK_LEFT && g_scr == SCR_RUN) { run_close(g_focus); if (g_focus >= g_runN) g_focus = g_runN - 1; }
        else if (k == VK_RETURN) { activate(H_ROW, g_focus); return 1; }
        if (g_focus >= 0) list_ensure(g_focus, g_scr == SCR_RUN ? S(48) : 0, ROW_H);
        break;
    case SCR_SET:
        if (k == VK_UP && g_focus > 0) g_focus--;
        else if (k == VK_DOWN && g_focus < O_N - 1) g_focus++;
        else if (g_focus == O_VOL && (k == VK_LEFT || k == VK_RIGHT)) set_volume(g_vol + (k == VK_RIGHT ? 1 : -1));
        else if (k == VK_RETURN && g_focus != O_VOL) option_do(g_focus);
        if (g_focus >= 0) list_ensure(g_focus, g_focus * ROW_H + (g_focus >= O_WALL ? S(24) : 0) + (g_focus >= O_RESET ? S(24) : 0), ROW_H + S(14));
        break;
    }
    redraw();
    return 1;
}

/* ------------------------------------------------------------------ */
/* 오늘 화면이 앞에 나오면 M480 홈을 대신 보여 줌                         */
/* ------------------------------------------------------------------ */
static void watch_today(void)
{
    HWND fg;
    wchar_t cls[48];
    if (!g_set.replaceToday) return;
    fg = GetForegroundWindow();
    if (!fg || fg == g_hwnd) return;
    cls[0] = 0;
    GetClassNameW(fg, cls, 48);
    if (wcscmp(cls, L"DesktopExplorerWindow") == 0) {
        g_scr = SCR_HOME;
        g_menu.on = 0;
        g_addMode = 0;
        SetForegroundWindow(g_hwnd);
        redraw();
    }
}

/* ------------------------------------------------------------------ */
/* 창                                                                   */
/* ------------------------------------------------------------------ */
static void fit_window(void)
{
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
#ifndef PREVIEW_W
    MoveWindow(g_hwnd, wa.left, wa.top, wa.right - wa.left, wa.bottom - wa.top, TRUE);
#endif
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        SetTimer(hwnd, 1, 500, NULL);
        return 0;
    case WM_SIZE:
        W = LOWORD(lp); H = HIWORD(lp);
        if (W != g_wallW || H != g_wallH) build_wall();
        clamp_scroll(g_scr);
        redraw();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (W > 0 && H > 0) paint_all(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (wp == 1) {
            static int tick;
            SYSTEMTIME st;
            watch_today();
            GetLocalTime(&st);
            if (++tick % 10 == 0) {          /* 5초마다 배터리·인터넷·실행 중 */
                int b = g_batt, c = g_charging, n = g_net, r = g_runN;
                read_status();
                if (GetForegroundWindow() == g_hwnd && g_scr != SCR_RUN) scan_running();
                if (b != g_batt || c != g_charging || n != g_net || r != g_runN) redraw();
            }
            if (st.wMinute != g_lastMin) { g_lastMin = st.wMinute; if (g_scr == SCR_HOME) redraw(); }
        } else if (wp == 2) on_inertia();
        else if (wp == 3) {
            KillTimer(hwnd, 3);
            if (t_down && !t_moved && t_hit >= 0 && t_hit < g_hitN) {
                t_long = 1;
                long_press(g_hit[t_hit].id, g_hit[t_hit].arg);
            }
        } else if (wp == 4) {
            if (GetTickCount() > g_toastUntil) { KillTimer(hwnd, 4); g_toast[0] = 0; redraw(); }
        } else if (wp == 6) {
            if (g_dragging && g_dx < S(18) && g_page > 0) { g_page--; g_hover = pos_from_xy(g_dx, g_dy); redraw(); }
            else if (g_dragging && g_dx > W - S(18) && g_page < home_pages() - 1) { g_page++; g_hover = pos_from_xy(g_dx, g_dy); redraw(); }
            else KillTimer(hwnd, 6);
        } else if (wp == 5) {
            KillTimer(hwnd, 5);
            scan_running();
            redraw();
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) {
            fit_window();
            read_status();
            scan_running();
            redraw();
        }
        break;
    case WM_SETTINGCHANGE:
        fit_window();
        break;
    case WM_LBUTTONDOWN: on_down((short)LOWORD(lp), (short)HIWORD(lp)); return 0;
    case WM_MOUSEMOVE:   on_move((short)LOWORD(lp), (short)HIWORD(lp)); return 0;
    case WM_LBUTTONUP:   on_up((short)LOWORD(lp), (short)HIWORD(lp)); return 0;
    case WM_KEYDOWN:
        if (on_key(wp)) return 0;
        break;
    case WM_CLOSE:
        /* 런처는 닫지 않고 뒤로 보냄 (끝내기는 설정에서) */
        if (g_set.replaceToday) { go(SCR_HOME); return 0; }
        break;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        save_settings();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HFONT make_font(int px, int bold)
{
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -px;
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = 5; /* CLEARTYPE_QUALITY */
#ifdef PREVIEW_FONT
    wcscpy(lf.lfFaceName, PREVIEW_FONT);
#endif
    return CreateFontIndirectW(&lf);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmd, int show)
{
    WNDCLASSW wc;
    MSG m;
    HWND prev;
    HDC sdc;
    RECT wa;
    wchar_t *slash;
    (void)hPrev; (void)cmd;

    prev = FindWindowW(APP_CLASS, NULL);
    if (prev) { SetForegroundWindow((HWND)((ULONG_PTR)prev | 1)); return 0; }
    g_inst = hInst;

    GetModuleFileNameW(NULL, g_exePath, MAX_PATH);
    wcscpy(g_exeDir, g_exePath);
    slash = wcsrchr(g_exeDir, L'\\');
    if (slash) slash[1] = 0; else wcscpy(g_exeDir, L"\\");

    sdc = GetDC(NULL);
    g_scale = GetDeviceCaps(sdc, LOGPIXELSY) >= 150 ? 2 : 1;
    ReleaseDC(NULL, sdc);
    fClock = make_font(S(34), 1);
    fHead = make_font(S(17), 1);
    fBold = make_font(S(14), 1);
    fNorm = make_font(S(13), 0);
    fSmall = make_font(S(12), 0);
    fLetter = make_font(S(18), 1);
    fLetterB = make_font(S(26), 1);

    load_funcs();
    scan_all();
    if (!load_settings()) {
        fav_defaults();
        save_settings();
        if (!autostart_on()) autostart_set(1);
    }
    read_status();
    read_volume();

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hbrBackground = NULL;
    wc.lpszClassName = APP_CLASS;
    RegisterClassW(&wc);

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
#ifdef PREVIEW_W
    {
        RECT r;
        r.left = 0; r.top = 0; r.right = PREVIEW_W; r.bottom = PREVIEW_H;
        AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
        g_hwnd = CreateWindowW(APP_CLASS, APP_TITLE, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               0, 0, r.right - r.left, r.bottom - r.top, NULL, NULL, hInst, NULL);
    }
#ifdef PREVIEW_SCR
    if (PREVIEW_SCR == 9) { g_menu.on = 0; long_press(H_TILE, 1); }
    else go(PREVIEW_SCR);
#endif
#else
    g_hwnd = CreateWindowW(APP_CLASS, APP_TITLE, WS_VISIBLE,
                           wa.left, wa.top, wa.right - wa.left, wa.bottom - wa.top, NULL, NULL, hInst, NULL);
#endif
    if (!g_hwnd) return 0;
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);
    scan_running();

    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
