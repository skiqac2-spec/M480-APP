/*
 * M480Call - SCH-M480(WM6.1)을 스마트폰의 블루투스 핸즈프리(HFP)로 쓰는 앱
 * 0.1: 테스트판 - 연결 / 통화 제어 / 음성(SCO) 경로를 확인하고 기록을 남김
 *
 * 구조
 *  - 블루투스 RFCOMM 소켓으로 폰(오디오 게이트웨이)에 연결해 AT 명령으로 통화 제어
 *  - 폰 쪽에서 연결해 올 수 있게 핸즈프리 SDP 레코드 등록 + 대기 소켓
 *  - 음성은 SCO 링크(btdrt.dll) + 파형 장치 중계로 시험
 *  - 모든 과정은 exe 옆 M480Call_log.txt 에 기록
 */
#include <windows.h>
#include <winsock2.h>
#include <mmsystem.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define APP_CLASS L"M480CallWnd"
#define APP_TITLE L"M480 통화"
#define APP_VER   L"0.2 테스트판"

#define WM_APP_REFRESH (WM_APP + 1)
#define WM_APP_RING    (WM_APP + 2)

#ifndef VK_TTALK
#define VK_TTALK VK_F3
#endif
#ifndef VK_TEND
#define VK_TEND VK_F4
#endif

typedef unsigned long long BTADDR;

/* ------------------------------------------------------------------ */
/* 전역 상태                                                            */
/* ------------------------------------------------------------------ */
static HWND g_hwnd;
static HINSTANCE g_inst;
static wchar_t g_exeDir[MAX_PATH];
static DWORD g_t0;
static int g_scale = 1;
static CRITICAL_SECTION g_cs;
static volatile int g_quit;

/* 기기 목록 */
#define MAX_DEV 24
typedef struct { BTADDR a; wchar_t name[64]; int paired; } Dev;
static Dev g_dev[MAX_DEV];
static int g_devN, g_devSel = -1;
static volatile int g_inquiring;

/* 연결 상태 */
enum { ST_IDLE, ST_CONNECTING, ST_SLC, ST_READY, ST_FAIL };
static volatile int g_state = ST_IDLE;
static SOCKET g_sock = INVALID_SOCKET;
static BTADDR g_peer;
static volatile int g_session;      /* 세션 진행 중 */
static volatile int g_stopSession;
static unsigned g_agFeat;
static int g_everReady;             /* 한 번이라도 연결 성공 */
static int g_ctrlOk;                /* 통화 제어 명령이 OK 받음 */
static wchar_t g_ctrlWhat[48];

/* 표시기 (+CIND) */
#define MAX_IND 12
static char g_indName[MAX_IND][16];
static int g_indVal[MAX_IND];
static int g_indN;
static wchar_t g_oper[40];
static char g_clip[40];             /* 걸려 온 번호 */
static DWORD g_callStart;
static int g_inbandRing;

/* 입력 번호 */
static char g_num[32];

/* SCO / 음성 */
typedef int (*PFN_ReadLocalAddr)(BTADDR *);
typedef int (*PFN_CreateSCO)(BTADDR *, unsigned short *);
typedef int (*PFN_CloseConn)(unsigned short);
typedef int (*PFN_AcceptSCO)(BOOL);
typedef int (*PFN_GetBB)(int, void *, int *);
typedef int (*PFN_GetMode)(DWORD *);
typedef int (*PFN_SetMode)(DWORD);
static PFN_ReadLocalAddr pReadLocalAddr;
static PFN_CreateSCO pCreateSCO;
static PFN_CloseConn pCloseConn;
static PFN_AcceptSCO pAcceptSCO;
static PFN_GetBB pGetBB;
static PFN_GetMode pGetMode;
static PFN_SetMode pSetMode;
typedef int (*PFN_ReadCOD)(unsigned int *);
typedef int (*PFN_WriteCOD)(unsigned int);
typedef int (*PFN_SetPIN)(BTADDR *, int, unsigned char *);
typedef int (*PFN_PairReq)(BTADDR *, int, unsigned char *);
static PFN_ReadCOD pReadCOD;
static PFN_WriteCOD pWriteCOD;
static PFN_SetPIN pSetPIN;
static PFN_PairReq pPairReq;
static unsigned int g_origCod;
static int g_codSaved, g_hfMode;
static DWORD g_origMode = 99;
static volatile int g_pairing;
#define HF_COD 0x200408   /* 오디오 서비스 + 오디오/비디오 기기 + 핸즈프리 */
static unsigned short g_scoHandle;
static int g_scoMine;               /* 내가 만든 SCO */
static int g_scoUp, g_scoEver;
static int g_aclN;

static UINT g_nOut, g_nIn;
static int g_audioDev = 1;          /* 중계에 쓸 장치 번호(1부터) */
static int g_bridgeOn;

/* SDP / 대기 소켓 */
typedef struct {
    DWORD dwSize; LPWSTR lpszServiceInstanceName; LPGUID lpServiceClassId; void *lpVersion;
    LPWSTR lpszComment; DWORD dwNameSpace; LPGUID lpNSProviderId; LPWSTR lpszContext;
    DWORD dwNumberOfProtocols; void *lpafpProtocols; LPWSTR lpszQueryString;
    DWORD dwNumberOfCsAddrs; void *lpcsaBuffer; DWORD dwOutputFlags; void *lpBlob;
} MYQS;
typedef struct { ULONG cbSize; BYTE *pBlobData; } MYBLOB;
typedef struct { struct sockaddr *lpSockaddr; INT iSockaddrLength; } MYSOCKADDR;
typedef struct { MYSOCKADDR LocalAddr, RemoteAddr; INT iSocketType, iProtocol; } MYCSADDR;
typedef INT (WINAPI *PFN_SetService)(MYQS *, int, DWORD);
typedef INT (WINAPI *PFN_LBegin)(MYQS *, DWORD, HANDLE *);
typedef INT (WINAPI *PFN_LNext)(HANDLE, DWORD, DWORD *, MYQS *);
typedef INT (WINAPI *PFN_LEnd)(HANDLE);
static PFN_SetService pSetService;
static PFN_LBegin pLBegin;
static PFN_LNext pLNext;
static PFN_LEnd pLEnd;
static ULONG g_sdpHandle;
static int g_sdpOk;
static int g_listenCh;
static SOCKET g_listenSock = INVALID_SOCKET;
static int g_saLayout = 0;          /* 0 = CE(40바이트) 1 = 데스크톱식(30바이트) */

/* 화면 */
enum { SCR_MAIN, SCR_LOG };
static int g_scr = SCR_MAIN;
static int g_logScroll;             /* 위로 올린 줄 수 */
static HFONT fBig, fNorm, fBold, fSmall, fTiny;

/* ------------------------------------------------------------------ */
/* 기록                                                                 */
/* ------------------------------------------------------------------ */
#define LOG_MAX 400
static wchar_t *g_log[LOG_MAX];
static int g_logN;
static HANDLE g_logFile = INVALID_HANDLE_VALUE;

static void redraw(void) { if (g_hwnd) PostMessageW(g_hwnd, WM_APP_REFRESH, 0, 0); }

static void logw(const wchar_t *fmt, ...)
{
    wchar_t buf[400], line[440];
    char u8[1400];
    va_list ap;
    DWORD t = GetTickCount() - g_t0, wr;
    int n;
    va_start(ap, fmt);
    _vsnwprintf(buf, 399, fmt, ap);
    va_end(ap);
    buf[399] = 0;
    _snwprintf(line, 439, L"%3lu.%lu %s", t / 1000, (t % 1000) / 100, buf);
    line[439] = 0;
    EnterCriticalSection(&g_cs);
    if (g_logN == LOG_MAX) {
        free(g_log[0]);
        memmove(g_log, g_log + 1, sizeof(g_log[0]) * (LOG_MAX - 1));
        g_logN--;
    }
    {
        size_t L = (wcslen(line) + 1) * sizeof(wchar_t);
        wchar_t *d = (wchar_t *)malloc(L);
        if (d) memcpy(d, line, L);
        g_log[g_logN++] = d ? d : NULL;
        if (!d) g_logN--;
    }
    if (g_logFile != INVALID_HANDLE_VALUE) {
        n = WideCharToMultiByte(CP_UTF8, 0, line, -1, u8, sizeof(u8) - 3, NULL, NULL);
        if (n > 0) {
            n--;
            u8[n++] = '\r'; u8[n++] = '\n';
            WriteFile(g_logFile, u8, n, &wr, NULL);
            FlushFileBuffers(g_logFile);
        }
    }
    LeaveCriticalSection(&g_cs);
    redraw();
}

static void log_open(void)
{
    wchar_t p[MAX_PATH];
    DWORD wr;
    static const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    wcscpy(p, g_exeDir); wcscat(p, L"M480Call_log.txt");
    g_logFile = CreateFileW(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_logFile != INVALID_HANDLE_VALUE) WriteFile(g_logFile, bom, 3, &wr, NULL);
}

static void addr_str(BTADDR a, wchar_t *out)
{
    _snwprintf(out, 18, L"%02X:%02X:%02X:%02X:%02X:%02X",
               (int)((a >> 40) & 0xFF), (int)((a >> 32) & 0xFF), (int)((a >> 24) & 0xFF),
               (int)((a >> 16) & 0xFF), (int)((a >> 8) & 0xFF), (int)(a & 0xFF));
    out[17] = 0;
}

/* ------------------------------------------------------------------ */
/* 설정 (마지막으로 고른 폰)                                             */
/* ------------------------------------------------------------------ */
static BTADDR g_savedAddr;

static void settings_path(wchar_t *p) { wcscpy(p, g_exeDir); wcscat(p, L"M480Call.ini"); }

static void load_settings(void)
{
    wchar_t p[MAX_PATH];
    char b[64];
    DWORD rd = 0;
    HANDLE h;
    settings_path(p);
    h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    memset(b, 0, sizeof(b));
    ReadFile(h, b, sizeof(b) - 1, &rd, NULL);
    CloseHandle(h);
    {
        int i;
        BTADDR a = 0;
        for (i = 0; b[i]; i++) {
            char c = b[i];
            int v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
            if (v < 0) break;
            a = (a << 4) | (BTADDR)v;
        }
        g_savedAddr = a;
    }
}

static void save_settings(void)
{
    wchar_t p[MAX_PATH];
    char b[64];
    DWORD wr;
    HANDLE h;
    if (g_devSel < 0) return;
    settings_path(p);
    h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    {
        int i;
        BTADDR a = g_dev[g_devSel].a;
        for (i = 0; i < 12; i++) b[i] = "0123456789ABCDEF"[(a >> ((11 - i) * 4)) & 15];
        b[12] = '\r'; b[13] = '\n'; b[14] = 0;
    }
    WriteFile(h, b, strlen(b), &wr, NULL);
    CloseHandle(h);
}

/* ------------------------------------------------------------------ */
/* 블루투스 기본                                                        */
/* ------------------------------------------------------------------ */
static void make_uuid16(unsigned short u, GUID *g)
{
    static const unsigned char base[8] = { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB };
    g->Data1 = u; g->Data2 = 0; g->Data3 = 0x1000;
    memcpy(g->Data4, base, 8);
}

static int make_sa(unsigned char *sa, int layout, BTADDR a, const GUID *g, unsigned long port)
{
    memset(sa, 0, 40);
    sa[0] = 32; /* AF_BT */
    if (layout == 0) {
        memcpy(sa + 8, &a, 8);
        if (g) memcpy(sa + 16, g, 16);
        memcpy(sa + 32, &port, 4);
        return 40;
    }
    memcpy(sa + 2, &a, 8);
    if (g) memcpy(sa + 10, g, 16);
    memcpy(sa + 26, &port, 4);
    return 30;
}

static BTADDR sa_addr(const unsigned char *sa, int len)
{
    BTADDR a = 0;
    memcpy(&a, sa + (len >= 40 ? 8 : 2), 8);
    return a & 0xFFFFFFFFFFFFULL;
}

static unsigned long sa_port(const unsigned char *sa, int len)
{
    unsigned long p = 0;
    memcpy(&p, sa + (len >= 40 ? 32 : 26), 4);
    return p;
}

static void bt_load(void)
{
    HMODULE h;
    h = LoadLibraryW(L"btdrt.dll");
    if (h) {
        pReadLocalAddr = (PFN_ReadLocalAddr)GetProcAddressW(h, L"BthReadLocalAddr");
        pCreateSCO = (PFN_CreateSCO)GetProcAddressW(h, L"BthCreateSCOConnection");
        pCloseConn = (PFN_CloseConn)GetProcAddressW(h, L"BthCloseConnection");
        pAcceptSCO = (PFN_AcceptSCO)GetProcAddressW(h, L"BthAcceptSCOConnections");
        pGetBB = (PFN_GetBB)GetProcAddressW(h, L"BthGetBasebandConnections");
        pReadCOD = (PFN_ReadCOD)GetProcAddressW(h, L"BthReadCOD");
        pWriteCOD = (PFN_WriteCOD)GetProcAddressW(h, L"BthWriteCOD");
        pSetPIN = (PFN_SetPIN)GetProcAddressW(h, L"BthSetPIN");
        pPairReq = (PFN_PairReq)GetProcAddressW(h, L"BthPairRequest");
    }
    logw(L"btdrt.dll %s: 주소 %d, SCO만들기 %d, 끊기 %d, SCO수락 %d, 링크목록 %d",
         h ? L"있음" : L"없음", pReadLocalAddr != NULL, pCreateSCO != NULL, pCloseConn != NULL,
         pAcceptSCO != NULL, pGetBB != NULL);
    logw(L"기기종류 읽기 %d, 쓰기 %d, PIN설정 %d, 페어링요청 %d",
         pReadCOD != NULL, pWriteCOD != NULL, pSetPIN != NULL, pPairReq != NULL);
    h = LoadLibraryW(L"bthutil.dll");
    if (h) {
        pGetMode = (PFN_GetMode)GetProcAddressW(h, L"BthGetMode");
        pSetMode = (PFN_SetMode)GetProcAddressW(h, L"BthSetMode");
    }
    h = LoadLibraryW(L"ws2.dll");
    if (h) {
        pSetService = (PFN_SetService)GetProcAddressW(h, L"WSASetServiceW");
        if (!pSetService) pSetService = (PFN_SetService)GetProcAddressW(h, L"WSASetService");
        pLBegin = (PFN_LBegin)GetProcAddressW(h, L"WSALookupServiceBeginW");
        if (!pLBegin) pLBegin = (PFN_LBegin)GetProcAddressW(h, L"WSALookupServiceBegin");
        pLNext = (PFN_LNext)GetProcAddressW(h, L"WSALookupServiceNextW");
        if (!pLNext) pLNext = (PFN_LNext)GetProcAddressW(h, L"WSALookupServiceNext");
        pLEnd = (PFN_LEnd)GetProcAddressW(h, L"WSALookupServiceEnd");
    }
    logw(L"ws2.dll: SDP등록 %d, 검색 %d/%d/%d", pSetService != NULL, pLBegin != NULL, pLNext != NULL, pLEnd != NULL);
}

static int reg_exists(const wchar_t *path)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, 0, &k) == ERROR_SUCCESS) { RegCloseKey(k); return 1; }
    return 0;
}

static void bt_info(void)
{
    OSVERSIONINFOW ov;
    BTADDR me = 0;
    wchar_t as[20];
    DWORD mode = 99;
    memset(&ov, 0, sizeof(ov)); ov.dwOSVersionInfoSize = sizeof(ov);
    GetVersionExW(&ov);
    logw(L"M480Call %s / OS %lu.%lu.%lu", APP_VER, ov.dwMajorVersion, ov.dwMinorVersion, ov.dwBuildNumber);
    logw(L"블루투스 스택: MS=%d Widcomm=%d Broadcom=%d",
         reg_exists(L"Software\\Microsoft\\Bluetooth"), reg_exists(L"Software\\Widcomm"),
         reg_exists(L"Software\\Broadcom"));
    if (pGetMode && pGetMode(&mode) == 0) {
        g_origMode = mode;
        logw(L"블루투스 모드: %lu (0=꺼짐 1=켜짐 2=검색허용)", mode);
        if (mode == 0 && pSetMode) {
            int r = pSetMode(1);
            logw(L"블루투스 켜기 시도 → %d", r);
            Sleep(1500);
        }
    } else logw(L"블루투스 모드 확인 불가");
    if (pReadLocalAddr && pReadLocalAddr(&me) == 0) {
        addr_str(me, as);
        logw(L"M480 블루투스 주소: %s", as);
    } else logw(L"M480 주소 읽기 실패 (블루투스가 꺼져 있을 수 있음)");
}

/* 아이폰 호환: M480을 '핸즈프리' 기기로 보이게 함 (끝날 때 원래대로) */
static void hf_mode(int on)
{
    if (on) {
        unsigned int c = 0;
        if (pReadCOD && pReadCOD(&c) == 0) {
            if (!g_codSaved) { g_origCod = c; g_codSaved = 1; }
            logw(L"원래 기기 종류: %06X", c);
        }
        if (!pWriteCOD) { logw(L"기기 종류를 바꿀 수 없음 (함수 없음)"); return; }
        {
            int r = pWriteCOD(HF_COD);
            logw(L"기기 종류를 핸즈프리(%06X)로 → %d", HF_COD, r);
            g_hfMode = (r == 0);
        }
        if (pSetMode) logw(L"다른 기기에서 보이기 켜기 → %d", pSetMode(2));
        if (pReadCOD && pReadCOD(&c) == 0) logw(L"지금 기기 종류: %06X", c);
    } else {
        if (g_codSaved && pWriteCOD) logw(L"기기 종류 원래대로 → %d", pWriteCOD(g_origCod));
        if (g_origMode <= 2 && pSetMode) pSetMode(g_origMode);
        g_hfMode = 0;
    }
    redraw();
}

/* 페어링된 기기: HKLM\Software\Microsoft\Bluetooth\Device\<주소> */
static void load_paired(void)
{
    HKEY k, sk;
    DWORD i, len, type, sz;
    wchar_t kn[64], nm[64], as[20];
    g_devN = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Bluetooth\\Device", 0, 0, &k) != ERROR_SUCCESS) {
        logw(L"페어링 목록 레지스트리 없음");
        return;
    }
    for (i = 0; g_devN < MAX_DEV; i++) {
        len = 64;
        if (RegEnumKeyExW(k, i, kn, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        {
            BTADDR a = 0;
            int j, ok = 0;
            for (j = 0; kn[j]; j++) {
                wchar_t c = kn[j];
                int v = -1;
                if (c >= L'0' && c <= L'9') v = c - L'0';
                else if (c >= L'a' && c <= L'f') v = c - L'a' + 10;
                else if (c >= L'A' && c <= L'F') v = c - L'A' + 10;
                if (v < 0) continue;
                a = (a << 4) | (BTADDR)v; ok++;
            }
            if (ok < 12) continue;
            nm[0] = 0;
            if (RegOpenKeyExW(k, kn, 0, 0, &sk) == ERROR_SUCCESS) {
                sz = sizeof(nm);
                if (RegQueryValueExW(sk, L"name", NULL, &type, (LPBYTE)nm, &sz) != ERROR_SUCCESS || type != REG_SZ) nm[0] = 0;
                nm[63] = 0;
                RegCloseKey(sk);
            }
            g_dev[g_devN].a = a & 0xFFFFFFFFFFFFULL;
            if (nm[0]) wcscpy(g_dev[g_devN].name, nm);
            else { addr_str(a, as); wcscpy(g_dev[g_devN].name, as); }
            g_dev[g_devN].paired = 1;
            addr_str(a, as);
            logw(L"페어링된 기기 %d: %s (%s)", g_devN + 1, g_dev[g_devN].name, as);
            g_devN++;
        }
    }
    RegCloseKey(k);
    if (!g_devN) logw(L"페어링된 기기 없음 - 먼저 블루투스 설정에서 폰과 페어링하세요");
}

static void pick_saved(void)
{
    int i;
    g_devSel = g_devN ? 0 : -1;
    for (i = 0; i < g_devN; i++) if (g_dev[i].a == g_savedAddr) g_devSel = i;
}

/* M480에서 페어링 요청 (PIN 0000) */
static void load_paired(void);
static DWORD WINAPI pair_thread(LPVOID p)
{
    BTADDR a = *(BTADDR *)p;
    unsigned char pin[4] = { '0', '0', '0', '0' };
    wchar_t as[20];
    int r;
    free(p);
    addr_str(a, as);
    logw(L"%s 에 페어링 요청 (PIN 0000) - 폰에 입력창이 뜨면 0000 입력", as);
    if (pSetPIN) logw(L"PIN 미리 설정 → %d", pSetPIN(&a, 4, pin));
    if (!pPairReq) { logw(L"페어링 요청 함수 없음 - 폰 쪽에서 M480을 눌러 주세요"); g_pairing = 0; redraw(); return 0; }
    SetLastError(0);
    r = pPairReq(&a, 4, pin);
    if (r == 0) {
        int i;
        logw(L"[성공] 페어링 완료");
        EnterCriticalSection(&g_cs);
        for (i = 0; i < g_devN; i++) if (g_dev[i].a == a) g_dev[i].paired = 1;
        LeaveCriticalSection(&g_cs);
    } else logw(L"[실패] 페어링 실패 (결과 %d, 오류 %lu)", r, GetLastError());
    g_pairing = 0;
    redraw();
    return 0;
}

/* 주변 기기 찾기 */
static DWORD WINAPI inquiry_thread(LPVOID p)
{
    MYQS qs;
    HANDLE h;
    union { MYQS q; unsigned char b[4096]; } res;
    DWORD len;
    int found = 0;
    (void)p;
    logw(L"주변 기기 찾는 중... (10초쯤 걸림, 폰은 '검색 허용' 상태여야 함)");
    if (!pLBegin || !pLNext || !pLEnd) { logw(L"검색 함수 없음"); g_inquiring = 0; redraw(); return 0; }
    memset(&qs, 0, sizeof(qs));
    qs.dwSize = sizeof(qs);
    qs.dwNameSpace = 16; /* NS_BTH */
    if (pLBegin(&qs, 0x0002 /*LUP_CONTAINERS*/, &h) != 0) {
        logw(L"검색 시작 실패 (오류 %d)", WSAGetLastError());
        g_inquiring = 0; redraw(); return 0;
    }
    for (;;) {
        len = sizeof(res);
        memset(&res, 0, sizeof(res));
        res.q.dwSize = sizeof(MYQS);
        if (pLNext(h, 0x0010 | 0x0100 /*NAME|ADDR*/, &len, &res.q) != 0) break;
        if (res.q.lpcsaBuffer) {
            MYCSADDR *ca = (MYCSADDR *)res.q.lpcsaBuffer;
            BTADDR a = sa_addr((unsigned char *)ca->RemoteAddr.lpSockaddr, ca->RemoteAddr.iSockaddrLength);
            int i, dup = -1;
            wchar_t as[20];
            for (i = 0; i < g_devN; i++) if (g_dev[i].a == a) dup = i;
            addr_str(a, as);
            logw(L"발견: %s (%s)", res.q.lpszServiceInstanceName ? res.q.lpszServiceInstanceName : L"?", as);
            found++;
            EnterCriticalSection(&g_cs);
            if (dup < 0 && g_devN < MAX_DEV) {
                g_dev[g_devN].a = a;
                g_dev[g_devN].paired = 0;
                if (res.q.lpszServiceInstanceName && res.q.lpszServiceInstanceName[0]) {
                    wcsncpy(g_dev[g_devN].name, res.q.lpszServiceInstanceName, 63);
                    g_dev[g_devN].name[63] = 0;
                } else wcscpy(g_dev[g_devN].name, as);
                if (g_devSel < 0) g_devSel = g_devN;
                g_devN++;
            }
            LeaveCriticalSection(&g_cs);
        }
    }
    pLEnd(h);
    logw(L"찾기 끝: %d대", found);
    g_inquiring = 0;
    redraw();
    return 0;
}

/* ------------------------------------------------------------------ */
/* AT 명령 세션 (HFP 핸즈프리 쪽)                                        */
/* ------------------------------------------------------------------ */
static char g_rbuf[1024];
static int g_rlen;

/* 1줄 읽기: 1=줄, 0=시간 초과, -1=끊김 */
static int read_line(SOCKET s, char *out, int max, int timeout_ms)
{
    DWORD t0 = GetTickCount();
    for (;;) {
        int i;
        /* 버퍼에서 줄 찾기 */
        for (i = 0; i < g_rlen; i++) {
            if (g_rbuf[i] == '\r' || g_rbuf[i] == '\n') {
                int n = i < max - 1 ? i : max - 1;
                memcpy(out, g_rbuf, n); out[n] = 0;
                memmove(g_rbuf, g_rbuf + i + 1, g_rlen - i - 1);
                g_rlen -= i + 1;
                if (n > 0) return 1;
                i = -1;
            }
        }
        if (g_rlen >= (int)sizeof(g_rbuf) - 1) g_rlen = 0;
        {
            fd_set fs;
            struct timeval tv;
            int left = timeout_ms - (int)(GetTickCount() - t0), r;
            if (left <= 0 || g_stopSession) return 0;
            if (left > 500) left = 500;
            FD_ZERO(&fs); FD_SET(s, &fs);
            tv.tv_sec = left / 1000; tv.tv_usec = (left % 1000) * 1000;
            r = select(0, &fs, NULL, NULL, &tv);
            if (r < 0) return -1;
            if (r == 0) continue;
            r = recv(s, g_rbuf + g_rlen, sizeof(g_rbuf) - 1 - g_rlen, 0);
            if (r <= 0) return -1;
            g_rlen += r;
        }
    }
}

static int send_raw(SOCKET s, const char *cmd)
{
    wchar_t w[80];
    int n = strlen(cmd), r;
    MultiByteToWideChar(CP_ACP, 0, cmd, -1, w, 80); w[79] = 0;
    { wchar_t *e = wcschr(w, L'\r'); if (e) *e = 0; }
    logw(L"→ %s", w);
    r = send(s, cmd, n, 0);
    if (r != n) { logw(L"보내기 실패 (오류 %d)", WSAGetLastError()); return 0; }
    return 1;
}

static int ind_index(const char *name)
{
    int i;
    for (i = 0; i < g_indN; i++) if (!strcmp(g_indName[i], name)) return i;
    return -1;
}

static int ind_get(const char *name)
{
    int i = ind_index(name);
    return i >= 0 ? g_indVal[i] : -1;
}

static int g_cindMode; /* 1 = 목록(=?) 2 = 값(?) */

static void handle_line(const char *ln)
{
    wchar_t w[300];
    MultiByteToWideChar(CP_UTF8, 0, ln, -1, w, 300); w[299] = 0;
    logw(L"← %s", w);

    if (!strncmp(ln, "+BRSF:", 6)) {
        g_agFeat = (unsigned)atoi(ln + 6);
        logw(L"폰 기능: %u (3자통화 %d, 인밴드벨 %d, 통화목록 %d, 코덱선택 %d)", g_agFeat,
             (g_agFeat & 1) != 0, (g_agFeat & 8) != 0, (g_agFeat & 64) != 0, (g_agFeat & 512) != 0);
        g_inbandRing = (g_agFeat & 8) != 0;
    } else if (!strncmp(ln, "+CIND:", 6)) {
        const char *p = ln + 6;
        if (g_cindMode == 1) {
            g_indN = 0;
            while ((p = strchr(p, '"')) != NULL && g_indN < MAX_IND) {
                const char *e = strchr(p + 1, '"');
                int n;
                if (!e) break;
                n = (int)(e - p - 1); if (n > 15) n = 15;
                memcpy(g_indName[g_indN], p + 1, n); g_indName[g_indN][n] = 0;
                g_indVal[g_indN] = 0;
                g_indN++;
                p = strchr(e, ')');
                if (!p) break;
                p = strchr(p + 1, ')');   /* ("name",(0-1)) 의 바깥 괄호 */
                if (!p) break;
            }
        } else {
            int i = 0;
            while (*p && i < MAX_IND) {
                while (*p == ' ') p++;
                g_indVal[i++] = atoi(p);
                p = strchr(p, ',');
                if (!p) break;
                p++;
            }
        }
    } else if (!strncmp(ln, "+CIEV:", 6)) {
        int idx = atoi(ln + 6) - 1, v;
        const char *c = strchr(ln, ',');
        v = c ? atoi(c + 1) : 0;
        if (idx >= 0 && idx < g_indN) {
            int old = g_indVal[idx];
            g_indVal[idx] = v;
            if (!strcmp(g_indName[idx], "call")) {
                if (v == 1 && old != 1) { g_callStart = GetTickCount(); logw(L"[통화 시작]"); }
                if (v == 0 && old == 1) { logw(L"[통화 끝]"); g_clip[0] = 0; }
            }
            if (!strcmp(g_indName[idx], "callsetup") && v == 0 && ind_get("call") != 1) g_clip[0] = 0;
        }
    } else if (!strcmp(ln, "RING")) {
        PostMessageW(g_hwnd, WM_APP_RING, 0, 0);
    } else if (!strncmp(ln, "+CLIP:", 6)) {
        const char *p = strchr(ln, '"'), *e;
        if (p && (e = strchr(p + 1, '"')) != NULL) {
            int n = (int)(e - p - 1); if (n > 38) n = 38;
            memcpy(g_clip, p + 1, n); g_clip[n] = 0;
        }
    } else if (!strncmp(ln, "+COPS:", 6)) {
        const char *p = strchr(ln, '"'), *e;
        if (p && (e = strchr(p + 1, '"')) != NULL) {
            char t[40];
            int n = (int)(e - p - 1); if (n > 38) n = 38;
            memcpy(t, p + 1, n); t[n] = 0;
            MultiByteToWideChar(CP_UTF8, 0, t, -1, g_oper, 40); g_oper[39] = 0;
        }
    } else if (!strncmp(ln, "+BSIR:", 6)) {
        g_inbandRing = atoi(ln + 6);
    } else if (!strncmp(ln, "+BCS:", 5)) {
        /* 코덱 선택: 우리는 CVSD(1)만 지원 */
        char b[24];
        sprintf(b, "AT+BCS=%d\r", atoi(ln + 5));
        send_raw(g_sock, b);
    }
    redraw();
}

/* 명령 보내고 OK/ERROR 기다림: 1=OK 0=ERROR -1=시간초과 -2=끊김 */
static int at_cmd(SOCKET s, const char *cmd, int timeout_ms)
{
    char ln[300];
    DWORD t0 = GetTickCount();
    if (!send_raw(s, cmd)) return -2;
    for (;;) {
        int left = timeout_ms - (int)(GetTickCount() - t0), r;
        if (left <= 0) { logw(L"(응답 없음)"); return -1; }
        r = read_line(s, ln, sizeof(ln), left);
        if (r < 0) return -2;
        if (r == 0) { if (g_stopSession) return -2; continue; }
        if (!strcmp(ln, "OK")) { logw(L"← OK"); return 1; }
        if (!strcmp(ln, "ERROR") || !strncmp(ln, "+CME ERROR", 10)) { logw(L"← %hs", ln); return 0; }
        handle_line(ln);
    }
}

/* UI에서 보내는 명령 (세션 스레드가 응답을 받음) */
static void ui_send(const char *cmd, const wchar_t *what)
{
    if (g_state != ST_READY || g_sock == INVALID_SOCKET) { logw(L"연결 안 됨 - 명령 취소"); return; }
    if (what) { wcsncpy(g_ctrlWhat, what, 47); g_ctrlWhat[47] = 0; }
    EnterCriticalSection(&g_cs);
    send_raw(g_sock, cmd);
    LeaveCriticalSection(&g_cs);
}

static void sco_close(void);
static void bridge_stop(void);

static void run_session(SOCKET s, BTADDR peer)
{
    char ln[300];
    int r;
    wchar_t as[20];
    g_sock = s;
    g_peer = peer;
    g_rlen = 0;
    g_state = ST_SLC;
    g_indN = 0; g_agFeat = 0; g_clip[0] = 0; g_oper[0] = 0;
    addr_str(peer, as);
    logw(L"RFCOMM 연결됨 (%s) - 핸즈프리 연결 절차 시작", as);
    redraw();

    r = at_cmd(s, "AT+BRSF=52\r", 6000);
    if (r < 0) { logw(L"폰이 AT+BRSF에 답하지 않음 - 핸즈프리 서비스가 아닐 수 있음"); goto fail; }
    g_cindMode = 1;
    if (at_cmd(s, "AT+CIND=?\r", 5000) < 0) goto fail;
    g_cindMode = 2;
    if (at_cmd(s, "AT+CIND?\r", 5000) < 0) goto fail;
    g_cindMode = 0;
    if (ind_get("call") == 1) g_callStart = GetTickCount();
    if (at_cmd(s, "AT+CMER=3,0,0,1\r", 5000) < 0) goto fail;

    g_state = ST_READY;
    g_everReady = 1;
    logw(L"[성공] 핸즈프리 연결 완료 - 이제 통화 제어 가능");
    if (pAcceptSCO) logw(L"SCO 수락 켜기 → %d", pAcceptSCO(TRUE));
    redraw();

    at_cmd(s, "AT+CLIP=1\r", 3000);
    at_cmd(s, "AT+COPS=3,0\r", 3000);
    at_cmd(s, "AT+COPS?\r", 3000);
    if (g_agFeat & 64) at_cmd(s, "AT+CLCC\r", 3000);

    while (!g_stopSession && !g_quit) {
        r = read_line(s, ln, sizeof(ln), 1000);
        if (r < 0) { logw(L"폰이 연결을 끊음"); break; }
        if (r == 0) continue;
        if (!strcmp(ln, "OK")) {
            logw(L"← OK");
            if (g_ctrlWhat[0]) { g_ctrlOk = 1; logw(L"[성공] 통화 제어 OK: %s", g_ctrlWhat); g_ctrlWhat[0] = 0; }
            redraw();
            continue;
        }
        if (!strcmp(ln, "ERROR") || !strncmp(ln, "+CME ERROR", 10)) {
            logw(L"← %hs (명령 거부)", ln);
            g_ctrlWhat[0] = 0;
            continue;
        }
        handle_line(ln);
    }
    goto done;
fail:
    logw(L"[실패] 핸즈프리 연결 절차 실패");
    g_state = ST_FAIL;
done:
    bridge_stop();
    sco_close();
    EnterCriticalSection(&g_cs);
    closesocket(s);
    g_sock = INVALID_SOCKET;
    LeaveCriticalSection(&g_cs);
    if (g_state != ST_FAIL) g_state = ST_IDLE;
    g_indN = 0;
    g_clip[0] = 0;
    redraw();
}

/* 바깥으로 연결 시도 */
static SOCKET try_connect_layout(BTADDR a, int layout, const GUID *g, unsigned long port, int *err)
{
    unsigned char sa[40];
    int len;
    SOCKET s = socket(32, SOCK_STREAM, 3 /*BTHPROTO_RFCOMM*/);
    if (s == INVALID_SOCKET) { *err = WSAGetLastError(); return s; }
    len = make_sa(sa, layout, a, g, port);
    if (connect(s, (struct sockaddr *)sa, len) == 0) { *err = 0; return s; }
    *err = WSAGetLastError();
    closesocket(s);
    return INVALID_SOCKET;
}

/* 채널 직접 찾기: 연결되면 AT+BRSF 로 핸즈프리인지 확인 */
static int probe_channel(SOCKET s)
{
    char ln[200];
    int k;
    g_rlen = 0;
    if (send(s, "AT+BRSF=52\r", 11, 0) != 11) return 0;
    for (k = 0; k < 6; k++) {
        int r = read_line(s, ln, sizeof(ln), 2500);
        if (r <= 0) return 0;
        if (!strncmp(ln, "+BRSF", 5)) return 1;
        if (!strcmp(ln, "OK") || !strcmp(ln, "ERROR")) return 0;
    }
    return 0;
}

static DWORD WINAPI connect_thread(LPVOID p)
{
    BTADDR a = *(BTADDR *)p;
    GUID ag;
    SOCKET s;
    int err, layout, ch;
    wchar_t as[20];
    free(p);
    g_session = 1;
    g_stopSession = 0;
    g_state = ST_CONNECTING;
    redraw();
    addr_str(a, as);
    make_uuid16(0x111F, &ag); /* Handsfree Audio Gateway */
    logw(L"%s 에 핸즈프리 연결 시도 (서비스 검색 방식)", as);
    for (layout = 0; layout < 2; layout++) {
        s = try_connect_layout(a, layout, &ag, 0, &err);
        if (s != INVALID_SOCKET) {
            g_saLayout = layout;
            logw(L"연결 성공 (주소형식 %d)", layout);
            run_session(s, a);
            g_session = 0;
            return 0;
        }
        logw(L"주소형식 %d 연결 실패 (오류 %d)", layout, err);
        if (g_stopSession) break;
        if (err != WSAEFAULT && err != WSAEINVAL && err != WSAEAFNOSUPPORT) { g_saLayout = layout; break; }
    }
    if (!g_stopSession) {
        logw(L"채널 직접 찾기 (1~30)...");
        for (ch = 1; ch <= 30 && !g_stopSession && !g_quit; ch++) {
            s = try_connect_layout(a, g_saLayout, NULL, ch, &err);
            if (s == INVALID_SOCKET) continue;
            logw(L"채널 %d 열림 - 핸즈프리인지 확인", ch);
            if (probe_channel(s)) {
                closesocket(s);
                Sleep(800);
                s = try_connect_layout(a, g_saLayout, NULL, ch, &err);
                if (s != INVALID_SOCKET) {
                    logw(L"채널 %d 이 폰의 핸즈프리 서비스", ch);
                    run_session(s, a);
                    g_session = 0;
                    return 0;
                }
            } else closesocket(s);
        }
    }
    logw(L"[실패] 폰의 핸즈프리 서비스에 연결 못 함");
    g_state = ST_FAIL;
    g_session = 0;
    redraw();
    return 0;
}

/* ------------------------------------------------------------------ */
/* 폰 쪽에서 연결해 오는 경우: SDP 핸즈프리 레코드 + 대기 소켓              */
/* ------------------------------------------------------------------ */
static int build_hf_record(unsigned char *r, int ch, int withHandle)
{
    unsigned char b[160];
    int n = 0;
#define B(x) b[n++] = (unsigned char)(x)
    if (withHandle) { B(0x09); B(0x00); B(0x00); B(0x0A); B(0); B(0); B(0); B(0); }
    B(0x09); B(0x00); B(0x01); B(0x35); B(6); B(0x19); B(0x11); B(0x1E); B(0x19); B(0x12); B(0x03);
    B(0x09); B(0x00); B(0x04); B(0x35); B(12);
        B(0x35); B(3); B(0x19); B(0x01); B(0x00);
        B(0x35); B(5); B(0x19); B(0x00); B(0x03); B(0x08); B(ch);
    B(0x09); B(0x00); B(0x05); B(0x35); B(3); B(0x19); B(0x10); B(0x02);
    B(0x09); B(0x00); B(0x09); B(0x35); B(8); B(0x35); B(6); B(0x19); B(0x11); B(0x1E); B(0x09); B(0x01); B(0x05);
    B(0x09); B(0x01); B(0x00); B(0x25); B(10);
    memcpy(b + n, "Hands-Free", 10); n += 10;
    B(0x09); B(0x03); B(0x11); B(0x09); B(0x00); B(0x14);
#undef B
    r[0] = 0x35; r[1] = (unsigned char)n;
    memcpy(r + 2, b, n);
    return n + 2;
}

static int sdp_set(int op, const unsigned char *rec, int len)
{
    unsigned char blob[16 + 200];
    ULONG *h = (ULONG *)blob;
    MYBLOB bl;
    MYQS qs;
    if (!pSetService) return -1;
    memset(blob, 0, sizeof(blob));
    h[0] = (ULONG)(ULONG_PTR)&g_sdpHandle;
    h[1] = 0; h[2] = 0; h[3] = (ULONG)len;
    if (len) memcpy(blob + 16, rec, len);
    bl.cbSize = 16 + (len ? len : 1);
    bl.pBlobData = blob;
    memset(&qs, 0, sizeof(qs));
    qs.dwSize = sizeof(qs);
    qs.dwNameSpace = 16;
    qs.lpBlob = &bl;
    if (pSetService(&qs, op, 0) != 0) return WSAGetLastError();
    return 0;
}

static void sdp_register(int ch)
{
    unsigned char rec[200];
    int len, e;
    len = build_hf_record(rec, ch, 0);
    g_sdpHandle = 0;
    e = sdp_set(0 /*RNRSERVICE_REGISTER*/, rec, len);
    if (e) {
        logw(L"SDP 등록 실패(%d) - 다른 형식으로 재시도", e);
        len = build_hf_record(rec, ch, 1);
        g_sdpHandle = 0;
        e = sdp_set(0, rec, len);
    }
    if (!e) { g_sdpOk = 1; logw(L"핸즈프리 서비스 등록 성공 (채널 %d, 핸들 %lu)", ch, g_sdpHandle); }
    else logw(L"핸즈프리 서비스 등록 실패 (오류 %d) - 폰 쪽 연결은 안 될 수 있음", e);
}

static void sdp_unregister(void)
{
    if (g_sdpOk) { sdp_set(2 /*RNRSERVICE_DELETE*/, NULL, 0); g_sdpOk = 0; }
}

static DWORD WINAPI listen_thread(LPVOID p)
{
    SOCKET ls;
    unsigned char sa[40];
    int len, ch = 0, layout;
    (void)p;
    for (layout = 0; layout < 2 && !ch; layout++) {
        ls = socket(32, SOCK_STREAM, 3);
        if (ls == INVALID_SOCKET) { logw(L"대기 소켓 생성 실패 (%d)", WSAGetLastError()); return 0; }
        len = make_sa(sa, layout, 0, NULL, 0);
        if (bind(ls, (struct sockaddr *)sa, len) == 0) {
            int l2 = 40;
            memset(sa, 0, sizeof(sa));
            if (getsockname(ls, (struct sockaddr *)sa, &l2) == 0) {
                unsigned long pt = sa_port(sa, layout == 0 ? 40 : 30);
                if (pt >= 1 && pt <= 30) ch = (int)pt;
            }
            if (ch) break;
        }
        closesocket(ls);
        /* 직접 채널 지정 */
        for (ch = 3; ch <= 30; ch++) {
            ls = socket(32, SOCK_STREAM, 3);
            len = make_sa(sa, layout, 0, NULL, ch);
            if (bind(ls, (struct sockaddr *)sa, len) == 0) break;
            closesocket(ls);
        }
        if (ch > 30) ch = 0; else break;
    }
    if (!ch) { logw(L"대기 소켓 채널 잡기 실패 (%d)", WSAGetLastError()); return 0; }
    if (listen(ls, 1) != 0) { logw(L"대기 실패 (%d)", WSAGetLastError()); closesocket(ls); return 0; }
    g_listenCh = ch;
    g_listenSock = ls;
    logw(L"폰에서 연결해 오기를 대기 (채널 %d)", ch);
    sdp_register(ch);
    redraw();
    while (!g_quit) {
        unsigned char ps[40];
        int pl = 40;
        SOCKET s;
        memset(ps, 0, sizeof(ps));
        s = accept(ls, (struct sockaddr *)ps, &pl);
        if (s == INVALID_SOCKET) { if (!g_quit) logw(L"대기 중단 (%d)", WSAGetLastError()); break; }
        if (g_session) { logw(L"폰에서 연결해 왔지만 이미 연결 중 - 무시"); closesocket(s); continue; }
        logw(L"폰이 M480으로 연결해 옴");
        g_session = 1; g_stopSession = 0;
        run_session(s, sa_addr(ps, pl));
        g_session = 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* SCO / 음성                                                          */
/* ------------------------------------------------------------------ */
typedef struct {
    unsigned short hConnection;
    BTADDR baAddress;
    int cDataPacketsPending;
    unsigned int fLinkType : 1;
    unsigned int fMode : 2;
} BBCONN;

static void sco_poll(void)
{
    BBCONN c[8];
    int n = 0, i, sco = 0, acl = 0;
    if (!pGetBB) return;
    memset(c, 0, sizeof(c));
    if (pGetBB(8, c, &n) != 0) return;
    if (n > 8) n = 8;
    for (i = 0; i < n; i++) { if (c[i].fLinkType == 0) { sco++; if (!g_scoMine) g_scoHandle = c[i].hConnection; } else acl++; }
    if ((sco > 0) != g_scoUp || acl != g_aclN) {
        logw(L"무선 링크 변화: 데이터 %d개, 음성(SCO) %d개", acl, sco);
        for (i = 0; i < n; i++) logw(L"  링크 %d: 핸들 %u 종류 %u 모드 %u", i, c[i].hConnection, c[i].fLinkType, c[i].fMode);
        if (sco && !g_scoUp) logw(L"[성공] 음성(SCO) 링크 연결됨 - M480에서 소리가 나는지 확인하세요");
        if (!sco && g_scoUp) logw(L"음성(SCO) 링크 끊김");
        g_scoUp = sco > 0;
        if (g_scoUp) g_scoEver = 1;
        g_aclN = acl;
        redraw();
    }
}

static void sco_open(void)
{
    unsigned short h = 0;
    int r;
    BTADDR a = g_peer;
    if (!pCreateSCO) { logw(L"이 기기에는 SCO 만들기 함수가 없음"); return; }
    if (g_state != ST_READY) { logw(L"먼저 폰에 연결하세요"); return; }
    logw(L"음성(SCO) 연결 시도...");
    r = pCreateSCO(&a, &h);
    logw(L"SCO 만들기 결과 %d (핸들 %u, 오류 %lu)", r, h, GetLastError());
    if (r == 0) { g_scoHandle = h; g_scoMine = 1; }
    sco_poll();
}

static void sco_close(void)
{
    if (g_scoUp && pCloseConn && g_scoHandle) {
        int r = pCloseConn(g_scoHandle);
        logw(L"SCO 끊기 → %d", r);
    }
    g_scoMine = 0;
    g_scoHandle = 0;
}

static int name_bt(const wchar_t *nm)
{
    wchar_t l[40];
    int i;
    for (i = 0; i < 39 && nm[i]; i++) l[i] = (nm[i] >= L'A' && nm[i] <= L'Z') ? nm[i] + 32 : nm[i];
    l[i] = 0;
    return wcsstr(l, L"blue") || wcsstr(l, L"bt") || wcsstr(l, L"sco") || wcsstr(l, L"hands") ||
           wcsstr(l, L"head") || wcsstr(nm, L"블루투스");
}

static void wave_info(void)
{
    UINT i;
    g_nOut = waveOutGetNumDevs();
    g_nIn = waveInGetNumDevs();
    logw(L"출력 장치 %u개, 입력 장치 %u개", g_nOut, g_nIn);
    for (i = 0; i < g_nOut && i < 8; i++) {
        WAVEOUTCAPS c; memset(&c, 0, sizeof(c));
        if (waveOutGetDevCaps(i, &c, sizeof(c)) == 0)
            logw(L"  출력 %u: %s%s", i, c.szPname, name_bt(c.szPname) ? L" (블루투스?)" : L"");
    }
    for (i = 0; i < g_nIn && i < 8; i++) {
        WAVEINCAPS c; memset(&c, 0, sizeof(c));
        if (waveInGetDevCaps(i, &c, sizeof(c)) == 0)
            logw(L"  입력 %u: %s%s", i, c.szPname, name_bt(c.szPname) ? L" (블루투스?)" : L"");
    }
}

/* 파형 중계: 입력 장치 → 출력 장치 */
#define BR_NB 6
#define BR_FR 160
typedef struct {
    UINT inDev, outDev;
    HWAVEIN wi; HWAVEOUT wo;
    WAVEHDR ih[BR_NB], oh[BR_NB];
    short ib[BR_NB][BR_FR * 4], ob[BR_NB][BR_FR * 4];
    HANDLE ev, th;
    volatile int stop;
    const wchar_t *label;
} Bridge;
static Bridge g_br[2];

static DWORD WINAPI bridge_thread(LPVOID p)
{
    Bridge *b = (Bridge *)p;
    static const DWORD rates[] = { 8000, 16000, 11025, 22050 };
    WAVEFORMATEX f;
    int i, ri, ni = 0, oi = 0, frames = 0;
    MMRESULT r1 = 0, r2 = 0;
    DWORD rate = 0;
    for (ri = 0; ri < 4; ri++) {
        memset(&f, 0, sizeof(f));
        f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = 1; f.nSamplesPerSec = rates[ri];
        f.wBitsPerSample = 16; f.nBlockAlign = 2; f.nAvgBytesPerSec = rates[ri] * 2;
        r1 = waveInOpen(&b->wi, b->inDev, &f, (DWORD)b->ev, 0, CALLBACK_EVENT);
        if (r1 != 0) continue;
        r2 = waveOutOpen(&b->wo, b->outDev, &f, 0, 0, CALLBACK_NULL);
        if (r2 == 0) { rate = rates[ri]; break; }
        waveInClose(b->wi);
    }
    if (!rate) {
        logw(L"%s: 장치 열기 실패 (입력 %u 결과 %u, 출력 %u 결과 %u)", b->label, b->inDev, r1, b->outDev, r2);
        return 0;
    }
    frames = (int)(rate / 50); /* 20ms */
    logw(L"%s: 입력 %u → 출력 %u, %luHz 중계 시작", b->label, b->inDev, b->outDev, rate);
    for (i = 0; i < BR_NB; i++) {
        memset(&b->ih[i], 0, sizeof(WAVEHDR)); memset(&b->oh[i], 0, sizeof(WAVEHDR));
        b->ih[i].lpData = (LPSTR)b->ib[i]; b->ih[i].dwBufferLength = frames * 2;
        b->oh[i].lpData = (LPSTR)b->ob[i]; b->oh[i].dwBufferLength = frames * 2;
        waveInPrepareHeader(b->wi, &b->ih[i], sizeof(WAVEHDR));
        waveOutPrepareHeader(b->wo, &b->oh[i], sizeof(WAVEHDR));
        waveInAddBuffer(b->wi, &b->ih[i], sizeof(WAVEHDR));
    }
    waveInStart(b->wi);
    while (!b->stop && !g_quit) {
        WaitForSingleObject(b->ev, 200);
        while (b->ih[ni].dwFlags & WHDR_DONE) {
            WAVEHDR *ih = &b->ih[ni], *oh = &b->oh[oi];
            if (!(oh->dwFlags & WHDR_INQUEUE) && ih->dwBytesRecorded) {
                memcpy(oh->lpData, ih->lpData, ih->dwBytesRecorded);
                oh->dwBufferLength = ih->dwBytesRecorded;
                oh->dwFlags &= ~WHDR_DONE;
                waveOutWrite(b->wo, oh, sizeof(WAVEHDR));
                oi = (oi + 1) % BR_NB;
            }
            ih->dwFlags &= ~WHDR_DONE;
            ih->dwBytesRecorded = 0;
            waveInAddBuffer(b->wi, ih, sizeof(WAVEHDR));
            ni = (ni + 1) % BR_NB;
            if (b->stop) break;
        }
    }
    waveInReset(b->wi);
    waveOutReset(b->wo);
    for (i = 0; i < BR_NB; i++) {
        waveInUnprepareHeader(b->wi, &b->ih[i], sizeof(WAVEHDR));
        waveOutUnprepareHeader(b->wo, &b->oh[i], sizeof(WAVEHDR));
    }
    waveInClose(b->wi);
    waveOutClose(b->wo);
    logw(L"%s: 중계 끝", b->label);
    return 0;
}

static void bridge_stop(void)
{
    int k;
    if (!g_bridgeOn) return;
    for (k = 0; k < 2; k++) {
        g_br[k].stop = 1;
        if (g_br[k].th) { WaitForSingleObject(g_br[k].th, 2000); CloseHandle(g_br[k].th); g_br[k].th = NULL; }
        if (g_br[k].ev) { CloseHandle(g_br[k].ev); g_br[k].ev = NULL; }
    }
    g_bridgeOn = 0;
    redraw();
}

static void bridge_start(void)
{
    UINT bo, bi;
    DWORD tid;
    int k;
    if (g_bridgeOn) { bridge_stop(); return; }
    if (g_nOut < 2 || g_nIn < 2) {
        logw(L"소리 장치가 1개뿐 - 앱이 중계할 수 없음. 음성이 연결되면 하드웨어 경로로 M480 스피커에 나오는지만 확인하세요");
        return;
    }
    bo = (UINT)g_audioDev < g_nOut ? (UINT)g_audioDev : g_nOut - 1;
    bi = (UINT)g_audioDev < g_nIn ? (UINT)g_audioDev : g_nIn - 1;
    g_br[0].inDev = bi; g_br[0].outDev = 0; g_br[0].label = L"폰→M480 스피커";
    g_br[1].inDev = 0; g_br[1].outDev = bo; g_br[1].label = L"M480 마이크→폰";
    for (k = 0; k < 2; k++) {
        g_br[k].stop = 0;
        g_br[k].ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        g_br[k].th = CreateThread(NULL, 0, bridge_thread, &g_br[k], 0, &tid);
    }
    g_bridgeOn = 1;
    redraw();
}

/* ------------------------------------------------------------------ */
/* 동작                                                                */
/* ------------------------------------------------------------------ */
static void act_connect(void)
{
    BTADDR *pa;
    DWORD tid;
    HANDLE h;
    if (g_session) {
        logw(L"연결 끊기");
        g_stopSession = 1;
        return;
    }
    if (g_devSel < 0) { logw(L"폰을 먼저 고르세요 (페어링 또는 [찾기])"); return; }
    save_settings();
    pa = (BTADDR *)malloc(sizeof(BTADDR));
    *pa = g_dev[g_devSel].a;
    h = CreateThread(NULL, 0, connect_thread, pa, 0, &tid);
    if (h) CloseHandle(h);
}

static void act_find(void)
{
    DWORD tid;
    HANDLE h;
    if (g_inquiring || g_session) return;
    g_inquiring = 1;
    h = CreateThread(NULL, 0, inquiry_thread, NULL, 0, &tid);
    if (h) CloseHandle(h);
    redraw();
}

static void act_pair(void)
{
    BTADDR *pa;
    DWORD tid;
    HANDLE h;
    if (g_pairing || g_session) return;
    if (g_devSel < 0) { logw(L"[찾기]로 폰을 먼저 찾으세요 (아이폰은 설정 > 블루투스 화면을 열어 둔 상태)"); return; }
    g_pairing = 1;
    pa = (BTADDR *)malloc(sizeof(BTADDR));
    *pa = g_dev[g_devSel].a;
    h = CreateThread(NULL, 0, pair_thread, pa, 0, &tid);
    if (h) CloseHandle(h);
    redraw();
}

static int in_call(void) { return ind_get("call") == 1; }
static int ringing(void) { return ind_get("callsetup") == 1; }

static void act_dial(void)
{
    char b[48];
    if (ringing()) { ui_send("ATA\r", L"전화 받기"); return; }
    if (!g_num[0]) { ui_send("AT+BLDN\r", L"마지막 번호 다시 걸기"); return; }
    sprintf(b, "ATD%s;\r", g_num);
    ui_send(b, L"전화 걸기");
}

static void act_digit(char c)
{
    if (in_call()) {
        char b[16];
        sprintf(b, "AT+VTS=%c\r", c);
        ui_send(b, NULL);
        return;
    }
    {
        int n = strlen(g_num);
        if (n < 30) { g_num[n] = c; g_num[n + 1] = 0; }
    }
    redraw();
}

static void act_back(void)
{
    int n = strlen(g_num);
    if (n) g_num[n - 1] = 0;
    redraw();
}

static void act_quit(void)
{
    g_quit = 1;
    g_stopSession = 1;
    DestroyWindow(g_hwnd);
}

/* ------------------------------------------------------------------ */
/* 화면                                                                */
/* ------------------------------------------------------------------ */
#define MAX_BTN 40
typedef struct { RECT r; int id; } Btn;
static Btn g_btn[MAX_BTN];
static int g_btnN;
static int g_pressId = -1;

enum {
    B_TAB_MAIN = 1, B_TAB_LOG, B_QUIT, B_PREV, B_NEXT, B_FIND, B_CONN,
    B_DIAL, B_ANSWER, B_HANG, B_BACK, B_SCO, B_BRIDGE, B_ADEV,
    B_LUP, B_LDOWN, B_LEND, B_PAIR, B_HFMODE, B_KEY0 = 100
};

#define C_BG   RGB(18, 20, 26)
#define C_PAN  RGB(34, 38, 48)
#define C_BTN  RGB(52, 58, 72)
#define C_BTNP RGB(90, 100, 124)
#define C_TXT  RGB(235, 238, 245)
#define C_DIM  RGB(140, 148, 165)
#define C_OK   RGB(80, 210, 120)
#define C_BAD  RGB(240, 90, 80)
#define C_WARN RGB(250, 190, 70)
#define C_GRN  RGB(40, 150, 80)
#define C_RED  RGB(190, 50, 50)

static void fill(HDC dc, const RECT *r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, r, b);
    DeleteObject(b);
}

static void text(HDC dc, const wchar_t *s, RECT *r, HFONT f, COLORREF c, UINT fl)
{
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, r, fl | DT_NOPREFIX);
}

static void button(HDC dc, int x, int y, int w, int h, int id, const wchar_t *label, COLORREF bg, HFONT f)
{
    RECT r;
    int m = g_scale;
    SetRect(&r, x + m, y + m, x + w - m, y + h - m);
    fill(dc, &r, g_pressId == id ? C_BTNP : bg);
    text(dc, label, &r, f, C_TXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (g_btnN < MAX_BTN) { SetRect(&g_btn[g_btnN].r, x, y, x + w, y + h); g_btn[g_btnN].id = id; g_btnN++; }
}

static const wchar_t *state_str(COLORREF *c)
{
    switch (g_state) {
    case ST_CONNECTING: *c = C_WARN; return L"연결 시도 중...";
    case ST_SLC: *c = C_WARN; return L"핸즈프리 설정 중...";
    case ST_READY: *c = C_OK; return L"연결됨";
    case ST_FAIL: *c = C_BAD; return L"연결 실패";
    default: *c = C_DIM; return L"연결 안 됨";
    }
}

static void paint_main(HDC dc, int W, int H, int y0)
{
    wchar_t s[160], w[64];
    RECT r;
    COLORREF sc;
    int u, y = y0, x, i, bw, pad = 4 * g_scale;
    int rowH[9] = { 36, 26, 26, 22, 28, 112, 32, 28, 42 };
    int need = 0;
    for (i = 0; i < 9; i++) need += rowH[i];
    u = (H - y0) * 100 / (need * g_scale);
    if (u > 100) u = 100;
#define RH(k) (rowH[k] * g_scale * u / 100)

    /* 1. 상태 */
    SetRect(&r, 0, y, W, y + RH(0)); fill(dc, &r, C_PAN);
    {
        const wchar_t *st = state_str(&sc);
        SetRect(&r, pad, y, W - pad, y + RH(0) / 2 + g_scale);
        text(dc, st, &r, fBold, sc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (g_state == ST_READY) {
            int sig = ind_get("signal"), bat = ind_get("battchg"), svc = ind_get("service");
            s[0] = 0;
            if (g_oper[0]) { wcscat(s, g_oper); wcscat(s, L"  "); }
            if (svc == 0) wcscat(s, L"서비스 없음  ");
            if (sig >= 0) { _snwprintf(w, 63, L"신호 %d/5  ", sig); wcscat(s, w); }
            if (bat >= 0) { _snwprintf(w, 63, L"배터리 %d/5", bat); wcscat(s, w); }
            SetRect(&r, pad, y + RH(0) / 2, W - pad, y + RH(0));
            text(dc, s, &r, fSmall, C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        } else {
            SetRect(&r, pad, y + RH(0) / 2, W - pad, y + RH(0));
            text(dc, g_listenCh ? L"폰에서 연결해 와도 됩니다" : L"", &r, fSmall, C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }
    y += RH(0);

    /* 2. 기기 선택 */
    bw = W / 8;
    button(dc, 0, y, bw, RH(1), B_PREV, L"◀", C_BTN, fBold);
    SetRect(&r, bw, y, W - bw * 4, y + RH(1));
    if (g_devSel >= 0) {
        _snwprintf(s, 159, L"%s%s", g_dev[g_devSel].name, g_dev[g_devSel].paired ? L"" : L" (미페어링)");
        s[159] = 0;
    } else wcscpy(s, L"폰 없음");
    text(dc, s, &r, fNorm, C_TXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    button(dc, W - bw * 4, y, bw, RH(1), B_NEXT, L"▶", C_BTN, fBold);
    button(dc, W - bw * 3, y, bw * 3, RH(1), B_CONN,
           g_session ? L"끊기" : L"연결", g_session ? C_RED : C_GRN, fBold);
    y += RH(1);

    /* 2b. 기기 관리 */
    bw = W / 3;
    button(dc, 0, y, bw, RH(2), B_FIND, g_inquiring ? L"찾는 중..." : L"폰 찾기", C_BTN, fSmall);
    button(dc, bw, y, bw, RH(2), B_PAIR, g_pairing ? L"페어링 중..." : L"페어링", C_BTN, fSmall);
    button(dc, bw * 2, y, W - bw * 2, RH(2), B_HFMODE, g_hfMode ? L"핸즈프리로 보임" : L"폰으로 보임", g_hfMode ? C_GRN : C_BTN, fSmall);
    y += RH(2);

    /* 3. 통화 상태 */
    SetRect(&r, pad, y, W - pad, y + RH(3));
    {
        COLORREF c = C_DIM;
        int cs = ind_get("callsetup");
        if (g_state != ST_READY) wcscpy(s, L"");
        else if (cs == 1) {
            wchar_t n[40];
            MultiByteToWideChar(CP_ACP, 0, g_clip[0] ? g_clip : "번호 없음", -1, n, 40);
            if (!g_clip[0]) wcscpy(n, L"번호 없음");
            _snwprintf(s, 159, L"전화 왔어요: %s", n); c = C_WARN;
        } else if (in_call()) {
            DWORD t = (GetTickCount() - g_callStart) / 1000;
            _snwprintf(s, 159, L"통화 중 %02lu:%02lu%s", t / 60, t % 60, g_scoUp ? L"  (음성 M480)" : L"  (음성 폰)");
            c = C_OK;
        } else if (cs == 2 || cs == 3) { wcscpy(s, L"거는 중..."); c = C_WARN; }
        else wcscpy(s, L"대기 중");
        s[159] = 0;
        text(dc, s, &r, fBold, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    y += RH(3);

    /* 4. 번호 */
    SetRect(&r, pad, y, W - W / 5, y + RH(4)); fill(dc, &r, C_PAN);
    MultiByteToWideChar(CP_ACP, 0, g_num, -1, s, 40);
    if (!g_num[0]) wcscpy(s, L"번호 입력");
    text(dc, s, &r, g_num[0] ? fBig : fNorm, g_num[0] ? C_TXT : C_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    button(dc, W - W / 5, y, W / 5, RH(4), B_BACK, L"지움", C_BTN, fSmall);
    y += RH(4);

    /* 5. 다이얼 */
    {
        static const char keys[] = "123456789*0#";
        int kh = RH(5) / 4, kw = W / 3;
        for (i = 0; i < 12; i++) {
            wchar_t k[2] = { (wchar_t)keys[i], 0 };
            x = (i % 3) * kw;
            button(dc, x, y + (i / 3) * kh, (i % 3 == 2) ? W - x : kw, kh, B_KEY0 + keys[i], k, C_BTN, fBig);
        }
        y += kh * 4;
    }

    /* 6. 통화 버튼 */
    bw = W / 3;
    button(dc, 0, y, bw, RH(6), B_DIAL, ringing() ? L"받기" : L"통화", C_GRN, fBold);
    button(dc, bw, y, bw, RH(6), B_ANSWER, L"받기", C_GRN, fBold);
    button(dc, bw * 2, y, W - bw * 2, RH(6), B_HANG, L"끊기", C_RED, fBold);
    y += RH(6);

    /* 7. 음성 */
    button(dc, 0, y, bw, RH(7), B_SCO, g_scoUp ? L"음성 끊기" : L"음성 연결", C_BTN, fSmall);
    button(dc, bw, y, bw, RH(7), B_BRIDGE, g_bridgeOn ? L"중계 중지" : L"소리 중계", g_bridgeOn ? C_GRN : C_BTN, fSmall);
    _snwprintf(w, 63, L"장치 %d/%u", g_audioDev, g_nOut > 1 ? g_nOut - 1 : 0);
    button(dc, bw * 2, y, W - bw * 2, RH(7), B_ADEV, w, C_BTN, fSmall);
    y += RH(7);

    /* 8. 테스트 결과 요약 */
    {
        int lh = RH(8) / 3;
        SetRect(&r, pad, y, W - pad, y + lh);
        text(dc, g_everReady ? L"① 폰 연결: 성공" : (g_state == ST_FAIL ? L"① 폰 연결: 실패" : L"① 폰 연결: 아직"),
             &r, fTiny, g_everReady ? C_OK : (g_state == ST_FAIL ? C_BAD : C_DIM), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetRect(&r, pad, y + lh, W - pad, y + lh * 2);
        text(dc, g_ctrlOk ? L"② 통화 제어: 성공 (폰이 명령을 받음)" : L"② 통화 제어: 아직",
             &r, fTiny, g_ctrlOk ? C_OK : C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetRect(&r, pad, y + lh * 2, W - pad, y + lh * 3);
        _snwprintf(s, 159, L"③ 음성 링크: %s / 소리장치 %u개", g_scoUp ? L"연결됨" : (g_scoEver ? L"한 번 연결됐었음" : L"아직"), g_nOut);
        text(dc, s, &r, fTiny, g_scoUp || g_scoEver ? C_OK : C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
#undef RH
}

static void paint_log(HDC dc, int W, int H, int y0)
{
    RECT r;
    int bh = 28 * g_scale, y, i, last;
    int pad = 3 * g_scale;
    int bw = W / 3;
    /* 아래 버튼 */
    button(dc, 0, H - bh, bw, bh, B_LUP, L"▲ 위로", C_BTN, fSmall);
    button(dc, bw, H - bh, bw, bh, B_LDOWN, L"▼ 아래로", C_BTN, fSmall);
    button(dc, bw * 2, H - bh, W - bw * 2, bh, B_LEND, L"맨 끝", C_BTN, fSmall);
    /* 아래에서 위로 그림 */
    EnterCriticalSection(&g_cs);
    last = g_logN - 1 - g_logScroll;
    y = H - bh - pad;
    SelectObject(dc, fTiny);
    for (i = last; i >= 0 && y > y0; i--) {
        RECT m;
        int h;
        SetRect(&m, pad, 0, W - pad, 0);
        DrawTextW(dc, g_log[i], -1, &m, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        h = m.bottom - m.top;
        SetRect(&r, pad, y - h, W - pad, y);
        {
            COLORREF c = C_TXT;
            if (wcsstr(g_log[i], L"[성공]")) c = C_OK;
            else if (wcsstr(g_log[i], L"[실패]") || wcsstr(g_log[i], L"실패")) c = C_BAD;
            else if (wcsstr(g_log[i], L"→") || wcsstr(g_log[i], L"←")) c = C_DIM;
            text(dc, g_log[i], &r, fTiny, c, DT_LEFT | DT_WORDBREAK);
        }
        y -= h + g_scale;
    }
    LeaveCriticalSection(&g_cs);
    SetRect(&r, pad, y0, W - pad, y0 + 14 * g_scale);
    (void)r;
}

static void paint(HDC wdc)
{
    RECT cr, r;
    HDC dc;
    HBITMAP bm, old;
    int W, H, th, tw;
    GetClientRect(g_hwnd, &cr);
    W = cr.right; H = cr.bottom;
    dc = CreateCompatibleDC(wdc);
    bm = CreateCompatibleBitmap(wdc, W, H);
    old = (HBITMAP)SelectObject(dc, bm);
    SetBkMode(dc, TRANSPARENT);
    fill(dc, &cr, C_BG);
    g_btnN = 0;

    /* 탭 */
    th = 24 * g_scale;
    tw = (W - W / 5) / 2;
    SetRect(&r, 0, 0, W, th); fill(dc, &r, C_PAN);
    button(dc, 0, 0, tw, th, B_TAB_MAIN, L"전화", g_scr == SCR_MAIN ? C_BTNP : C_PAN, fBold);
    button(dc, tw, 0, tw, th, B_TAB_LOG, L"기록", g_scr == SCR_LOG ? C_BTNP : C_PAN, fBold);
    button(dc, tw * 2, 0, W - tw * 2, th, B_QUIT, L"종료", C_PAN, fSmall);

    if (g_scr == SCR_MAIN) paint_main(dc, W, H, th);
    else paint_log(dc, W, H, th);

    BitBlt(wdc, 0, 0, W, H, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bm);
    DeleteDC(dc);
}

static int hit(int x, int y)
{
    int i;
    for (i = 0; i < g_btnN; i++) {
        RECT *r = &g_btn[i].r;
        if (x >= r->left && x < r->right && y >= r->top && y < r->bottom) return g_btn[i].id;
    }
    return -1;
}

static void do_button(int id)
{
    if (id >= B_KEY0) { act_digit((char)(id - B_KEY0)); return; }
    switch (id) {
    case B_TAB_MAIN: g_scr = SCR_MAIN; break;
    case B_TAB_LOG: g_scr = SCR_LOG; g_logScroll = 0; break;
    case B_QUIT: act_quit(); return;
    case B_PREV: if (!g_session && g_devN) { g_devSel = (g_devSel + g_devN - 1) % g_devN; g_state = ST_IDLE; } break;
    case B_NEXT: if (!g_session && g_devN) { g_devSel = (g_devSel + 1) % g_devN; g_state = ST_IDLE; } break;
    case B_FIND: act_find(); break;
    case B_PAIR: act_pair(); break;
    case B_HFMODE: hf_mode(!g_hfMode); break;
    case B_CONN: act_connect(); break;
    case B_DIAL: act_dial(); break;
    case B_ANSWER: ui_send("ATA\r", L"전화 받기"); break;
    case B_HANG: ui_send("AT+CHUP\r", L"끊기/거절"); break;
    case B_BACK: act_back(); break;
    case B_SCO: if (g_scoUp) { bridge_stop(); sco_close(); sco_poll(); } else sco_open(); break;
    case B_BRIDGE: bridge_start(); break;
    case B_ADEV:
        if (g_nOut > 1) { g_audioDev++; if ((UINT)g_audioDev >= g_nOut) g_audioDev = 1; }
        logw(L"중계 장치 번호 → %d", g_audioDev);
        break;
    case B_LUP: g_logScroll += 8; if (g_logScroll > g_logN - 1) g_logScroll = g_logN > 0 ? g_logN - 1 : 0; break;
    case B_LDOWN: g_logScroll -= 8; if (g_logScroll < 0) g_logScroll = 0; break;
    case B_LEND: g_logScroll = 0; break;
    }
    redraw();
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_APP_REFRESH: InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_APP_RING:
        MessageBeep(MB_ICONASTERISK);
        if (g_scr != SCR_MAIN) g_scr = SCR_MAIN;
        SetForegroundWindow(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_TIMER:
        sco_poll();
        if (in_call()) InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        g_pressId = hit((short)LOWORD(lp), (short)HIWORD(lp));
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        int id = hit((short)LOWORD(lp), (short)HIWORD(lp));
        int was = g_pressId;
        g_pressId = -1;
        if (id >= 0 && id == was) do_button(id);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_CHAR:
        if ((wp >= '0' && wp <= '9') || wp == '*' || wp == '#' || wp == '+') { act_digit((char)wp); return 0; }
        if (wp == 8) { act_back(); return 0; }
        break;
    case WM_KEYDOWN:
        if (wp == VK_TTALK) { act_dial(); return 0; }
        if (wp == VK_TEND) { ui_send("AT+CHUP\r", L"끊기/거절"); return 0; }
        if (wp == VK_F1) { g_scr = SCR_MAIN; redraw(); return 0; }
        if (wp == VK_F2) { g_scr = SCR_LOG; g_logScroll = 0; redraw(); return 0; }
        if (g_scr == SCR_LOG && wp == VK_UP) { do_button(B_LUP); return 0; }
        if (g_scr == SCR_LOG && wp == VK_DOWN) { do_button(B_LDOWN); return 0; }
        break;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
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
    lf.lfQuality = 5;
    return CreateFontIndirectW(&lf);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmd, int show)
{
    WNDCLASSW wc;
    MSG m;
    HWND prev;
    HDC sdc;
    RECT wa;
    WSADATA wd;
    DWORD tid;
    HANDLE h;
    wchar_t *slash;
    (void)hPrev; (void)cmd;

    prev = FindWindowW(APP_CLASS, NULL);
    if (prev) { SetForegroundWindow((HWND)((ULONG_PTR)prev | 1)); return 0; }
    g_inst = hInst;
    g_t0 = GetTickCount();
    InitializeCriticalSection(&g_cs);

    GetModuleFileNameW(NULL, g_exeDir, MAX_PATH);
    slash = wcsrchr(g_exeDir, L'\\');
    if (slash) slash[1] = 0; else wcscpy(g_exeDir, L"\\");
    log_open();

    sdc = GetDC(NULL);
    g_scale = GetDeviceCaps(sdc, LOGPIXELSY) >= 150 ? 2 : 1;
    ReleaseDC(NULL, sdc);
    fBig = make_font(18 * g_scale, 1);
    fBold = make_font(14 * g_scale, 1);
    fNorm = make_font(13 * g_scale, 0);
    fSmall = make_font(12 * g_scale, 0);
    fTiny = make_font(11 * g_scale, 0);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.lpszClassName = APP_CLASS;
    RegisterClassW(&wc);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    g_hwnd = CreateWindowW(APP_CLASS, APP_TITLE, WS_VISIBLE, wa.left, wa.top,
                           wa.right - wa.left, wa.bottom - wa.top, NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 0;
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    WSAStartup(MAKEWORD(2, 2), &wd);
    bt_load();
    bt_info();
    load_settings();
    load_paired();
    pick_saved();
    wave_info();
    hf_mode(1);
    {
        wchar_t lp[MAX_PATH];
        wcscpy(lp, g_exeDir); wcscat(lp, L"M480Call_log.txt");
        logw(L"기록 파일: %s", lp);
    }
    h = CreateThread(NULL, 0, listen_thread, NULL, 0, &tid);
    if (h) CloseHandle(h);
    SetTimer(g_hwnd, 1, 1000, NULL);
    redraw();

    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    g_quit = 1;
    g_stopSession = 1;
    bridge_stop();
    sco_close();
    sdp_unregister();
    hf_mode(0);
    if (g_listenSock != INVALID_SOCKET) closesocket(g_listenSock);
    Sleep(300);
    save_settings();
    logw(L"종료");
    if (g_logFile != INVALID_HANDLE_VALUE) CloseHandle(g_logFile);
    return 0;
}
