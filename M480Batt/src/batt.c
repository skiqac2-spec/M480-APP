/*
 * M480Batt - SCH-M480(WM6.1)용 배터리 진단
 *
 * 목적: "전원 버튼을 누르면 가끔 바로 꺼지는" 원인을 가려내기 위해
 *   - 10초마다 잔량·전압·전류·온도·충전 여부를 기록 (한 줄씩 바로 저장 → 갑자기 꺼져도 남음)
 *   - 전원 버튼으로 잠들기/깨어나기, 화면 꺼짐, 충전기 연결 같은 전원 사건을 기록
 *   - 깨어난 직후 3초 동안 0.15초마다 전압을 재서 순간 하락(배터리 노화 신호)을 측정
 *   - 다시 켰을 때 지난 기록이 '종료' 없이 끊겨 있으면 "갑자기 꺼짐"으로 보고,
 *     꺼지기 직전 수치로 원인을 판정해서 보여 줌
 *
 * 기록 파일: 실행 파일과 같은 폴더의 M480Batt_log.csv (글자는 모두 영문/숫자)
 *   시각,틱(ms),종류,잔량%,전압mV,전류mA,온도(0.1도),충전(1=충전기 연결)[,추가1,추가2]
 *
 * 키: 왼쪽 소프트키 = 기록 보기, 오른쪽 소프트키 = 숨기기(계속 기록), 뒤로 = 처음 화면
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_CLASS L"M480BattWnd"
#define APP_TITLE L"배터리 진단"
#define APP_VER   L"1.1"

#define SAMPLE_MS     10000   /* 평소 기록 간격 */
#define FAST_MS       150     /* 깨어난 직후 측정 간격 */
#define FAST_COUNT    20      /* 깨어난 직후 측정 횟수 (3초) */
#define LOG_MAX_BYTES (512 * 1024)
#define LOW_MV        3550    /* 이보다 낮은 전압에서 꺼지면 배터리 탓 */
#define DROP_WARN_MV  250     /* 깨어날 때 이만큼 떨어지면 노화 신호 */

#define WM_POWEREV (WM_APP + 1)
#define WM_SHOWME  (WM_APP + 2)

#define T_SAMPLE 1
#define T_FAST   2
#define T_TOAST  3

#ifndef VK_TSOFT1
#define VK_TSOFT1 VK_F1
#define VK_TSOFT2 VK_F2
#endif

/* ------------------------------------------------------------------ */
/* 색 (M480Home과 같은 어두운 테마)                                    */
/* ------------------------------------------------------------------ */
#define C_BG      RGB(12, 12, 14)
#define C_CARD    RGB(30, 30, 34)
#define C_PRESS   RGB(58, 58, 66)
#define C_TEXT    RGB(246, 246, 248)
#define C_SUB     RGB(150, 150, 158)
#define C_DIM     RGB(90, 90, 98)
#define C_SEP     RGB(48, 48, 54)
#define C_ACCENT  RGB(255, 112, 84)
#define C_OK      RGB(90, 210, 120)
#define C_WARN    RGB(255, 196, 60)
#define C_BAD     RGB(240, 70, 70)
#define C_LINE    RGB(110, 180, 255)

/* ------------------------------------------------------------------ */
/* CE 전원 API (coredll에서 직접 불러옴)                                */
/* ------------------------------------------------------------------ */
typedef struct {
    BYTE ACLineStatus, BatteryFlag, BatteryLifePercent, Reserved1;
    DWORD BatteryLifeTime, BatteryFullLifeTime;
    BYTE Reserved2, BackupBatteryFlag, BackupBatteryLifePercent, Reserved3;
    DWORD BackupBatteryLifeTime, BackupBatteryFullLifeTime;
    DWORD BatteryVoltage;          /* mV */
    LONG  BatteryCurrent;          /* mA, 음수 = 방전 */
    DWORD BatteryAverageCurrent;
    DWORD BatteryAverageInterval;
    DWORD BatterymAHourConsumed;
    DWORD BatteryTemperature;      /* 0.1도 */
    DWORD BackupBatteryVoltage;
    BYTE  BatteryChemistry;
} PowEx2;

typedef struct {
    DWORD dwSize, dwFlags, dwMaxMessages, cbMaxMessage;
    BOOL bReadAccess;
} MsgQOpt;

typedef struct {
    DWORD Message, Flags, Length;
    WCHAR SystemPowerState[1];
} PowBcast;

#define PBT_TRANSITION_       0x00000001
#define PBT_RESUME_           0x00000002
#define PBT_POWERSTATUSCHANGE_ 0x00000004
#define POWER_NOTIFY_ALL_     0xFFFFFFFF
#define PS_ON          0x00010000
#define PS_OFF         0x00020000
#define PS_CRITICAL    0x00040000
#define PS_BOOT        0x00080000
#define PS_IDLE        0x00100000
#define PS_SUSPEND     0x00200000
#define PS_UNATTENDED  0x00400000
#define PS_RESET       0x00800000
#define PS_USERIDLE    0x01000000
#define PS_BACKLIGHTON 0x02000000

typedef DWORD  (WINAPI *PFN_PowEx2)(PowEx2 *, DWORD, BOOL);
typedef HANDLE (WINAPI *PFN_CreateMQ)(LPCWSTR, MsgQOpt *);
typedef BOOL   (WINAPI *PFN_ReadMQ)(HANDLE, LPVOID, DWORD, LPDWORD, DWORD, DWORD *);
typedef HANDLE (WINAPI *PFN_ReqPN)(HANDLE, DWORD);
typedef BOOL   (WINAPI *PFN_FullScreen)(HWND, DWORD);
typedef BOOL   (WINAPI *PFN_Special)(HWND, LPWSTR, int, BOOL);
static PFN_PowEx2 pPowEx2;
static PFN_CreateMQ pCreateMQ;
static PFN_ReadMQ pReadMQ;
static PFN_ReqPN pReqPN;
static PFN_Special pSpecial;
static PFN_FullScreen pFullScreen;
static HWND g_sipHidden;
static DWORD g_rawV = 0xFFFFFFFF, g_rawT, g_rawC;   /* 기기가 준 원래 값 (확인용) */
static int g_rawChem, g_rawOK = -1;

static void load_funcs(void)
{
    HMODULE h = LoadLibraryW(L"coredll.dll");
    if (!h) return;
    pPowEx2 = (PFN_PowEx2)GetProcAddressW(h, L"GetSystemPowerStatusEx2");
    pCreateMQ = (PFN_CreateMQ)GetProcAddressW(h, L"CreateMsgQueue");
    pReadMQ = (PFN_ReadMQ)GetProcAddressW(h, L"ReadMsgQueue");
    pReqPN = (PFN_ReqPN)GetProcAddressW(h, L"RequestPowerNotifications");
    pSpecial = (PFN_Special)GetProcAddressW(h, L"SHGetSpecialFolderPath");
    h = LoadLibraryW(L"aygshell.dll");
    if (h) pFullScreen = (PFN_FullScreen)GetProcAddressW(h, L"SHFullScreen");
}

/* ------------------------------------------------------------------ */
/* 측정값                                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    int pct;    /* 0..100, -1 모름 */
    int mv;     /* 0 = 기기가 안 알려 줌 */
    int ma;
    int temp;   /* 0.1도, 0 = 모름 */
    int ac;     /* 1 = 충전기 연결 */
    int chem;
} Meas;

static int g_fake;  /* PC 미리보기용 가짜 값 */

static void measure(Meas *m, BOOL update)
{
    PowEx2 s;
    memset(m, 0, sizeof(*m));
    m->pct = -1;
    if (!pPowEx2) {
        /* 미리보기: 천천히 줄어드는 가짜 배터리 */
        g_fake++;
        m->pct = 62 - (g_fake / 30) % 40;
        m->mv = 3880 - (g_fake % 17) * 6 - ((g_fake % 50) == 7 ? 330 : 0);
        m->ma = -210;
        m->temp = 296;
        return;
    }
    memset(&s, 0, sizeof(s));
    if (!pPowEx2(&s, sizeof(s), update)) { g_rawOK = 0; return; }
    g_rawOK = 1;
    g_rawV = s.BatteryVoltage; g_rawT = s.BatteryTemperature; g_rawC = (DWORD)s.BatteryCurrent;
    g_rawChem = s.BatteryChemistry;
    if (s.BatteryLifePercent <= 100) m->pct = s.BatteryLifePercent;
    if (s.BatteryVoltage > 2000 && s.BatteryVoltage < 5000) m->mv = (int)s.BatteryVoltage;
    else if (s.BatteryVoltage > 20000 && s.BatteryVoltage < 50000) m->mv = (int)(s.BatteryVoltage / 10);
    m->ma = (int)s.BatteryCurrent;
    if (m->ma < -5000 || m->ma > 5000) m->ma = 0;
    if (s.BatteryTemperature > 0 && s.BatteryTemperature < 900) m->temp = (int)s.BatteryTemperature;
    m->ac = (s.ACLineStatus == 1) ? 1 : 0;
    m->chem = s.BatteryChemistry;
}

/* ------------------------------------------------------------------ */
/* 기록 레코드                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    char t[20];       /* 2026-11-26 09:30:12 */
    DWORD tick;
    char kind[12];
    int pct, mv, ma, temp, ac;
    int x1, x2;
    char xs[20];
} Rec;

/* 전역 */
static HWND g_hwnd;
static HINSTANCE g_inst;
static wchar_t g_exeDir[MAX_PATH], g_exePath[MAX_PATH], g_logPath[MAX_PATH], g_oldPath[MAX_PATH];
static int g_scale = 1;
#define S(x) ((x) * g_scale)
static int W, H;
static HDC g_dc;
static HFONT fHuge, fBig, fHead, fBold, fNorm, fSmall;

static Meas g_now;
static int g_hasVolt;           /* 전압을 한 번이라도 받았나 */

#define HIST 180                /* 그래프: 10초 x 180 = 30분 */
static short g_hist[HIST];
static char g_histMark[HIST];   /* 1 = 깨어남 */
static int g_histN;

#define EVN 60                  /* 사건 목록 (S 제외) */
static Rec g_ev[EVN];
static int g_evN, g_evHead;

#define CRN 8
static Rec g_cr[CRN];           /* 갑자기 꺼짐 기록 (최근 순 아님, 들어온 순) */
static int g_crN, g_crTotal;

static int g_dropMax, g_dropLast = -1, g_dropN;
static int g_lastMvBeforeSleep; /* 잠들기 직전 전압 */
static int g_fastLeft, g_fastMin;
static int g_newCrash;          /* 이번 실행에서 막 발견한 갑자기 꺼짐 */
static int g_logOK = 1;

static int g_scr;               /* 0 처음, 1 기록 */
static int g_scroll, g_contentH;
static wchar_t g_toast[80];
static DWORD g_toastUntil;
static DWORD g_clearArm;

/* 누름 영역 */
typedef struct { RECT r; int id; } Hit;
static Hit g_hit[16];
static int g_hitN;
static int g_down = -1, g_downY, g_dragged, g_scroll0;
enum { B_AUTO = 1, B_LOG, B_HIDE, B_QUIT, B_BACK, B_CLEAR };

/* ------------------------------------------------------------------ */
/* 작은 도우미                                                          */
/* ------------------------------------------------------------------ */
static int file_exists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != 0xFFFFFFFF && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static void redraw(void) { InvalidateRect(g_hwnd, NULL, FALSE); }
static void toast(const wchar_t *s)
{
    wcsncpy(g_toast, s, 79); g_toast[79] = 0;
    g_toastUntil = GetTickCount() + 2000;
    SetTimer(g_hwnd, T_TOAST, 300, NULL);
    redraw();
}
static void now_str(char *out)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snprintf(out, 20, "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond);
    out[19] = 0;
}
/* "2026-11-26 09:30:12" -> L"11/26 09:30" */
static void short_time(const char *t, wchar_t *out, int n)
{
    if (strlen(t) >= 16)
        _snwprintf(out, n, L"%c%c/%c%c %c%c:%c%c", t[5], t[6], t[8], t[9], t[11], t[12], t[14], t[15]);
    else
        _snwprintf(out, n, L"-");
    out[n - 1] = 0;
}

/* ------------------------------------------------------------------ */
/* 기록 파일                                                            */
/* ------------------------------------------------------------------ */
static void rec_line(const Rec *r, char *s, int n)
{
    if (r->xs[0] || r->x1 || r->x2)
        _snprintf(s, n, "%s,%lu,%s,%d,%d,%d,%d,%d,%d,%d,%s\r\n", r->t, (unsigned long)r->tick, r->kind,
                  r->pct, r->mv, r->ma, r->temp, r->ac, r->x1, r->x2, r->xs);
    else
        _snprintf(s, n, "%s,%lu,%s,%d,%d,%d,%d,%d\r\n", r->t, (unsigned long)r->tick, r->kind,
                  r->pct, r->mv, r->ma, r->temp, r->ac);
    s[n - 1] = 0;
}

static void ev_push(const Rec *r)
{
    g_ev[g_evHead] = *r;
    g_evHead = (g_evHead + 1) % EVN;
    if (g_evN < EVN) g_evN++;
}
static void cr_push(const Rec *r)
{
    if (g_crN == CRN) { memmove(g_cr, g_cr + 1, sizeof(Rec) * (CRN - 1)); g_crN--; }
    g_cr[g_crN++] = *r;
    g_crTotal++;
}

/* 한 줄 쓰고 바로 닫음: 갑자기 꺼져도 앞 줄까지는 남음 */
static void log_write(const Rec *r)
{
    char s[200];
    HANDLE f;
    DWORD wr;
    rec_line(r, s, sizeof(s));
    f = CreateFileW(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { g_logOK = 0; return; }
    g_logOK = 1;
    SetFilePointer(f, 0, NULL, FILE_END);
    WriteFile(f, s, (DWORD)strlen(s), &wr, NULL);
    FlushFileBuffers(f);
    CloseHandle(f);
}

static void make_rec(Rec *r, const char *kind, const Meas *m)
{
    memset(r, 0, sizeof(*r));
    now_str(r->t);
    r->tick = GetTickCount();
    strncpy(r->kind, kind, 11);
    if (m) { r->pct = m->pct; r->mv = m->mv; r->ma = m->ma; r->temp = m->temp; r->ac = m->ac; }
}

static void log_kind(const char *kind, const Meas *m)
{
    Rec r;
    make_rec(&r, kind, m);
    log_write(&r);
    if (strcmp(kind, "S") != 0 && strcmp(kind, "W") != 0) ev_push(&r);
}

static int parse_line(char *s, Rec *r)
{
    char *f[12];
    int n = 0;
    char *p = s;
    memset(r, 0, sizeof(*r));
    f[n++] = p;
    while (*p && n < 12) {
        if (*p == ',') { *p = 0; f[n++] = p + 1; }
        p++;
    }
    if (n < 8 || strlen(f[0]) != 19) return 0;
    strcpy(r->t, f[0]);
    r->tick = strtoul(f[1], NULL, 10);
    strncpy(r->kind, f[2], 11);
    r->pct = atoi(f[3]); r->mv = atoi(f[4]); r->ma = atoi(f[5]);
    r->temp = atoi(f[6]); r->ac = atoi(f[7]);
    if (n > 8) r->x1 = atoi(f[8]);
    if (n > 9) r->x2 = atoi(f[9]);
    if (n > 10) { strncpy(r->xs, f[10], 19); r->xs[19] = 0; }
    return 1;
}

/* 지난 기록 읽기 + 갑자기 꺼졌는지 판정 */
#define TAILN 24
static Rec g_tail[TAILN];
static int g_tailN, g_tailHead;

static void tail_push(const Rec *r)
{
    g_tail[g_tailHead] = *r;
    g_tailHead = (g_tailHead + 1) % TAILN;
    if (g_tailN < TAILN) g_tailN++;
}
static const Rec *tail_get(int back) /* 0 = 마지막 */
{
    if (back >= g_tailN) return NULL;
    return &g_tail[(g_tailHead - 1 - back + TAILN * 2) % TAILN];
}

static void load_log(void)
{
    HANDLE f;
    char buf[4096], line[256];
    int ln = 0;
    DWORD rd, size;

    f = CreateFileW(g_logPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    size = GetFileSize(f, NULL);
    while (ReadFile(f, buf, sizeof(buf), &rd, NULL) && rd > 0) {
        DWORD i;
        for (i = 0; i < rd; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\r') {
                if (ln > 0) {
                    Rec r;
                    line[ln] = 0;
                    if (parse_line(line, &r)) {
                        tail_push(&r);
                        if (strcmp(r.kind, "CRASH") == 0) cr_push(&r);
                        else if (strcmp(r.kind, "DROP") == 0) {
                            if (r.x1 > g_dropMax) g_dropMax = r.x1;
                            g_dropLast = r.x1;
                            g_dropN++;
                        }
                        if (strcmp(r.kind, "S") != 0 && strcmp(r.kind, "W") != 0) ev_push(&r);
                    }
                }
                ln = 0;
            } else if (ln < (int)sizeof(line) - 1) {
                line[ln++] = c;
            }
        }
    }
    CloseHandle(f);

    /* 너무 커지면 예전 기록으로 옮기고 새로 시작 (갑자기 꺼짐 요약은 새 파일로 옮김) */
    if (size > LOG_MAX_BYTES) {
        int i;
        DeleteFileW(g_oldPath);
        MoveFileW(g_logPath, g_oldPath);
        for (i = 0; i < g_crN; i++) log_write(&g_cr[i]);
        for (i = g_tailN - 1; i >= 0; i--) log_write(tail_get(i));
    }
}

/* 켜 둔 채 며칠씩 쓸 때: 한 시간마다 크기 확인 */
static void rotate_if_big(void)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    int i;
    if (!GetFileAttributesExW(g_logPath, GetFileExInfoStandard, &fa)) return;
    if (fa.nFileSizeLow < LOG_MAX_BYTES * 2) return;
    DeleteFileW(g_oldPath);
    MoveFileW(g_logPath, g_oldPath);
    for (i = 0; i < g_crN; i++) log_write(&g_cr[i]);
    if (g_dropN > 0) {
        Rec r;
        memset(&r, 0, sizeof(r));
        now_str(r.t);
        r.tick = GetTickCount();
        strcpy(r.kind, "DROP");
        r.x1 = g_dropMax;
        log_write(&r);
    }
}

/* 기록 시각 - 틱 = 부팅 시각(초). 같은 부팅이면 거의 같음 */
static long long t_sec(const char *t)
{
    SYSTEMTIME st;
    FILETIME ft;
    memset(&st, 0, sizeof(st));
    if (strlen(t) < 19) return 0;
    st.wYear = (WORD)atoi(t); st.wMonth = (WORD)atoi(t + 5); st.wDay = (WORD)atoi(t + 8);
    st.wHour = (WORD)atoi(t + 11); st.wMinute = (WORD)atoi(t + 14); st.wSecond = (WORD)atoi(t + 17);
    if (!SystemTimeToFileTime(&st, &ft)) return 0;
    return (long long)((((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000000ULL);
}
static long long boot_sec(const char *t, DWORD tick)
{
    long long a = t_sec(t);
    return a ? a - (long long)(tick / 1000) : 0;
}

static int is_wake_kind(const char *k)
{
    return !strcmp(k, "W") || !strcmp(k, "WAKE") || !strcmp(k, "ON") || !strcmp(k, "BLON");
}

/* 판정 번호
 *  1 전압 낮음 → 배터리 노화
 *  2 화면 꺼짐/잠든 상태에서 끊김 (전원 버튼 누른 순간 포함)
 *  3 깨어나는 순간 끊김, 직전 전압 정상
 *  4 충전기 꽂은 채 꺼짐 → 버튼/기기 쪽
 *  5 전압 정보 없음, 잔량 남았는데 꺼짐
 *  6 사용 중 꺼짐, 전압 정상
 */
static void check_crash(void)
{
    const Rec *last = tail_get(0);
    DWORD cur = GetTickCount();
    Rec c;
    int i, minmv = 0, code, wake = 0, sleep = 0;

    if (!last) return;
    if (!strcmp(last->kind, "END") || !strcmp(last->kind, "CRASH") || !strcmp(last->kind, "KILLED")) return;

    if (cur >= last->tick) {
        char nt[20];
        long long b0 = boot_sec(last->t, last->tick), b1;
        now_str(nt);
        b1 = boot_sec(nt, cur);
        if (b0 && b1 && b1 - b0 < 120 && b0 - b1 < 120) {
            /* 같은 부팅 → 기기는 켜진 채였고 앱만 강제로 닫혔던 것 */
            log_kind("KILLED", NULL);
            return;
        }
    }

    /* 마지막 몇 줄 살펴보기 */
    for (i = 0; i < 8; i++) {
        const Rec *r = tail_get(i);
        if (!r) break;
        if (r->mv > 0 && (minmv == 0 || r->mv < minmv)) minmv = r->mv;
        if (i < 3 && is_wake_kind(r->kind)) wake = 1;
        if (i < 3 && (!strcmp(r->kind, "SLEEP") || !strcmp(r->kind, "SCROFF") || !strcmp(r->kind, "OFF"))) sleep = 1;
    }

    if (last->ac) code = 4;
    else if (minmv > 0 && minmv < LOW_MV) code = 1;
    else if (minmv == 0 && last->pct > 15) code = 5;
    else if (wake) code = 3;
    else if (sleep) code = 2;
    else code = 6;

    make_rec(&c, "CRASH", NULL);
    c.pct = last->pct; c.mv = minmv ? minmv : last->mv; c.ma = last->ma; c.temp = last->temp; c.ac = last->ac;
    c.x1 = code;
    c.x2 = 0;
    strcpy(c.xs, last->t);
    log_write(&c);
    cr_push(&c);
    ev_push(&c);
    g_newCrash = 1;
}

/* ------------------------------------------------------------------ */
/* 자동 시작 (시작프로그램 폴더의 바로 가기, 숨긴 채로 시작)              */
/* ------------------------------------------------------------------ */
static void startup_lnk(wchar_t *p)
{
    wchar_t d[MAX_PATH];
    static const wchar_t *const fb[] = { L"\\Windows\\StartUp", L"\\Windows\\시작프로그램", L"\\Windows\\시작 프로그램", NULL };
    int i;
    d[0] = 0;
    if (pSpecial && pSpecial(NULL, d, 7 /* CSIDL_STARTUP */, FALSE) && d[0]) {
        /* ok */
    } else {
        wcscpy(d, fb[0]);
        for (i = 0; fb[i]; i++) {
            DWORD a = GetFileAttributesW(fb[i]);
            if (a != 0xFFFFFFFF && (a & FILE_ATTRIBUTE_DIRECTORY)) { wcscpy(d, fb[i]); break; }
        }
    }
    _snwprintf(p, MAX_PATH, L"%s\\M480Batt.lnk", d);
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
        char a[MAX_PATH * 2], body[MAX_PATH * 2 + 16], s[MAX_PATH * 2 + 32];
        HANDLE f;
        DWORD wr;
        WideCharToMultiByte(CP_ACP, 0, g_exePath, -1, a, sizeof(a), NULL, NULL);
        _snprintf(body, sizeof(body), "\"%s\" /bg", a);
        _snprintf(s, sizeof(s), "%d#%s", (int)strlen(body), body);
        f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) { WriteFile(f, s, (DWORD)strlen(s), &wr, NULL); CloseHandle(f); }
    } else {
        DeleteFileW(p);
    }
}

/* ------------------------------------------------------------------ */
/* 전원 알림 (따로 도는 스레드 → 창으로 전달)                            */
/* ------------------------------------------------------------------ */
static DWORD WINAPI power_thread(LPVOID arg)
{
    HANDLE q = (HANDLE)arg;
    union { PowBcast pb; BYTE raw[sizeof(PowBcast) + 64 * sizeof(WCHAR)]; } u;
    (void)arg;
    for (;;) {
        DWORD rd = 0, fl = 0;
        if (WaitForSingleObject(q, INFINITE) != WAIT_OBJECT_0) break;
        if (pReadMQ(q, &u, sizeof(u), &rd, 0, &fl) && rd >= 8)
            PostMessageW(g_hwnd, WM_POWEREV, u.pb.Message, u.pb.Flags);
    }
    return 0;
}

static void power_init(void)
{
    MsgQOpt o;
    HANDLE q, t;
    if (!pCreateMQ || !pReadMQ || !pReqPN) return;
    memset(&o, 0, sizeof(o));
    o.dwSize = sizeof(o);
    o.dwFlags = 0;
    o.dwMaxMessages = 0;
    o.cbMaxMessage = sizeof(PowBcast) + 64 * sizeof(WCHAR);
    o.bReadAccess = TRUE;
    q = pCreateMQ(NULL, &o);
    if (!q) return;
    if (!pReqPN(q, POWER_NOTIFY_ALL_)) return;
    t = CreateThread(NULL, 0, power_thread, q, 0, NULL);
    if (t) CloseHandle(t);
}

static void hist_push(int mv, int mark)
{
    if (g_histN == HIST) {
        memmove(g_hist, g_hist + 1, sizeof(short) * (HIST - 1));
        memmove(g_histMark, g_histMark + 1, HIST - 1);
        g_histN--;
    }
    g_hist[g_histN] = (short)mv;
    g_histMark[g_histN] = (char)mark;
    g_histN++;
}

static void start_fast(void)
{
    if (g_fastLeft > 0) return;   /* 이미 재는 중 (깨어남 알림이 여러 개 옴) */
    g_fastLeft = FAST_COUNT;
    g_fastMin = 0;
    SetTimer(g_hwnd, T_FAST, FAST_MS, NULL);
}

static void on_power(DWORD msg, DWORD flags)
{
    Meas m;
    measure(&m, TRUE);
    if (msg == PBT_TRANSITION_) {
        const char *k = NULL;
        if (flags & PS_SUSPEND) k = "SLEEP";
        else if (flags & (PS_RESET)) k = "RESET";
        else if (flags & PS_CRITICAL) k = "CRIT";
        else if (flags & PS_OFF) k = "OFF";
        else if (flags & PS_UNATTENDED) k = "SCROFF";
        else if (flags & PS_USERIDLE) k = "DIM";
        else if (flags & PS_BACKLIGHTON) k = "BLON";
        else if (flags & PS_ON) k = "ON";
        else if (flags & PS_BOOT) k = "BOOT";
        if (!k) return;
        if (!strcmp(k, "SLEEP") || !strcmp(k, "SCROFF")) {
            if (g_now.mv > 0) g_lastMvBeforeSleep = g_now.mv;
        }
        log_kind(k, &m);
        if (!strcmp(k, "ON") || !strcmp(k, "BLON")) start_fast();
    } else if (msg == PBT_RESUME_) {
        log_kind("WAKE", &m);
        start_fast();
    } else if (msg == PBT_POWERSTATUSCHANGE_) {
        if (m.ac != g_now.ac) log_kind(m.ac ? "ACIN" : "ACOUT", &m);
    }
    g_now = m;
    if (m.mv > 0) g_hasVolt = 1;
    redraw();
}

static void on_fast(void)
{
    Meas m;
    measure(&m, TRUE);
    log_kind("W", &m);
    if (m.mv > 0 && (g_fastMin == 0 || m.mv < g_fastMin)) g_fastMin = m.mv;
    g_now = m;
    if (--g_fastLeft <= 0) {
        KillTimer(g_hwnd, T_FAST);
        if (g_fastMin > 0) {
            int pre = g_lastMvBeforeSleep;
            hist_push(g_fastMin, 1);
            if (pre > 0 && !m.ac) {
                Rec r;
                int drop = pre - g_fastMin;
                if (drop < 0) drop = 0;
                make_rec(&r, "DROP", &m);
                r.mv = g_fastMin;
                r.x1 = drop;
                r.x2 = pre;
                log_write(&r);
                if (drop > g_dropMax) g_dropMax = drop;
                g_dropLast = drop;
                g_dropN++;
            }
        }
    }
    redraw();
}

static void on_sample(void)
{
    static int cnt;
    Meas m;
    if (++cnt % 360 == 0) rotate_if_big();
    measure(&m, TRUE);
    log_kind("S", &m);
    if (g_now.ac != m.ac && g_now.pct >= 0) log_kind(m.ac ? "ACIN" : "ACOUT", &m);
    g_now = m;
    if (m.mv > 0) { g_hasVolt = 1; if (g_fastLeft == 0) g_lastMvBeforeSleep = m.mv; }
    hist_push(m.mv > 0 ? m.mv : (m.pct >= 0 ? m.pct : 0), 0);
    redraw();
}

/* ------------------------------------------------------------------ */
/* 그리기 도우미                                                        */
/* ------------------------------------------------------------------ */
static void hit_add(int x, int y, int w, int h, int id)
{
    if (g_hitN >= 16) return;
    g_hit[g_hitN].r.left = x; g_hit[g_hitN].r.top = y;
    g_hit[g_hitN].r.right = x + w; g_hit[g_hitN].r.bottom = y + h;
    g_hit[g_hitN].id = id;
    g_hitN++;
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
static void line(int x0, int y0, int x1, int y1, int th, COLORREF c)
{
    POINT p[2];
    HPEN pen = CreatePen(PS_SOLID, th, c), op;
    op = (HPEN)SelectObject(g_dc, pen);
    p[0].x = x0; p[0].y = y0; p[1].x = x1; p[1].y = y1;
    Polyline(g_dc, p, 2);
    SelectObject(g_dc, op);
    DeleteObject(pen);
}
static int text(HFONT f, COLORREF c, int x, int y, int w, int h, const wchar_t *s, int align)
{
    wchar_t buf[200];
    SIZE sz;
    int n = (int)wcslen(s);
    RECT r;
    UINT fl = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX;
    SelectObject(g_dc, f);
    SetTextColor(g_dc, c);
    if (n > 190) n = 190;
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
    DrawTextW(g_dc, buf, -1, &r, fl);
    return sz.cx;
}
/* 여러 줄 글자, 쓴 높이를 돌려줌 */
static int para(HFONT f, COLORREF c, int x, int y, int w, const wchar_t *s, int draw)
{
    RECT r;
    SelectObject(g_dc, f);
    SetTextColor(g_dc, c);
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + 2000;
    DrawTextW(g_dc, s, -1, &r, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    if (draw) {
        RECT r2 = r;
        r2.right = x + w;
        DrawTextW(g_dc, s, -1, &r2, DT_WORDBREAK | DT_NOPREFIX);
    }
    return r.bottom - r.top;
}

static void button(int x, int y, int w, int h, int id, const wchar_t *lab, COLORREF bg, COLORREF fg)
{
    rrect(x, y, w, h, S(8), g_down == id && !g_dragged ? C_PRESS : bg);
    text(fBold, fg, x, y, w, h, lab, 1);
    hit_add(x, y, w, h, id);
}

/* 배터리 모양 아이콘 */
static void battery_glyph(int x, int y, int w, int h, int pct, COLORREF c)
{
    int t = S(2), fw;
    fill(x, y, w, t, c); fill(x, y + h - t, w, t, c);
    fill(x, y, t, h, c); fill(x + w - t, y, t, h, c);
    fill(x + w, y + h / 3, S(3), h / 3, c);
    if (pct < 0) pct = 0;
    fw = (w - t * 4) * pct / 100;
    if (fw > 0) fill(x + t * 2, y + t * 2, fw, h - t * 4, c);
}

/* ------------------------------------------------------------------ */
/* 판정 문장                                                            */
/* ------------------------------------------------------------------ */
static void crash_text(const Rec *c, wchar_t *out, int n, int brief)
{
    wchar_t v[16];
    if (c->mv > 0) _snwprintf(v, 16, L"%d.%02dV", c->mv / 1000, (c->mv % 1000) / 10);
    else wcscpy(v, L"모름");
    if (brief) {
        switch (c->x1) {
        case 1: _snwprintf(out, n, L"꺼지기 직전 전압이 %s(잔량 %d%%)로 낮았습니다. 배터리 노화 가능성이 높아 교체를 권합니다.", v, c->pct); break;
        case 2: _snwprintf(out, n, L"화면이 꺼져 있거나 전원 버튼을 누른 순간 꺼졌습니다(직전 %s). 배터리 노화나 접점 문제로 보입니다.", v); break;
        case 3: _snwprintf(out, n, L"깨어나는 순간 꺼졌습니다(직전 %s). 순간 전압 하락, 즉 배터리 노화나 접점 문제로 보입니다.", v); break;
        case 4: _snwprintf(out, n, L"충전기를 꽂은 채 꺼졌습니다. 배터리보다 전원 버튼이나 기기 쪽 문제일 가능성이 높습니다."); break;
        case 5: _snwprintf(out, n, L"잔량이 %d%% 남았는데 꺼졌습니다. 배터리 노화 가능성이 높습니다.", c->pct); break;
        default: _snwprintf(out, n, L"쓰던 중이거나 잠든 동안 꺼졌습니다(직전 %s, %d%%). 접점이나 버튼 쪽도 의심해 보세요.", v, c->pct); break;
        }
        out[n - 1] = 0;
        return;
    }
    switch (c->x1) {
    case 1:
        _snwprintf(out, n, L"꺼지기 직전 전압이 %s로 낮았습니다(잔량 %d%%). 배터리가 낡아 전압이 버티지 못한 것으로 보입니다. 배터리 교체를 권합니다.", v, c->pct);
        break;
    case 2:
        _snwprintf(out, n, L"화면이 꺼져 있거나 전원 버튼을 누른 순간 꺼졌습니다(직전 %s, 잔량 %d%%). 깨어날 때 전력이 몰리며 배터리 전압이 순간 무너졌거나, 배터리 접점이 떨어졌을 수 있습니다.", v, c->pct);
        break;
    case 3:
        _snwprintf(out, n, L"전원 버튼으로 깨어나는 순간 꺼졌습니다. 직전 전압은 %s로 정상이었지만 순간 하락은 측정 사이에 일어날 수 있습니다. 배터리 노화 또는 접점 문제로 보입니다.", v);
        break;
    case 4:
        _snwprintf(out, n, L"충전기를 꽂은 상태에서 꺼졌습니다. 배터리보다는 전원 버튼이나 기기 쪽 문제일 가능성이 높습니다.");
        break;
    case 5:
        _snwprintf(out, n, L"잔량이 %d%% 남았는데 꺼졌습니다(이 기기는 전압 정보를 주지 않음). 배터리 노화 가능성이 높습니다.", c->pct);
        break;
    default:
        _snwprintf(out, n, L"쓰던 중이거나 잠들어 있는 동안 꺼졌습니다(직전 %s, 잔량 %d%%). 전압이 정상이었다면 배터리 접점이나 버튼 쪽도 의심해 보세요.", v, c->pct);
        break;
    }
    out[n - 1] = 0;
}

static COLORREF crash_color(int code)
{
    return code == 1 || code == 5 ? C_BAD : code == 4 ? C_WARN : C_ACCENT;
}

static const wchar_t *kind_name(const char *k)
{
    if (!strcmp(k, "START")) return L"진단 시작";
    if (!strcmp(k, "END")) return L"진단 종료";
    if (!strcmp(k, "KILLED")) return L"앱이 강제로 닫혔음";
    if (!strcmp(k, "CRASH")) return L"갑자기 꺼짐";
    if (!strcmp(k, "SLEEP")) return L"잠듦 (전원 버튼)";
    if (!strcmp(k, "WAKE")) return L"깨어남";
    if (!strcmp(k, "ON")) return L"켜짐";
    if (!strcmp(k, "BLON")) return L"화면 켜짐";
    if (!strcmp(k, "SCROFF")) return L"화면 꺼짐";
    if (!strcmp(k, "DIM")) return L"화면 어두워짐";
    if (!strcmp(k, "OFF")) return L"전원 끄기";
    if (!strcmp(k, "RESET")) return L"재시작";
    if (!strcmp(k, "CRIT")) return L"배터리 위험";
    if (!strcmp(k, "BOOT")) return L"부팅";
    if (!strcmp(k, "ACIN")) return L"충전기 연결";
    if (!strcmp(k, "ACOUT")) return L"충전기 분리";
    if (!strcmp(k, "DROP")) return L"깨어날 때 전압 하락";
    return L"기록";
}

/* ------------------------------------------------------------------ */
/* 화면: 처음                                                          */
/* ------------------------------------------------------------------ */
static void draw_main(void)
{
    wchar_t s[300];
    int x = S(6), w = W - S(12), y = S(4), cardH, gy, gh, by, bh, bw, i;
    COLORREF pc;

    /* 머리 */
    text(fHead, C_TEXT, x + S(2), y, w, S(22), APP_TITLE, 0);
    _snwprintf(s, 64, g_logOK ? L"● 기록 중 · %d초마다" : L"● 기록 저장 실패", SAMPLE_MS / 1000);
    text(fSmall, g_logOK ? C_OK : C_BAD, x, y, w - S(2), S(22), s, 2);
    y += S(24);

    /* 지금 상태 카드 */
    cardH = S(70);
    rrect(x, y, w, cardH, S(10), C_CARD);
    pc = g_now.pct < 0 ? C_SUB : g_now.pct <= 15 ? C_BAD : g_now.pct <= 35 ? C_WARN : C_OK;
    if (g_now.ac) pc = C_LINE;
    battery_glyph(x + S(10), y + S(12), S(34), S(18), g_now.pct, pc);
    if (g_now.pct >= 0) _snwprintf(s, 16, L"%d%%", g_now.pct); else wcscpy(s, L"--");
    text(fHuge, C_TEXT, x + S(8), y + S(32), S(80), S(34), s, 0);
    if (g_now.mv > 0) _snwprintf(s, 32, L"%d.%03d V", g_now.mv / 1000, g_now.mv % 1000);
    else wcscpy(s, L"전압 정보 없음");
    text(g_now.mv > 0 ? fBig : fBold, g_now.mv > 0 && g_now.mv < LOW_MV ? C_BAD : C_TEXT,
         x + S(92), y + S(6), w - S(100), S(28), s, 2);
    {
        wchar_t a[40], t[24];
        if (g_now.ac) wcscpy(a, L"충전기 연결됨");
        else if (g_now.ma) _snwprintf(a, 40, L"%d mA 사용", g_now.ma < 0 ? -g_now.ma : g_now.ma);
        else wcscpy(a, L"배터리 사용");
        if (g_now.temp > 0) _snwprintf(t, 24, L" · %d.%d°C", g_now.temp / 10, g_now.temp % 10);
        else t[0] = 0;
        _snwprintf(s, 80, L"%s%s", a, t);
        text(fSmall, C_SUB, x + S(92), y + S(36), w - S(100), S(16), s, 2);
    }
    if (g_dropN > 0 && g_dropLast >= 0) {
        _snwprintf(s, 80, L"깨울 때 하락 최대 %dmV", g_dropMax);
        text(fSmall, g_dropMax >= DROP_WARN_MV ? C_WARN : C_SUB, x + S(92), y + S(52), w - S(100), S(16), s, 2);
    }
    y += cardH + S(5);

    /* 그래프 */
    gh = S(54);
    rrect(x, y, w, gh, S(10), C_CARD);
    gy = y;
    {
        int lo, hi, gx0 = x + S(8), gx1 = x + w - S(8), gt = gy + S(16), gb = gy + gh - S(6);
        int volt = g_hasVolt;
        text(fSmall, C_SUB, x + S(8), gy + S(1), w - S(16), S(15),
             volt ? L"전압 (최근 30분)" : L"잔량 (최근 30분)", 0);
        if (volt) { lo = 3300; hi = 4300; } else { lo = 0; hi = 100; }
        text(fSmall, C_DIM, x + S(8), gy + S(1), w - S(16), S(15), volt ? L"3.3 ~ 4.3V" : L"0 ~ 100%", 2);
        if (volt) {
            int ly = gb - (LOW_MV - lo) * (gb - gt) / (hi - lo);
            line(gx0, ly, gx1, ly, 1, RGB(90, 40, 40));
        }
        if (g_histN > 1) {
            POINT pts[HIST];
            int n = 0;
            for (i = 0; i < g_histN; i++) {
                int v = g_hist[i];
                if (v < lo) v = lo;
                if (v > hi) v = hi;
                pts[n].x = gx0 + (gx1 - gx0) * (HIST - g_histN + i) / (HIST - 1);
                pts[n].y = gb - (v - lo) * (gb - gt) / (hi - lo);
                n++;
            }
            {
                HPEN pen = CreatePen(PS_SOLID, S(2), C_LINE), op;
                op = (HPEN)SelectObject(g_dc, pen);
                Polyline(g_dc, pts, n);
                SelectObject(g_dc, op);
                DeleteObject(pen);
            }
            for (i = 0; i < g_histN; i++)
                if (g_histMark[i]) fill(pts[i].x - S(1), pts[i].y - S(3), S(3), S(6), C_ACCENT);
        } else {
            text(fSmall, C_DIM, x, gt, w, gb - gt, L"기록이 쌓이면 여기에 그래프가 나옵니다", 1);
        }
    }
    y += gh + S(5);

    /* 판정 카드 */
    bh = S(30);
    by = H - bh - S(5);
    {
        int ch = by - S(5) - y;
        wchar_t head[80];
        COLORREF hc;
        rrect(x, y, w, ch, S(10), C_CARD);
        hit_add(x, y, w, ch, B_LOG);
        if (g_crN > 0) {
            const Rec *c = &g_cr[g_crN - 1];
            wchar_t t[24];
            short_time(c->xs, t, 24);
            hc = crash_color(c->x1);
            if (g_newCrash) _snwprintf(head, 80, L"방금 갑자기 꺼졌습니다 (%s)", t);
            else _snwprintf(head, 80, L"갑자기 꺼짐 %d번 · 최근 %s", g_crTotal, t);
            crash_text(c, s, 300, 1);
        } else {
            hc = C_OK;
            wcscpy(head, L"아직 갑자기 꺼진 기록 없음");
            if (g_dropN > 0 && g_dropMax >= DROP_WARN_MV)
                _snwprintf(s, 300, L"깨어날 때 전압이 최대 %d.%02dV 떨어졌습니다. 배터리가 낡았다는 신호입니다.",
                           g_dropMax / 1000, (g_dropMax % 1000) / 10);
            else
                wcscpy(s, L"이 앱을 켜 둔 채(숨기기) 평소처럼 쓰세요. 갑자기 꺼지면 다시 켰을 때 원인을 여기에 알려 드립니다.");
        }
        text(fBold, hc, x + S(8), y + S(4), w - S(16), S(18), head, 0);
        {
            HRGN rg = CreateRectRgn(x, y, x + w, y + ch - S(3));
            SelectClipRgn(g_dc, rg);
            para(fSmall, C_TEXT, x + S(8), y + S(23), w - S(16), s, 1);
            SelectClipRgn(g_dc, NULL);
            DeleteObject(rg);
        }
    }

    /* 단추 */
    bw = (w - S(12)) / 4;
    button(x, by, bw, bh, B_AUTO, autostart_on() ? L"자동 켜짐" : L"자동 꺼짐", C_CARD, autostart_on() ? C_OK : C_SUB);
    button(x + (bw + S(4)), by, bw, bh, B_LOG, L"기록", C_CARD, C_TEXT);
    button(x + (bw + S(4)) * 2, by, bw, bh, B_HIDE, L"숨기기", C_CARD, C_TEXT);
    button(x + (bw + S(4)) * 3, by, w - (bw + S(4)) * 3, bh, B_QUIT, L"종료", C_CARD, C_ACCENT);
}

/* ------------------------------------------------------------------ */
/* 화면: 기록                                                          */
/* ------------------------------------------------------------------ */
static void draw_log(void)
{
    wchar_t s[300], t[24];
    int x = S(6), w = W - S(12), y, top = S(32), bot = H - S(40), i, bw;
    HRGN rg;

    text(fHead, C_TEXT, x + S(2), S(4), w, S(22), L"기록", 0);
    _snwprintf(s, 64, L"갑자기 꺼짐 %d번", g_crTotal);
    text(fSmall, g_crTotal ? C_ACCENT : C_SUB, x, S(4), w - S(2), S(22), s, 2);

    rg = CreateRectRgn(0, top, W, bot);
    SelectClipRgn(g_dc, rg);
    y = top - g_scroll;

    /* 갑자기 꺼짐 (최근 것부터) */
    for (i = g_crN - 1; i >= 0; i--) {
        const Rec *c = &g_cr[i];
        int ph;
        short_time(c->xs, t, 24);
        crash_text(c, s, 300, 0);
        ph = para(fSmall, C_TEXT, x + S(8), 0, w - S(16), s, 0);
        rrect(x, y, w, ph + S(28), S(10), C_CARD);
        {
            wchar_t h[64];
            _snwprintf(h, 64, L"갑자기 꺼짐 · %s", t);
            text(fBold, crash_color(c->x1), x + S(8), y + S(3), w - S(16), S(20), h, 0);
        }
        para(fSmall, C_TEXT, x + S(8), y + S(23), w - S(16), s, 1);
        y += ph + S(28) + S(5);
    }

    /* 전원 사건 (최근 것부터) */
    text(fSmall, C_SUB, x + S(4), y, w, S(18), L"최근 전원 사건", 0);
    y += S(19);
    if (g_evN == 0) {
        text(fSmall, C_DIM, x + S(4), y, w, S(18), L"아직 없음", 0);
        y += S(20);
    }
    for (i = 0; i < g_evN; i++) {
        const Rec *r = &g_ev[(g_evHead - 1 - i + EVN * 2) % EVN];
        wchar_t v[48];
        short_time(r->t, t, 24);
        text(fSmall, C_SUB, x + S(4), y, S(76), S(18), t, 0);
        text(fSmall, !strcmp(r->kind, "CRASH") ? C_ACCENT : C_TEXT, x + S(80), y, w - S(160), S(18), kind_name(r->kind), 0);
        if (!strcmp(r->kind, "DROP")) _snwprintf(v, 48, L"-%d mV", r->x1);
        else if (r->mv > 0) _snwprintf(v, 48, L"%d.%02dV %d%%", r->mv / 1000, (r->mv % 1000) / 10, r->pct);
        else if (r->pct > 0) _snwprintf(v, 48, L"%d%%", r->pct);
        else v[0] = 0;
        text(fSmall, C_SUB, x + w - S(84), y, S(80), S(18), v, 2);
        y += S(19);
        fill(x + S(4), y - S(1), w - S(8), 1, C_SEP);
    }
    y += S(6);
    if (g_rawOK == 1)
        _snwprintf(s, 200, L"기기가 주는 값: 전압 %lu · 전류 %ld · 온도 %lu · 종류 %d%s",
                   (unsigned long)g_rawV, (long)(LONG)g_rawC, (unsigned long)g_rawT, g_rawChem,
                   g_hasVolt ? L"" : L" (전압을 안 알려 주는 기기라 잔량으로 판정합니다)");
    else
        wcscpy(s, L"기기가 배터리 자세한 정보를 주지 않습니다 (잔량으로 판정합니다)");
    y += para(fSmall, C_DIM, x + S(4), y, w - S(8), s, 1) + S(6);
    para(fSmall, C_DIM, x + S(4), y, w - S(8), L"기록 파일: 앱 폴더의 M480Batt_log.csv (PC에서 엑셀로 열 수 있음)", 1);
    y += S(32);
    g_contentH = y + g_scroll - top;

    SelectClipRgn(g_dc, NULL);
    DeleteObject(rg);

    bw = (w - S(4)) / 2;
    button(x, H - S(35), bw, S(30), B_BACK, L"← 돌아가기", C_CARD, C_TEXT);
    button(x + bw + S(4), H - S(35), w - bw - S(4), S(30), B_CLEAR,
           g_clearArm && GetTickCount() - g_clearArm < 3000 ? L"한 번 더 누르면 지움" : L"기록 지우기", C_CARD, C_BAD);
}

static void paint(HDC hdc)
{
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H), ob = (HBITMAP)SelectObject(mem, bmp);
    g_dc = mem;
    g_hitN = 0;
    SetBkMode(mem, TRANSPARENT);
    fill(0, 0, W, H, C_BG);
    if (g_scr == 0) draw_main(); else draw_log();
    if (g_toast[0] && GetTickCount() < g_toastUntil) {
        int tw = W - S(40), th = S(30);
        rrect((W - tw) / 2, H / 2 - th / 2, tw, th, S(10), RGB(60, 60, 70));
        text(fBold, C_TEXT, (W - tw) / 2, H / 2 - th / 2, tw, th, g_toast, 1);
    }
    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
}

/* ------------------------------------------------------------------ */
/* 동작                                                                */
/* ------------------------------------------------------------------ */
/* 화면 아래 가운데 키보드(가) 단추가 아래 단추를 가리지 않게: 앞에 있을 때만 숨김 */
static void sip_button(int hide)
{
    HWND sb;
    if (pFullScreen) pFullScreen(g_hwnd, hide ? 0x0008 /* SHFS_HIDESIPBUTTON */ : 0x0004 /* SHFS_SHOWSIPBUTTON */);
    if (hide) {
        sb = FindWindowW(L"MS_SIPBUTTON", NULL);
        if (sb && IsWindowVisible(sb)) { ShowWindow(sb, SW_HIDE); g_sipHidden = sb; }
    } else if (g_sipHidden) {
        if (IsWindow(g_sipHidden)) ShowWindow(g_sipHidden, SW_SHOW);
        g_sipHidden = NULL;
    }
}

static void hide_me(void)
{
    sip_button(0);
    ShowWindow(g_hwnd, SW_HIDE);
}

static void quit_app(void)
{
    Meas m;
    sip_button(0);
    measure(&m, FALSE);
    log_kind("END", &m);
    PostQuitMessage(0);
}

static void clear_log(void)
{
    DeleteFileW(g_logPath);
    DeleteFileW(g_oldPath);
    g_crN = g_crTotal = 0;
    g_evN = g_evHead = 0;
    g_dropN = g_dropMax = 0; g_dropLast = -1;
    g_histN = 0;
    g_newCrash = 0;
    log_kind("START", &g_now);
    toast(L"기록을 지웠습니다");
}

static void act(int id)
{
    switch (id) {
    case B_AUTO:
        autostart_set(!autostart_on());
        toast(autostart_on() ? L"켤 때 자동으로 기록 시작" : L"자동 시작 끔");
        break;
    case B_LOG: g_scr = 1; g_scroll = 0; g_newCrash = 0; redraw(); break;
    case B_HIDE: hide_me(); break;
    case B_QUIT: quit_app(); break;
    case B_BACK: g_scr = 0; redraw(); break;
    case B_CLEAR:
        if (g_clearArm && GetTickCount() - g_clearArm < 3000) { g_clearArm = 0; clear_log(); }
        else { g_clearArm = GetTickCount(); SetTimer(g_hwnd, T_TOAST, 300, NULL); }
        redraw();
        break;
    }
}

static int hit_at(int x, int y)
{
    int i;
    for (i = g_hitN - 1; i >= 0; i--) {
        RECT *r = &g_hit[i].r;
        if (x >= r->left && x < r->right && y >= r->top && y < r->bottom) return g_hit[i].id;
    }
    return -1;
}

static void clamp_scroll(void)
{
    int view = H - S(40) - S(32);
    int mx = g_contentH - view;
    if (mx < 0) mx = 0;
    if (g_scroll > mx) g_scroll = mx;
    if (g_scroll < 0) g_scroll = 0;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        W = LOWORD(lp); H = HIWORD(lp);
        redraw();
        return 0;
    case WM_TIMER:
        if (wp == T_SAMPLE) on_sample();
        else if (wp == T_FAST) on_fast();
        else if (wp == 4) { KillTimer(hwnd, 4); if (GetForegroundWindow() == hwnd) sip_button(1); }
        else if (wp == T_TOAST) {
            DWORD now = GetTickCount();
            int busy = (g_toast[0] && now < g_toastUntil) || (g_clearArm && now - g_clearArm < 3000);
            if (!busy) { KillTimer(hwnd, T_TOAST); g_toast[0] = 0; g_clearArm = 0; }
            redraw();
        }
        return 0;
    case WM_POWEREV:
        on_power((DWORD)wp, (DWORD)lp);
        return 0;
    case WM_SHOWME:
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        sip_button(1);
        redraw();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) { sip_button(1); SetTimer(hwnd, 4, 300, NULL); }
        else sip_button(0);
        break;
    case WM_LBUTTONDOWN:
        g_down = hit_at((short)LOWORD(lp), (short)HIWORD(lp));
        g_downY = (short)HIWORD(lp);
        g_scroll0 = g_scroll;
        g_dragged = 0;
        SetCapture(hwnd);
        redraw();
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == hwnd && g_scr == 1) {
            int dy = (short)HIWORD(lp) - g_downY;
            if (dy > S(6) || dy < -S(6)) g_dragged = 1;
            if (g_dragged) { g_scroll = g_scroll0 - dy; clamp_scroll(); redraw(); }
        }
        return 0;
    case WM_LBUTTONUP: {
        int id = hit_at((short)LOWORD(lp), (short)HIWORD(lp));
        ReleaseCapture();
        if (!g_dragged && id > 0 && id == g_down) act(id);
        g_down = -1;
        redraw();
        return 0;
    }
    case WM_KEYDOWN:
        switch (wp) {
        case VK_TSOFT1: act(g_scr ? B_BACK : B_LOG); break;
        case VK_TSOFT2: act(B_HIDE); break;
        case VK_ESCAPE: case VK_BACK:
            if (g_scr) act(B_BACK); else act(B_HIDE);
            break;
        case VK_UP: if (g_scr) { g_scroll -= S(40); clamp_scroll(); redraw(); } break;
        case VK_DOWN: if (g_scr) { g_scroll += S(40); clamp_scroll(); redraw(); } break;
        case VK_RETURN: if (!g_scr) act(B_LOG); break;
        }
        return 0;
    case WM_CLOSE:
        hide_me();
        return 0;
    case WM_DESTROY:
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
    int bg = cmd && wcsstr(cmd, L"/bg") != NULL;
    (void)hPrev;

    prev = FindWindowW(APP_CLASS, NULL);
    if (prev) {
        if (!bg) PostMessageW(prev, WM_SHOWME, 0, 0);
        return 0;
    }
    g_inst = hInst;

    GetModuleFileNameW(NULL, g_exePath, MAX_PATH);
    wcscpy(g_exeDir, g_exePath);
    slash = wcsrchr(g_exeDir, L'\\');
    if (slash) slash[1] = 0; else wcscpy(g_exeDir, L"\\");
    _snwprintf(g_logPath, MAX_PATH, L"%sM480Batt_log.csv", g_exeDir);
    _snwprintf(g_oldPath, MAX_PATH, L"%sM480Batt_old.csv", g_exeDir);

    sdc = GetDC(NULL);
    g_scale = GetDeviceCaps(sdc, LOGPIXELSY) >= 150 ? 2 : 1;
    ReleaseDC(NULL, sdc);
    fHuge = make_font(S(30), 1);
    fBig = make_font(S(22), 1);
    fHead = make_font(S(17), 1);
    fBold = make_font(S(13), 1);
    fNorm = make_font(S(13), 0);
    fSmall = make_font(S(12), 0);

    load_funcs();
    load_log();
    check_crash();
#ifdef PREVIEW_CRASH
    {
        Rec c;
        make_rec(&c, "CRASH", NULL);
        c.pct = 48; c.mv = 3412; c.x1 = PREVIEW_CRASH;
        strcpy(c.xs, "2026-11-26 08:41:07");
        cr_push(&c); ev_push(&c);
        g_newCrash = 1;
    }
#endif

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hbrBackground = NULL;
    wc.lpszClassName = APP_CLASS;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
#ifdef PREVIEW_W
    {
        RECT r;
        r.left = 0; r.top = 0; r.right = PREVIEW_W; r.bottom = PREVIEW_H;
        AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
        g_hwnd = CreateWindowW(APP_CLASS, APP_TITLE, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                               0, 0, r.right - r.left, r.bottom - r.top, NULL, NULL, hInst, NULL);
    }
#else
    g_hwnd = CreateWindowW(APP_CLASS, APP_TITLE, WS_CLIPCHILDREN | (bg ? 0 : WS_VISIBLE),
                           wa.left, wa.top, wa.right - wa.left, wa.bottom - wa.top, NULL, NULL, hInst, NULL);
#endif
    if (!g_hwnd) return 0;

    measure(&g_now, TRUE);
    if (g_now.mv > 0) { g_hasVolt = 1; g_lastMvBeforeSleep = g_now.mv; }
    log_kind("START", &g_now);
    hist_push(g_now.mv > 0 ? g_now.mv : (g_now.pct >= 0 ? g_now.pct : 0), 0);
#ifdef PREVIEW_W
    {
        int i;
        for (i = 0; i < 120; i++) { Meas t; measure(&t, FALSE); hist_push(t.mv, (i % 50) == 7); }
        g_dropN = 3; g_dropLast = 120; g_dropMax = 330;
        if (PREVIEW_SCR == 1) g_scr = 1;
    }
#endif
    power_init();
    SetTimer(g_hwnd, T_SAMPLE, SAMPLE_MS, NULL);

    if (!bg) {
        ShowWindow(g_hwnd, show ? show : SW_SHOW);
        UpdateWindow(g_hwnd);
    }

    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
