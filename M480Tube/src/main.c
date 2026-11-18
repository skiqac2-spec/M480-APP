/* M480Tube - YouTube viewer for Windows Mobile 6.x (SCH-M480)
 * Search list with thumbnails, 240p playback in a full-screen player window. */
#include <winsock2.h>
#include "util.h"
#include "net.h"
#include "url.h"
#include "yt.h"
#include "player.h"
#include "tubelog.h"
#include <stdio.h>
#ifdef _WIN32_WCE
#define TXT_ELLIPSIS 0
#else
#define TXT_ELLIPSIS DT_END_ELLIPSIS
#endif
#ifdef _WIN32_WCE
#include <aygshell.h>
#endif

#define APPNAME   L"M480Tube"
#define PLAYCLASS L"M480TubePlayer"
#define VERSION   "1.3"

unsigned short *thumb_decode(const unsigned char *data, int len, int tw, int th);

#define WM_APP_SEARCHED (WM_APP + 1)
#define WM_APP_THUMB    (WM_APP + 2)
#define WM_APP_STREAMS  (WM_APP + 3)
#define WM_APP_CONFIG   (WM_APP + 4)

#define ID_EDIT 100
#define TM_PLAYER 1
#define TM_KINETIC 2

#ifdef _WIN32_WCE
#define THREAD_FLAGS 0x00010000
#define GDIFLUSH()
#else
#define THREAD_FLAGS 0
#define GDIFLUSH() GdiFlush()
#endif

enum { M_Q240 = 1000, M_Q144, M_Q360, M_ORI_P, M_ORI_L, M_STATS, M_LOOP, M_UPDATE, M_ABOUT, M_EXIT };

/* ---------------- state ---------------- */
static HINSTANCE g_inst;
static HWND g_hwnd, g_edit, g_play_hwnd;
static WNDPROC g_edit_proc;
static HFONT g_font, g_fontb, g_fonts;
static int g_topbar = 32, g_status_h = 18;
static char g_status[300];

static int g_quality = 0, g_start_ori = ORI_LAND_LEFT, g_stats = 1;

static CRITICAL_SECTION g_lcs;     /* protects g_list / g_thumb */
static YtList g_list;
static unsigned short **g_thumb;
static char *g_tstate;              /* 0 none, 1 loading, 2 done, 3 failed */
static char *g_query;
static volatile LONG g_gen;
static int g_searching, g_loading_more;
static int g_scroll;
static HANDLE g_thumb_ev;
static volatile int g_quit;

#define ROW_H 64
#define TW 96
#define TH 54

/* list back buffer */
static HBITMAP g_bb, g_bb_old;
static HDC g_bbdc;
static unsigned short *g_bbits;
static int g_bbw, g_bbh, g_bbstride;

/* touch */
static int g_down, g_dragging, g_down_y, g_down_x, g_last_y, g_down_scroll;
static DWORD g_last_t;
static int g_vel;
static int g_pressed = -1;

/* player */
static Player *g_player;
static volatile int g_opening;

/* ---------------- settings ---------------- */
static void settings_load(void)
{
	WCHAR p[MAX_PATH];
	FILE *f;
	char line[200];
	app_path(p, L"m480tube.ini");
	f = _wfopen(p, L"rb");
	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		if (!strncmp(line, "quality=", 8)) g_quality = atoi(line + 8);
		else if (!strncmp(line, "orientation=", 12)) g_start_ori = atoi(line + 12);
		else if (!strncmp(line, "stats=", 6)) g_stats = atoi(line + 6);
	}
	fclose(f);
	if (g_quality < 0 || g_quality > 2) g_quality = 0;
	if (g_start_ori < 0 || g_start_ori > 2) g_start_ori = ORI_LAND_LEFT;
}

static void settings_save(void)
{
	WCHAR p[MAX_PATH];
	FILE *f;
	app_path(p, L"m480tube.ini");
	f = _wfopen(p, L"wb");
	if (!f) return;
	fprintf(f, "quality=%d\r\norientation=%d\r\nstats=%d\r\n", g_quality, g_start_ori, g_stats);
	fclose(f);
}

/* ---------------- helpers ---------------- */
typedef BOOL (WINAPI *PSipShowIM)(DWORD);
typedef struct { DWORD cbSize, fdwFlags; RECT rcVisibleDesktop, rcSipRect; DWORD dwImDataSize; void *pvImData; } M_SIPINFO;
typedef BOOL (WINAPI *PSipGetInfo)(M_SIPINFO *);

static void sip_show(int on)
{
#ifdef _WIN32_WCE
	static PSipShowIM f;
	if (!f) { HMODULE h = LoadLibraryW(L"coredll.dll"); if (h) f = (PSipShowIM)GetProcAddressW(h, L"SipShowIM"); }
	if (f) f(on ? 1 : 0);
#else
	(void)on;
#endif
}

static void fit_window(void)
{
#ifdef _WIN32_WCE
	RECT r;
	static PSipGetInfo f;
	M_SIPINFO si;
	if (!f) { HMODULE h = LoadLibraryW(L"coredll.dll"); if (h) f = (PSipGetInfo)GetProcAddressW(h, L"SipGetInfo"); }
	memset(&si, 0, sizeof si);
	si.cbSize = sizeof si;
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0);
	if (f && f(&si) && (si.fdwFlags & 1) && si.rcSipRect.top > r.top + 40 && si.rcSipRect.top < r.bottom)
		r.bottom = si.rcSipRect.top;
	MoveWindow(g_hwnd, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
#endif
}

static void status_set(const char *s)
{
	RECT r;
	strncpy(g_status, s, sizeof g_status - 1);
	g_status[sizeof g_status - 1] = 0;
	GetClientRect(g_hwnd, &r);
	r.top = r.bottom - g_status_h;
	InvalidateRect(g_hwnd, &r, FALSE);
}

static void text_u8(HDC dc, const char *s, RECT *r, UINT fmt)
{
	WCHAR *w;
	if (!s) return;
	w = utf8_to_w(s, -1, NULL);
	if (!w) return;
	DrawTextW(dc, w, -1, r, fmt | DT_NOPREFIX);
	free(w);
}

static void fill(HDC dc, int l, int t, int r, int b, COLORREF c)
{
	RECT rc;
	HBRUSH br = CreateSolidBrush(c);
	SetRect(&rc, l, t, r, b);
	FillRect(dc, &rc, br);
	DeleteObject(br);
}

static int list_top(void) { return g_topbar; }
static int list_height(void)
{
	RECT r;
	GetClientRect(g_hwnd, &r);
	return r.bottom - g_topbar - g_status_h;
}
static int content_height(void)
{
	int n = g_list.n + ((g_list.cont || g_loading_more) ? 1 : 0);
	return n * ROW_H;
}
static void clamp_scroll(void)
{
	int maxs = content_height() - list_height();
	if (maxs < 0) maxs = 0;
	if (g_scroll > maxs) g_scroll = maxs;
	if (g_scroll < 0) g_scroll = 0;
}

static void list_invalidate(void)
{
	RECT r;
	GetClientRect(g_hwnd, &r);
	r.top = g_topbar;
	r.bottom -= g_status_h;
	InvalidateRect(g_hwnd, &r, FALSE);
}

/* ---------------- thumbnails ---------------- */
static DWORD WINAPI thumb_thread(LPVOID arg)
{
	(void)arg;
	while (!g_quit) {
		char id[16];
		int idx = -1, i, first;
		LONG gen;
		EnterCriticalSection(&g_lcs);
		gen = g_gen;
		/* prefer rows near the visible area */
		first = g_scroll / ROW_H;
		for (i = 0; i < g_list.n && idx < 0; i++) {
			int k = (first + i) % g_list.n;
			if (!g_tstate[k]) idx = k;
		}
		if (idx >= 0) { strcpy(id, g_list.items[idx].id); g_tstate[idx] = 1; }
		LeaveCriticalSection(&g_lcs);
		if (idx < 0) { WaitForSingleObject(g_thumb_ev, 2000); continue; }
		{
			char url[120];
			HttpResp r;
			NetCtl ctl;
			unsigned short *px = NULL;
			memset(&ctl, 0, sizeof ctl);
			_snprintf(url, sizeof url, "https://i.ytimg.com/vi/%s/mqdefault.jpg", id);
			if (http_request(url, "GET", NULL, 0, NULL, NULL, &r, &ctl, 300000) && r.status == 200 && r.body.len > 0)
				px = thumb_decode((const unsigned char *)r.body.p, r.body.len, TW, TH);
			http_resp_free(&r);
			EnterCriticalSection(&g_lcs);
			if (gen == g_gen && idx < g_list.n && !strcmp(g_list.items[idx].id, id)) {
				g_thumb[idx] = px;
				g_tstate[idx] = px ? 2 : 3;
				px = NULL;
			}
			LeaveCriticalSection(&g_lcs);
			free(px);
			PostMessageW(g_hwnd, WM_APP_THUMB, 0, 0);
		}
	}
	return 0;
}

static void list_clear(void)
{
	int i;
	EnterCriticalSection(&g_lcs);
	InterlockedIncrement((LONG *)&g_gen);
	for (i = 0; i < g_list.n; i++) free(g_thumb[i]);
	free(g_thumb); g_thumb = NULL;
	free(g_tstate); g_tstate = NULL;
	yt_list_free(&g_list);
	LeaveCriticalSection(&g_lcs);
	g_scroll = 0;
}

/* ---------------- search ---------------- */
typedef struct {
	char *query, *cont;
	LONG gen;
	int append;
	int ok;
	YtList res;
	NetCtl ctl;
} SearchJob;

static DWORD WINAPI search_thread(LPVOID arg)
{
	SearchJob *j = (SearchJob *)arg;
	j->ok = yt_search(j->query, j->cont, &j->res, &j->ctl);
	PostMessageW(g_hwnd, WM_APP_SEARCHED, 0, (LPARAM)j);
	return 0;
}

static void start_search(const char *q, int more)
{
	SearchJob *j;
	HANDLE th;
	if (g_searching) return;
	if (!more) {
		list_clear();
		free(g_query);
		g_query = xstrdup(q);
	} else if (!g_list.cont) return;
	j = (SearchJob *)calloc(1, sizeof(SearchJob));
	j->query = xstrdup(g_query);
	j->cont = more ? xstrdup(g_list.cont) : NULL;
	j->gen = g_gen;
	j->append = more;
	g_searching = 1;
	g_loading_more = more;
	status_set(more ? "더 불러오는 중..." : "검색 중...");
	th = CreateThread(NULL, 0x20000, search_thread, j, THREAD_FLAGS, NULL);
	if (th) CloseHandle(th);
	else { g_searching = 0; free(j->query); free(j->cont); free(j); status_set("스레드 생성 실패"); }
	InvalidateRect(g_hwnd, NULL, FALSE);
}

static void on_searched(SearchJob *j)
{
	g_searching = 0;
	g_loading_more = 0;
	if (j->gen == g_gen) {
		if (!j->ok && !j->res.n) status_set(j->res.err[0] ? j->res.err : "검색 실패");
		else {
			int i, n0;
			EnterCriticalSection(&g_lcs);
			n0 = g_list.n;
			g_list.items = (YtItem *)realloc(g_list.items, (n0 + j->res.n) * sizeof(YtItem));
			g_thumb = (unsigned short **)realloc(g_thumb, (n0 + j->res.n + 1) * sizeof(unsigned short *));
			g_tstate = (char *)realloc(g_tstate, n0 + j->res.n + 1);
			for (i = 0; i < j->res.n; i++) {
				int k, dup = 0;
				for (k = 0; k < g_list.n; k++) if (!strcmp(g_list.items[k].id, j->res.items[i].id)) dup = 1;
				if (dup) {
					free(j->res.items[i].title); free(j->res.items[i].channel);
					free(j->res.items[i].length); free(j->res.items[i].meta);
					continue;
				}
				g_list.items[g_list.n] = j->res.items[i];
				g_thumb[g_list.n] = NULL;
				g_tstate[g_list.n] = 0;
				g_list.n++;
			}
			free(j->res.items);
			j->res.items = NULL;
			j->res.n = 0;
			free(g_list.cont);
			g_list.cont = j->res.cont;
			j->res.cont = NULL;
			LeaveCriticalSection(&g_lcs);
			{
				char s[80];
				_snprintf(s, sizeof s, "영상 %d개", g_list.n);
				status_set(g_tune.notice[0] ? g_tune.notice : s);
			}
			SetEvent(g_thumb_ev);
		}
	}
	yt_list_free(&j->res);
	free(j->query);
	free(j->cont);
	free(j);
	clamp_scroll();
	InvalidateRect(g_hwnd, NULL, FALSE);
}

static void do_search_from_edit(void)
{
	WCHAR w[300];
	char *q;
	int n = GetWindowTextW(g_edit, w, 299);
	w[n < 0 ? 0 : n] = 0;
	q = w_to_utf8(w, -1);
	if (q && q[0]) {
		sip_show(0);
		SetFocus(g_hwnd);
		start_search(q, 0);
	}
	free(q);
}

/* ---------------- streams / player ---------------- */
typedef struct {
	char id[16];
	int quality;
	YtStreams s;
	int ok;
	NetCtl ctl;
} StreamJob;

static DWORD WINAPI streams_thread(LPVOID arg)
{
	StreamJob *j = (StreamJob *)arg;
	j->ok = yt_streams(j->id, j->quality, &j->s, &j->ctl);
	PostMessageW(g_hwnd, WM_APP_STREAMS, 0, (LPARAM)j);
	return 0;
}

static void open_video(int idx)
{
	StreamJob *j;
	HANDLE th;
	if (g_opening || g_player || idx < 0 || idx >= g_list.n) return;
	j = (StreamJob *)calloc(1, sizeof(StreamJob));
	strcpy(j->id, g_list.items[idx].id);
	j->quality = g_quality;
	g_opening = 1;
	status_set("재생 정보를 가져오는 중...");
	th = CreateThread(NULL, 0x20000, streams_thread, j, THREAD_FLAGS, NULL);
	if (th) CloseHandle(th);
	else { g_opening = 0; free(j); }
}

static void fullscreen(HWND h, int on)
{
#ifdef _WIN32_WCE
	if (on) {
		SHFullScreen(h, SHFS_HIDETASKBAR | SHFS_HIDESIPBUTTON | SHFS_HIDESTARTICON);
		MoveWindow(h, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), TRUE);
	} else {
		SHFullScreen(h, SHFS_SHOWTASKBAR | SHFS_SHOWSIPBUTTON | SHFS_SHOWSTARTICON);
	}
#else
	(void)h; (void)on;
#endif
}

static void on_streams(StreamJob *j)
{
	g_opening = 0;
	if (!j->ok) {
		WCHAR *w = utf8_to_w(j->s.err, -1, NULL);
		status_set(j->s.err);
		MessageBoxW(g_hwnd, w ? w : L"오류", APPNAME, MB_OK | MB_ICONEXCLAMATION);
		free(w);
	} else {
		HWND h;
#ifdef _WIN32_WCE
		h = CreateWindowW(PLAYCLASS, L"M480Tube 재생", WS_VISIBLE, 0, 0,
		                  GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, g_inst, NULL);
#else
		RECT r;
		GetWindowRect(g_hwnd, &r);
		h = CreateWindowW(PLAYCLASS, L"M480Tube 재생", WS_POPUP | WS_VISIBLE, r.left, r.top,
		                  r.right - r.left, r.bottom - r.top, NULL, NULL, g_inst, NULL);
#endif
		if (h) {
			g_play_hwnd = h;
			fullscreen(h, 1);
			g_player = player_open(h, &j->s, g_start_ori, g_stats);
			SetTimer(h, TM_PLAYER, 250, NULL);
			SetForegroundWindow(h);
			status_set("");
		}
	}
	yt_streams_free(&j->s);
	free(j);
}

static void close_player(void)
{
	if (g_play_hwnd) {
		HWND h = g_play_hwnd;
		KillTimer(h, TM_PLAYER);
		if (g_player) {
			g_start_ori = player_orientation(g_player);
			settings_save();
			player_close(g_player);
			g_player = NULL;
		}
		g_play_hwnd = NULL;
		fullscreen(h, 0);
		DestroyWindow(h);
		SetForegroundWindow(g_hwnd);
		InvalidateRect(g_hwnd, NULL, TRUE);
	}
}

static LRESULT CALLBACK play_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch (m) {
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(h, &ps);
		if (g_player) player_paint(g_player, dc);
		else fill(dc, 0, 0, 2000, 2000, RGB(0, 0, 0));
		EndPaint(h, &ps);
		return 0;
	}
	case WM_SIZE:
		if (g_player) player_resize(g_player);
		return 0;
	case WM_ACTIVATE:
		if (LOWORD(w) != WA_INACTIVE) fullscreen(h, 1);
		break;
	case WM_LBUTTONDOWN:
		if (g_player && player_tap(g_player, (short)LOWORD(l), (short)HIWORD(l)) == TAP_BACK)
			PostMessageW(h, WM_CLOSE, 0, 0);
		return 0;
	case WM_MOUSEMOVE:
		if (g_player && (w & MK_LBUTTON)) player_drag(g_player, (short)LOWORD(l), (short)HIWORD(l), 0);
		return 0;
	case WM_LBUTTONUP:
		if (g_player) player_drag(g_player, (short)LOWORD(l), (short)HIWORD(l), 1);
		return 0;
	case WM_KEYDOWN:
		if (w == VK_ESCAPE || w == VK_BACK || w == VK_F2) { PostMessageW(h, WM_CLOSE, 0, 0); return 0; }
		if (g_player) player_key(g_player, (int)w);
		return 0;
	case WM_TIMER:
		if (g_player) player_tick(g_player);
		return 0;
	case WM_CLOSE:
		close_player();
		return 0;
	}
	return DefWindowProcW(h, m, w, l);
}

/* ---------------- list painting ---------------- */
static void make_backbuffer(int w, int h)
{
	struct { BITMAPINFOHEADER h; DWORD m[3]; } bi;
	HDC dc;
	if (g_bb && g_bbw == w && g_bbh == h) return;
	if (g_bbdc) { SelectObject(g_bbdc, g_bb_old); DeleteDC(g_bbdc); g_bbdc = NULL; }
	if (g_bb) { DeleteObject(g_bb); g_bb = NULL; }
	if (w <= 0 || h <= 0) return;
	memset(&bi, 0, sizeof bi);
	bi.h.biSize = sizeof(BITMAPINFOHEADER);
	bi.h.biWidth = w;
	bi.h.biHeight = -h;
	bi.h.biPlanes = 1;
	bi.h.biBitCount = 16;
	bi.h.biCompression = BI_BITFIELDS;
	bi.m[0] = 0xF800; bi.m[1] = 0x07E0; bi.m[2] = 0x001F;
	dc = GetDC(g_hwnd);
	g_bb = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, (void **)&g_bbits, NULL, 0);
	g_bbdc = CreateCompatibleDC(dc);
	g_bb_old = (HBITMAP)SelectObject(g_bbdc, g_bb);
	ReleaseDC(g_hwnd, dc);
	g_bbw = w;
	g_bbh = h;
	g_bbstride = ((w * 2 + 3) & ~3) / 2;
}

static void paint_list(HDC dc, int w, int h)
{
	int i, first, y;
	make_backbuffer(w, h);
	if (!g_bbdc) return;
	fill(g_bbdc, 0, 0, w, h, RGB(255, 255, 255));
	SetBkMode(g_bbdc, TRANSPARENT);
	if (g_list.n == 0) {
		RECT r;
		SetRect(&r, 10, 20, w - 10, h - 10);
		SelectObject(g_bbdc, g_font);
		SetTextColor(g_bbdc, RGB(90, 90, 90));
		if (g_searching) text_u8(g_bbdc, "검색 중...", &r, DT_CENTER | DT_WORDBREAK);
		else text_u8(g_bbdc, "위 칸에 검색어를 입력하고\n'검색'을 누르세요.\n\n영상을 누르면 재생됩니다.\n화질은 오른쪽 위 메뉴에서 바꿀 수 있습니다.", &r, DT_CENTER | DT_WORDBREAK);
	}
	EnterCriticalSection(&g_lcs);
	first = g_scroll / ROW_H;
	for (i = first, y = first * ROW_H - g_scroll; i < g_list.n && y < h; i++, y += ROW_H) {
		YtItem *it = &g_list.items[i];
		RECT r;
		char line[200];
		if (i == g_pressed) fill(g_bbdc, 0, y, w, y + ROW_H, RGB(220, 230, 255));
		fill(g_bbdc, 4, y + 5, 4 + TW, y + 5 + TH, RGB(200, 200, 200));
		GDIFLUSH();
		if (g_thumb[i]) {
			int ty;
			for (ty = 0; ty < TH; ty++) {
				int yy = y + 5 + ty;
				if (yy < 0 || yy >= h) continue;
				memcpy(g_bbits + yy * g_bbstride + 4, g_thumb[i] + ty * TW, TW * 2);
			}
		}
		if (it->length) {
			SelectObject(g_bbdc, g_fonts);
			SetRect(&r, 4, y + 5 + TH - 14, 4 + TW - 2, y + 5 + TH);
			{
				SIZE sz;
				WCHAR *wl = utf8_to_w(it->length, -1, NULL);
				if (wl) {
					GetTextExtentPoint32W(g_bbdc, wl, (int)wcslen(wl), &sz);
					fill(g_bbdc, 4 + TW - sz.cx - 4, y + 5 + TH - 14, 4 + TW, y + 5 + TH, RGB(0, 0, 0));
					SetTextColor(g_bbdc, RGB(255, 255, 255));
					DrawTextW(g_bbdc, wl, -1, &r, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
					free(wl);
				}
			}
		}
		SelectObject(g_bbdc, g_fontb);
		SetTextColor(g_bbdc, RGB(10, 10, 10));
		SetRect(&r, TW + 10, y + 3, w - 3, y + 3 + 32);
		text_u8(g_bbdc, it->title, &r, DT_LEFT | DT_WORDBREAK);
		SelectObject(g_bbdc, g_fonts);
		SetTextColor(g_bbdc, RGB(100, 100, 100));
		_snprintf(line, sizeof line, "%s", it->channel ? it->channel : "");
		SetRect(&r, TW + 10, y + 35, w - 3, y + 49);
		text_u8(g_bbdc, line, &r, DT_LEFT | DT_SINGLELINE | TXT_ELLIPSIS);
		SetRect(&r, TW + 10, y + 48, w - 3, y + 62);
		text_u8(g_bbdc, it->meta ? it->meta : "", &r, DT_LEFT | DT_SINGLELINE | TXT_ELLIPSIS);
		fill(g_bbdc, 0, y + ROW_H - 1, w, y + ROW_H, RGB(225, 225, 225));
	}
	if ((g_list.cont || g_loading_more) && y < h && i >= g_list.n) {
		RECT r;
		fill(g_bbdc, 0, y, w, y + ROW_H, g_pressed == g_list.n ? RGB(220, 230, 255) : RGB(245, 245, 245));
		SetRect(&r, 0, y, w, y + ROW_H);
		SelectObject(g_bbdc, g_fontb);
		SetTextColor(g_bbdc, RGB(30, 80, 200));
		text_u8(g_bbdc, g_loading_more ? "불러오는 중..." : "결과 더 보기", &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	LeaveCriticalSection(&g_lcs);
	/* scroll indicator */
	{
		int ch = content_height();
		if (ch > h) {
			int bh = h * h / ch, by = (int)((long)g_scroll * (h - bh) / (ch - h));
			if (bh < 16) bh = 16;
			fill(g_bbdc, w - 4, by, w - 1, by + bh, RGB(150, 150, 150));
		}
	}
	BitBlt(dc, 0, list_top(), w, h, g_bbdc, 0, 0, SRCCOPY);
}

static void paint_main(HDC dc)
{
	RECT cr, r;
	GetClientRect(g_hwnd, &cr);
	/* top bar */
	fill(dc, 0, 0, cr.right, g_topbar, RGB(200, 20, 20));
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(255, 255, 255));
	SelectObject(dc, g_fontb);
	SetRect(&r, cr.right - 78, 0, cr.right - 34, g_topbar);
	fill(dc, r.left + 2, 4, r.right - 2, g_topbar - 4, RGB(150, 10, 10));
	text_u8(dc, "검색", &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	SetRect(&r, cr.right - 32, 0, cr.right, g_topbar);
	fill(dc, r.left + 2, 4, r.right - 2, g_topbar - 4, RGB(150, 10, 10));
	{
		int cx = (r.left + r.right) / 2, cy = g_topbar / 2;
		fill(dc, cx - 7, cy - 6, cx + 7, cy - 4, RGB(255, 255, 255));
		fill(dc, cx - 7, cy - 1, cx + 7, cy + 1, RGB(255, 255, 255));
		fill(dc, cx - 7, cy + 4, cx + 7, cy + 6, RGB(255, 255, 255));
	}
	/* list */
	paint_list(dc, cr.right, cr.bottom - g_topbar - g_status_h);
	/* status */
	fill(dc, 0, cr.bottom - g_status_h, cr.right, cr.bottom, RGB(235, 235, 235));
	SelectObject(dc, g_fonts);
	SetTextColor(dc, RGB(60, 60, 60));
	SetRect(&r, 4, cr.bottom - g_status_h, cr.right - 4, cr.bottom);
	text_u8(dc, g_status, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | TXT_ELLIPSIS);
}

static void layout_children(void)
{
	RECT cr;
	GetClientRect(g_hwnd, &cr);
	MoveWindow(g_edit, 4, 4, cr.right - 86, g_topbar - 8, TRUE);
	clamp_scroll();
}

/* ---------------- menu ---------------- */
static void show_menu(void)
{
	HMENU m = CreatePopupMenu();
	RECT cr;
	POINT pt;
	AppendMenuW(m, MF_STRING | (g_quality == 0 ? MF_CHECKED : 0), M_Q240, L"화질 240p (기본)");
	AppendMenuW(m, MF_STRING | (g_quality == 1 ? MF_CHECKED : 0), M_Q144, L"화질 144p (빠름)");
	AppendMenuW(m, MF_STRING | (g_quality == 2 ? MF_CHECKED : 0), M_Q360, L"화질 360p (느릴 수 있음)");
	AppendMenuW(m, MF_SEPARATOR, 0, NULL);
	AppendMenuW(m, MF_STRING | (g_start_ori == ORI_PORTRAIT ? MF_CHECKED : 0), M_ORI_P, L"재생 화면: 세로");
	AppendMenuW(m, MF_STRING | (g_start_ori != ORI_PORTRAIT ? MF_CHECKED : 0), M_ORI_L, L"재생 화면: 가로");
	AppendMenuW(m, MF_STRING | (g_stats ? MF_CHECKED : 0), M_STATS, L"재생 중 초당 프레임 표시");
	AppendMenuW(m, MF_SEPARATOR, 0, NULL);
	AppendMenuW(m, MF_STRING, M_UPDATE, L"유튜브 연결 설정 다시 받기");
	AppendMenuW(m, MF_STRING, M_ABOUT, L"정보");
	AppendMenuW(m, MF_STRING, M_EXIT, L"종료");
	GetClientRect(g_hwnd, &cr);
	pt.x = cr.right - 4;
	pt.y = g_topbar;
	ClientToScreen(g_hwnd, &pt);
	{
		int cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, pt.x, pt.y, g_hwnd, NULL);
		DestroyMenu(m);
		if (cmd) PostMessageW(g_hwnd, WM_COMMAND, cmd, 0);
	}
}

static DWORD WINAPI config_thread(LPVOID arg)
{
	NetCtl ctl;
	int r;
	(void)arg;
	memset(&ctl, 0, sizeof ctl);
	if (!arg) net_time_sync(&ctl);
	r = yt_config_update(&ctl);
	PostMessageW(g_hwnd, WM_APP_CONFIG, (WPARAM)r, (LPARAM)arg);
	return 0;
}

static void start_config_update(int verbose)
{
	HANDLE th = CreateThread(NULL, 0x10000, config_thread, (LPVOID)(LONG_PTR)verbose, THREAD_FLAGS, NULL);
	if (th) CloseHandle(th);
}

/* ---------------- input ---------------- */
static LRESULT CALLBACK edit_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WM_KEYDOWN && w == VK_RETURN) { do_search_from_edit(); return 0; }
	if (m == WM_CHAR && (w == '\r' || w == '\n')) return 0;
	if (m == WM_SETFOCUS) sip_show(1);
	return CallWindowProcW(g_edit_proc, h, m, w, l);
}

static int row_at(int y)
{
	int ly = y - list_top() + g_scroll;
	int idx;
	if (y < list_top() || y >= list_top() + list_height() || ly < 0) return -1;
	idx = ly / ROW_H;
	if (idx < g_list.n) return idx;
	if (idx == g_list.n && g_list.cont) return idx;
	return -1;
}

static void maybe_load_more(void)
{
	if (g_list.cont && !g_searching && g_scroll + list_height() >= content_height() - ROW_H)
		start_search(NULL, 1);
}

static LRESULT CALLBACK main_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch (m) {
	case WM_CREATE:
		g_hwnd = h;
		g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 10, 10, h, (HMENU)ID_EDIT, g_inst, NULL);
		g_edit_proc = (WNDPROC)SetWindowLongW(g_edit, GWL_WNDPROC, (LONG)edit_proc);
		SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_font, TRUE);
		return 0;
	case WM_SIZE:
		layout_children();
		InvalidateRect(h, NULL, FALSE);
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(h, &ps);
		paint_main(dc);
		EndPaint(h, &ps);
		return 0;
	}
	case WM_LBUTTONDOWN: {
		int x = (short)LOWORD(l), y = (short)HIWORD(l);
		RECT cr;
		GetClientRect(h, &cr);
		if (y < g_topbar) {
			if (x >= cr.right - 32) show_menu();
			else if (x >= cr.right - 78) do_search_from_edit();
			return 0;
		}
		SetFocus(h);
		sip_show(0);
		KillTimer(h, TM_KINETIC);
		g_vel = 0;
		g_down = 1;
		g_dragging = 0;
		g_down_x = x;
		g_down_y = g_last_y = y;
		g_last_t = GetTickCount();
		g_down_scroll = g_scroll;
		g_pressed = row_at(y);
		SetCapture(h);
		list_invalidate();
		return 0;
	}
	case WM_MOUSEMOVE:
		if (g_down) {
			int y = (short)HIWORD(l);
			DWORD t = GetTickCount();
			if (!g_dragging && abs(y - g_down_y) > 8) { g_dragging = 1; g_pressed = -1; }
			if (g_dragging) {
				int dt = (int)(t - g_last_t);
				if (dt > 0) g_vel = (g_last_y - y) * 16 * 25 / dt;
				g_scroll = g_down_scroll + (g_down_y - y);
				clamp_scroll();
				g_last_y = y;
				g_last_t = t;
				list_invalidate();
			}
		}
		return 0;
	case WM_LBUTTONUP:
		if (g_down) {
			int y = (short)HIWORD(l);
			g_down = 0;
			ReleaseCapture();
			if (!g_dragging) {
				int idx = row_at(y);
				if (idx >= 0 && idx == g_pressed) {
					if (idx == g_list.n) start_search(NULL, 1);
					else open_video(idx);
				}
			} else if (abs(g_vel) > 24 && GetTickCount() - g_last_t < 100) {
				SetTimer(h, TM_KINETIC, 25, NULL);
			} else maybe_load_more();
			g_pressed = -1;
			list_invalidate();
		}
		return 0;
	case WM_TIMER:
		if (w == TM_KINETIC) {
			int before = g_scroll;
			g_scroll += g_vel / 16;
			clamp_scroll();
			g_vel = g_vel * 93 / 100;
			list_invalidate();
			if (g_scroll == before || abs(g_vel) < 16) { KillTimer(h, TM_KINETIC); g_vel = 0; maybe_load_more(); }
		}
		return 0;
	case WM_KEYDOWN:
		if (w == VK_DOWN) { g_scroll += ROW_H; clamp_scroll(); list_invalidate(); maybe_load_more(); }
		else if (w == VK_UP) { g_scroll -= ROW_H; clamp_scroll(); list_invalidate(); }
		else if (w == VK_RETURN) { int idx = g_scroll / ROW_H; if (idx < g_list.n) open_video(idx); }
		return 0;
	case WM_COMMAND:
		switch (LOWORD(w)) {
		case M_Q240: g_quality = 0; settings_save(); return 0;
		case M_Q144: g_quality = 1; settings_save(); return 0;
		case M_Q360: g_quality = 2; settings_save(); return 0;
		case M_ORI_P: g_start_ori = ORI_PORTRAIT; settings_save(); return 0;
		case M_ORI_L: g_start_ori = ORI_LAND_LEFT; settings_save(); return 0;
		case M_STATS: g_stats = !g_stats; settings_save(); return 0;
		case M_UPDATE: status_set("설정을 받는 중..."); start_config_update(1); return 0;
		case M_ABOUT:
			MessageBoxW(h, L"M480Tube 1.3\nSCH-M480용 유튜브 뷰어\n\n"
			            L"유튜브가 바뀌어 재생이 안 되면 메뉴의 '유튜브 연결 설정 다시 받기'를 눌러 보세요.\n"
			            L"문제가 계속되면 프로그램 폴더의 m480tube_log.txt 내용을 알려 주세요.", APPNAME, MB_OK);
			return 0;
		case M_EXIT: DestroyWindow(h); return 0;
		}
		break;
	case WM_APP_SEARCHED:
		on_searched((SearchJob *)l);
		return 0;
	case WM_APP_THUMB:
		list_invalidate();
		return 0;
	case WM_APP_STREAMS:
		on_streams((StreamJob *)l);
		return 0;
	case WM_APP_CONFIG:
		if (l) status_set(w ? "새 연결 설정을 적용했습니다" : "이미 최신 설정입니다 (또는 받기 실패)");
		else if (g_tune.notice[0]) status_set(g_tune.notice);
		return 0;
	case WM_SETTINGCHANGE:
		if (w == 224 || w == SPI_SETWORKAREA) fit_window();
		break;
	case WM_ACTIVATE:
		if (LOWORD(w) != WA_INACTIVE) fit_window();
		break;
	case WM_CLOSE:
		DestroyWindow(h);
		return 0;
	case WM_DESTROY:
		close_player();
		g_quit = 1;
		SetEvent(g_thumb_ev);
		settings_save();
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(h, m, w, l);
}

#ifndef _WIN32_WCE
/* desktop test mode: M480TUBE_TEST="videourl|audiourl|dur_ms|w|h" plays directly,
   M480TUBE_SCRIPT="t:action,..." (actions: seek=ms, pause, shot=file.bmp, quit) */
static char *g_script;
static DWORD g_script_t0;
static void CALLBACK script_timer(HWND h, UINT m, UINT_PTR id, DWORD t)
{
	char *s = g_script, *next;
	(void)h; (void)m; (void)id; (void)t;
	while (s && *s) {
		long at = atol(s);
		char *colon = strchr(s, ':');
		next = strchr(s, ',');
		if (!colon) break;
		if (GetTickCount() - g_script_t0 >= (DWORD)at && *s != '#') {
			char act[200];
			int n = next ? (int)(next - colon - 1) : (int)strlen(colon + 1);
			if (n > 199) n = 199;
			memcpy(act, colon + 1, n);
			act[n] = 0;
			*s = '#';
			tube_log("script: %s", act);
			if (!strncmp(act, "seek=", 5) && g_player) player_debug(g_player, "seek", atol(act + 5));
			else if (!strcmp(act, "pause") && g_player) player_key(g_player, VK_RETURN);
			else if (!strncmp(act, "shot=", 5) && g_player) player_debug(g_player, act + 5, 0);
			else if (!strncmp(act, "tap=", 4) && g_player) { int x = atoi(act + 4), y = atoi(strchr(act, '/') + 1); player_tap(g_player, x, y); }
			else if (!strcmp(act, "stat") && g_player) player_debug(g_player, "stat", 0);
			else if (!strcmp(act, "quit")) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
			else if (!strncmp(act, "listshot=", 9) && g_bbits) {
				FILE *f = fopen(act + 9, "wb");
				BITMAPFILEHEADER fh;
				struct { BITMAPINFOHEADER h; DWORD m[3]; } bi;
				int yy;
				if (f) {
					memset(&fh, 0, sizeof fh); memset(&bi, 0, sizeof bi);
					fh.bfType = 0x4D42; fh.bfOffBits = sizeof fh + sizeof bi;
					bi.h.biSize = sizeof(BITMAPINFOHEADER); bi.h.biWidth = g_bbw; bi.h.biHeight = -g_bbh;
					bi.h.biPlanes = 1; bi.h.biBitCount = 16; bi.h.biCompression = BI_BITFIELDS;
					bi.m[0] = 0xF800; bi.m[1] = 0x07E0; bi.m[2] = 0x001F;
					fwrite(&fh, sizeof fh, 1, f); fwrite(&bi, sizeof bi, 1, f);
					for (yy = 0; yy < g_bbh; yy++) fwrite(g_bbits + yy * g_bbstride, 2, g_bbstride, f);
					fclose(f);
				}
			}
			else if (!strncmp(act, "scroll=", 7)) { g_scroll = atoi(act + 7); clamp_scroll(); list_invalidate(); }
		}
		s = next ? next + 1 : NULL;
	}
}

static void test_hook(void)
{
	char *t = getenv("M480TUBE_TEST");
	StreamJob *j;
	char *f[5];
	int n = 0;
	char *p;
	if (getenv("M480TUBE_LISTMOCK")) {
		FILE *fp = fopen(getenv("M480TUBE_LISTMOCK"), "rb");
		long len;
		char *d;
		SearchJob *sj = (SearchJob *)calloc(1, sizeof(SearchJob));
		if (!fp) return;
		fseek(fp, 0, SEEK_END); len = ftell(fp); fseek(fp, 0, SEEK_SET);
		d = (char *)malloc(len + 1); fread(d, 1, len, fp); d[len] = 0; fclose(fp);
		sj->ok = yt_parse_search(d, (int)len, &sj->res);
		sj->gen = g_gen;
		sj->query = xstrdup("test");
		free(g_query); g_query = xstrdup("test");
		g_searching = 1;
		free(d);
		on_searched(sj);
		if (getenv("M480TUBE_SCRIPT")) {
			g_script = xstrdup(getenv("M480TUBE_SCRIPT"));
			g_script_t0 = GetTickCount();
			SetTimer(NULL, 0, 100, script_timer);
		}
		return;
	}
	if (getenv("M480TUBE_PARSE") || getenv("M480TUBE_PARSEP")) {
		const char *fn = getenv("M480TUBE_PARSE") ? getenv("M480TUBE_PARSE") : getenv("M480TUBE_PARSEP");
		FILE *fp = fopen(fn, "rb");
		long len;
		char *d;
		int i, q;
		if (!fp) { tube_log("cannot open %s", fn); PostMessageW(g_hwnd, WM_CLOSE, 0, 0); return; }
		fseek(fp, 0, SEEK_END); len = ftell(fp); fseek(fp, 0, SEEK_SET);
		d = (char *)malloc(len + 1); fread(d, 1, len, fp); d[len] = 0; fclose(fp);
		if (getenv("M480TUBE_PARSE")) {
			YtList l;
			memset(&l, 0, sizeof l);
			yt_parse_search(d, (int)len, &l);
			tube_log("parsed %d items, cont=%s", l.n, l.cont ? "yes" : "no");
			for (i = 0; i < l.n; i++) tube_log("%s | %s | %s | %s | %s", l.items[i].id, l.items[i].title, l.items[i].channel ? l.items[i].channel : "-", l.items[i].length ? l.items[i].length : "-", l.items[i].meta ? l.items[i].meta : "-");
			yt_list_free(&l);
		} else {
			for (q = 0; q < 3; q++) {
				YtStreams s;
				int r;
				memset(&s, 0, sizeof s);
				r = yt_parse_player(d, (int)len, q, &s);
				tube_log("q%d: r=%d v=%d a=%d %dx%d dur=%ld title=%s err=%s vsize=%ld asize=%ld", q, r, s.vitag, s.aitag, s.w, s.h, s.dur_ms, s.title ? s.title : "", s.err, s.vsize, s.asize);
				yt_streams_free(&s);
			}
		}
		free(d);
		PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
		return;
	}
	if (!t) return;
	t = xstrdup(t);
	p = t;
	f[n++] = p;
	while (*p && n < 5) { if (*p == '|') { *p = 0; f[n++] = p + 1; } p++; }
	j = (StreamJob *)calloc(1, sizeof(StreamJob));
	j->ok = 1;
	j->s.vurl = xstrdup(f[0]);
	if (n > 1 && f[1][0]) j->s.aurl = xstrdup(f[1]);
	j->s.dur_ms = n > 2 ? atol(f[2]) : 60000;
	j->s.w = n > 3 ? atoi(f[3]) : 426;
	j->s.h = n > 4 ? atoi(f[4]) : 240;
	j->s.vsize = j->s.asize = -1;
	j->s.vitag = 133; j->s.aitag = 140;
	j->s.media_post = getenv("M480TUBE_POST") ? 1 : 0;
	j->s.media_rn = j->s.media_post;
	j->s.title = xstrdup("테스트 영상 Test video title that is fairly long");
	strcpy(j->s.client, "TEST");
	free(t);
	on_streams(j);
	if (getenv("M480TUBE_SCRIPT")) {
		g_script = xstrdup(getenv("M480TUBE_SCRIPT"));
		g_script_t0 = GetTickCount();
		SetTimer(NULL, 0, 100, script_timer);
	}
}
#endif

#ifdef _WIN32_WCE
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cmd, int show)
#else
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cmd, int show)
#endif
{
	WNDCLASSW wc;
	MSG msg;
	HWND prev;
	LOGFONTW lf;
	HANDLE th;
	(void)hp; (void)cmd;
	prev = FindWindowW(APPNAME, NULL);
	if (prev) { SetForegroundWindow((HWND)((ULONG_PTR)prev | 1)); return 0; }
	g_inst = hi;
	tube_log_init();
	tube_log("M480Tube %s start", VERSION);
	InitializeCriticalSection(&g_lcs);
	settings_load();
	yt_config_init();
	net_init();
	entropy_stir(&hi, sizeof hi);

	memset(&lf, 0, sizeof lf);
	lf.lfHeight = -14;
	lf.lfQuality = 5;
	g_font = CreateFontIndirectW(&lf);
	lf.lfWeight = FW_BOLD;
	lf.lfHeight = -13;
	g_fontb = CreateFontIndirectW(&lf);
	lf.lfWeight = FW_NORMAL;
	lf.lfHeight = -12;
	g_fonts = CreateFontIndirectW(&lf);

	g_thumb_ev = CreateEventW(NULL, FALSE, FALSE, NULL);

	memset(&wc, 0, sizeof wc);
	wc.lpfnWndProc = play_proc;
	wc.hInstance = hi;
	wc.lpszClassName = PLAYCLASS;
	RegisterClassW(&wc);

	memset(&wc, 0, sizeof wc);
	wc.lpfnWndProc = main_proc;
	wc.hInstance = hi;
	wc.lpszClassName = APPNAME;
#ifdef _WIN32_WCE
	wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
#endif
	RegisterClassW(&wc);
#ifdef _WIN32_WCE
	g_hwnd = CreateWindowW(APPNAME, APPNAME, WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hi, NULL);
	fit_window();
#else
	{
		RECT r = { 0, 0, 240, 400 };
		AdjustWindowRect(&r, WS_POPUP | WS_BORDER, FALSE);
		g_hwnd = CreateWindowW(APPNAME, APPNAME, WS_POPUP | WS_BORDER | WS_VISIBLE, 0, 0, r.right - r.left, r.bottom - r.top, NULL, NULL, hi, NULL);
	}
#endif
	if (!g_hwnd) return 1;
	layout_children();
	ShowWindow(g_hwnd, show);
	UpdateWindow(g_hwnd);
	th = CreateThread(NULL, 0x20000, thumb_thread, NULL, THREAD_FLAGS, NULL);
	if (th) CloseHandle(th);
	start_config_update(0);
	status_set("M480Tube " VERSION " - 검색어를 입력하세요");
#ifndef _WIN32_WCE
	test_hook();
#endif
	while (GetMessageW(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	net_shutdown();
	return 0;
}
