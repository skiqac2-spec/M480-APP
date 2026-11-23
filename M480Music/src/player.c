/* player.c - M480 안전 뮤직 플레이어 (Windows Mobile 6.1, ARM)
 *
 * 음악을 직접 디코딩해서 게임 소리처럼 waveOut으로 내보낸다.
 * - 기본 22050Hz 16bit 출력 (게임과 같은 형식 → 기기에서 깨지지 않음)
 * - 출력 레벨을 낮추고 리미터로 피크를 눌러 이어폰 앰프 클리핑 방지
 * - 저음 컷으로 이어폰/스피커 찌그러짐 감소
 * - 끊김(언더런)이나 CPU 과부하가 감지되면 음질을 자동으로 한 단계씩 낮춤
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>
#include "source.h"
#include "dsp.h"

#define APP_CLASS   L"M480MusicWnd"
#define APP_TITLE   L"M480 뮤직"

#define WM_APP_TRACKEND (WM_APP + 1)
#define WM_APP_TRACKERR (WM_APP + 2)

#define MAX_FILES   4000
#define NBUF        10           /* 버퍼 10개 × 200ms = 약 2초 여유 (SD카드 지연 대비) */

/* ------------------------------------------------------------ 음질 단계 */
typedef struct { int rate, ch; const wchar_t *name; } QLevel;
static const QLevel g_levels[4] = {
    { 44100, 2, L"높음" },
    { 22050, 2, L"보통" },
    { 22050, 1, L"낮음" },
    { 11025, 1, L"최저" },
};
static const int g_levelPct[4] = { 45, 60, 75, 90 };
static const wchar_t *g_levelPctName[4] = { L"작게", L"보통", L"크게", L"최대" };
static const wchar_t *g_orderName[3] = { L"차례대로", L"무작위", L"한곡반복" };

/* ------------------------------------------------------------ 설정 */
typedef struct {
    DWORD magic;
    int qmode;      /* 0=자동, 1..4 = 높음/보통/낮음/최저 고정 */
    int earphone;   /* 1=이어폰 모드 */
    int levelIdx;   /* 출력 레벨 */
    int order;      /* 0 차례 1 무작위 2 한곡반복 */
    int lastIndex;
    DWORD lastPosMs;
    int autoLevel;  /* 자동 모드가 마지막으로 정한 단계 */
    int compact;    /* 1=버튼 줄 숨김 */
    int panel;      /* 1=설정 버튼 펼침 */
} Settings;
#define SET_MAGIC 0x4D343831 /* 'M481' */

static Settings g_set = { SET_MAGIC, 0, 1, 1, 0, 0, 0, 1, 0, 0 };
static wchar_t g_exeDir[MAX_PATH];

/* ------------------------------------------------------------ 곡 목록 */
static wchar_t **g_files;
static int g_fileCount;

/* ------------------------------------------------------------ 오디오 스레드 공유 상태 */
enum { CMD_PLAY = 1, CMD_PAUSE, CMD_STOP, CMD_SEEK, CMD_RECONFIG, CMD_QUIT };
typedef struct { int cmd; int index; DWORD ms; } Cmd;
#define CMDQ 16
static Cmd g_cmdq[CMDQ];
static int g_cmdHead, g_cmdTail;
static CRITICAL_SECTION g_cs;
static HANDLE g_cmdEvent, g_waveEvent, g_audioThread;

typedef struct {
    int state;           /* 0 정지 1 재생 2 일시정지 */
    int index;
    DWORD posMs, durMs;
    int srcType, srcRate, srcCh, kbps;
    int outRate, outCh, level;
    int underruns;
    int loadPct;
    DWORD limiterHits;
    int openFailed;
} Status;
static Status g_st;

static HWND g_hwnd;

static void post_cmd(int cmd, int index, DWORD ms)
{
    EnterCriticalSection(&g_cs);
    if (((g_cmdTail + 1) % CMDQ) != g_cmdHead) {
        g_cmdq[g_cmdTail].cmd = cmd;
        g_cmdq[g_cmdTail].index = index;
        g_cmdq[g_cmdTail].ms = ms;
        g_cmdTail = (g_cmdTail + 1) % CMDQ;
    }
    LeaveCriticalSection(&g_cs);
    SetEvent(g_cmdEvent);
}

/* ============================================================ 전원 관리 */
/* 화면이 꺼져도 음악이 계속 나오도록 '무인 모드' + 오디오 장치 전원 유지 */
typedef BOOL (WINAPI *PFN_PPN)(DWORD, DWORD);
typedef HANDLE (WINAPI *PFN_SPR)(PVOID, int, ULONG, PVOID, ULONG);
typedef DWORD (WINAPI *PFN_RPR)(HANDLE);
static PFN_PPN pPowerPolicyNotify;
static PFN_SPR pSetPowerRequirement;
static PFN_RPR pReleasePowerRequirement;
static HANDLE g_powerReq;
static int g_unattended;

static void power_init(void)
{
    HMODULE h = LoadLibraryW(L"coredll.dll");
    if (!h) return;
    pPowerPolicyNotify = (PFN_PPN)GetProcAddressW(h, L"PowerPolicyNotify");
    pSetPowerRequirement = (PFN_SPR)GetProcAddressW(h, L"SetPowerRequirement");
    pReleasePowerRequirement = (PFN_RPR)GetProcAddressW(h, L"ReleasePowerRequirement");
}

static void power_keep(int on)
{
    if (on && !g_unattended) {
        if (pPowerPolicyNotify) pPowerPolicyNotify(0x0003 /* PPN_UNATTENDEDMODE */, TRUE);
        if (pSetPowerRequirement && !g_powerReq)
            g_powerReq = pSetPowerRequirement((PVOID)L"wav1:", 0 /* D0 */, 0x00000001 | 0x00001000 /* POWER_NAME|POWER_FORCE */, NULL, 0);
        g_unattended = 1;
    } else if (!on && g_unattended) {
        if (pPowerPolicyNotify) pPowerPolicyNotify(0x0003, FALSE);
        if (pReleasePowerRequirement && g_powerReq) pReleasePowerRequirement(g_powerReq);
        g_powerReq = NULL;
        g_unattended = 0;
    }
}

/* ============================================================ 오디오 스레드 */
typedef struct {
    HWAVEOUT hwo;
    int outRate, outCh;      /* 현재 열린 출력 형식 */
    int bufFrames;
    WAVEHDR hdr[NBUF];
    short *buf[NBUF];
    int queued[NBUF];

    Source src;
    int srcOpen;
    Dsp dsp;
    short in[1152 * 2];
    int inPos, inCount;
    int eof;

    int playing, paused, primed;
    int index;
    DWORD baseMs;            /* 마지막 reset 시점의 곡 위치 */
    DWORD framesWritten;

    int curLevel;
    int underrunsWindow;
    DWORD underrunTick;
    DWORD busyMs, audioMsDone;
} Audio;
static Audio A;

static void out_close(void)
{
    int i;
    if (!A.hwo) return;
    waveOutReset(A.hwo);
    for (i = 0; i < NBUF; i++) {
        if (A.hdr[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(A.hwo, &A.hdr[i], sizeof(WAVEHDR));
        free(A.buf[i]); A.buf[i] = NULL;
        A.queued[i] = 0;
    }
    waveOutClose(A.hwo);
    A.hwo = NULL;
    A.outRate = A.outCh = 0;
}

static int out_open(int rate, int ch)
{
    WAVEFORMATEX wf;
    int i;
    if (A.hwo && A.outRate == rate && A.outCh == ch) return 1;
    out_close();
    memset(&wf, 0, sizeof(wf));
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = (WORD)ch;
    wf.nSamplesPerSec = (DWORD)rate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = (WORD)(2 * ch);
    wf.nAvgBytesPerSec = (DWORD)(rate * 2 * ch);
    if (waveOutOpen(&A.hwo, WAVE_MAPPER, &wf, (DWORD)g_waveEvent, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        A.hwo = NULL;
        return 0;
    }
    A.outRate = rate;
    A.outCh = ch;
    A.bufFrames = rate / 5;                 /* 200ms */
    for (i = 0; i < NBUF; i++) {
        A.buf[i] = (short *)malloc((size_t)(A.bufFrames * ch) * sizeof(short));
        memset(&A.hdr[i], 0, sizeof(WAVEHDR));
        A.hdr[i].lpData = (LPSTR)A.buf[i];
        A.hdr[i].dwBufferLength = (DWORD)(A.bufFrames * ch * 2);
        waveOutPrepareHeader(A.hwo, &A.hdr[i], sizeof(WAVEHDR));
        A.queued[i] = 0;
    }
    return 1;
}

static int level_now(void)
{
    if (g_set.qmode == 0) return A.curLevel;
    return g_set.qmode - 1;
}

static void choose_format(int lvl, int *rate, int *ch)
{
    int r = g_levels[lvl].rate, c = g_levels[lvl].ch;
    if (A.src.rate < r) {
        /* 원본이 더 낮으면 원본 레이트 그대로(가까운 표준값) */
        r = A.src.rate;
        if (r != 8000 && r != 11025 && r != 16000 && r != 22050 && r != 32000) r = (r < 22050) ? 11025 : 22050;
    }
    if (A.src.channels == 1) c = 1;
    *rate = r; *ch = c;
}

static void setup_dsp(void)
{
    DspParams p;
    p.outRate = A.outRate;
    p.outCh = A.outCh;
    p.levelPct = g_levelPct[g_set.levelIdx & 3];
    p.hpfHz = g_set.earphone ? 90 : 160;   /* 이어폰은 약하게, 내장 스피커는 강하게 저음 컷 */
    p.lpfHz = 0;
    dsp_init(&A.dsp, A.src.rate, A.src.channels, &p);
    A.inPos = A.inCount = 0;
}

/* 현재 들리고 있는 위치(ms) */
static DWORD cur_pos_ms(void)
{
    DWORD played = 0;
    if (A.hwo) {
        MMTIME mt;
        mt.wType = TIME_SAMPLES;
        if (waveOutGetPosition(A.hwo, &mt, sizeof(mt)) == MMSYSERR_NOERROR) {
            if (mt.wType == TIME_SAMPLES) played = mt.u.sample;
            else if (mt.wType == TIME_BYTES) played = mt.u.cb / (DWORD)(2 * A.outCh);
            else played = A.framesWritten;
        } else {
            played = A.framesWritten;
        }
        if (played > A.framesWritten) played = A.framesWritten;
        return A.baseMs + (DWORD)((unsigned long long)played * 1000 / (DWORD)A.outRate);
    }
    return A.baseMs;
}

/* 버퍼 하나 채우기. 채운 프레임 수 반환 */
static int fill_one(int i)
{
    short *dst = A.buf[i];
    int frames = 0;
    while (frames < A.bufFrames) {
        int used, o;
        if (A.inPos >= A.inCount) {
            if (A.eof) break;
            A.inCount = src_read(&A.src, A.in, 1152);
            A.inPos = 0;
            if (A.inCount <= 0) { A.inCount = 0; A.eof = 1; break; }
        }
        o = dsp_process(&A.dsp, A.in + A.inPos * A.src.channels, A.inCount - A.inPos, &used,
                        dst + frames * A.outCh, A.bufFrames - frames);
        A.inPos += used;
        frames += o;
        if (!used && !o) break;
    }
    return frames;
}

static void write_one(int i, int frames)
{
    A.hdr[i].dwBufferLength = (DWORD)(frames * A.outCh * 2);
    A.hdr[i].dwFlags &= ~WHDR_DONE;
    if (waveOutWrite(A.hwo, &A.hdr[i], sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
        A.queued[i] = 1;
        A.framesWritten += (DWORD)frames;
    }
}

/* 출력 초기화 후 버퍼를 전부 채우고 시작 */
static void restart_output(DWORD posMs)
{
    int i;
    waveOutReset(A.hwo);
    for (i = 0; i < NBUF; i++) A.queued[i] = 0;
    A.baseMs = posMs;
    A.framesWritten = 0;
    A.primed = 0;
    waveOutPause(A.hwo);
    for (i = 0; i < NBUF && !A.eof; i++) {
        int n = fill_one(i);
        if (n > 0) write_one(i, n);
    }
    if (!A.paused) waveOutRestart(A.hwo);
}

static void publish(void)
{
    EnterCriticalSection(&g_cs);
    g_st.state = A.playing ? (A.paused ? 2 : 1) : 0;
    g_st.index = A.index;
    g_st.posMs = A.playing ? cur_pos_ms() : 0;
    g_st.durMs = A.srcOpen ? A.src.durationMs : 0;
    g_st.srcType = A.srcOpen ? A.src.type : 0;
    g_st.srcRate = A.src.rate;
    g_st.srcCh = A.src.channels;
    g_st.kbps = A.src.bitrate;
    g_st.outRate = A.outRate;
    g_st.outCh = A.outCh;
    g_st.level = A.curLevel;
    g_st.limiterHits = A.dsp.clipCount;
    LeaveCriticalSection(&g_cs);
}

/* 형식 변경(음질 변경/자동 하향) - 현재 위치 유지 */
static void reconfigure(void)
{
    int rate, ch;
    DWORD pos;
    if (!A.playing || !A.srcOpen) return;
    pos = cur_pos_ms();
    choose_format(level_now(), &rate, &ch);
    if (!out_open(rate, ch)) {
        if (!out_open(22050, 2) && !out_open(11025, 1)) { A.playing = 0; g_st.openFailed = 1; return; }
    }
    setup_dsp();
    A.eof = 0;
    src_seek_ms(&A.src, pos);
    restart_output(pos);
}

static void start_track(int index, DWORD startMs)
{
    int rate, ch;
    if (A.srcOpen) { src_close(&A.src); A.srcOpen = 0; }
    A.playing = 0;
    A.eof = 0;
    if (index < 0 || index >= g_fileCount) return;
    A.index = index;
    if (!src_open(&A.src, g_files[index])) {
        PostMessageW(g_hwnd, WM_APP_TRACKERR, (WPARAM)index, 0);
        return;
    }
    A.srcOpen = 1;
    choose_format(level_now(), &rate, &ch);
    if (!out_open(rate, ch)) {
        if (!out_open(22050, 2) && !out_open(11025, 1)) {
            g_st.openFailed = 1;
            src_close(&A.src); A.srcOpen = 0;
            return;
        }
    }
    g_st.openFailed = 0;
    setup_dsp();
    if (startMs > 0) src_seek_ms(&A.src, startMs);
    A.playing = 1;
    A.paused = 0;
    A.underrunsWindow = 0;
    A.busyMs = A.audioMsDone = 0;
    power_keep(1);
    restart_output(startMs);
}

static void auto_drop(const char *why)
{
    (void)why;
    if (g_set.qmode != 0 || A.curLevel >= 3) return;
    A.curLevel++;
    g_set.autoLevel = A.curLevel;
    A.underrunsWindow = 0;
    A.busyMs = A.audioMsDone = 0;
    reconfigure();
}

static void service(void)
{
    int i, queuedNow = 0, freed = 0;
    for (i = 0; i < NBUF; i++) {
        if (A.queued[i] && (A.hdr[i].dwFlags & WHDR_DONE)) { A.queued[i] = 0; freed++; }
        if (A.queued[i]) queuedNow++;
    }
    if (freed && A.primed && queuedNow == 0 && !A.eof) {
        /* 버퍼가 모두 비었음 = 끊김 발생 */
        DWORD now = GetTickCount();
        g_st.underruns++;
        if (now - A.underrunTick > 20000) A.underrunsWindow = 0;
        A.underrunTick = now;
        A.underrunsWindow++;
        if (A.underrunsWindow >= 2) { auto_drop("underrun"); return; }
    }
    if (freed) A.primed = 1;

    for (i = 0; i < NBUF && !A.eof; i++) {
        if (!A.queued[i]) {
            DWORD t0 = GetTickCount();
            int n = fill_one(i);
            A.busyMs += GetTickCount() - t0;
            if (n > 0) {
                write_one(i, n);
                A.audioMsDone += (DWORD)(n * 1000 / A.outRate);
            }
        }
    }
    /* 약 6초 분량마다 CPU 부하 확인: 디코딩에 재생 시간의 75% 이상 쓰면 음질 하향 */
    if (A.audioMsDone >= 6000) {
        int load = (int)(A.busyMs * 100 / A.audioMsDone);
        g_st.loadPct = load;
        A.busyMs = A.audioMsDone = 0;
        if (load >= 75) { auto_drop("cpu"); return; }
    }

    if (A.eof) {
        int any = 0;
        for (i = 0; i < NBUF; i++) if (A.queued[i]) any = 1;
        if (!any) {
            A.playing = 0;
            PostMessageW(g_hwnd, WM_APP_TRACKEND, (WPARAM)A.index, 0);
        }
    }
}

static DWORD WINAPI audio_thread(LPVOID unused)
{
    (void)unused;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    A.curLevel = (g_set.autoLevel >= 0 && g_set.autoLevel <= 3) ? g_set.autoLevel : 1;
    for (;;) {
        Cmd c;
        int have;
        {
            HANDLE hs[2];
            hs[0] = g_cmdEvent; hs[1] = g_waveEvent;
            WaitForMultipleObjects(2, hs, FALSE, (A.playing && !A.paused) ? 40 : 300);
        }
        for (;;) {
            EnterCriticalSection(&g_cs);
            have = (g_cmdHead != g_cmdTail);
            if (have) { c = g_cmdq[g_cmdHead]; g_cmdHead = (g_cmdHead + 1) % CMDQ; }
            LeaveCriticalSection(&g_cs);
            if (!have) break;
            switch (c.cmd) {
            case CMD_PLAY:
                start_track(c.index, c.ms);
                break;
            case CMD_PAUSE:
                if (A.playing && A.hwo) {
                    if (A.paused) { waveOutRestart(A.hwo); A.paused = 0; power_keep(1); }
                    else { waveOutPause(A.hwo); A.paused = 1; power_keep(0); }
                }
                break;
            case CMD_STOP:
                if (A.hwo) { waveOutReset(A.hwo); { int i; for (i = 0; i < NBUF; i++) A.queued[i] = 0; } }
                A.playing = 0; A.paused = 0;
                power_keep(0);
                break;
            case CMD_SEEK:
                if (A.playing && A.srcOpen) {
                    A.eof = 0;
                    src_seek_ms(&A.src, c.ms);
                    setup_dsp();
                    restart_output(c.ms);
                }
                break;
            case CMD_RECONFIG:
                if (g_set.qmode != 0) A.curLevel = g_set.qmode - 1;
                else if (c.index >= 0) A.curLevel = c.index;   /* 자동으로 되돌릴 때 '보통'부터 */
                if (A.playing) {
                    /* 레벨/이어폰 모드 변경도 여기서 반영 */
                    reconfigure();
                }
                break;
            case CMD_QUIT:
                out_close();
                if (A.srcOpen) src_close(&A.src);
                power_keep(0);
                return 0;
            }
        }
        if (A.playing && !A.paused && A.hwo) service();
        publish();
    }
}

/* ============================================================ 파일 검색 */
static int is_music(const wchar_t *name)
{
    const wchar_t *e = wcsrchr(name, L'.');
    if (!e) return 0;
    return !_wcsicmp(e, L".mp3") || !_wcsicmp(e, L".wav");
}

static void add_file(const wchar_t *path)
{
    wchar_t *p;
    if (g_fileCount >= MAX_FILES) return;
    p = (wchar_t *)malloc((wcslen(path) + 1) * sizeof(wchar_t));
    if (!p) return;
    wcscpy(p, path);
    g_files[g_fileCount++] = p;
}

static void scan_dir(const wchar_t *dir, int depth)
{
    WIN32_FIND_DATAW fd;
    HANDLE h;
    wchar_t pat[MAX_PATH];
    if (depth > 8 || wcslen(dir) + 4 >= MAX_PATH) return;
    wcscpy(pat, dir);
    wcscat(pat, L"\\*");
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        wchar_t full[MAX_PATH];
        if (fd.cFileName[0] == L'.') continue;
        if (wcslen(dir) + wcslen(fd.cFileName) + 2 >= MAX_PATH) continue;
        wcscpy(full, dir);
        wcscat(full, L"\\");
        wcscat(full, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;
            scan_dir(full, depth + 1);
        } else if (is_music(fd.cFileName)) {
            add_file(full);
        }
    } while (FindNextFileW(h, &fd) && g_fileCount < MAX_FILES);
    FindClose(h);
}

static int cmp_path(const void *a, const void *b)
{
    return _wcsicmp(*(wchar_t * const *)a, *(wchar_t * const *)b);
}

static void free_files(void)
{
    int i;
    for (i = 0; i < g_fileCount; i++) free(g_files[i]);
    g_fileCount = 0;
}

/* SD카드(루트의 '임시' 속성 폴더 = 저장 카드)와 내 문서 폴더를 검색 */
static void scan_all(void)
{
    WIN32_FIND_DATAW fd;
    HANDLE h;
    free_files();
    h = FindFirstFileW(L"\\*", &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                (fd.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY)) {
                wchar_t root[MAX_PATH];
                root[0] = L'\\'; root[1] = 0;
                wcscat(root, fd.cFileName);
                scan_dir(root, 0);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    scan_dir(L"\\My Documents", 0);
    if (g_fileCount > 1) qsort(g_files, (size_t)g_fileCount, sizeof(wchar_t *), cmp_path);
}

static void path_in_exe_dir(wchar_t *out, const wchar_t *name)
{
    wcscpy(out, g_exeDir);
    wcscat(out, name);
}

/* 목록 캐시(UTF-16 텍스트, 줄 단위) */
static void save_list(void)
{
    wchar_t p[MAX_PATH];
    HANDLE h;
    DWORD w;
    int i;
    path_in_exe_dir(p, L"M480Music_list.txt");
    h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, "\xFF\xFE", 2, &w, NULL);
    for (i = 0; i < g_fileCount; i++) {
        WriteFile(h, g_files[i], (DWORD)(wcslen(g_files[i]) * 2), &w, NULL);
        WriteFile(h, L"\r\n", 4, &w, NULL);
    }
    CloseHandle(h);
}

static int load_list(void)
{
    wchar_t p[MAX_PATH];
    HANDLE h;
    DWORD sz, got;
    wchar_t *buf, *s, *e;
    path_in_exe_dir(p, L"M480Music_list.txt");
    h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    sz = GetFileSize(h, NULL);
    if (sz < 4 || sz > 4 * 1024 * 1024) { CloseHandle(h); return 0; }
    buf = (wchar_t *)malloc(sz + 2);
    if (!buf) { CloseHandle(h); return 0; }
    ReadFile(h, buf, sz, &got, NULL);
    CloseHandle(h);
    buf[got / 2] = 0;
    free_files();
    s = buf + (buf[0] == 0xFEFF ? 1 : 0);
    while (*s) {
        e = s;
        while (*e && *e != L'\r' && *e != L'\n') e++;
        if (e > s) {
            wchar_t c = *e;
            *e = 0;
            if (GetFileAttributesW(s) != 0xFFFFFFFF || g_fileCount < 50) add_file(s);
            *e = c;
        }
        while (*e == L'\r' || *e == L'\n') e++;
        s = e;
    }
    free(buf);
    return g_fileCount > 0;
}

static void save_settings(void)
{
    wchar_t p[MAX_PATH];
    HANDLE h;
    DWORD w;
    path_in_exe_dir(p, L"M480Music.dat");
    h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, &g_set, sizeof(g_set), &w, NULL);
    CloseHandle(h);
}

static void load_settings(void)
{
    wchar_t p[MAX_PATH];
    HANDLE h;
    DWORD got = 0;
    Settings s = g_set;   /* 예전 버전 설정 파일(짧음)도 읽히도록 기본값 위에 덮어씀 */
    path_in_exe_dir(p, L"M480Music.dat");
    h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    ReadFile(h, &s, sizeof(s), &got, NULL);
    CloseHandle(h);
    if (got >= 8 * sizeof(int) && s.magic == SET_MAGIC) {
        g_set = s;
        if (g_set.qmode < 0 || g_set.qmode > 4) g_set.qmode = 0;
        if (g_set.levelIdx < 0 || g_set.levelIdx > 3) g_set.levelIdx = 1;
        if (g_set.order < 0 || g_set.order > 2) g_set.order = 0;
        if (g_set.autoLevel < 0 || g_set.autoLevel > 3) g_set.autoLevel = 1;
        g_set.compact = g_set.compact ? 1 : 0;
        g_set.panel = g_set.panel ? 1 : 0;
    }
}

/* ============================================================ UI */
/* 화면 3개를 모두 직접 그린다 (애플뮤직/유튜브뮤직 스타일 다크 테마)
 *  보관함   : 큰 제목, [재생][셔플] 버튼, 앨범아트가 있는 곡 목록, 아래쪽 미니 플레이어
 *  재생 화면: 앨범아트(ID3/폴더 커버), 제목/가수, 진행 막대, 큰 재생 버튼, 설정 칩
 *  설정     : iOS 설정식 목록
 */
#include "art.h"

#define SCR_LIB 0
#define SCR_NOW 1
#define SCR_SET 2

#define WM_APP_ART (WM_APP + 3)

#define C_BG      RGB(12, 12, 14)
#define C_CARD    RGB(30, 30, 34)
#define C_PRESS   RGB(46, 46, 52)
#define C_TEXT    RGB(246, 246, 248)
#define C_SUB     RGB(150, 150, 158)
#define C_DIM     RGB(105, 105, 112)
#define C_SEP     RGB(38, 38, 42)
#define C_ACCENT  RGB(255, 112, 84)
#define C_MINI    RGB(28, 28, 32)
#define C_CHIP    RGB(255, 255, 255)

static int g_scale = 1;
#define S(x) ((x) * g_scale)

static HFONT fTitle, fHead, fBold, fNorm, fSmall, fGlyphS, fGlyphB;
static int W, H;
static int g_scr = SCR_LIB;
static int g_curIndex = -1;
static int g_errorStreak;
static int g_sel = -1;              /* 키 이동용 선택 */
static int g_selVisible;            /* 방향키를 쓴 뒤에만 선택 표시 */
static int g_scroll[3];
static wchar_t **g_title, **g_sub;  /* 목록 표시용 제목/부제 */

/* 폴더별 보기 */
typedef struct { wchar_t *dir; const wchar_t *name; int *items; int n, cap; } Folder;
static Folder *g_folders;
static int g_folderCount, g_folderCap;
#define LIB_SONGS   0
#define LIB_FOLDERS 1
#define LIB_INSIDE  2
static int g_libMode;               /* 0 노래 / 1 폴더 목록 / 2 폴더 안 */
static int g_openFolder = -1;
static int g_libScroll[3];
static RECT g_hit[5];               /* 머리말 버튼: 탭0/뒤로, 탭1, 재생, 셔플, ••• */
/* 재생 대기열: NULL이면 전체 곡 */
static int *g_queue;
static int g_qLen;
static HBITMAP g_bb;                /* 백버퍼 */
static int g_bbW, g_bbH;
static int g_busy;                  /* 목록 검색 중 */
static DWORD g_tick;                /* 이퀄라이저 애니메이션 */

/* 앨범아트 */
static ArtResult *g_art;
static int g_artIndex = -1, g_artGen;
static HBITMAP g_artBig, g_artSmall;
static int g_artBigN, g_artSmallN;

/* 레이아웃 */
static int L_listTop, L_listBot, L_rowH, L_miniH;
static int N_side, N_art, N_artX, N_artY, N_txtX, N_txtY, N_txtW;
static int N_barY, N_ctrlY, N_chipY;
static RECT N_bar;

/* 터치 */
static int t_down, t_drag, t_x0, t_y0, t_lastY, t_prevY, t_scroll0, t_pressRow = -1;
static DWORD t_lastT, t_prevT;
static int t_vel;                   /* 관성 스크롤 속도 (px/틱, Q4) */
static int t_scrub;
static DWORD t_scrubMs;

static const COLORREF g_pal[8][2] = {
    { RGB(236, 72, 98),  RGB(120, 24, 60) },  { RGB(255, 140, 60), RGB(150, 50, 20) },
    { RGB(90, 160, 255), RGB(30, 50, 140) },  { RGB(120, 210, 150), RGB(20, 90, 70) },
    { RGB(180, 110, 255), RGB(70, 30, 140) }, { RGB(255, 200, 70),  RGB(160, 90, 10) },
    { RGB(70, 200, 220), RGB(20, 80, 110) },  { RGB(240, 110, 180), RGB(110, 30, 90) },
};

/* ---------------------------------------------------------------- 공통 */
static const wchar_t *base_name(const wchar_t *p)
{
    const wchar_t *s = wcsrchr(p, L'\\');
    return s ? s + 1 : p;
}

static unsigned name_hash(const wchar_t *s)
{
    unsigned h = 5381;
    while (*s) h = h * 33 + (unsigned)*s++;
    return h;
}

static wchar_t *wdup(const wchar_t *s, int n)
{
    wchar_t *p = (wchar_t *)malloc((size_t)(n + 1) * sizeof(wchar_t));
    if (!p) return NULL;
    memcpy(p, s, (size_t)n * sizeof(wchar_t));
    p[n] = 0;
    return p;
}

static void trim_copy(const wchar_t *s, int n, wchar_t **out)
{
    while (n > 0 && *s == L' ') { s++; n--; }
    while (n > 0 && s[n - 1] == L' ') n--;
    *out = wdup(s, n);
}

/* "가수 - 제목.mp3" → 제목/가수, 아니면 제목/폴더명 */
static void build_names(void)
{
    int i;
    if (g_title) {
        for (i = 0; i < MAX_FILES; i++) { free(g_title[i]); free(g_sub[i]); g_title[i] = g_sub[i] = NULL; }
    } else {
        g_title = (wchar_t **)calloc(MAX_FILES, sizeof(wchar_t *));
        g_sub = (wchar_t **)calloc(MAX_FILES, sizeof(wchar_t *));
    }
    for (i = 0; i < g_fileCount; i++) {
        const wchar_t *b = base_name(g_files[i]);
        const wchar_t *dot = wcsrchr(b, L'.');
        int n = dot ? (int)(dot - b) : (int)wcslen(b);
        const wchar_t *dash = NULL;
        int k;
        for (k = 0; k + 2 < n; k++)
            if (b[k] == L' ' && b[k + 1] == L'-' && b[k + 2] == L' ') { dash = b + k; break; }
        if (dash && dash > b) {
            trim_copy(dash + 3, n - (int)(dash - b) - 3, &g_title[i]);
            trim_copy(b, (int)(dash - b), &g_sub[i]);
        } else {
            const wchar_t *e = b - 1, *s;
            trim_copy(b, n, &g_title[i]);
            /* 부제: 상위 폴더 이름 */
            s = e;
            while (s > g_files[i] && s[-1] != L'\\') s--;
            if (e > s) trim_copy(s, (int)(e - s), &g_sub[i]);
            else g_sub[i] = wdup(L"", 0);
        }
        if (!g_title[i]) g_title[i] = wdup(L"?", 1);
        if (!g_sub[i]) g_sub[i] = wdup(L"", 0);
    }
}

static void free_folders(void)
{
    int i;
    for (i = 0; i < g_folderCount; i++) { free(g_folders[i].dir); free(g_folders[i].items); }
    g_folderCount = 0;
}

static int cmp_folder(const void *a, const void *b)
{
    const Folder *x = (const Folder *)a, *y = (const Folder *)b;
    int c = _wcsicmp(x->name, y->name);
    return c ? c : _wcsicmp(x->dir, y->dir);
}

/* 곡이 들어 있는 폴더(바로 위 폴더) 별로 묶기 */
static void build_folders(void)
{
    int i, last = -1;
    free_folders();
    g_queue = NULL; g_qLen = 0;
    for (i = 0; i < g_fileCount; i++) {
        const wchar_t *path = g_files[i];
        const wchar_t *sl = wcsrchr(path, L'\\');
        int dl = sl ? (int)(sl - path) : 0, f = -1, k;
        if (last >= 0 && (int)wcslen(g_folders[last].dir) == dl && !_wcsnicmp(g_folders[last].dir, path, (size_t)dl)) f = last;
        for (k = 0; f < 0 && k < g_folderCount; k++)
            if ((int)wcslen(g_folders[k].dir) == dl && !_wcsnicmp(g_folders[k].dir, path, (size_t)dl)) f = k;
        if (f < 0) {
            Folder *fo;
            const wchar_t *nm;
            if (g_folderCount >= g_folderCap) {
                int nc = g_folderCap ? g_folderCap * 2 : 32;
                Folder *nf = (Folder *)realloc(g_folders, (size_t)nc * sizeof(Folder));
                if (!nf) continue;
                g_folders = nf; g_folderCap = nc;
            }
            fo = &g_folders[g_folderCount];
            memset(fo, 0, sizeof(*fo));
            fo->dir = wdup(path, dl);
            if (!fo->dir) continue;
            nm = wcsrchr(fo->dir, L'\\');
            fo->name = (nm && nm[1]) ? nm + 1 : (dl ? fo->dir : L"\\");
            f = g_folderCount++;
        }
        if (g_folders[f].n >= g_folders[f].cap) {
            int nc = g_folders[f].cap ? g_folders[f].cap * 2 : 16;
            int *ni = (int *)realloc(g_folders[f].items, (size_t)nc * sizeof(int));
            if (!ni) continue;
            g_folders[f].items = ni; g_folders[f].cap = nc;
        }
        g_folders[f].items[g_folders[f].n++] = i;
        last = f;
    }
    if (g_folderCount > 1) qsort(g_folders, (size_t)g_folderCount, sizeof(Folder), cmp_folder);
    if (g_libMode == LIB_INSIDE) g_libMode = LIB_FOLDERS;
    g_openFolder = -1;
}

/* 현재 보기의 줄 수 / 줄 → 곡 번호 */
static int lib_count(void)
{
    if (g_libMode == LIB_FOLDERS) return g_folderCount;
    if (g_libMode == LIB_INSIDE && g_openFolder >= 0) return g_folders[g_openFolder].n;
    return g_fileCount;
}

static int lib_file(int row)
{
    if (row < 0) return -1;
    if (g_libMode == LIB_FOLDERS) return -1;
    if (g_libMode == LIB_INSIDE && g_openFolder >= 0)
        return row < g_folders[g_openFolder].n ? g_folders[g_openFolder].items[row] : -1;
    return row < g_fileCount ? row : -1;
}

static int folder_has(int f, int file)
{
    int i;
    if (f < 0 || file < 0) return 0;
    for (i = 0; i < g_folders[f].n; i++) if (g_folders[f].items[i] == file) return 1;
    return 0;
}

static void fmt_time(wchar_t *o, DWORD ms)
{
    DWORD s = ms / 1000;
    wsprintfW(o, L"%d:%02d", (int)(s / 60), (int)(s % 60));
}

static Status get_status(void)
{
    Status st;
    EnterCriticalSection(&g_cs); st = g_st; LeaveCriticalSection(&g_cs);
    return st;
}

static COLORREF mix(COLORREF a, COLORREF b, int t256)
{
    int r = (GetRValue(a) * (256 - t256) + GetRValue(b) * t256) >> 8;
    int g = (GetGValue(a) * (256 - t256) + GetGValue(b) * t256) >> 8;
    int bl = (GetBValue(a) * (256 - t256) + GetBValue(b) * t256) >> 8;
    return RGB(r, g, bl);
}

/* ---------------------------------------------------------------- 그리기 도구 */
static void fill(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    RECT r;
    HBRUSH b = CreateSolidBrush(c);
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void rrect(HDC dc, int x, int y, int w, int h, int rad, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c), ob;
    HPEN op = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
    ob = (HBRUSH)SelectObject(dc, b);
    RoundRect(dc, x, y, x + w + 1, y + h + 1, rad * 2, rad * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
}

static void circle(HDC dc, int cx, int cy, int r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c), ob;
    HPEN op = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
    ob = (HBRUSH)SelectObject(dc, b);
    Ellipse(dc, cx - r, cy - r, cx + r + 1, cy + r + 1);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
}

static void poly(HDC dc, POINT *p, int n, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c), ob;
    HPEN pen = CreatePen(PS_SOLID, 1, c), op;
    ob = (HBRUSH)SelectObject(dc, b);
    op = (HPEN)SelectObject(dc, pen);
    Polygon(dc, p, n);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(pen);
}

static void lines(HDC dc, POINT *p, int n, int width, COLORREF c)
{
    HPEN pen = CreatePen(PS_SOLID, width, c), op;
    op = (HPEN)SelectObject(dc, pen);
    Polyline(dc, p, n);
    SelectObject(dc, op);
    DeleteObject(pen);
}

static void text(HDC dc, const wchar_t *s, int x, int y, int w, int h, HFONT f, COLORREF c, UINT fmt)
{
    RECT r;
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fmt | DT_NOPREFIX);
}
#define TL (DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS)
#define TC (DT_CENTER | DT_SINGLELINE | DT_VCENTER)
#define TR (DT_RIGHT | DT_SINGLELINE | DT_VCENTER)

static int text_w(HDC dc, const wchar_t *s, HFONT f)
{
    RECT r;
    r.left = r.top = 0; r.right = 2000; r.bottom = 100;
    SelectObject(dc, f);
    DrawTextW(dc, s, -1, &r, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
    return r.right;
}

static void icon_play(HDC dc, int cx, int cy, int sz, COLORREF c)
{
    POINT p[3];
    p[0].x = cx - sz * 4 / 10; p[0].y = cy - sz / 2;
    p[1].x = cx - sz * 4 / 10; p[1].y = cy + sz / 2;
    p[2].x = cx + sz / 2;      p[2].y = cy;
    poly(dc, p, 3, c);
}

static void icon_pause(HDC dc, int cx, int cy, int sz, COLORREF c)
{
    int bw = sz * 3 / 10 + 1;
    rrect(dc, cx - sz / 2 + 1, cy - sz / 2, bw, sz, 1, c);
    rrect(dc, cx + sz / 2 - bw, cy - sz / 2, bw, sz, 1, c);
}

static void icon_skip(HDC dc, int cx, int cy, int sz, int dir, COLORREF c)
{
    /* ▶▶ 또는 ◀◀ */
    POINT p[3];
    int h = sz / 2, w = sz / 2, k;
    for (k = 0; k < 2; k++) {
        int x0 = cx + (k ? 0 : -w) * dir;
        p[0].x = x0;            p[0].y = cy - h;
        p[1].x = x0;            p[1].y = cy + h;
        p[2].x = x0 + w * dir;  p[2].y = cy;
        poly(dc, p, 3, c);
    }
}

static void icon_dots(HDC dc, int cx, int cy, COLORREF c)
{
    circle(dc, cx - S(6), cy, S(2), c);
    circle(dc, cx, cy, S(2), c);
    circle(dc, cx + S(6), cy, S(2), c);
}

static void icon_chevron_down(HDC dc, int cx, int cy, COLORREF c)
{
    POINT p[3];
    p[0].x = cx - S(8); p[0].y = cy - S(3);
    p[1].x = cx;        p[1].y = cy + S(4);
    p[2].x = cx + S(8); p[2].y = cy - S(3);
    lines(dc, p, 3, S(3), c);
}

static void icon_chevron_left(HDC dc, int cx, int cy, COLORREF c)
{
    POINT p[3];
    p[0].x = cx + S(4); p[0].y = cy - S(8);
    p[1].x = cx - S(4); p[1].y = cy;
    p[2].x = cx + S(4); p[2].y = cy + S(8);
    lines(dc, p, 3, S(3), c);
}

static void icon_chevron_right(HDC dc, int cx, int cy, COLORREF c)
{
    POINT p[3];
    p[0].x = cx - S(3); p[0].y = cy - S(5);
    p[1].x = cx + S(2); p[1].y = cy;
    p[2].x = cx - S(3); p[2].y = cy + S(5);
    lines(dc, p, 3, S(2), c);
}

static void icon_shuffle(HDC dc, int cx, int cy, COLORREF c)
{
    POINT a[2], b[2], h1[3], h2[3];
    int w = S(7), h = S(5);
    a[0].x = cx - w; a[0].y = cy - h; a[1].x = cx + w - S(2); a[1].y = cy + h;
    b[0].x = cx - w; b[0].y = cy + h; b[1].x = cx + w - S(2); b[1].y = cy - h;
    lines(dc, a, 2, S(2), c);
    lines(dc, b, 2, S(2), c);
    h1[0].x = cx + w - S(4); h1[0].y = cy + h - S(3); h1[1].x = cx + w + S(1); h1[1].y = cy + h; h1[2].x = cx + w - S(4); h1[2].y = cy + h + S(3);
    h2[0].x = cx + w - S(4); h2[0].y = cy - h - S(3); h2[1].x = cx + w + S(1); h2[1].y = cy - h; h2[2].x = cx + w - S(4); h2[2].y = cy - h + S(3);
    poly(dc, h1, 3, c);
    poly(dc, h2, 3, c);
}

/* 수직 그라데이션 */
static void grad(HDC dc, int x, int y, int w, int h, COLORREF top, COLORREF bot)
{
    int i, steps = h / S(4);
    if (steps < 1) steps = 1;
    for (i = 0; i < steps; i++) {
        int y0 = y + h * i / steps, y1 = y + h * (i + 1) / steps;
        fill(dc, x, y0, w, y1 - y0, mix(top, bot, i * 256 / steps));
    }
}

/* 둥근 모서리: 모서리 바깥 픽셀을 배경색으로 */
static void round_corners(HDC dc, int x, int y, int s, int r, COLORREF bgTop, COLORREF bgBot)
{
    int dx, dy;
    for (dy = 0; dy < r; dy++)
        for (dx = 0; dx < r; dx++) {
            int ex = r - dx, ey = r - dy;
            if (ex * ex + ey * ey > r * r + r / 2) {
                SetPixel(dc, x + dx, y + dy, bgTop);
                SetPixel(dc, x + s - 1 - dx, y + dy, bgTop);
                SetPixel(dc, x + dx, y + s - 1 - dy, bgBot);
                SetPixel(dc, x + s - 1 - dx, y + s - 1 - dy, bgBot);
            }
        }
}

static HBITMAP make_dib565(HDC ref, const unsigned short *px, int n)
{
    struct { BITMAPINFOHEADER h; DWORD m[3]; } bi;
    unsigned char *bits = NULL;
    HBITMAP bmp;
    int y, stride = ((n * 2) + 3) & ~3;
    memset(&bi, 0, sizeof(bi));
    bi.h.biSize = sizeof(BITMAPINFOHEADER);
    bi.h.biWidth = n;
    bi.h.biHeight = -n;
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 16;
    bi.h.biCompression = BI_BITFIELDS;
    bi.m[0] = 0xF800; bi.m[1] = 0x07E0; bi.m[2] = 0x001F;
    bmp = CreateDIBSection(ref, (BITMAPINFO *)&bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (!bmp || !bits) return NULL;
    for (y = 0; y < n; y++) memcpy(bits + y * stride, px + y * n, (size_t)n * 2);
    return bmp;
}

static void blit_bmp(HDC dc, HBITMAP bmp, int n, int x, int y, int s)
{
    HDC m = CreateCompatibleDC(dc);
    HBITMAP o = (HBITMAP)SelectObject(m, bmp);
    if (n == s) BitBlt(dc, x, y, s, s, m, 0, 0, SRCCOPY);
    else StretchBlt(dc, x, y, s, s, m, 0, 0, n, n, SRCCOPY);
    SelectObject(m, o);
    DeleteDC(m);
}

/* 앨범아트 또는 자동 생성 커버 */
static void draw_art(HDC dc, int idx, int x, int y, int s, int big, COLORREF bgTop, COLORREF bgBot)
{
    int rad = s >= S(80) ? S(8) : S(4);
    if (idx == g_artIndex && idx >= 0 && (big ? g_artBig : g_artSmall)) {
        if (big) blit_bmp(dc, g_artBig, g_artBigN, x, y, s);
        else blit_bmp(dc, g_artSmall, g_artSmallN, x, y, s);
    } else {
        unsigned h = (idx >= 0 && idx < g_fileCount) ? name_hash(g_sub[idx][0] ? g_sub[idx] : g_title[idx]) : 0;
        const COLORREF *p = g_pal[h & 7];
        grad(dc, x, y, s, s, p[0], p[1]);
        SetBkMode(dc, TRANSPARENT);
        text(dc, L"♪", x, y, s, s, big ? fGlyphB : fGlyphS, RGB(255, 255, 255), TC);
    }
    round_corners(dc, x, y, s, rad, bgTop, bgBot);
}

static void draw_eq(HDC dc, int x, int y, int s, COLORREF c, int animate)
{
    static const int ph[8] = { 3, 6, 9, 7, 4, 8, 5, 9 };
    int bw = s / 8, gap = s / 12, i, total = bw * 3 + gap * 2;
    int x0 = x + (s - total) / 2, base = y + s * 3 / 4;
    for (i = 0; i < 3; i++) {
        int hh = animate ? ph[(g_tick + i * 3) & 7] : 4;
        hh = s * hh / 20;
        fill(dc, x0 + i * (bw + gap), base - hh, bw, hh, c);
    }
}

/* ---------------------------------------------------------------- 레이아웃 */
static void layout(void)
{
    int fixedSide, fixedVert;
    L_rowH = S(40);
    L_miniH = S(50);
    L_listTop = S(46);   /* 머리말 높이 (목록과 함께 스크롤됨) */
    L_listBot = H - ((g_curIndex >= 0) ? L_miniH : 0);

    /* 재생 화면: 정사각/가로 화면이면 아트 옆에 글자, 세로 화면이면 아트 위 */
    fixedSide = S(30) + S(8) + S(14) + S(30) + S(56) + S(36) + S(6);
    fixedVert = S(30) + S(10) + S(46) + S(30) + S(56) + S(36) + S(6);
    N_side = (H < W * 5 / 4);
    if (N_side) {
        N_art = H - fixedSide;
        if (N_art > W / 2) N_art = W / 2;
        if (N_art < S(60)) N_art = S(60);
        N_artX = S(16);
        N_artY = S(30) + S(8);
        N_txtX = N_artX + N_art + S(12);
        N_txtY = N_artY + S(2);
        N_txtW = W - N_txtX - S(14);
        N_barY = N_artY + N_art + S(14);
    } else {
        N_art = H - fixedVert;
        if (N_art > W - S(48)) N_art = W - S(48);
        if (N_art < S(60)) N_art = S(60);
        N_artX = (W - N_art) / 2;
        N_artY = S(30) + S(6);
        N_txtX = S(20);
        N_txtW = W - S(40);
        N_txtY = N_artY + N_art + S(10);
        N_barY = N_txtY + S(46);
    }
    N_bar.left = S(18); N_bar.right = W - S(18);
    N_bar.top = N_barY; N_bar.bottom = N_barY + S(4);
    N_ctrlY = N_barY + S(30) + S(26);
    N_chipY = N_ctrlY + S(30);
    if (N_chipY + S(30) > H) N_chipY = H - S(32);
}

static int list_content_h(void) { return L_listTop + lib_count() * L_rowH; }
static int set_rows(void) { return 7; }
#define SET_ROWH S(40)
#define SET_TOP  S(48)

static int max_scroll(int scr)
{
    int m = 0;
    if (scr == SCR_LIB) m = list_content_h() - L_listBot;
    if (scr == SCR_SET) m = SET_TOP + set_rows() * SET_ROWH + S(40) - H;
    return m > 0 ? m : 0;
}

static void clamp_scroll(int scr)
{
    int m = max_scroll(scr);
    if (g_scroll[scr] > m) g_scroll[scr] = m;
    if (g_scroll[scr] < 0) g_scroll[scr] = 0;
}

static void ensure_visible(int idx)
{
    int top = L_listTop + idx * L_rowH, view = L_listBot;
    if (idx < 0) return;
    if (top < g_scroll[SCR_LIB]) g_scroll[SCR_LIB] = top;
    else if (top + L_rowH > g_scroll[SCR_LIB] + view) g_scroll[SCR_LIB] = top + L_rowH - view;
    clamp_scroll(SCR_LIB);
}

/* ---------------------------------------------------------------- 곡 정보 */
static const wchar_t *cur_title(void)
{
    if (g_curIndex < 0 || g_curIndex >= g_fileCount) return L"";
    if (g_art && g_artIndex == g_curIndex && g_art->title[0]) return g_art->title;
    return g_title[g_curIndex];
}

static const wchar_t *cur_artist(void)
{
    if (g_curIndex < 0 || g_curIndex >= g_fileCount) return L"";
    if (g_art && g_artIndex == g_curIndex && g_art->artist[0]) return g_art->artist;
    return g_sub[g_curIndex];
}

static COLORREF now_tint(void)
{
    if (g_art && g_artIndex == g_curIndex && g_art->bigPx) {
        COLORREF a = g_art->avg;
        /* 너무 어둡거나 밝은 색은 보정 */
        int l = (GetRValue(a) + GetGValue(a) * 2 + GetBValue(a)) / 4;
        if (l < 60) a = mix(a, RGB(90, 90, 100), 120);
        return a;
    }
    if (g_curIndex >= 0 && g_curIndex < g_fileCount) {
        unsigned h = name_hash(g_sub[g_curIndex][0] ? g_sub[g_curIndex] : g_title[g_curIndex]);
        return g_pal[h & 7][1];
    }
    return RGB(60, 60, 70);
}

static void now_bg(COLORREF *top, COLORREF *bot)
{
    COLORREF t = now_tint();
    *top = mix(C_BG, t, 150);
    *bot = mix(C_BG, t, 30);
}

static COLORREF now_bg_at(int y)
{
    COLORREF t, b;
    now_bg(&t, &b);
    if (y < 0) y = 0;
    if (y > H) y = H;
    return mix(t, b, H ? y * 256 / H : 0);
}

/* ---------------------------------------------------------------- 보관함 화면 */
static void set_hit(int k, int x, int y, int w, int h)
{
    g_hit[k].left = x; g_hit[k].top = y; g_hit[k].right = x + w; g_hit[k].bottom = y + h;
}

static void draw_folder_art(HDC dc, int f, int x, int y, int s, COLORREF bg)
{
    const COLORREF *p = g_pal[name_hash(g_folders[f].name) & 7];
    grad(dc, x, y, s, s, p[0], p[1]);
    rrect(dc, x + s * 22 / 100, y + s * 30 / 100, s * 26 / 100, s * 14 / 100, S(2), RGB(255, 255, 255));
    rrect(dc, x + s * 22 / 100, y + s * 37 / 100, s * 56 / 100, s * 38 / 100, S(2), RGB(255, 255, 255));
    round_corners(dc, x, y, s, S(4), bg, bg);
}

static void paint_lib(HDC dc, const Status *st)
{
    int i, first, count = lib_count();
    wchar_t buf[64];
    int scroll = g_scroll[SCR_LIB];
    int hy = -scroll;             /* 머리말 위치 */
    int art = L_rowH - S(10);

    fill(dc, 0, 0, W, H, C_BG);
    SetBkMode(dc, TRANSPARENT);
    memset(g_hit, 0, sizeof(g_hit));

    /* 머리말 (목록과 함께 위로 밀려 올라감) */
    if (hy + L_listTop > 0) {
        int cy = hy + L_listTop / 2;
        int rr = S(15);
        int pcx = W - S(94), scx = W - S(58);
        if (g_libMode == LIB_INSIDE && g_openFolder >= 0) {
            Folder *fo = &g_folders[g_openFolder];
            icon_chevron_left(dc, S(16), cy, C_ACCENT);
            set_hit(0, 0, hy, pcx - rr - S(4), L_listTop);
            text(dc, fo->name, S(30), hy + S(4), pcx - rr - S(36), S(22), fHead, C_TEXT, TL);
            wsprintfW(buf, L"%d곡", fo->n);
            text(dc, buf, S(30), hy + S(25), pcx - rr - S(36), S(16), fSmall, C_SUB, TL);
        } else {
            int k, x = S(12);
            static const wchar_t *tabs[2] = { L"노래", L"폴더" };
            for (k = 0; k < 2; k++) {
                int tw = text_w(dc, tabs[k], fHead) + S(24);
                int on = (g_libMode == k);
                rrect(dc, x, cy - S(15), tw, S(30), S(15), on ? RGB(245, 245, 248) : C_CARD);
                text(dc, tabs[k], x, cy - S(15), tw, S(30), fHead, on ? C_BG : C_SUB, TC);
                set_hit(k, x, hy, tw, L_listTop);
                x += tw + S(6);
            }
        }
        circle(dc, pcx, cy, rr, C_CARD);
        icon_play(dc, pcx + S(1), cy, S(12), C_ACCENT);
        circle(dc, scx, cy, rr, C_CARD);
        icon_shuffle(dc, scx, cy, C_ACCENT);
        icon_dots(dc, W - S(22), cy, C_ACCENT);
        set_hit(2, pcx - rr - S(3), hy, rr * 2 + S(5), L_listTop);
        set_hit(3, scx - rr - S(2), hy, rr * 2 + S(5), L_listTop);
        set_hit(4, W - S(40), hy, S(40), L_listTop);
    }

    /* 목록 */
    if (count > 0) {
        first = (scroll - L_listTop) / L_rowH;
        if (first < 0) first = 0;
        for (i = first; i < count; i++) {
            int ry = L_listTop + i * L_rowH - scroll;
            COLORREF rowbg = C_BG;
            const wchar_t *t1, *t2;
            int playing, f = lib_file(i);
            if (ry >= L_listBot) break;
            if (ry + L_rowH <= 0) continue;
            if (i == t_pressRow || (i == g_sel && t_pressRow < 0 && g_selVisible)) rowbg = C_PRESS;
            fill(dc, 0, ry, W, L_rowH, rowbg);
            if (g_libMode == LIB_FOLDERS) {
                playing = folder_has(i, g_curIndex);
                draw_folder_art(dc, i, S(12), ry + S(5), art, rowbg);
                t1 = g_folders[i].name;
                wsprintfW(buf, L"%d곡", g_folders[i].n);
                t2 = buf;
                icon_chevron_right(dc, W - S(18), ry + L_rowH / 2, C_DIM);
            } else {
                playing = (f == g_curIndex);
                draw_art(dc, f, S(12), ry + S(5), art, 0, rowbg, rowbg);
                t1 = g_title[f];
                t2 = g_sub[f];
            }
            if (playing && st->state != 0) draw_eq(dc, S(12), ry + S(5), art, RGB(255, 255, 255), st->state == 1);
            text(dc, t1, S(12) + art + S(10), ry + S(3), W - art - S(40), S(19),
                 fBold, playing ? C_ACCENT : C_TEXT, TL);
            text(dc, t2, S(12) + art + S(10), ry + S(20), W - art - S(40), S(16),
                 fSmall, C_SUB, TL);
            fill(dc, S(12) + art + S(10), ry + L_rowH - 1, W, 1, C_SEP);
        }
        /* 스크롤 위치 표시 */
        if (max_scroll(SCR_LIB) > 0) {
            int view = L_listBot;
            int th = view * view / list_content_h();
            int ty;
            if (th < S(24)) th = S(24);
            ty = (view - th) * scroll / max_scroll(SCR_LIB);
            rrect(dc, W - S(4), ty, S(3), th, S(1), t_drag ? RGB(140, 140, 150) : RGB(70, 70, 78));
        }
    } else if (!g_busy) {
        text(dc, L"음악이 없습니다", 0, L_listTop + S(30), W, S(22), fHead, C_TEXT, TC);
        text(dc, L"SD카드에 MP3를 넣고", 0, L_listTop + S(58), W, S(18), fNorm, C_SUB, TC);
        text(dc, L"오른쪽 위 ••• → 목록 새로고침", 0, L_listTop + S(76), W, S(18), fNorm, C_SUB, TC);
    }

    if (g_busy) {
        rrect(dc, W / 2 - S(80), H / 2 - S(24), S(160), S(48), S(10), C_CARD);
        text(dc, L"음악 찾는 중...", W / 2 - S(80), H / 2 - S(24), S(160), S(48), fBold, C_TEXT, TC);
    }

    /* 미니 플레이어 */
    if (g_curIndex >= 0) {
        int my = H - L_miniH, a = L_miniH - S(14);
        fill(dc, 0, my, W, L_miniH, C_MINI);
        fill(dc, 0, my, W, S(2), RGB(55, 55, 60));
        if (st->durMs > 0) {
            DWORD p = st->posMs > st->durMs ? st->durMs : st->posMs;
            fill(dc, 0, my, (int)((unsigned long long)W * p / st->durMs), S(2), C_ACCENT);
        }
        draw_art(dc, g_curIndex, S(10), my + S(8), a, 0, C_MINI, C_MINI);
        text(dc, cur_title(), S(10) + a + S(10), my + S(6), W - a - S(110), S(20), fBold, C_TEXT, TL);
        text(dc, cur_artist(), S(10) + a + S(10), my + S(25), W - a - S(110), S(16), fSmall, C_SUB, TL);
        if (st->state == 1) icon_pause(dc, W - S(66), my + L_miniH / 2, S(18), C_TEXT);
        else icon_play(dc, W - S(66), my + L_miniH / 2, S(20), C_TEXT);
        icon_skip(dc, W - S(24), my + L_miniH / 2, S(16), 1, C_TEXT);
    }
}

/* ---------------------------------------------------------------- 재생 화면 */
static void chip(HDC dc, int x, int y, int w, int h, const wchar_t *s, COLORREF bg, COLORREF fg)
{
    rrect(dc, x, y, w, h, h / 2, bg);
    text(dc, s, x + S(2), y, w - S(4), h, fSmall, fg, TC | DT_END_ELLIPSIS);
}

static void chip_texts(const Status *st, wchar_t t[4][32])
{
    wcscpy(t[0], g_orderName[g_set.order % 3]);
    if (g_set.qmode == 0) wsprintfW(t[1], L"자동 %dk", st->outRate ? st->outRate / 1000 : g_levels[st->level & 3].rate / 1000);
    else wsprintfW(t[1], L"%s %dk", g_levels[g_set.qmode - 1].name, g_levels[g_set.qmode - 1].rate / 1000);
    wcscpy(t[2], g_set.earphone ? L"이어폰" : L"스피커");
    wsprintfW(t[3], L"소리 %s", g_levelPctName[g_set.levelIdx & 3]);
}

static void paint_now(HDC dc, const Status *st)
{
    COLORREF top, bot, onBg;
    wchar_t a[16], b[16], ct[4][32];
    DWORD pos = t_scrub ? t_scrubMs : st->posMs;
    int bw, k, cx, cy;

    now_bg(&top, &bot);
    grad(dc, 0, 0, W, H, top, bot);
    SetBkMode(dc, TRANSPARENT);

    /* 윗줄 */
    icon_chevron_down(dc, S(22), S(15), RGB(220, 220, 228));
    text(dc, L"지금 재생 중", 0, 0, W, S(30), fSmall, RGB(210, 210, 218), TC);
    icon_dots(dc, W - S(22), S(15), RGB(220, 220, 228));

    /* 앨범아트 */
    draw_art(dc, g_curIndex, N_artX, N_artY, N_art, 1, now_bg_at(N_artY), now_bg_at(N_artY + N_art));

    /* 제목/가수 */
    if (N_side) {
        RECT r;
        int th;
        r.left = N_txtX; r.top = N_txtY; r.right = N_txtX + N_txtW; r.bottom = N_txtY + N_art - S(30);
        SelectObject(dc, fHead);
        SetTextColor(dc, C_TEXT);
        th = DrawTextW(dc, cur_title(), -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (th > S(60)) th = S(60);
        text(dc, cur_artist(), N_txtX, N_txtY + th + S(4), N_txtW, S(18), fNorm, RGB(225, 225, 232), TL);
        if (g_art && g_artIndex == g_curIndex && g_art->album[0])
            text(dc, g_art->album, N_txtX, N_txtY + th + S(22), N_txtW, S(16), fSmall, RGB(170, 170, 180), TL);
        /* 파일 정보 */
        if (st->srcType) {
            wchar_t inf[48];
            wsprintfW(inf, L"%s · %dkbps", st->srcType == 1 ? L"MP3" : L"WAV", st->kbps);
            text(dc, inf, N_txtX, N_artY + N_art - S(16), N_txtW, S(16), fSmall, RGB(150, 150, 160), TL);
        }
    } else {
        text(dc, cur_title(), N_txtX, N_txtY, N_txtW, S(22), fHead, C_TEXT, TC | DT_END_ELLIPSIS);
        text(dc, cur_artist(), N_txtX, N_txtY + S(22), N_txtW, S(18), fNorm, RGB(225, 225, 232), TC | DT_END_ELLIPSIS);
    }

    /* 진행 막대 */
    onBg = now_bg_at(N_barY);
    bw = N_bar.right - N_bar.left;
    rrect(dc, N_bar.left, N_bar.top, bw, S(4), S(2), mix(onBg, RGB(255, 255, 255), 70));
    if (st->durMs > 0) {
        DWORD p = pos > st->durMs ? st->durMs : pos;
        int fw = (int)((unsigned long long)bw * p / st->durMs);
        rrect(dc, N_bar.left, N_bar.top, fw, S(4), S(2), RGB(240, 240, 245));
        circle(dc, N_bar.left + fw, N_bar.top + S(2), t_scrub ? S(8) : S(5), RGB(255, 255, 255));
    }
    fmt_time(a, pos);
    fmt_time(b, st->durMs);
    text(dc, a, N_bar.left, N_bar.top + S(8), S(60), S(16), fSmall, RGB(190, 190, 198), TL);
    text(dc, b, N_bar.right - S(60), N_bar.top + S(8), S(60), S(16), fSmall, RGB(190, 190, 198), TR);

    /* 재생 버튼들 */
    cx = W / 2; cy = N_ctrlY;
    circle(dc, cx, cy, S(26), RGB(255, 255, 255));
    if (st->state == 1) icon_pause(dc, cx, cy, S(20), RGB(20, 20, 24));
    else icon_play(dc, cx + S(2), cy, S(22), RGB(20, 20, 24));
    icon_skip(dc, cx - S(78), cy, S(22), -1, C_TEXT);
    icon_skip(dc, cx + S(78), cy, S(22), 1, C_TEXT);

    /* 설정 칩 */
    chip_texts(st, ct);
    {
        int gap = S(6), cw = (W - S(16) * 2 - gap * 3) / 4;
        COLORREF cb = mix(now_bg_at(N_chipY), RGB(255, 255, 255), 40);
        for (k = 0; k < 4; k++) {
            int hi = (k == 0 && g_set.order != 0);
            chip(dc, S(16) + k * (cw + gap), N_chipY, cw, S(28), ct[k],
                 hi ? RGB(245, 245, 248) : cb, hi ? RGB(20, 20, 24) : RGB(235, 235, 240));
        }
    }
}

/* ---------------------------------------------------------------- 설정 화면 */
static void set_row_text(int i, const Status *st, const wchar_t **label, wchar_t *val, int *kind)
{
    *kind = 0;   /* 0 = 값+화살표, 1 = 정보, 2 = 위험(종료) */
    val[0] = 0;
    switch (i) {
    case 0: *label = L"음질";
        if (g_set.qmode == 0) wsprintfW(val, L"자동 (%s)", g_levels[st->level & 3].name);
        else wcscpy(val, g_levels[g_set.qmode - 1].name);
        break;
    case 1: *label = L"출력 모드"; wcscpy(val, g_set.earphone ? L"이어폰" : L"스피커"); break;
    case 2: *label = L"소리 크기"; wcscpy(val, g_levelPctName[g_set.levelIdx & 3]); break;
    case 3: *label = L"재생 순서"; wcscpy(val, g_orderName[g_set.order % 3]); break;
    case 4: *label = L"음악 목록 새로고침"; wsprintfW(val, L"%d곡", g_fileCount); break;
    case 5: *label = L"출력"; *kind = 1;
        if (st->openFailed) wcscpy(val, L"오디오 장치 오류");
        else if (st->srcType)
            wsprintfW(val, L"%d.%dkHz %s · 끊김 %d", st->outRate / 1000, (st->outRate % 1000) / 100,
                      st->outCh == 2 ? L"스테레오" : L"모노", st->underruns);
        else wcscpy(val, L"-");
        break;
    default: *label = L"앱 종료"; *kind = 2; break;
    }
}

static void paint_set(HDC dc, const Status *st)
{
    int i, y0 = SET_TOP - g_scroll[SCR_SET];
    fill(dc, 0, 0, W, H, C_BG);
    SetBkMode(dc, TRANSPARENT);

    /* 그룹 카드 */
    rrect(dc, S(10), y0, W - S(20), 4 * SET_ROWH, S(10), C_CARD);
    rrect(dc, S(10), y0 + 4 * SET_ROWH + S(14), W - S(20), 2 * SET_ROWH, S(10), C_CARD);
    rrect(dc, S(10), y0 + 6 * SET_ROWH + S(28), W - S(20), SET_ROWH, S(10), C_CARD);
    for (i = 0; i < set_rows(); i++) {
        const wchar_t *label;
        wchar_t val[64];
        int kind;
        int ry = y0 + i * SET_ROWH + (i >= 4 ? S(14) : 0) + (i >= 6 ? S(14) : 0);
        set_row_text(i, st, &label, val, &kind);
        if (i == t_pressRow && kind != 1) rrect(dc, S(10), ry, W - S(20), SET_ROWH, S(10), C_PRESS);
        if (kind == 2) {
            text(dc, label, S(10), ry, W - S(20), SET_ROWH, fNorm, C_ACCENT, TC);
            continue;
        }
        text(dc, label, S(24), ry, W / 2, SET_ROWH, fNorm, C_TEXT, TL);
        if (kind == 0) {
            text(dc, val, W / 2 - S(20), ry, W / 2 - S(20), SET_ROWH, fNorm, C_SUB, TR);
            icon_chevron_right(dc, W - S(26), ry + SET_ROWH / 2, C_DIM);
        } else {
            text(dc, val, S(70), ry, W - S(94), SET_ROWH, fSmall, C_SUB, TR | DT_END_ELLIPSIS);
        }
        if (i != 3 && i != 5 && i != 6) fill(dc, S(24), ry + SET_ROWH - 1, W - S(34), 1, C_SEP);
    }
    text(dc, L"이어폰에서 소리가 깨지면 '소리 크기'를 줄이세요", S(16), y0 + 7 * SET_ROWH + S(34), W - S(32), S(18), fSmall, C_DIM, TC | DT_END_ELLIPSIS);

    /* 머리말 */
    fill(dc, 0, 0, W, SET_TOP - S(6), C_BG);
    icon_chevron_left(dc, S(20), S(21), C_ACCENT);
    text(dc, L"설정", 0, 0, W, S(42), fHead, C_TEXT, TC);
}

/* ---------------------------------------------------------------- 그리기 진입점 */
static void paint_all(HDC hdc)
{
    HDC dc;
    HBITMAP old;
    Status st = get_status();
    if (!g_bb || g_bbW != W || g_bbH != H) {
        if (g_bb) DeleteObject(g_bb);
        g_bb = CreateCompatibleBitmap(hdc, W, H);
        g_bbW = W; g_bbH = H;
    }
    dc = CreateCompatibleDC(hdc);
    old = (HBITMAP)SelectObject(dc, g_bb);
    if (g_scr == SCR_NOW && g_curIndex >= 0) paint_now(dc, &st);
    else if (g_scr == SCR_SET) paint_set(dc, &st);
    else paint_lib(dc, &st);
    BitBlt(hdc, 0, 0, W, H, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteDC(dc);
}

static void redraw(void) { InvalidateRect(g_hwnd, NULL, FALSE); }

/* ---------------------------------------------------------------- 앨범아트 로딩 스레드 */
typedef struct { int gen, index, bigSz, smallSz; wchar_t path[MAX_PATH]; } ArtReq;

static DWORD WINAPI art_thread(LPVOID p)
{
    ArtReq *rq = (ArtReq *)p;
    ArtResult *r = art_load(rq->path, rq->bigSz, rq->smallSz);
    if (r) {
        r->gen = rq->gen;
        if (!PostMessageW(g_hwnd, WM_APP_ART, (WPARAM)rq->index, (LPARAM)r)) art_free(r);
    }
    free(rq);
    return 0;
}

static void request_art(int idx)
{
    ArtReq *rq;
    HANDLE th;
    DWORD tid;
    if (idx < 0 || idx >= g_fileCount || idx == g_artIndex) return;
    /* 이전 곡 아트는 바로 내림 (다른 곡 커버가 남지 않게) */
    if (g_artBig) { DeleteObject(g_artBig); g_artBig = NULL; }
    if (g_artSmall) { DeleteObject(g_artSmall); g_artSmall = NULL; }
    if (g_art) { art_free(g_art); g_art = NULL; }
    g_artIndex = idx;
    rq = (ArtReq *)malloc(sizeof(ArtReq));
    if (!rq) return;
    rq->gen = ++g_artGen;
    rq->index = idx;
    rq->bigSz = (N_art + 1) & ~1;
    rq->smallSz = (L_miniH - S(14) + 1) & ~1;
    wcscpy(rq->path, g_files[idx]);
    th = CreateThread(NULL, 0, art_thread, rq, 0, &tid);
    if (th) { SetThreadPriority(th, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(th); }
    else free(rq);
}

static void apply_art(int idx, ArtResult *r)
{
    HDC dc;
    if (r->gen != g_artGen || idx != g_artIndex) { art_free(r); return; }
    if (g_art) art_free(g_art);
    g_art = r;
    dc = GetDC(g_hwnd);
    if (r->bigPx) { g_artBig = make_dib565(dc, r->bigPx, r->bigSz); g_artBigN = r->bigSz; }
    if (r->smallPx) { g_artSmall = make_dib565(dc, r->smallPx, r->smallSz); g_artSmallN = r->smallSz; }
    ReleaseDC(g_hwnd, dc);
    redraw();
}

/* ---------------------------------------------------------------- 동작 */
static void play_index(int idx, DWORD ms)
{
    int hadMini = (g_curIndex >= 0);
    if (idx < 0 || idx >= g_fileCount) return;
    g_curIndex = idx;
    g_selVisible = 0;
    g_set.lastIndex = idx;
    post_cmd(CMD_PLAY, idx, ms);
    if (!hadMini) { layout(); clamp_scroll(SCR_LIB); }
    request_art(idx);
    redraw();
}

/* 대기열 정하기: 폴더 안에서 재생하면 그 폴더 곡만, 아니면 전체 */
static void set_queue_for_view(void)
{
    if (g_libMode == LIB_INSIDE && g_openFolder >= 0) {
        g_queue = g_folders[g_openFolder].items;
        g_qLen = g_folders[g_openFolder].n;
    } else {
        g_queue = NULL;
        g_qLen = 0;
    }
}

static int q_len(void) { return g_queue ? g_qLen : g_fileCount; }
static int q_at(int p) { return g_queue ? g_queue[p] : p; }
static int q_pos(int file)
{
    int i;
    if (!g_queue) return file;
    for (i = 0; i < g_qLen; i++) if (g_queue[i] == file) return i;
    return -1;
}

static int next_index(int dir, int fromEnd)
{
    int n = q_len(), p;
    if (n <= 0) return -1;
    if (fromEnd && g_set.order == 2 && g_curIndex >= 0) return g_curIndex;
    if (g_set.order == 1 && n > 1) {
        int r;
        do { r = q_at((int)(((unsigned)rand() * 32768u + (unsigned)rand()) % (unsigned)n)); } while (r == g_curIndex);
        return r;
    }
    p = g_curIndex >= 0 ? q_pos(g_curIndex) : -1;
    if (p < 0) return q_at(dir < 0 ? n - 1 : 0);
    return q_at((p + dir + n) % n);
}

static void act_playpause(void)
{
    Status st = get_status();
    if (st.state == 0) {
        int idx = g_curIndex >= 0 ? g_curIndex : (g_set.lastIndex >= 0 ? g_set.lastIndex : 0);
        g_errorStreak = 0;
        if (idx == g_set.lastIndex && g_set.lastPosMs > 3000) play_index(idx, g_set.lastPosMs);   /* 이어 듣기 */
        else play_index(idx, 0);
    } else {
        post_cmd(CMD_PAUSE, 0, 0);
    }
}

static void act_prev(void)
{
    Status st = get_status();
    if (st.state != 0 && st.posMs > 3000) play_index(g_curIndex, 0);
    else play_index(next_index(-1, 0), 0);
}

static void act_next(void) { play_index(next_index(1, 0), 0); }

static void do_rescan(void)
{
    post_cmd(CMD_STOP, 0, 0);
    g_busy = 1;
    redraw();
    UpdateWindow(g_hwnd);
    Sleep(100);
    g_curIndex = -1;
    g_sel = -1;
    g_artIndex = -1;
    scan_all();
    save_list();
    build_names();
    build_folders();
    g_busy = 0;
    g_scroll[SCR_LIB] = 0;
    memset(g_libScroll, 0, sizeof(g_libScroll));
    layout();
    redraw();
}

static void set_action(int row)
{
    switch (row) {
    case 0: g_set.qmode = (g_set.qmode + 1) % 5; post_cmd(CMD_RECONFIG, g_set.qmode == 0 ? 1 : -1, 0); break;
    case 1: g_set.earphone = !g_set.earphone; post_cmd(CMD_RECONFIG, -1, 0); break;
    case 2: g_set.levelIdx = (g_set.levelIdx + 1) & 3; post_cmd(CMD_RECONFIG, -1, 0); break;
    case 3: g_set.order = (g_set.order + 1) % 3; break;
    case 4: g_scr = SCR_LIB; do_rescan(); break;
    case 6: DestroyWindow(g_hwnd); return;
    }
    save_settings();
    redraw();
}

static void chip_action(int k)
{
    switch (k) {
    case 0: set_action(3); break;
    case 1: set_action(0); break;
    case 2: set_action(1); break;
    case 3: set_action(2); break;
    }
}

static void go(int scr)
{
    g_scr = scr;
    t_pressRow = -1;
    redraw();
}

/* ---------------------------------------------------------------- 터치 */
static int set_row_at(int y)
{
    int i, y0 = SET_TOP - g_scroll[SCR_SET];
    for (i = 0; i < set_rows(); i++) {
        int ry = y0 + i * SET_ROWH + (i >= 4 ? S(14) : 0) + (i >= 6 ? S(14) : 0);
        if (y >= ry && y < ry + SET_ROWH) return i;
    }
    return -1;
}

static int lib_row_at(int x, int y)
{
    (void)x;
    if (y >= L_listBot) return -1;
    y = y + g_scroll[SCR_LIB] - L_listTop;
    if (y >= 0 && y / L_rowH < lib_count()) return y / L_rowH;
    return -1;
}

static DWORD bar_ms(int x, DWORD dur)
{
    if (x < N_bar.left) x = N_bar.left;
    if (x > N_bar.right) x = N_bar.right;
    return (DWORD)((unsigned long long)dur * (DWORD)(x - N_bar.left) / (DWORD)(N_bar.right - N_bar.left));
}

static void on_down(int x, int y)
{
    Status st = get_status();
    g_selVisible = 0;
    t_down = 1; t_drag = 0; t_x0 = x; t_y0 = t_lastY = t_prevY = y;
    t_lastT = t_prevT = GetTickCount();
    t_scroll0 = g_scroll[g_scr];
    t_vel = 0;
    t_pressRow = -1;
    KillTimer(g_hwnd, 2);
    if (g_scr == SCR_LIB) t_pressRow = lib_row_at(x, y);
    else if (g_scr == SCR_SET) t_pressRow = set_row_at(y);
    else if (g_scr == SCR_NOW && st.durMs > 0 &&
             y >= N_bar.top - S(14) && y <= N_bar.bottom + S(14) && x >= N_bar.left - S(10) && x <= N_bar.right + S(10)) {
        t_scrub = 1;
        t_scrubMs = bar_ms(x, st.durMs);
    }
    SetCapture(g_hwnd);
    redraw();
}

static void on_move(int x, int y)
{
    DWORD now = GetTickCount();
    if (!t_down) return;
    if (t_scrub) {
        Status st = get_status();
        t_scrubMs = bar_ms(x, st.durMs);
        redraw();
        return;
    }
    if (!t_drag && (y - t_y0 > S(7) || t_y0 - y > S(7)) && (g_scr == SCR_LIB || g_scr == SCR_SET)) {
        t_drag = 1;
        t_pressRow = -1;
    }
    if (t_drag) {
        g_scroll[g_scr] = t_scroll0 - (y - t_y0);
        clamp_scroll(g_scr);
        if (now != t_lastT) { t_prevY = t_lastY; t_prevT = t_lastT; }
        t_lastY = y; t_lastT = now;
        redraw();
    }
}

static void set_lib_mode(int m)
{
    g_libScroll[g_libMode] = g_scroll[SCR_LIB];
    g_libMode = m;
    g_scroll[SCR_LIB] = (m == LIB_INSIDE) ? 0 : g_libScroll[m];
    g_sel = -1; g_selVisible = 0;
    clamp_scroll(SCR_LIB);
    if (m != LIB_INSIDE) { g_set.panel = m; save_settings(); }
    redraw();
}

static void open_folder(int f)
{
    if (f < 0 || f >= g_folderCount) return;
    g_openFolder = f;
    set_lib_mode(LIB_INSIDE);
}

static void lib_back(void)
{
    if (g_libMode == LIB_INSIDE) set_lib_mode(LIB_FOLDERS);
}

static void lib_play_row(int r)
{
    int f;
    if (g_libMode == LIB_FOLDERS) { open_folder(r); return; }
    f = lib_file(r);
    if (f < 0) return;
    g_errorStreak = 0;
    if (f == g_curIndex && get_status().state != 0) { go(SCR_NOW); return; }
    set_queue_for_view();
    play_index(f, 0);
}

static int in_rect(const RECT *r, int x, int y)
{
    return x >= r->left && x < r->right && y >= r->top && y < r->bottom;
}

static void tap_lib(int x, int y)
{
    if (g_curIndex >= 0 && y >= H - L_miniH) {
        if (x >= W - S(88) && x < W - S(44)) act_playpause();
        else if (x >= W - S(44)) act_next();
        else go(SCR_NOW);
        return;
    }
    if (y + g_scroll[SCR_LIB] < L_listTop) {
        if (in_rect(&g_hit[4], x, y)) { go(SCR_SET); return; }
        if (in_rect(&g_hit[2], x, y) || in_rect(&g_hit[3], x, y)) {
            int shuffle = in_rect(&g_hit[3], x, y);
            if (g_fileCount <= 0) return;
            g_errorStreak = 0;
            set_queue_for_view();
            g_set.order = shuffle ? 1 : (g_set.order == 1 ? 0 : g_set.order);
            g_curIndex = -1;
            play_index(shuffle ? next_index(1, 0) : q_at(0), 0);
            save_settings();
            return;
        }
        if (g_libMode == LIB_INSIDE) { if (in_rect(&g_hit[0], x, y)) lib_back(); return; }
        if (in_rect(&g_hit[0], x, y) && g_libMode != LIB_SONGS) set_lib_mode(LIB_SONGS);
        else if (in_rect(&g_hit[1], x, y) && g_libMode != LIB_FOLDERS) set_lib_mode(LIB_FOLDERS);
        return;
    }
    lib_play_row(lib_row_at(x, y));
}

static void tap_now(int x, int y)
{
    int cx = W / 2;
    if (y < S(34)) {
        if (x < S(56)) go(SCR_LIB);
        else if (x > W - S(56)) go(SCR_SET);
        return;
    }
    if (y >= N_ctrlY - S(30) && y <= N_ctrlY + S(30)) {
        if (x >= cx - S(36) && x <= cx + S(36)) act_playpause();
        else if (x >= cx - S(110) && x < cx - S(36)) act_prev();
        else if (x > cx + S(36) && x <= cx + S(110)) act_next();
        return;
    }
    if (y >= N_chipY - S(4) && y <= N_chipY + S(32)) {
        int gap = S(6), cw = (W - S(16) * 2 - gap * 3) / 4;
        int k = (x - S(16)) / (cw + gap);
        if (x >= S(16) && k >= 0 && k < 4) chip_action(k);
        return;
    }
    /* 앨범아트를 누르면 재생/일시정지 */
    if (x >= N_artX && x < N_artX + N_art && y >= N_artY && y < N_artY + N_art) act_playpause();
}

static void on_up(int x, int y)
{
    if (!t_down) return;
    t_down = 0;
    ReleaseCapture();
    if (t_scrub) {
        t_scrub = 0;
        post_cmd(CMD_SEEK, 0, t_scrubMs);
        /* 상태가 갱신될 때까지 잠깐 그 위치로 보이게 */
        EnterCriticalSection(&g_cs); g_st.posMs = t_scrubMs; LeaveCriticalSection(&g_cs);
        redraw();
        return;
    }
    if (t_drag) {
        DWORD dt = t_lastT - t_prevT;
        t_drag = 0;
        if (dt > 0 && dt < 120 && GetTickCount() - t_lastT < 80) {
            t_vel = (t_lastY - t_prevY) * 16 * 30 / (int)dt;   /* Q4, 30ms 틱 기준 */
            if (t_vel > S(60) * 16) t_vel = S(60) * 16;
            if (t_vel < -S(60) * 16) t_vel = -S(60) * 16;
            if (t_vel > 32 || t_vel < -32) SetTimer(g_hwnd, 2, 30, NULL);
        }
        redraw();
        return;
    }
    t_pressRow = -1;
    if (g_scr == SCR_LIB) tap_lib(x, y);
    else if (g_scr == SCR_NOW) tap_now(x, y);
    else {
        if (y < SET_TOP - S(6)) { if (x < S(80)) go(g_curIndex >= 0 ? SCR_NOW : SCR_LIB); }
        else { int r = set_row_at(y); if (r >= 0 && r != 5) set_action(r); }
    }
    redraw();
}

static void on_inertia(void)
{
    g_scroll[g_scr] -= t_vel / 16;
    clamp_scroll(g_scr);
    t_vel = t_vel * 14 / 16;
    if ((t_vel < 24 && t_vel > -24) || g_scroll[g_scr] == 0 || g_scroll[g_scr] == max_scroll(g_scr))
        KillTimer(g_hwnd, 2);
    redraw();
}

/* ---------------------------------------------------------------- 키 */
static int on_key(WPARAM k)
{
    switch (k) {
    case VK_LEFT:  act_prev(); return 1;
    case VK_RIGHT: act_next(); return 1;
    case VK_UP:
    case VK_DOWN:
        if (g_scr == SCR_LIB && lib_count() > 0) {
            if (g_sel < 0 || !g_selVisible) {
                int r = (g_libMode == LIB_SONGS) ? g_curIndex : -1, i;
                if (g_libMode == LIB_INSIDE) for (i = 0; i < lib_count(); i++) if (lib_file(i) == g_curIndex) r = i;
                if (g_sel < 0) g_sel = r >= 0 ? r : (g_scroll[SCR_LIB] - L_listTop + L_rowH - 1) / L_rowH;
            } else g_sel += (k == VK_DOWN) ? 1 : -1;
            if (g_sel < 0) g_sel = 0;
            if (g_sel >= lib_count()) g_sel = lib_count() - 1;
            g_selVisible = 1;
            ensure_visible(g_sel);
            redraw();
        } else if (g_scr == SCR_SET) {
            g_scroll[SCR_SET] += (k == VK_DOWN) ? SET_ROWH : -SET_ROWH;
            clamp_scroll(SCR_SET);
            redraw();
        }
        return 1;
    case VK_RETURN:
        if (g_scr == SCR_LIB && g_selVisible && g_sel >= 0 &&
            (g_libMode == LIB_FOLDERS || lib_file(g_sel) != g_curIndex)) lib_play_row(g_sel);
        else act_playpause();
        return 1;
    case VK_F1:            /* 왼쪽 소프트키: 보관함 ↔ 재생 화면 */
        go(g_scr == SCR_LIB && g_curIndex >= 0 ? SCR_NOW : SCR_LIB);
        return 1;
    case VK_F2:            /* 오른쪽 소프트키: 설정 */
        go(g_scr == SCR_SET ? SCR_LIB : SCR_SET);
        return 1;
    case VK_ESCAPE:
    case VK_BACK:
        if (g_scr != SCR_LIB) { go(SCR_LIB); return 1; }
        if (g_libMode == LIB_INSIDE) { lib_back(); return 1; }
        return 0;
    }
    return 0;
}

/* ---------------------------------------------------------------- 창 */
static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        SetTimer(hwnd, 1, 300, NULL);
        return 0;
    case WM_SIZE:
        W = LOWORD(lp); H = HIWORD(lp);
        layout();
        clamp_scroll(SCR_LIB);
        clamp_scroll(SCR_SET);
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
        if (wp == 2) { on_inertia(); return 0; }
        {
            static int lastState = -1;
            static DWORD lastPos;
            Status st = get_status();
            g_tick++;
            if (st.state == 1) { g_set.lastPosMs = st.posMs; SystemIdleTimerReset(); }
            /* 화면 안 바뀌면 다시 그리지 않음 (배터리) */
            if (st.state == 1 || st.state != lastState || st.posMs / 1000 != lastPos / 1000) redraw();
            lastState = st.state;
            lastPos = st.posMs;
        }
        return 0;
    case WM_LBUTTONDOWN: on_down((short)LOWORD(lp), (short)HIWORD(lp)); return 0;
    case WM_MOUSEMOVE:   on_move((short)LOWORD(lp), (short)HIWORD(lp)); return 0;
    case WM_LBUTTONUP:   on_up((short)LOWORD(lp), (short)HIWORD(lp)); return 0;
    case WM_KEYDOWN:
        if (on_key(wp)) return 0;
        break;
    case WM_APP_ART:
        apply_art((int)wp, (ArtResult *)lp);
        return 0;
    case WM_APP_TRACKEND:
        g_errorStreak = 0;
        g_set.lastPosMs = 0;
        play_index(next_index(1, 1), 0);
        return 0;
    case WM_APP_TRACKERR:
        /* 열 수 없는 파일은 건너뜀 (전부 실패하면 멈춤) */
        if (++g_errorStreak < g_fileCount && g_errorStreak < 30) {
            g_curIndex = (int)wp;
            play_index(next_index(1, 0), 0);
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
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
    lf.lfQuality = 5; /* CLEARTYPE_QUALITY (지원 안 하면 무시됨) */
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
    DWORD tid;
    wchar_t *slash;
    (void)hPrev; (void)cmd;

    /* 이미 실행 중이면 그 창을 앞으로 */
    prev = FindWindowW(APP_CLASS, NULL);
    if (prev) { SetForegroundWindow((HWND)((ULONG_PTR)prev | 1)); return 0; }

    GetModuleFileNameW(NULL, g_exeDir, MAX_PATH);
    slash = wcsrchr(g_exeDir, L'\\');
    if (slash) slash[1] = 0; else wcscpy(g_exeDir, L"\\");

    sdc = GetDC(NULL);
    g_scale = GetDeviceCaps(sdc, LOGPIXELSY) >= 150 ? 2 : 1;
    ReleaseDC(NULL, sdc);
    fTitle = make_font(S(24), 1);
    fHead = make_font(S(17), 1);
    fBold = make_font(S(14), 1);
    fNorm = make_font(S(14), 0);
    fSmall = make_font(S(12), 0);
    fGlyphS = make_font(S(20), 1);
    fGlyphB = make_font(S(56), 1);

    InitializeCriticalSection(&g_cs);
    g_cmdEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_waveEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_files = (wchar_t **)malloc(sizeof(wchar_t *) * MAX_FILES);
    srand(GetTickCount());
    power_init();
    load_settings();

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hbrBackground = NULL;
    wc.lpszClassName = APP_CLASS;
    wc.hCursor = NULL;
    RegisterClassW(&wc);

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
#ifdef PREVIEW_W
    wa.left = 0; wa.top = 0; wa.right = PREVIEW_W; wa.bottom = PREVIEW_H;
    {
        RECT r = wa;
        AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
        g_hwnd = CreateWindowW(APP_CLASS, APP_TITLE, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               0, 0, r.right - r.left, r.bottom - r.top, NULL, NULL, hInst, NULL);
    }
#else
    g_hwnd = CreateWindowW(APP_CLASS, APP_TITLE, WS_VISIBLE,
                           wa.left, wa.top, wa.right - wa.left, wa.bottom - wa.top,
                           NULL, NULL, hInst, NULL);
#endif
    if (!g_hwnd) return 0;
    g_busy = 1;
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    if (!load_list()) { scan_all(); save_list(); }
    build_names();
    g_libMode = (g_set.panel == LIB_FOLDERS) ? LIB_FOLDERS : LIB_SONGS;
    build_folders();
    if (g_set.lastIndex < 0 || g_set.lastIndex >= g_fileCount) { g_set.lastIndex = 0; g_set.lastPosMs = 0; }
    g_sel = (g_fileCount > 0 && g_libMode == LIB_SONGS) ? g_set.lastIndex : -1;
    g_busy = 0;
    layout();
    ensure_visible(g_sel);
    g_sel = -1;
    redraw();

    g_audioThread = CreateThread(NULL, 0, audio_thread, NULL, 0, &tid);

    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    post_cmd(CMD_QUIT, 0, 0);
    WaitForSingleObject(g_audioThread, 3000);
    save_settings();
    return 0;
}
