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
/* 화면 구성 (위→아래)
 *  [곡 제목 ............ ▲]   ← 제목 줄을 누르면 버튼 줄 숨김/보이기
 *  [0:14 / 2:51  ======진행막대======]
 *  [이전][재생][다음][설정]     ← 숨김 가능
 *  (설정 열면) 출력 정보 줄 + [정지][순서][새로고침][종료] / [음질][모드][소리]
 *  [곡 목록 - 남은 공간 전부]   ← 손가락으로 끌어서 스크롤, 선택된 곡을 한 번 더 누르면 재생
 */
enum {
    ID_LIST = 100,
    ID_PREV, ID_PLAY, ID_NEXT, ID_MENU,             /* 기본 줄 */
    ID_STOP, ID_ORDER, ID_RESCAN, ID_EXIT,          /* 설정 1줄 */
    ID_QUAL, ID_EAR, ID_LEVEL                       /* 설정 2줄 */
};
#define NBTN 11
static HWND g_list, g_btn[NBTN];
static HFONT g_font, g_fontBold, g_fontSmall;
static int g_scale = 1;
static RECT g_hdrRect, g_barRect, g_titleRect;
static int g_curIndex = -1;
static int g_errorStreak;
static int g_cw, g_ch;
static int g_itemH = 24;
static WNDPROC g_oldListProc;

#define BTN(id) g_btn[(id) - ID_PREV]

static const wchar_t *base_name(const wchar_t *p)
{
    const wchar_t *s = wcsrchr(p, L'\\');
    return s ? s + 1 : p;
}

static void fill_listbox(void)
{
    int i;
    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_fileCount; i++) {
        wchar_t name[MAX_PATH];
        wchar_t *dot;
        wcscpy(name, base_name(g_files[i]));
        dot = wcsrchr(name, L'.');
        if (dot) *dot = 0;
        SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)name);
    }
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, NULL, TRUE);
}

static void update_buttons(void)
{
    wchar_t t[64];
    Status st;
    EnterCriticalSection(&g_cs); st = g_st; LeaveCriticalSection(&g_cs);
    if (g_set.qmode == 0) wsprintfW(t, L"음질:자동(%s)", g_levels[st.level & 3].name);
    else wsprintfW(t, L"음질:%s", g_levels[g_set.qmode - 1].name);
    SetWindowTextW(BTN(ID_QUAL), t);
    SetWindowTextW(BTN(ID_EAR), g_set.earphone ? L"이어폰 모드" : L"스피커 모드");
    wsprintfW(t, L"소리:%s", g_levelPctName[g_set.levelIdx & 3]);
    SetWindowTextW(BTN(ID_LEVEL), t);
    SetWindowTextW(BTN(ID_ORDER), g_orderName[g_set.order % 3]);
    SetWindowTextW(BTN(ID_PLAY), st.state == 1 ? L"일시정지" : L"재생");
    SetWindowTextW(BTN(ID_MENU), g_set.panel ? L"설정 닫기" : L"설정");
}

static void play_index(int idx, DWORD ms)
{
    if (idx < 0 || idx >= g_fileCount) return;
    g_curIndex = idx;
    g_set.lastIndex = idx;
    SendMessageW(g_list, LB_SETCURSEL, (WPARAM)idx, 0);
    InvalidateRect(g_list, NULL, FALSE);
    post_cmd(CMD_PLAY, idx, ms);
}

static int next_index(int dir, int fromEnd)
{
    if (g_fileCount <= 0) return -1;
    if (fromEnd && g_set.order == 2) return g_curIndex;
    if (g_set.order == 1 && g_fileCount > 1) {
        int r;
        do { r = (int)(((unsigned)rand() * 32768u + (unsigned)rand()) % (unsigned)g_fileCount); } while (r == g_curIndex);
        return r;
    }
    if (g_curIndex < 0) return 0;
    return (g_curIndex + dir + g_fileCount) % g_fileCount;
}

static void fmt_time(wchar_t *o, DWORD ms)
{
    DWORD s = ms / 1000;
    wsprintfW(o, L"%d:%02d", (int)(s / 60), (int)(s % 60));
}

static int header_height(void)
{
    int h = 2 * g_scale + 18 * g_scale + 18 * g_scale + 3 * g_scale;
    if (g_set.panel) h += 15 * g_scale;
    return h;
}

static void paint_header(HDC hdc)
{
    Status st;
    RECT r = g_hdrRect, t, tr;
    HDC mdc;
    HBITMAP bmp, oldb;
    HBRUSH bg, bar, barbg;
    int w = r.right - r.left, h = r.bottom - r.top;
    int arrowW = 22 * g_scale;
    wchar_t s1[32], s2[32], buf[200];

    EnterCriticalSection(&g_cs); st = g_st; LeaveCriticalSection(&g_cs);

    mdc = CreateCompatibleDC(hdc);
    bmp = CreateCompatibleBitmap(hdc, w, h);
    oldb = (HBITMAP)SelectObject(mdc, bmp);
    bg = CreateSolidBrush(RGB(24, 28, 40));
    t.left = 0; t.top = 0; t.right = w; t.bottom = h;
    FillRect(mdc, &t, bg);
    SetBkMode(mdc, TRANSPARENT);

    /* 1줄: 곡 제목 + 숨김 화살표 */
    SelectObject(mdc, g_fontBold);
    SetTextColor(mdc, RGB(255, 255, 255));
    t.left = 4 * g_scale; t.right = w - arrowW; t.top = 2 * g_scale; t.bottom = t.top + 18 * g_scale;
    g_titleRect.left = 0; g_titleRect.right = w; g_titleRect.top = 0; g_titleRect.bottom = t.bottom;
    if (g_fileCount == 0) wcscpy(buf, L"음악 없음 - 설정 → 새로고침");
    else if (g_curIndex >= 0 && g_curIndex < g_fileCount) wcscpy(buf, base_name(g_files[g_curIndex]));
    else wsprintfW(buf, L"곡 %d개 - 곡을 눌러 재생", g_fileCount);
    DrawTextW(mdc, buf, -1, &t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | DT_VCENTER);
    SetTextColor(mdc, RGB(120, 180, 255));
    tr = t; tr.left = w - arrowW; tr.right = w - 2 * g_scale;
    DrawTextW(mdc, g_set.compact ? L"▼" : L"▲", -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    /* 2줄: 시간 + 진행 막대 */
    SelectObject(mdc, g_font);
    SetTextColor(mdc, RGB(190, 200, 220));
    fmt_time(s1, st.posMs);
    fmt_time(s2, st.durMs);
    wsprintfW(buf, L"%s%s / %s", st.state == 2 ? L"∥ " : L"", s1, s2);
    t.top = t.bottom; t.bottom = t.top + 18 * g_scale;
    tr = t; tr.right = w;
    DrawTextW(mdc, buf, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
    tr.top = t.top; tr.bottom = t.bottom;
    DrawTextW(mdc, buf, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_VCENTER);
    {
        RECT b;
        b.left = tr.right + 6 * g_scale;
        b.right = w - 6 * g_scale;
        b.top = t.top + (t.bottom - t.top) / 2 - 4 * g_scale;
        b.bottom = b.top + 8 * g_scale;
        if (b.right - b.left < 20 * g_scale) b.left = b.right - 20 * g_scale;
        g_barRect = b;
        OffsetRect(&g_barRect, r.left, r.top);
        barbg = CreateSolidBrush(RGB(60, 66, 84));
        FillRect(mdc, &b, barbg);
        DeleteObject(barbg);
        if (st.durMs > 0) {
            DWORD p = st.posMs > st.durMs ? st.durMs : st.posMs;
            RECT f = b;
            f.right = f.left + (int)((unsigned long long)(b.right - b.left) * p / st.durMs);
            bar = CreateSolidBrush(RGB(80, 170, 255));
            FillRect(mdc, &f, bar);
            DeleteObject(bar);
        }
    }

    /* 3줄(설정 열었을 때만): 출력 정보 */
    if (g_set.panel) {
        SelectObject(mdc, g_fontSmall);
        SetTextColor(mdc, RGB(150, 160, 180));
        t.top = t.bottom + 1 * g_scale; t.bottom = t.top + 14 * g_scale;
        if (st.openFailed) wcscpy(buf, L"오디오 장치를 열 수 없음");
        else if (st.srcType)
            wsprintfW(buf, L"%s %dk %dkbps → %d.%dkHz %s | 끊김 %d",
                      st.srcType == 1 ? L"MP3" : L"WAV", st.srcRate / 1000, st.kbps,
                      st.outRate / 1000, (st.outRate % 1000) / 100,
                      st.outCh == 2 ? L"스테레오" : L"모노", st.underruns);
        else wcscpy(buf, L"←→ 이전/다음, 가운데 버튼 재생/일시정지");
        DrawTextW(mdc, buf, -1, &t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | DT_VCENTER);
    }

    BitBlt(hdc, r.left, r.top, w, h, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, oldb);
    DeleteObject(bmp);
    DeleteDC(mdc);
    DeleteObject(bg);
}

static void layout(int cw, int ch)
{
    int pad = 2 * g_scale;
    int bh = 28 * g_scale;     /* 기본 줄: 손가락으로 누르기 쉽게 크게 */
    int sh = 25 * g_scale;     /* 설정 줄 */
    int y, i, bw;
    g_cw = cw; g_ch = ch;
    g_hdrRect.left = 0; g_hdrRect.top = 0; g_hdrRect.right = cw; g_hdrRect.bottom = header_height();
    y = g_hdrRect.bottom + pad;

    for (i = 0; i < NBTN; i++) ShowWindow(g_btn[i], SW_HIDE);

    if (!g_set.compact) {
        bw = (cw - pad * 5) / 4;
        for (i = 0; i < 4; i++) {
            MoveWindow(g_btn[i], pad + i * (bw + pad), y, bw, bh, TRUE);
            ShowWindow(g_btn[i], SW_SHOW);
        }
        y += bh + pad;
        if (g_set.panel) {
            bw = (cw - pad * 5) / 4;
            for (i = 0; i < 4; i++) {
                MoveWindow(g_btn[4 + i], pad + i * (bw + pad), y, bw, sh, TRUE);
                ShowWindow(g_btn[4 + i], SW_SHOW);
            }
            y += sh + pad;
            bw = (cw - pad * 4) / 3;
            for (i = 0; i < 3; i++) {
                MoveWindow(g_btn[8 + i], pad + i * (bw + pad), y, bw, sh, TRUE);
                ShowWindow(g_btn[8 + i], SW_SHOW);
            }
            y += sh + pad;
        }
    }
    MoveWindow(g_list, 0, y, cw, ch - y, TRUE);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void relayout(void)
{
    layout(g_cw, g_ch);
    update_buttons();
    save_settings();
}

static void do_rescan(HWND hwnd)
{
    HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    post_cmd(CMD_STOP, 0, 0);
    Sleep(100);
    g_curIndex = -1;
    scan_all();
    save_list();
    fill_listbox();
    SetCursor(old);
    InvalidateRect(hwnd, &g_hdrRect, FALSE);
}

/* ---- 곡 목록: 직접 그리기 + 손가락 드래그 스크롤 ---- */
static void draw_item(const DRAWITEMSTRUCT *di)
{
    HDC hdc = di->hDC;
    RECT r = di->rcItem, tr;
    wchar_t text[MAX_PATH];
    int idx = (int)di->itemID;
    int sel = (di->itemState & ODS_SELECTED) != 0;
    int playing = (idx == g_curIndex);
    HBRUSH b;
    COLORREF bgc;

    if (idx < 0) return;
    if (sel) bgc = RGB(50, 110, 200);
    else if (idx & 1) bgc = RGB(238, 242, 248);
    else bgc = RGB(255, 255, 255);
    b = CreateSolidBrush(bgc);
    FillRect(hdc, &r, b);
    DeleteObject(b);

    if (playing) {
        RECT m = r;
        m.right = m.left + 4 * g_scale;
        b = CreateSolidBrush(RGB(255, 150, 30));
        FillRect(hdc, &m, b);
        DeleteObject(b);
    }

    text[0] = 0;
    SendMessageW(g_list, LB_GETTEXT, (WPARAM)idx, (LPARAM)text);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, playing ? g_fontBold : g_font);
    SetTextColor(hdc, sel ? RGB(255, 255, 255) : (playing ? RGB(200, 90, 0) : RGB(20, 20, 20)));
    tr = r;
    tr.left += 8 * g_scale;
    tr.right -= 2 * g_scale;
    DrawTextW(hdc, text, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | DT_VCENTER);

    /* 구분선 */
    {
        RECT ln = r;
        ln.top = ln.bottom - 1;
        b = CreateSolidBrush(RGB(215, 220, 230));
        FillRect(hdc, &ln, b);
        DeleteObject(b);
    }
}

static int s_down, s_drag, s_downY, s_downTop, s_lastY;

static LRESULT CALLBACK list_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        s_down = 1;
        s_drag = 0;
        s_downY = s_lastY = (short)HIWORD(lp);
        s_downTop = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
        SetCapture(hwnd);
        SetFocus(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        if (s_down) {
            int y = (short)HIWORD(lp);
            int dy = y - s_downY;
            if (!s_drag && (dy > 8 * g_scale || dy < -8 * g_scale)) s_drag = 1;
            if (s_drag) {
                int top = s_downTop - (dy >= 0 ? (dy + g_itemH / 2) / g_itemH : -((-dy + g_itemH / 2) / g_itemH));
                if (top < 0) top = 0;
                if (top > g_fileCount - 1) top = g_fileCount - 1;
                if (top != (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0))
                    SendMessageW(hwnd, LB_SETTOPINDEX, (WPARAM)top, 0);
            }
            s_lastY = y;
        }
        return 0;
    case WM_LBUTTONUP:
        if (s_down) {
            ReleaseCapture();
            s_down = 0;
            if (!s_drag) {
                int y = (short)HIWORD(lp);
                int top = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
                int idx = (y >= 0) ? top + y / g_itemH : -1;
                if (idx >= 0 && idx < g_fileCount) {
                    int cur = (int)SendMessageW(hwnd, LB_GETCURSEL, 0, 0);
                    if (idx == cur) {
                        /* 이미 선택된 곡을 한 번 더 누르면 재생 */
                        g_errorStreak = 0;
                        play_index(idx, 0);
                        update_buttons();
                    } else {
                        SendMessageW(hwnd, LB_SETCURSEL, (WPARAM)idx, 0);
                    }
                }
            }
        }
        return 0;
    case WM_CAPTURECHANGED:
        s_down = 0;
        break;
    }
    return CallWindowProcW(g_oldListProc, hwnd, msg, wp, lp);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        static const wchar_t *labels[NBTN] = {
            L"이전", L"재생", L"다음", L"설정",
            L"정지", L"순서", L"새로고침", L"종료",
            L"음질", L"이어폰", L"소리" };
        HINSTANCE hi = ((LPCREATESTRUCT)lp)->hInstance;
        int i;
        g_hwnd = hwnd;
        g_itemH = 24 * g_scale;
        g_list = CreateWindowW(L"LISTBOX", NULL,
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
                               LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
                               0, 0, 10, 10, hwnd, (HMENU)ID_LIST, hi, NULL);
        SendMessageW(g_list, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(g_list, LB_SETITEMHEIGHT, 0, (LPARAM)g_itemH);
        g_oldListProc = (WNDPROC)GetWindowLongW(g_list, GWL_WNDPROC);
        SetWindowLongW(g_list, GWL_WNDPROC, (LONG)list_proc);
        for (i = 0; i < NBTN; i++) {
            g_btn[i] = CreateWindowW(L"BUTTON", labels[i], WS_CHILD | BS_PUSHBUTTON,
                                     0, 0, 10, 10, hwnd, (HMENU)(ID_PREV + i), hi, NULL);
            SendMessageW(g_btn[i], WM_SETFONT, (WPARAM)(i < 4 ? g_font : g_fontSmall), TRUE);
        }
        SetTimer(hwnd, 1, 500, NULL);
        return 0;
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)lp;
        mi->itemHeight = (UINT)(24 * g_scale);
        return TRUE;
    }
    case WM_DRAWITEM:
        draw_item((const DRAWITEMSTRUCT *)lp);
        return TRUE;
    case WM_SIZE:
        layout(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint_header(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        InvalidateRect(hwnd, &g_hdrRect, FALSE);
        update_buttons();
        {
            Status st;
            EnterCriticalSection(&g_cs); st = g_st; LeaveCriticalSection(&g_cs);
            if (st.state == 1) { g_set.lastPosMs = st.posMs; SystemIdleTimerReset(); }
        }
        return 0;
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        Status st;
        EnterCriticalSection(&g_cs); st = g_st; LeaveCriticalSection(&g_cs);
        if (y >= g_titleRect.top && y < g_titleRect.bottom) {
            /* 제목 줄 누르면 버튼 숨기기/보이기 */
            g_set.compact = !g_set.compact;
            relayout();
        } else if (y >= g_barRect.top - 8 * g_scale && y <= g_barRect.bottom + 8 * g_scale &&
                   x >= g_barRect.left - 4 * g_scale && x <= g_barRect.right + 4 * g_scale &&
                   st.state != 0 && st.durMs > 0) {
            int px = x;
            if (px < g_barRect.left) px = g_barRect.left;
            if (px > g_barRect.right) px = g_barRect.right;
            post_cmd(CMD_SEEK, 0, (DWORD)((unsigned long long)st.durMs * (DWORD)(px - g_barRect.left) /
                                          (DWORD)(g_barRect.right - g_barRect.left)));
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        Status st;
        (void)code;
        if (id == ID_LIST) return 0;
        EnterCriticalSection(&g_cs); st = g_st; LeaveCriticalSection(&g_cs);
        switch (id) {
        case ID_PLAY: {
            int sel = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
            if (st.state == 0) {
                g_errorStreak = 0;
                if (sel < 0) sel = 0;
                if (sel == g_set.lastIndex && g_set.lastPosMs > 3000 && g_curIndex < 0)
                    play_index(sel, g_set.lastPosMs);   /* 지난번 위치에서 이어 듣기 */
                else
                    play_index(sel, 0);
            } else if (st.state == 2 && sel >= 0 && sel != g_curIndex) {
                play_index(sel, 0);
            } else {
                post_cmd(CMD_PAUSE, 0, 0);
            }
            break;
        }
        case ID_PREV:
            if (st.state != 0 && st.posMs > 3000) play_index(g_curIndex, 0);
            else play_index(next_index(-1, 0), 0);
            break;
        case ID_NEXT:
            play_index(next_index(1, 0), 0);
            break;
        case ID_MENU:
            g_set.panel = !g_set.panel;
            relayout();
            break;
        case ID_STOP:
            post_cmd(CMD_STOP, 0, 0);
            g_set.lastPosMs = 0;
            break;
        case ID_QUAL:
            g_set.qmode = (g_set.qmode + 1) % 5;
            /* 자동으로 돌아오면 '보통'부터 다시 시도 */
            post_cmd(CMD_RECONFIG, g_set.qmode == 0 ? 1 : -1, 0);
            break;
        case ID_EAR:
            g_set.earphone = !g_set.earphone;
            post_cmd(CMD_RECONFIG, -1, 0);
            break;
        case ID_LEVEL:
            g_set.levelIdx = (g_set.levelIdx + 1) & 3;
            post_cmd(CMD_RECONFIG, -1, 0);
            break;
        case ID_ORDER:
            g_set.order = (g_set.order + 1) % 3;
            break;
        case ID_RESCAN:
            do_rescan(hwnd);
            break;
        case ID_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }
        update_buttons();
        save_settings();
        SetFocus(g_list);
        return 0;
    }
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
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) SetFocus(g_list);
        return 0;
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
    lf.lfQuality = DEFAULT_QUALITY;
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
    if (prev) { SetForegroundWindow((HWND)((ULONG)prev | 1)); return 0; }

    GetModuleFileNameW(NULL, g_exeDir, MAX_PATH);
    slash = wcsrchr(g_exeDir, L'\\');
    if (slash) slash[1] = 0; else wcscpy(g_exeDir, L"\\");

    sdc = GetDC(NULL);
    g_scale = GetDeviceCaps(sdc, LOGPIXELSY) >= 150 ? 2 : 1;
    ReleaseDC(NULL, sdc);
    g_font = make_font(14 * g_scale, 0);
    g_fontBold = make_font(15 * g_scale, 1);
    g_fontSmall = make_font(12 * g_scale, 0);

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
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = APP_CLASS;
    wc.hCursor = NULL;
    RegisterClassW(&wc);

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    g_hwnd = CreateWindowW(APP_CLASS, APP_TITLE, WS_VISIBLE,
                           wa.left, wa.top, wa.right - wa.left, wa.bottom - wa.top,
                           NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 0;
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    if (!load_list()) { scan_all(); save_list(); }
    fill_listbox();
    if (g_set.lastIndex >= 0 && g_set.lastIndex < g_fileCount) {
        SendMessageW(g_list, LB_SETCURSEL, (WPARAM)g_set.lastIndex, 0);
    } else {
        g_set.lastIndex = 0;
        g_set.lastPosMs = 0;
    }
    update_buttons();
    SetFocus(g_list);

    g_audioThread = CreateThread(NULL, 0, audio_thread, NULL, 0, &tid);

    while (GetMessageW(&m, NULL, 0, 0)) {
        /* 하드웨어 키: 좌/우 = 이전/다음, 가운데(Enter) = 선택곡 재생/일시정지 */
        if (m.message == WM_KEYDOWN) {
            if (m.wParam == VK_LEFT)  { SendMessageW(g_hwnd, WM_COMMAND, ID_PREV, 0); continue; }
            if (m.wParam == VK_RIGHT) { SendMessageW(g_hwnd, WM_COMMAND, ID_NEXT, 0); continue; }
            if (m.wParam == VK_RETURN) {
                int sel = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel != g_curIndex) { g_errorStreak = 0; play_index(sel, 0); }
                else SendMessageW(g_hwnd, WM_COMMAND, ID_PLAY, 0);
                continue;
            }
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    post_cmd(CMD_QUIT, 0, 0);
    WaitForSingleObject(g_audioThread, 3000);
    save_settings();
    return 0;
}
