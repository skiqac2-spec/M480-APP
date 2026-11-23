/* M480Comic - comic / image viewer for Windows Mobile 6.x (SCH-M480)
 *
 * Type a page address -> the page is fetched over TLS 1.2 (certificate
 * errors ignored), every comic image on it is found (incl. lazy-load
 * attributes and URLs written in scripts) and shown fitted to the screen.
 * Images download in the background (2 connections) and are decoded only
 * near the visible area, so long chapters fit in memory. */
#include <winsock2.h>
#include "util.h"
#include "net.h"
#include "url.h"
#include "extract.h"
#include "imgdec.h"
#include <stdio.h>

#define APPNAME   L"M480Comic"
#define VIEWCLASS L"M480ComicView"
#define VERSION   "1.1"

#define WM_APP_PAGE  (WM_APP + 1)
#define WM_APP_ITEM  (WM_APP + 2)
#define WM_APP_DEC   (WM_APP + 3)
#define WM_APP_PROG  (WM_APP + 4)

#define ID_ADDR   100
#define ID_GO     101
#define ID_MENU   102
#define ID_LIST   103

#define TM_KINETIC 1
#define TM_HOLD    2
#define HK_SOFT2   0x51
#ifdef _WIN32_WCE
#define SUBCLASS(h, fn) ((WNDPROC)SetWindowLongW((h), GWL_WNDPROC, (LONG)(fn)))
#else
#define SUBCLASS(h, fn) ((WNDPROC)SetWindowLongPtrW((h), GWLP_WNDPROC, (LONG_PTR)(fn)))
#endif
#ifndef DT_END_ELLIPSIS
#define DT_END_ELLIPSIS 0
#endif
#ifndef MOD_KEYUP
#define MOD_KEYUP 0x1000
#endif

#ifdef _WIN32_WCE
#define THREAD_FLAGS 0x00010000 /* STACK_SIZE_PARAM_IS_A_RESERVATION */
#else
#define THREAD_FLAGS 0
#endif

enum { M_ADDR = 1000, M_RELOAD, M_NEXT, M_PREV, M_MODE0, M_MODE1, M_Z100, M_Z150, M_Z200, M_FULL, M_SMALL,
       M_ADDBM, M_BMLIST, M_HIST, M_LINKS, M_TOP, M_BOTTOM, M_SAVE, M_UA0, M_UA1, M_UA2, M_INFO, M_EXIT,
       M_BACK, M_DELITEM, M_CLOSELIST, M_COOKIES };

static const char *g_uas[3] = {
	"Mozilla/5.0 (Linux; Android 10; SM-G960N) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Mobile Safari/537.36",
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36",
	"Mozilla/5.0 (iPhone; CPU iPhone OS 16_6 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6 Mobile/15E148 Safari/604.1"
};

int g_scale100 = 100;
#define SC(v) (((v) * g_scale100 + 50) / 100)

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */
enum { IS_WAIT, IS_LOAD, IS_READY, IS_FAIL };

typedef struct {
	char *url;
	int state;
	int ow, oh;
	int tries;
	char err[80];
	int got, total;            /* download progress (bytes) */
	unsigned short *pix;       /* decoded RGB565, pw x ph */
	int pw, ph;
	int decoding;
	int dec_fail_tw;           /* target width a decode failed at */
	int is_small;               /* tiny image (icon/banner): hidden unless g_show_small */
	int y, dw, dh;             /* layout in scroll mode */
} Item;

static CRITICAL_SECTION g_cs;
static volatile LONG g_gen;
static Item *g_items;
static int g_nitems;
static int g_focus;            /* first visible item (download priority) */
typedef struct { int idx, tw; } Want;
static Want g_want[10];
static int g_nwant;
static HANDLE g_dl_ev, g_dec_ev;
static NetCtl g_dlctl[2];
static volatile int g_quit;

static HINSTANCE g_inst;
static HWND g_hwnd, g_addr, g_go, g_menubtn, g_view, g_list;
static WNDPROC g_addr_proc, g_list_proc;
static HFONT g_font, g_font_big, g_font_list;
static int g_tb_h, g_st_h;

/* page */
static char *g_url;            /* current page (final URL) */
static char *g_title;          /* UTF-8 */
static char *g_next, *g_prev;
static int g_nav_guess;        /* next/prev were guessed from the number in the address */
static Link *g_links; static int g_nlinks;
static char g_msg[300];        /* error / info shown in the view */
static int g_loading;
static int g_restore_item = -1;
static NetCtl g_pagectl;

/* settings */
static int g_mode;             /* 0 = scroll (fit width), 1 = one image per screen */
static int g_zoom = 100;
static int g_show_small;
static int g_auto_small;       /* page has only small images: show them */
static int g_full;
static int g_ua;

/* view */
static int g_scroll, g_scrollx, g_content_h, g_content_w;
static int g_page;             /* mode 1: index into g_pages (== g_npages -> end page) */
static int *g_pages; static int g_npages;
static RECT g_btn_prev, g_btn_next;
static HBITMAP g_bb; static HDC g_bbdc; static unsigned short *g_bbits; static int g_bbw, g_bbh, g_bbstride;
static HBITMAP g_bb_old;

static int g_down, g_dragging, g_down_x, g_down_y, g_last_x, g_last_y, g_down_scroll, g_down_scrollx;
static DWORD g_last_t;
static int g_vel;

/* lists: history / bookmarks / links */
typedef struct { char *url; char *title; int item; } Mark;
#define MAXHIST 40
#define MAXBM 100
static Mark g_hist[MAXHIST]; static int g_nhist;
static Mark g_bm[MAXBM]; static int g_nbm;
enum { L_NONE, L_HIST, L_BM, L_LINKS };
static int g_list_kind;
typedef struct { char *url; int item; } BackEnt;
#define MAXBACK 30
static BackEnt g_back[MAXBACK]; static int g_nback;

static void navigate(const char *url, int push_back, int restore_item);
static void relayout(void);
static void update_want(void);
static void view_invalidate(void);
static void layout_children(void);
static void list_close(void);

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */
static int view_w(void) { RECT r; GetClientRect(g_view, &r); return r.right > 0 ? r.right : 240; }
static int view_h(void) { RECT r; GetClientRect(g_view, &r); return r.bottom > 0 ? r.bottom : 300; }

static WCHAR *U(const char *s) { return utf8_to_w(s ? s : "", -1, NULL); }

static HFONT make_font(int px, int bold)
{
	LOGFONTW lf;
	memset(&lf, 0, sizeof lf);
	lf.lfHeight = -px;
	lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
	lf.lfCharSet = DEFAULT_CHARSET;
	lf.lfQuality = 5; /* CLEARTYPE_QUALITY (ignored where unsupported) */
	return CreateFontIndirectW(&lf);
}

static void msgbox(const char *t)
{
	WCHAR *w = U(t);
	MessageBoxW(g_hwnd, w, APPNAME, MB_OK);
	free(w);
}

static void str_set(char **dst, const char *s) { free(*dst); *dst = s ? xstrdup(s) : NULL; }

/* convert page bytes (charset) to UTF-8 */
static char *to_utf8(const char *s, const char *charset)
{
	if (!s) return NULL;
	if (charset && (stristr(charset, "euc-kr") || stristr(charset, "ks_c_5601") || stristr(charset, "cp949") || stristr(charset, "x-windows-949"))) {
		int n;
		WCHAR *w = cp_to_w(s, (int)strlen(s), 949, &n);
		char *u = w ? w_to_utf8(w, n) : xstrdup(s);
		free(w);
		return u;
	}
	return xstrdup(s);
}

static void cache_dir(WCHAR *out) { app_path(out, L"cache"); }

static void cache_file(WCHAR *out, LONG gen, int idx)
{
	WCHAR d[MAX_PATH];
	cache_dir(d);
	_snwprintf(out, MAX_PATH, L"%s\\%ld_%d.img", d, (long)gen, idx);
}

static void cache_clean(LONG keep_gen)
{
	WCHAR d[MAX_PATH], pat[MAX_PATH], f[MAX_PATH], pre[32];
	WIN32_FIND_DATAW fd;
	HANDLE h;
	cache_dir(d);
	CreateDirectoryW(d, NULL);
	_snwprintf(pat, MAX_PATH, L"%s\\*.img", d);
	_snwprintf(pre, 32, L"%ld_", (long)keep_gen);
	h = FindFirstFileW(pat, &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (keep_gen >= 0 && !wcsncmp(fd.cFileName, pre, wcslen(pre))) continue;
		_snwprintf(f, MAX_PATH, L"%s\\%s", d, fd.cFileName);
		DeleteFileW(f);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

static unsigned char *read_file(const WCHAR *path, int *len)
{
	FILE *f = _wfopen(path, L"rb");
	long n;
	unsigned char *d;
	*len = 0;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n <= 0 || n > 24 * 1024 * 1024) { fclose(f); return NULL; }
	d = (unsigned char *)big_malloc(n + 1);
	if (d && (long)fread(d, 1, n, f) != n) { big_free(d); d = NULL; }
	fclose(f);
	if (d) *len = (int)n;
	return d;
}

/* ------------------------------------------------------------------ */
/* settings, history, bookmarks                                        */
/* ------------------------------------------------------------------ */
static void settings_load(void)
{
	WCHAR p[MAX_PATH];
	char line[200];
	FILE *f;
	app_path(p, L"settings.ini");
	f = _wfopen(p, L"rb");
	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		if (!strncmp(line, "mode=", 5)) g_mode = atoi(line + 5) ? 1 : 0;
		else if (!strncmp(line, "zoom=", 5)) { g_zoom = atoi(line + 5); if (g_zoom != 150 && g_zoom != 200) g_zoom = 100; }
		else if (!strncmp(line, "small=", 6)) g_show_small = atoi(line + 6) ? 1 : 0;
		else if (!strncmp(line, "full=", 5)) g_full = atoi(line + 5) ? 1 : 0;
		else if (!strncmp(line, "ua=", 3)) { g_ua = atoi(line + 3); if (g_ua < 0 || g_ua > 2) g_ua = 0; }
	}
	fclose(f);
}

static void settings_save(void)
{
	WCHAR p[MAX_PATH];
	FILE *f;
	app_path(p, L"settings.ini");
	f = _wfopen(p, L"wb");
	if (!f) return;
	fprintf(f, "mode=%d\r\nzoom=%d\r\nsmall=%d\r\nfull=%d\r\nua=%d\r\n", g_mode, g_zoom, g_show_small, g_full, g_ua);
	fclose(f);
}

static void marks_load(const WCHAR *name, Mark *m, int *n, int max)
{
	WCHAR p[MAX_PATH];
	char line[2400];
	FILE *f;
	app_path(p, name);
	f = _wfopen(p, L"rb");
	*n = 0;
	if (!f) return;
	while (*n < max && fgets(line, sizeof line, f)) {
		char *t1, *t2;
		int l = (int)strlen(line);
		while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
		if (!l) continue;
		t1 = strchr(line, '\t');
		if (t1) *t1++ = 0;
		t2 = t1 ? strchr(t1, '\t') : NULL;
		if (t2) *t2++ = 0;
		m[*n].url = xstrdup(line);
		m[*n].title = xstrdup(t1 ? t1 : "");
		m[*n].item = t2 ? atoi(t2) : 0;
		(*n)++;
	}
	fclose(f);
}

static void marks_save(const WCHAR *name, Mark *m, int n)
{
	WCHAR p[MAX_PATH];
	FILE *f;
	int i;
	app_path(p, name);
	f = _wfopen(p, L"wb");
	if (!f) return;
	for (i = 0; i < n; i++) fprintf(f, "%s\t%s\t%d\r\n", m[i].url, m[i].title ? m[i].title : "", m[i].item);
	fclose(f);
}

static void mark_remove(Mark *m, int *n, int i)
{
	free(m[i].url); free(m[i].title);
	memmove(m + i, m + i + 1, sizeof(Mark) * (*n - i - 1));
	(*n)--;
}

static int mark_find(Mark *m, int n, const char *url)
{
	int i;
	for (i = 0; i < n; i++) if (!strcmp(m[i].url, url)) return i;
	return -1;
}

/* move/insert url to the top of the recent list */
static void hist_touch(const char *url, const char *title, int item)
{
	int i = mark_find(g_hist, g_nhist, url);
	Mark e;
	char *t;
	if (i >= 0) {
		e = g_hist[i];
		memmove(g_hist + 1, g_hist, sizeof(Mark) * i);
	} else {
		if (g_nhist == MAXHIST) mark_remove(g_hist, &g_nhist, MAXHIST - 1);
		memmove(g_hist + 1, g_hist, sizeof(Mark) * g_nhist);
		g_nhist++;
		e.url = xstrdup(url);
		e.title = xstrdup("");
		e.item = 0;
	}
	if (title && title[0]) {
		/* tabs/newlines would break the file format */
		t = xstrdup(title);
		for (i = 0; t[i]; i++) if (t[i] == '\t' || t[i] == '\r' || t[i] == '\n') t[i] = ' ';
		free(e.title); e.title = t;
	}
	if (item >= 0) e.item = item;
	g_hist[0] = e;
}

/* remember reading position of the current page */
static int focus_item(void);
static void save_position(void)
{
	if (g_url && g_nitems > 0) hist_touch(g_url, g_title, focus_item());
}

/* ------------------------------------------------------------------ */
/* worker: image downloads (2 threads)                                 */
/* ------------------------------------------------------------------ */
typedef struct { WCHAR path[MAX_PATH]; LONG gen; int idx; DWORD last_post; } DlJob;
static DlJob g_dljob[2];

static void *dl_on_headers(void *ctx, HttpResp *r)
{
	DlJob *j = (DlJob *)ctx;
	(void)r;
	return _wfopen(j->path, L"wb");
}

static void dl_progress(void *ctx, int got, int total)
{
	DlJob *j = (DlJob *)ctx;
	DWORD t = GetTickCount();
	EnterCriticalSection(&g_cs);
	if (j->gen == g_gen && j->idx < g_nitems) { g_items[j->idx].got = got; g_items[j->idx].total = total; }
	LeaveCriticalSection(&g_cs);
	if (t - j->last_post > 400) { j->last_post = t; PostMessageW(g_view, WM_APP_PROG, 0, 0); }
}

static int pick_download(void)
{
	int i;
	for (i = g_focus; i < g_nitems; i++) if (g_items[i].state == IS_WAIT) return i;
	for (i = 0; i < g_focus && i < g_nitems; i++) if (g_items[i].state == IS_WAIT) return i;
	return -1;
}

static DWORD WINAPI dl_thread(LPVOID param)
{
	int slot = (int)(INT_PTR)param;
	DlJob *j = &g_dljob[slot];
	NetCtl *ctl = &g_dlctl[slot];
	while (!g_quit) {
		char *url, *ref;
		int idx, ok, len, w = 0, h = 0;
		HttpResp resp;
		unsigned char *data;
		char err[80];
		EnterCriticalSection(&g_cs);
		idx = pick_download();
		if (idx < 0) {
			LeaveCriticalSection(&g_cs);
			WaitForSingleObject(g_dl_ev, 1000);
			continue;
		}
		g_items[idx].state = IS_LOAD;
		g_items[idx].got = 0;
		g_items[idx].total = 0;
		url = xstrdup(g_items[idx].url);
		ref = xstrdup(g_url ? g_url : "");
		j->gen = g_gen;
		j->idx = idx;
		j->last_post = 0;
		cache_file(j->path, j->gen, idx);
		memset(ctl, 0, sizeof *ctl);
		ctl->progress = dl_progress;
		ctl->on_headers = dl_on_headers;
		ctl->ctx = j;
		LeaveCriticalSection(&g_cs);
		PostMessageW(g_view, WM_APP_PROG, 0, 0);

		memset(&resp, 0, sizeof resp);
		err[0] = 0;
		ok = http_request(url, "GET", NULL, 0, NULL, ref, &resp, ctl, 16 * 1024 * 1024);
		if (resp.sink) { fclose((FILE *)resp.sink); resp.sink = NULL; }
		if (ok && resp.status >= 200 && resp.status < 300 && resp.body.len > 0) {
			/* body arrived in memory (compressed transfer): write it out */
			FILE *f = _wfopen(j->path, L"wb");
			if (f) { fwrite(resp.body.p, 1, resp.body.len, f); fclose(f); }
		}
		if (!ok) _snprintf(err, sizeof err, "%s", resp.err[0] ? resp.err : "받기 실패");
		else if (resp.status < 200 || resp.status >= 300) _snprintf(err, sizeof err, "서버 오류 %d", resp.status);
		http_resp_free(&resp);
		data = NULL; len = 0;
		if (!err[0]) {
			data = read_file(j->path, &len);
			if (!data) _snprintf(err, sizeof err, "빈 파일");
			else if (!imgdec_info(data, len, &w, &h)) {
				if (len > 4 && (data[0] == '<' || data[0] == ' ' || data[0] == '\n')) _snprintf(err, sizeof err, "이미지가 아님 (웹페이지)");
				else if (len > 12 && !memcmp(data + 4, "ftypavif", 8)) _snprintf(err, sizeof err, "AVIF 형식은 지원 안 함");
				else _snprintf(err, sizeof err, "지원하지 않는 이미지 형식");
			}
			big_free(data);
		}
		EnterCriticalSection(&g_cs);
		if (j->gen == g_gen && idx < g_nitems) {
			Item *it = &g_items[idx];
			if (!err[0]) {
				it->state = IS_READY;
				it->ow = w; it->oh = h;
				it->is_small = (w < 160 || h < 120);
			} else if (!ctl->abort && ++it->tries < 2) {
				it->state = IS_WAIT;   /* one automatic retry */
			} else {
				it->state = IS_FAIL;
				strncpy(it->err, err, sizeof it->err - 1);
				it->err[sizeof it->err - 1] = 0;
			}
			PostMessageW(g_view, WM_APP_ITEM, (WPARAM)j->gen, idx);
		}
		if (err[0]) DeleteFileW(j->path);
		j->idx = -1;
		LeaveCriticalSection(&g_cs);
		free(url);
		free(ref);
		if (!err[0]) SetEvent(g_dec_ev);
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* worker: decoder (decodes only what is near the screen)             */
/* ------------------------------------------------------------------ */
static DWORD WINAPI dec_thread(LPVOID param)
{
	(void)param;
	while (!g_quit) {
		int i, idx = -1, tw = 0, len, th = 0;
		LONG gen;
		WCHAR path[MAX_PATH];
		unsigned char *data;
		unsigned short *pix = NULL;
		char err[80];
		EnterCriticalSection(&g_cs);
		for (i = 0; i < g_nwant; i++) {
			Item *it;
			if (g_want[i].idx < 0 || g_want[i].idx >= g_nitems) continue;
			it = &g_items[g_want[i].idx];
			if (it->state != IS_READY || it->decoding) continue;
			if (it->pix && it->pw == g_want[i].tw) continue;
			if (it->dec_fail_tw == g_want[i].tw) continue;
			idx = g_want[i].idx;
			tw = g_want[i].tw;
			break;
		}
		if (idx < 0) {
			LeaveCriticalSection(&g_cs);
			WaitForSingleObject(g_dec_ev, 1000);
			continue;
		}
		g_items[idx].decoding = 1;
		gen = g_gen;
		cache_file(path, gen, idx);
		LeaveCriticalSection(&g_cs);

		err[0] = 0;
		data = read_file(path, &len);
		if (data) {
			pix = imgdec_decode(data, len, tw, &th, err, sizeof err);
			big_free(data);
		} else _snprintf(err, sizeof err, "캐시 파일 없음");

		EnterCriticalSection(&g_cs);
		if (gen == g_gen && idx < g_nitems) {
			Item *it = &g_items[idx];
			it->decoding = 0;
			if (pix) {
				big_free(it->pix);
				it->pix = pix; it->pw = tw; it->ph = th;
				pix = NULL;
			} else {
				it->dec_fail_tw = tw;
				if (!it->pix) {
					it->state = IS_FAIL;
					strncpy(it->err, err, sizeof it->err - 1);
					it->err[sizeof it->err - 1] = 0;
				}
			}
			PostMessageW(g_view, WM_APP_DEC, (WPARAM)gen, idx);
		}
		LeaveCriticalSection(&g_cs);
		big_free(pix);
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* page loading                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
	char *url;
	LONG gen;
	HttpResp resp;
	int ok;
	PageInfo pi;
	int is_image;
	char *title_utf8;
	char *charset;
} PageJob;

static DWORD WINAPI page_thread(LPVOID param)
{
	PageJob *j = (PageJob *)param;
	memset(&j->resp, 0, sizeof j->resp);
	j->ok = http_request(j->url, "GET", NULL, 0, NULL, NULL, &j->resp, &g_pagectl, 6 * 1024 * 1024);
	if (j->ok && j->resp.status >= 200 && j->resp.status < 300) {
		int w, h;
		const char *final = j->resp.url ? j->resp.url : j->url;
		if ((j->resp.ctype && !strncmp(j->resp.ctype, "image/", 6)) ||
		    imgdec_info((const unsigned char *)j->resp.body.p, j->resp.body.len, &w, &h)) {
			j->is_image = 1;
		} else {
			const char *cs;
			extract_page(j->resp.body.p ? j->resp.body.p : "", j->resp.body.len, final, &j->pi);
			cs = j->resp.charset ? j->resp.charset : j->pi.charset;
			j->charset = cs ? xstrdup(cs) : NULL;
			j->title_utf8 = to_utf8(j->pi.title, cs);
		}
	}
	PostMessageW(g_view, WM_APP_PAGE, 0, (LPARAM)j);
	return 0;
}

/* chapter address guess: change the last number in the URL by d (keeps zero padding) */
static char *url_bump(const char *u, int d)
{
	const char *end = strchr(u, '#'), *p, *s;
	char num[24], fmt[16], *out;
	long v;
	int n;
	if (!end) end = u + strlen(u);
	for (p = end; p > u && !(p[-1] >= '0' && p[-1] <= '9'); p--) ;
	if (p == u) return NULL;
	for (s = p; s > u && s[-1] >= '0' && s[-1] <= '9'; s--) ;
	n = (int)(p - s);
	if (n > 9) return NULL;
	/* the number must not be part of the host name */
	{ const char *h = strstr(u, "://"); if (h && !strchr(h + 3, '/')) return NULL; if (h && s < strchr(h + 3, '/')) return NULL; }
	memcpy(num, s, n); num[n] = 0;
	v = atol(num) + d;
	if (v < (d < 0 ? 1 : 0)) return NULL;
	if (s[0] == '0' && n > 1) _snprintf(fmt, sizeof fmt, "%%0%dld", n); else strcpy(fmt, "%ld");
	_snprintf(num, sizeof num, fmt, v);
	out = (char *)malloc(strlen(u) + 24);
	if (!out) return NULL;
	memcpy(out, u, s - u);
	strcpy(out + (s - u), num);
	strcat(out, p);
	return out;
}

static void items_free(void)
{
	int i;
	for (i = 0; i < g_nitems; i++) { free(g_items[i].url); big_free(g_items[i].pix); }
	free(g_items);
	g_items = NULL;
	g_nitems = 0;
	g_nwant = 0;
	free(g_pages); g_pages = NULL; g_npages = 0;
}

static void links_free(void)
{
	int i;
	for (i = 0; i < g_nlinks; i++) { free(g_links[i].href); free(g_links[i].text); }
	free(g_links);
	g_links = NULL;
	g_nlinks = 0;
}

static void set_addr(const char *u)
{
	WCHAR *w = U(u);
	SetWindowTextW(g_addr, w);
	free(w);
}

static void status_invalidate(void)
{
	RECT r;
	GetClientRect(g_hwnd, &r);
	r.top = r.bottom - g_st_h;
	InvalidateRect(g_hwnd, &r, FALSE);
}

static void navigate(const char *url, int push_back, int restore_item)
{
	PageJob *j;
	HANDLE th;
	int i;
	if (!url || !url[0]) return;
	list_close();
	save_position();
	if (push_back && g_url) {
		if (g_nback == MAXBACK) { free(g_back[0].url); memmove(g_back, g_back + 1, sizeof(BackEnt) * (MAXBACK - 1)); g_nback--; }
		g_back[g_nback].url = xstrdup(g_url);
		g_back[g_nback].item = focus_item();
		g_nback++;
	}
	/* stop everything belonging to the old page */
	g_pagectl.abort = 1;
	EnterCriticalSection(&g_cs);
	InterlockedIncrement(&g_gen);
	for (i = 0; i < 2; i++) g_dlctl[i].abort = 1;
	items_free();
	LeaveCriticalSection(&g_cs);
	cache_clean(g_gen);
	links_free();
	str_set(&g_next, NULL);
	str_set(&g_prev, NULL);
	str_set(&g_title, NULL);
	str_set(&g_url, url);
	g_scroll = g_scrollx = 0;
	g_page = 0;
	g_vel = 0;
	KillTimer(g_view, TM_KINETIC);
	if (restore_item < 0) {
		int h = mark_find(g_hist, g_nhist, url);
		if (h >= 0) restore_item = g_hist[h].item;
	}
	g_restore_item = restore_item > 0 ? restore_item : -1;
	g_auto_small = 0;
	_snprintf(g_msg, sizeof g_msg, "페이지 불러오는 중...");
	g_loading = 1;
	set_addr(url);
	relayout();
	view_invalidate();
	status_invalidate();

	j = (PageJob *)calloc(1, sizeof(PageJob));
	if (!j) return;
	j->url = xstrdup(url);
	j->gen = g_gen;
	memset(&g_pagectl, 0, sizeof g_pagectl);
	th = CreateThread(NULL, 0x40000, page_thread, j, THREAD_FLAGS, NULL);
	if (th) CloseHandle(th);
	else { free(j->url); free(j); g_loading = 0; _snprintf(g_msg, sizeof g_msg, "스레드를 만들 수 없음"); view_invalidate(); }
}

static void on_page(PageJob *j)
{
	int i;
	if (j->gen != g_gen) goto done;
	g_loading = 0;
	g_msg[0] = 0;
	if (j->resp.url && strcmp(j->resp.url, g_url)) { str_set(&g_url, j->resp.url); set_addr(g_url); }
	if (!j->ok) {
		_snprintf(g_msg, sizeof g_msg, "불러오기 실패\n%s", j->resp.err[0] ? j->resp.err : "알 수 없는 오류");
	} else if (j->resp.status < 200 || j->resp.status >= 300) {
		_snprintf(g_msg, sizeof g_msg, "서버 오류 %d", j->resp.status);
	} else if (j->is_image) {
		/* the address itself is an image */
		WCHAR p[MAX_PATH];
		FILE *f;
		cache_file(p, g_gen, 0);
		f = _wfopen(p, L"wb");
		if (f) { fwrite(j->resp.body.p, 1, j->resp.body.len, f); fclose(f); }
		EnterCriticalSection(&g_cs);
		g_items = (Item *)calloc(1, sizeof(Item));
		if (g_items) {
			int w = 0, h = 0;
			g_nitems = 1;
			g_items[0].url = xstrdup(g_url);
			if (imgdec_info((const unsigned char *)j->resp.body.p, j->resp.body.len, &w, &h)) {
				g_items[0].state = IS_READY; g_items[0].ow = w; g_items[0].oh = h;
			} else { g_items[0].state = IS_FAIL; strcpy(g_items[0].err, "지원하지 않는 이미지 형식"); }
		}
		LeaveCriticalSection(&g_cs);
		{
			const char *s = strrchr(g_url, '/');
			str_set(&g_title, s ? s + 1 : g_url);
		}
	} else {
		str_set(&g_title, j->title_utf8);
		str_set(&g_next, j->pi.next);
		str_set(&g_prev, j->pi.prev);
		g_nav_guess = 0;
		if (!g_next && !g_prev && j->pi.nimgs > 0) {
			g_next = url_bump(g_url, 1);
			g_prev = url_bump(g_url, -1);
			g_nav_guess = g_next || g_prev;
		}
		g_links = j->pi.links; g_nlinks = j->pi.nlinks;
		j->pi.links = NULL; j->pi.nlinks = 0;
		/* link texts to UTF-8 */
		for (i = 0; i < g_nlinks; i++) {
			char *u = to_utf8(g_links[i].text, j->charset);
			free(g_links[i].text);
			g_links[i].text = u;
		}
		EnterCriticalSection(&g_cs);
		if (j->pi.nimgs > 0) {
			g_items = (Item *)calloc(j->pi.nimgs, sizeof(Item));
			if (g_items) {
				for (i = 0; i < j->pi.nimgs; i++) { g_items[i].url = j->pi.imgs[i]; j->pi.imgs[i] = NULL; g_items[i].dec_fail_tw = -1; }
				g_nitems = j->pi.nimgs;
			}
		}
		LeaveCriticalSection(&g_cs);
		if (!g_nitems) _snprintf(g_msg, sizeof g_msg, "이 페이지에서 이미지를 찾지 못했습니다.\n\n메뉴 > 페이지 링크 목록 에서\n만화 회차를 골라 보세요.");
	}
	if (g_items) for (i = 0; i < g_nitems; i++) g_items[i].dec_fail_tw = -1;
	if (g_restore_item >= g_nitems) g_restore_item = -1;
	hist_touch(g_url, g_title, -1);
	relayout();
	if (g_restore_item > 0) {
		if (g_mode == 0) g_scroll = g_items[g_restore_item].y;
		else { for (i = 0; i < g_npages; i++) if (g_pages[i] >= g_restore_item) { g_page = i; break; } }
		relayout();
	}
	update_want();
	SetEvent(g_dl_ev);
	SetEvent(g_dl_ev);
	view_invalidate();
	status_invalidate();
done:
	http_resp_free(&j->resp);
	pageinfo_free(&j->pi);
	free(j->title_utf8);
	free(j->charset);
	free(j->url);
	free(j);
}

/* ------------------------------------------------------------------ */
/* layout                                                              */
/* ------------------------------------------------------------------ */
static int item_hidden(const Item *it)
{
	return it->state == IS_READY && it->is_small && !g_show_small && !g_auto_small;
}

/* width an item should be decoded at */
static int item_tw(int i)
{
	Item *it = &g_items[i];
	int vw = view_w(), vh = view_h(), tw;
	if (it->state != IS_READY || it->ow <= 0) return 0;
	if (g_mode == 0) tw = it->dw;
	else {
		/* fit whole image into the screen */
		tw = vw;
		if ((long)it->oh * vw > (long)vh * it->ow) tw = (int)((long)it->ow * vh / it->oh);
		if (it->is_small && tw > SC(it->ow)) tw = SC(it->ow);
	}
	if (tw < 1) tw = 1;
	/* keep the decoded buffer below ~2.4M pixels (4.8MB) */
	while ((long)tw * ((long)it->oh * tw / it->ow) > 2400000L && tw > 16) tw = tw * 7 / 8;
	return tw;
}

static void relayout(void)
{
	int vw = view_w(), vh = view_h(), i, y = 0, cw, anchor = -1, frac = 0;
	(void)vh;
	cw = vw * g_zoom / 100;
	g_content_w = cw;
	/* remember which item is at the top of the screen */
	if (g_mode == 0) {
		for (i = 0; i < g_nitems; i++) {
			Item *it = &g_items[i];
			if (it->dh > 0 && g_scroll >= it->y && g_scroll < it->y + it->dh) {
				anchor = i;
				frac = (int)((long)(g_scroll - it->y) * 4096 / it->dh);
				break;
			}
		}
	}
	EnterCriticalSection(&g_cs);
	for (i = 0; i < g_nitems; i++) {
		Item *it = &g_items[i];
		it->y = y;
		if (it->state == IS_READY && it->ow > 0) {
			if (item_hidden(it)) { it->dw = 0; it->dh = 0; }
			else {
				it->dw = it->is_small ? (SC(it->ow) < cw ? SC(it->ow) : cw) : cw;
				it->dh = (int)((long)it->oh * it->dw / it->ow);
				if (it->dh < 1) it->dh = 1;
			}
		} else if (it->state == IS_FAIL) { it->dw = cw; it->dh = SC(64); }
		else { it->dw = cw; it->dh = cw * 4 / 3; }
		y += it->dh;
	}
	/* page list for one-per-screen mode */
	free(g_pages);
	g_pages = (int *)malloc(sizeof(int) * (g_nitems + 1));
	g_npages = 0;
	if (g_pages) for (i = 0; i < g_nitems; i++) if (!item_hidden(&g_items[i])) g_pages[g_npages++] = i;
	LeaveCriticalSection(&g_cs);
	g_content_h = y + (g_nitems ? SC(96) : 0);
	if (anchor >= 0 && g_items[anchor].dh > 0) g_scroll = g_items[anchor].y + (int)((long)frac * g_items[anchor].dh / 4096);
	if (g_scroll > g_content_h - view_h()) g_scroll = g_content_h - view_h();
	if (g_scroll < 0) g_scroll = 0;
	if (g_scrollx > cw - vw) g_scrollx = cw - vw;
	if (g_scrollx < 0) g_scrollx = 0;
	if (g_page > g_npages) g_page = g_npages;
	if (g_page < 0) g_page = 0;
}

static int focus_item(void)
{
	int i;
	if (g_mode == 1) return (g_page < g_npages && g_pages) ? g_pages[g_page] : (g_nitems ? g_nitems - 1 : 0);
	for (i = 0; i < g_nitems; i++) if (g_items[i].dh > 0 && g_items[i].y + g_items[i].dh > g_scroll + 1) return i;
	return g_nitems ? g_nitems - 1 : 0;
}

/* decide what should be decoded, free what is far away */
static void update_want(void)
{
	int vh = view_h(), i, n = 0, f;
	long total = 0;
	EnterCriticalSection(&g_cs);
	f = focus_item();
	g_focus = f;
	if (g_mode == 0) {
		int lo = g_scroll - vh, hi = g_scroll + vh * 2;
		/* visible first, then below, then above */
		for (i = f; i < g_nitems && n < 10; i++) {
			if (g_items[i].y > hi) break;
			if (g_items[i].dh > 0) { g_want[n].idx = i; g_want[n].tw = item_tw(i); n++; }
		}
		for (i = f - 1; i >= 0 && n < 10; i--) {
			if (g_items[i].y + g_items[i].dh < lo) break;
			if (g_items[i].dh > 0) { g_want[n].idx = i; g_want[n].tw = item_tw(i); n++; }
		}
		lo = g_scroll - vh * 2; hi = g_scroll + vh * 4;
		for (i = 0; i < g_nitems; i++) {
			Item *it = &g_items[i];
			if (it->pix && (it->y + it->dh < lo || it->y > hi)) { big_free(it->pix); it->pix = NULL; }
		}
	} else {
		static const int order[4] = { 0, 1, -1, 2 };
		int k;
		for (k = 0; k < 4; k++) {
			int p = g_page + order[k];
			if (p >= 0 && p < g_npages) { g_want[n].idx = g_pages[p]; g_want[n].tw = item_tw(g_pages[p]); n++; }
		}
		for (i = 0; i < g_npages; i++) {
			Item *it = &g_items[g_pages[i]];
			if (it->pix && (i < g_page - 2 || i > g_page + 3)) { big_free(it->pix); it->pix = NULL; }
		}
	}
	g_nwant = n;
	/* hard memory cap: drop the farthest bitmaps */
	for (;;) {
		int farthest = -1, fd = -1;
		total = 0;
		for (i = 0; i < g_nitems; i++) if (g_items[i].pix) {
			int d = i > f ? i - f : f - i;
			total += (long)g_items[i].pw * g_items[i].ph * 2;
			if (d > fd) { fd = d; farthest = i; }
		}
		if (total <= 14L * 1024 * 1024 || farthest < 0 || fd <= 1) break;
		big_free(g_items[farthest].pix);
		g_items[farthest].pix = NULL;
	}
	LeaveCriticalSection(&g_cs);
	SetEvent(g_dec_ev);
}

/* ------------------------------------------------------------------ */
/* painting                                                            */
/* ------------------------------------------------------------------ */
static int ensure_bb(int w, int h)
{
	struct { BITMAPINFOHEADER h; DWORD masks[3]; } bi;
	void *bits = NULL;
	HDC sdc;
	if (g_bb && g_bbw == w && g_bbh == h) return 1;
	if (g_bbdc) { SelectObject(g_bbdc, g_bb_old); DeleteDC(g_bbdc); g_bbdc = NULL; }
	if (g_bb) { DeleteObject(g_bb); g_bb = NULL; }
	memset(&bi, 0, sizeof bi);
	bi.h.biSize = sizeof(BITMAPINFOHEADER);
	bi.h.biWidth = w;
	bi.h.biHeight = -h;
	bi.h.biPlanes = 1;
	bi.h.biBitCount = 16;
	bi.h.biCompression = BI_BITFIELDS;
	bi.masks[0] = 0xF800; bi.masks[1] = 0x07E0; bi.masks[2] = 0x001F;
	sdc = GetDC(NULL);
	g_bb = CreateDIBSection(sdc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
	g_bbdc = CreateCompatibleDC(sdc);
	ReleaseDC(NULL, sdc);
	if (!g_bb || !bits || !g_bbdc) return 0;
	g_bb_old = (HBITMAP)SelectObject(g_bbdc, g_bb);
	g_bbits = (unsigned short *)bits;
	g_bbw = w; g_bbh = h;
	g_bbstride = ((w * 2 + 3) & ~3) / 2;
	return 1;
}

static void fill_rect(int x0, int y0, int x1, int y1, unsigned short c)
{
	int x, y;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > g_bbw) x1 = g_bbw;
	if (y1 > g_bbh) y1 = g_bbh;
	for (y = y0; y < y1; y++) {
		unsigned short *d = g_bbits + (size_t)y * g_bbstride;
		for (x = x0; x < x1; x++) d[x] = c;
	}
}

/* scale-blit src (sw x sh) into dest rect, clipped to the back buffer */
static void blit(int dx, int dy, int dw, int dh, const unsigned short *src, int sw, int sh)
{
	static int xmap[1024];
	int x0 = dx < 0 ? 0 : dx, x1 = dx + dw > g_bbw ? g_bbw : dx + dw;
	int y0 = dy < 0 ? 0 : dy, y1 = dy + dh > g_bbh ? g_bbh : dy + dh;
	int x, y, n;
	if (x0 >= x1 || y0 >= y1 || dw <= 0 || dh <= 0) return;
	n = x1 - x0;
	if (n > 1024) n = 1024;
	for (x = 0; x < n; x++) {
		int sx = (int)((long)(x0 + x - dx) * sw / dw);
		xmap[x] = sx < sw ? sx : sw - 1;
	}
	for (y = y0; y < y1; y++) {
		int sy = (int)((long)(y - dy) * sh / dh);
		const unsigned short *row;
		unsigned short *d = g_bbits + (size_t)y * g_bbstride + x0;
		if (sy >= sh) sy = sh - 1;
		row = src + (size_t)sy * sw;
		if (dw == sw) memcpy(d, row + (x0 - dx), n * 2);
		else for (x = 0; x < n; x++) d[x] = row[xmap[x]];
	}
}

static void draw_text(HDC dc, const char *s, RECT *r, UINT fmt, COLORREF col, HFONT f)
{
	WCHAR *w = U(s);
	SetTextColor(dc, col);
	SelectObject(dc, f);
	DrawTextW(dc, w, -1, r, fmt);
	free(w);
}

static void draw_button(HDC dc, RECT *r, const char *label, int enabled)
{
	fill_rect(r->left, r->top, r->right, r->bottom, enabled ? 0x2945 : 0x18E3);
	fill_rect(r->left, r->top, r->right, r->top + 1, 0x632C);
	fill_rect(r->left, r->bottom - 1, r->right, r->bottom, 0x632C);
	fill_rect(r->left, r->top, r->left + 1, r->bottom, 0x632C);
	fill_rect(r->right - 1, r->top, r->right, r->bottom, 0x632C);
	draw_text(dc, label, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE, enabled ? RGB(255, 255, 255) : RGB(110, 110, 110), g_font_big);
}

static void draw_footer(HDC dc, int top, int vw)
{
	RECT r;
	char s[80];
	int bw = (vw - SC(24)) / 2, bh = SC(40);
	_snprintf(s, sizeof s, "끝 · %d장", g_npages);
	r.left = 0; r.right = vw; r.top = top + SC(6); r.bottom = top + SC(28);
	draw_text(dc, s, &r, DT_CENTER | DT_SINGLELINE, RGB(150, 150, 150), g_font);
	g_btn_prev.left = SC(8); g_btn_prev.right = SC(8) + bw; g_btn_prev.top = top + SC(32); g_btn_prev.bottom = g_btn_prev.top + bh;
	g_btn_next.left = vw - SC(8) - bw; g_btn_next.right = vw - SC(8); g_btn_next.top = g_btn_prev.top; g_btn_next.bottom = g_btn_prev.bottom;
	draw_button(dc, &g_btn_prev, "◀ 이전 화", g_prev != NULL);
	draw_button(dc, &g_btn_next, "다음 화 ▶", g_next != NULL);
}

static void draw_placeholder(HDC dc, Item *it, int idx, int x, int y, int w, int h)
{
	RECT r;
	char s[160];
	fill_rect(x, y, x + w, y + h, 0x0000);
	fill_rect(x + SC(4), y + SC(2), x + w - SC(4), y + h - SC(2), it->state == IS_FAIL ? 0x3000 : 0x18C3);
	if (it->state == IS_FAIL) _snprintf(s, sizeof s, "%d번 이미지 실패\n%s\n(눌러서 다시 시도)", idx + 1, it->err);
	else if (it->state == IS_LOAD) {
		if (it->total > 0) _snprintf(s, sizeof s, "%d / %d\n받는 중 %d%%", idx + 1, g_nitems, (int)((long long)it->got * 100 / it->total));
		else _snprintf(s, sizeof s, "%d / %d\n받는 중 %dKB", idx + 1, g_nitems, it->got / 1024);
	} else if (it->state == IS_READY) _snprintf(s, sizeof s, "%d / %d\n표시 준비 중...", idx + 1, g_nitems);
	else _snprintf(s, sizeof s, "%d / %d\n대기 중", idx + 1, g_nitems);
	r.left = x + SC(8); r.right = x + w - SC(8);
	r.top = y + SC(4); r.bottom = y + h - SC(4);
	/* keep the label on screen for tall placeholders */
	if (r.top < 0 && r.bottom > SC(60)) r.top = 0;
	if (r.bottom > g_bbh && r.top < g_bbh - SC(60)) r.bottom = g_bbh;
	{
		RECT c = r;
		WCHAR *w2 = U(s);
		int th;
		SelectObject(dc, g_font);
		th = DrawTextW(dc, w2, -1, &c, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
		r.top = r.top + ((r.bottom - r.top) - th) / 2;
		SetTextColor(dc, it->state == IS_FAIL ? RGB(255, 170, 170) : RGB(170, 170, 170));
		DrawTextW(dc, w2, -1, &r, DT_CENTER | DT_WORDBREAK);
		free(w2);
	}
}

static void draw_item(HDC dc, Item *it, int idx, int x, int y, int w, int h)
{
	if (it->pix) blit(x, y, w, h, it->pix, it->pw, it->ph);
	else draw_placeholder(dc, it, idx, x, y, w, h);
}

static void paint_view(HDC hdc)
{
	int vw = view_w(), vh = view_h(), i;
	if (!ensure_bb(vw, vh)) { RECT r = { 0, 0, vw, vh }; FillRect(hdc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH)); return; }
	memset(g_bbits, 0, (size_t)g_bbstride * 2 * vh);
	SetBkMode(g_bbdc, TRANSPARENT);
	SetRectEmpty(&g_btn_prev);
	SetRectEmpty(&g_btn_next);
	EnterCriticalSection(&g_cs);
	if (!g_nitems) {
		RECT r;
		const char *m = g_msg[0] ? g_msg :
			"M480 만화 뷰어 " VERSION "\n\n위 주소창에 만화 페이지 주소를\n입력하고 [이동]을 누르세요.\n\n"
			"· 보안 인증서 오류는 무시합니다\n· 이미지는 화면에 맞춰 표시\n· 메뉴(오른쪽 소프트키 또는 ≡)\n  > 최근 본 만화 / 즐겨찾기";
		r.left = SC(10); r.right = vw - SC(10); r.top = SC(24); r.bottom = vh;
		draw_text(g_bbdc, m, &r, DT_CENTER | DT_WORDBREAK, RGB(200, 200, 200), g_font_big);
	} else if (g_mode == 0) {
		int x0 = g_content_w > vw ? -g_scrollx : (vw - g_content_w) / 2;
		int foot = g_content_h - SC(96);
		for (i = 0; i < g_nitems; i++) {
			Item *it = &g_items[i];
			int y = it->y - g_scroll;
			if (it->dh <= 0) continue;
			if (y >= vh) break;
			if (y + it->dh <= 0) continue;
			draw_item(g_bbdc, it, i, x0 + (g_content_w - it->dw) / 2, y, it->dw, it->dh);
		}
		if (foot - g_scroll < vh) draw_footer(g_bbdc, foot - g_scroll, vw);
		/* scroll position bar */
		if (g_content_h > vh) {
			int th = vh * vh / g_content_h, ty;
			if (th < SC(16)) th = SC(16);
			ty = (int)((long)g_scroll * (vh - th) / (g_content_h - vh));
			fill_rect(vw - SC(3), ty, vw, ty + th, 0x8410);
		}
	} else {
		if (g_page < g_npages) {
			Item *it = &g_items[g_pages[g_page]];
			int w, h;
			if (it->state == IS_READY && it->ow > 0) {
				w = item_tw(g_pages[g_page]);
				if (it->pix && it->pw == w) h = it->ph;
				else h = (int)((long)it->oh * w / it->ow);
				/* item_tw may have shrunk for memory: stretch back to fit */
				if (h > 0 && w > 0) {
					if ((long)w * vh > (long)h * vw) { h = (int)((long)h * vw / w); w = vw; }
					else { w = (int)((long)w * vh / h); h = vh; }
					if (it->is_small && w > SC(it->ow)) { h = (int)((long)h * SC(it->ow) / w); w = SC(it->ow); }
				}
			} else { w = vw; h = vh; }
			draw_item(g_bbdc, it, g_pages[g_page], (vw - w) / 2, (vh - h) / 2, w, h);
		} else {
			draw_footer(g_bbdc, vh / 2 - SC(48), vw);
		}
		if (g_full) {
			RECT r;
			char s[32];
			_snprintf(s, sizeof s, "%d/%d", g_page + 1 > g_npages ? g_npages : g_page + 1, g_npages);
			r.left = vw - SC(70); r.right = vw - SC(4); r.top = vh - SC(18); r.bottom = vh;
			draw_text(g_bbdc, s, &r, DT_RIGHT | DT_SINGLELINE, RGB(130, 130, 130), g_font);
		}
	}
	LeaveCriticalSection(&g_cs);
	BitBlt(hdc, 0, 0, vw, vh, g_bbdc, 0, 0, SRCCOPY);
}

static void view_invalidate(void) { if (g_view) InvalidateRect(g_view, NULL, FALSE); }

/* ------------------------------------------------------------------ */
/* scrolling / navigation inside the chapter                           */
/* ------------------------------------------------------------------ */
static void set_scroll(int y)
{
	int max = g_content_h - view_h();
	if (y > max) y = max;
	if (y < 0) y = 0;
	if (y == g_scroll) return;
	g_scroll = y;
	update_want();
	view_invalidate();
	status_invalidate();
}

static void set_scrollx(int x)
{
	int max = g_content_w - view_w();
	if (x > max) x = max;
	if (x < 0) x = 0;
	if (x == g_scrollx) return;
	g_scrollx = x;
	view_invalidate();
}

static void set_page(int p)
{
	if (p < 0) p = 0;
	if (p > g_npages) p = g_npages;
	if (p == g_page) return;
	g_page = p;
	update_want();
	view_invalidate();
	status_invalidate();
}

static void page_step(int dir)
{
	if (g_mode == 1) { set_page(g_page + dir); return; }
	set_scroll(g_scroll + dir * (view_h() - SC(40)));
}

static void set_mode(int m)
{
	int f = focus_item();
	if (m == g_mode) return;
	g_mode = m;
	relayout();
	if (g_nitems) {
		int i;
		if (m == 0) set_scroll(g_items[f].y);
		else { g_page = 0; for (i = 0; i < g_npages; i++) if (g_pages[i] >= f) { g_page = i; break; } }
	}
	update_want();
	view_invalidate();
	status_invalidate();
}

static void set_zoom(int z)
{
	g_zoom = z;
	relayout();
	update_want();
	view_invalidate();
}

static void retry_item(int i)
{
	EnterCriticalSection(&g_cs);
	if (i >= 0 && i < g_nitems && g_items[i].state == IS_FAIL) {
		g_items[i].state = IS_WAIT;
		g_items[i].tries = 0;
		g_items[i].err[0] = 0;
		g_items[i].dec_fail_tw = -1;
	}
	LeaveCriticalSection(&g_cs);
	relayout();
	SetEvent(g_dl_ev);
	view_invalidate();
}

/* ------------------------------------------------------------------ */
/* fullscreen / window fitting                                         */
/* ------------------------------------------------------------------ */
typedef BOOL (WINAPI *PSHFullScreen)(HWND, DWORD);
typedef BOOL (WINAPI *PSipShowIM)(DWORD);
typedef struct { DWORD cbSize, fdwFlags; RECT rcVisibleDesktop, rcSipRect; DWORD dwImDataSize; void *pvImData; } M_SIPINFO;
typedef BOOL (WINAPI *PSipGetInfo)(M_SIPINFO *);

static void fit_window(void)
{
#ifdef _WIN32_WCE
	static PSHFullScreen fs;
	static PSipGetInfo sgi;
	static int looked;
	RECT r;
	if (!looked) {
		HMODULE a = LoadLibraryW(L"aygshell.dll"), c = LoadLibraryW(L"coredll.dll");
		looked = 1;
		if (a) fs = (PSHFullScreen)GetProcAddressW(a, L"SHFullScreen");
		if (c) sgi = (PSipGetInfo)GetProcAddressW(c, L"SipGetInfo");
	}
	if (g_full) {
		if (fs) fs(g_hwnd, 0x2 | 0x8 | 0x20); /* hide taskbar, SIP button, start icon */
		SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_SHOWWINDOW);
	} else {
		M_SIPINFO si;
		if (fs) fs(g_hwnd, 0x1 | 0x4 | 0x10);
		SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0);
		memset(&si, 0, sizeof si);
		si.cbSize = sizeof si;
		if (sgi && sgi(&si) && (si.fdwFlags & 1) && si.rcSipRect.top > r.top + 40 && si.rcSipRect.top < r.bottom)
			r.bottom = si.rcSipRect.top;
		MoveWindow(g_hwnd, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
	}
#endif
	layout_children();
}

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

static void set_full(int on)
{
	g_full = on;
	if (on) { sip_show(0); list_close(); }
	fit_window();
	relayout();
	update_want();
	InvalidateRect(g_hwnd, NULL, FALSE);
	view_invalidate();
}

static void layout_children(void)
{
	RECT r;
	int bw, top, bottom;
	if (!g_hwnd) return;
	GetClientRect(g_hwnd, &r);
	bw = g_tb_h + SC(6);
	top = g_full ? 0 : g_tb_h;
	bottom = g_full ? r.bottom : r.bottom - g_st_h;
	ShowWindow(g_addr, g_full ? SW_HIDE : SW_SHOW);
	ShowWindow(g_go, g_full ? SW_HIDE : SW_SHOW);
	ShowWindow(g_menubtn, g_full ? SW_HIDE : SW_SHOW);
	MoveWindow(g_menubtn, SC(2), SC(2), bw - SC(4), g_tb_h - SC(4), TRUE);
	MoveWindow(g_addr, bw, SC(3), r.right - bw * 2, g_tb_h - SC(6), TRUE);
	MoveWindow(g_go, r.right - bw + SC(2), SC(2), bw - SC(4), g_tb_h - SC(4), TRUE);
	MoveWindow(g_view, 0, top, r.right, bottom - top, TRUE);
	if (g_list) MoveWindow(g_list, 0, top, r.right, bottom - top, TRUE);
}

/* ------------------------------------------------------------------ */
/* list overlay (recent / bookmarks / page links)                      */
/* ------------------------------------------------------------------ */
static void list_close(void)
{
	if (!g_list) return;
	DestroyWindow(g_list);
	g_list = NULL;
	g_list_kind = L_NONE;
	ShowWindow(g_view, SW_SHOW);
	status_invalidate();
	SetFocus(g_view);
}

static void list_add(const char *title, const char *url, const char *extra)
{
	Buf b;
	WCHAR *w;
	buf_init(&b);
	if (title && title[0]) buf_adds(&b, title);
	else {
		const char *u = url;
		if (!strncmp(u, "https://", 8)) u += 8; else if (!strncmp(u, "http://", 7)) u += 7;
		buf_adds(&b, u);
	}
	if (extra) buf_adds(&b, extra);
	buf_addc(&b, 0);
	w = U(b.p);
	SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)w);
	free(w);
	buf_free(&b);
}

static void list_fill(void)
{
	int i;
	char ex[40];
	SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
	if (g_list_kind == L_HIST) {
		for (i = 0; i < g_nhist; i++) {
			_snprintf(ex, sizeof ex, "%s", g_hist[i].item > 0 ? "  (이어 보기)" : "");
			list_add(g_hist[i].title, g_hist[i].url, ex);
		}
	} else if (g_list_kind == L_BM) {
		for (i = 0; i < g_nbm; i++) list_add(g_bm[i].title, g_bm[i].url, NULL);
	} else if (g_list_kind == L_LINKS) {
		for (i = 0; i < g_nlinks; i++) list_add(g_links[i].text, g_links[i].href, NULL);
	}
	SendMessageW(g_list, LB_SETCURSEL, 0, 0);
}

static void list_open_sel(void)
{
	int s;
	char *u = NULL;
	int item = -1;
	if (!g_list) return;
	s = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
	if (s < 0) return;
	if (g_list_kind == L_HIST && s < g_nhist) { u = xstrdup(g_hist[s].url); item = g_hist[s].item; }
	else if (g_list_kind == L_BM && s < g_nbm) u = xstrdup(g_bm[s].url);
	else if (g_list_kind == L_LINKS && s < g_nlinks) u = xstrdup(g_links[s].href);
	if (u) { navigate(u, 1, item); free(u); }
}

static LRESULT CALLBACK list_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	static int down_y, moved;
	switch (m) {
	case WM_LBUTTONDOWN: down_y = (short)HIWORD(l); moved = 0; break;
	case WM_MOUSEMOVE: if (abs((short)HIWORD(l) - down_y) > SC(10)) moved = 1; break;
	case WM_LBUTTONUP: {
		LRESULT r = CallWindowProcW(g_list_proc, h, m, w, l);
		if (!moved) PostMessageW(g_hwnd, WM_COMMAND, MAKEWPARAM(ID_LIST, 0x7777), 0);
		return r;
	}
	case WM_KEYDOWN:
		if (w == VK_RETURN) { PostMessageW(g_hwnd, WM_COMMAND, MAKEWPARAM(ID_LIST, 0x7777), 0); return 0; }
		if (w == VK_ESCAPE || w == VK_BACK) { PostMessageW(g_hwnd, WM_COMMAND, M_CLOSELIST, 0); return 0; }
		break;
	}
	return CallWindowProcW(g_list_proc, h, m, w, l);
}

static void list_show(int kind)
{
	RECT r;
	int top;
	if (g_full) set_full(0);
	list_close();
	if (kind == L_LINKS && !g_nlinks) { msgbox("이 페이지에 링크가 없습니다."); return; }
	if (kind == L_BM && !g_nbm) { msgbox("즐겨찾기가 비어 있습니다.\n메뉴 > 즐겨찾기 추가 로 넣을 수 있습니다."); return; }
	if (kind == L_HIST && !g_nhist) { msgbox("최근 본 만화가 없습니다."); return; }
	GetClientRect(g_hwnd, &r);
	top = g_tb_h;
	g_list = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
	                         0, top, r.right, r.bottom - g_st_h - top, g_hwnd, (HMENU)ID_LIST, g_inst, NULL);
	if (!g_list) return;
	ShowWindow(g_view, SW_HIDE);
	SendMessageW(g_list, WM_SETFONT, (WPARAM)g_font_list, TRUE);
	g_list_proc = SUBCLASS(g_list, list_proc);
	g_list_kind = kind;
	list_fill();
	SetFocus(g_list);
	status_invalidate();
}

static void list_delete_sel(void)
{
	int s;
	if (!g_list) return;
	s = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
	if (s < 0) return;
	if (g_list_kind == L_HIST && s < g_nhist) { mark_remove(g_hist, &g_nhist, s); marks_save(L"history.txt", g_hist, g_nhist); }
	else if (g_list_kind == L_BM && s < g_nbm) { mark_remove(g_bm, &g_nbm, s); marks_save(L"bookmarks.txt", g_bm, g_nbm); }
	else return;
	list_fill();
	if (s >= (int)SendMessageW(g_list, LB_GETCOUNT, 0, 0)) s--;
	if (s >= 0) SendMessageW(g_list, LB_SETCURSEL, s, 0);
	else list_close();
}

/* ------------------------------------------------------------------ */
/* commands                                                            */
/* ------------------------------------------------------------------ */
static void go_back(void)
{
	char *u;
	int item;
	if (g_list) { list_close(); return; }
	if (g_full) { set_full(0); return; }
	if (!g_nback) return;
	g_nback--;
	u = g_back[g_nback].url;
	item = g_back[g_nback].item;
	navigate(u, 0, item);
	free(u);
}

static void go_typed(void)
{
	int n = GetWindowTextLengthW(g_addr);
	WCHAR *w;
	char *a, *u;
	if (n <= 0) return;
	w = (WCHAR *)malloc((n + 1) * sizeof(WCHAR));
	if (!w) return;
	GetWindowTextW(g_addr, w, n + 1);
	a = w_to_utf8(w, -1);
	free(w);
	u = url_fixup(a);
	free(a);
	sip_show(0);
	if (u && (!strncmp(u, "http://", 7) || !strncmp(u, "https://", 8))) navigate(u, 1, -1);
	free(u);
	SetFocus(g_view);
}

static void add_bookmark(void)
{
	int i;
	if (!g_url) return;
	i = mark_find(g_bm, g_nbm, g_url);
	if (i >= 0) { msgbox("이미 즐겨찾기에 있습니다."); return; }
	if (g_nbm >= MAXBM) { msgbox("즐겨찾기가 가득 찼습니다."); return; }
	memmove(g_bm + 1, g_bm, sizeof(Mark) * g_nbm);
	g_bm[0].url = xstrdup(g_url);
	g_bm[0].title = xstrdup(g_title ? g_title : "");
	for (i = 0; g_bm[0].title[i]; i++) if (g_bm[0].title[i] == '\t') g_bm[0].title[i] = ' ';
	g_bm[0].item = 0;
	g_nbm++;
	marks_save(L"bookmarks.txt", g_bm, g_nbm);
	msgbox("즐겨찾기에 추가했습니다.");
}

static void save_image(void)
{
	int f = focus_item(), state;
	WCHAR src[MAX_PATH], dst[MAX_PATH], dir[MAX_PATH], *name;
	char nm[120];
	const char *u, *s;
	int n;
	if (!g_nitems) return;
	EnterCriticalSection(&g_cs);
	state = g_items[f].state;
	u = g_items[f].url;
	s = strrchr(u, '/');
	s = s ? s + 1 : u;
	for (n = 0; s[n] && s[n] != '?' && s[n] != '#' && n < 80; n++) nm[n] = (s[n] == ':' || s[n] == '\\' || s[n] == '*' || s[n] == '"' || s[n] == '<' || s[n] == '>' || s[n] == '|') ? '_' : s[n];
	nm[n] = 0;
	LeaveCriticalSection(&g_cs);
	if (state != IS_READY) { msgbox("아직 받지 못한 이미지입니다."); return; }
	if (!nm[0] || !strchr(nm, '.')) _snprintf(nm + strlen(nm), sizeof nm - strlen(nm), "%simage%d.jpg", nm[0] ? "_" : "", f + 1);
	cache_file(src, g_gen, f);
	wcscpy(dir, L"\\My Documents\\M480Comic");
	CreateDirectoryW(L"\\My Documents", NULL);
	CreateDirectoryW(dir, NULL);
	name = U(nm);
	_snwprintf(dst, MAX_PATH, L"%s\\%s", dir, name);
	free(name);
	if (CopyFileW(src, dst, FALSE)) {
		char *d = w_to_utf8(dst, -1), msg[400];
		_snprintf(msg, sizeof msg, "저장했습니다:\n%s", d);
		free(d);
		msgbox(msg);
	} else msgbox("저장 실패");
}

static void show_info(void)
{
	char s[1200];
	int i, ready = 0, fail = 0, nsmall = 0;
	EnterCriticalSection(&g_cs);
	for (i = 0; i < g_nitems; i++) {
		if (g_items[i].state == IS_READY) { ready++; if (g_items[i].is_small) nsmall++; }
		if (g_items[i].state == IS_FAIL) fail++;
	}
	LeaveCriticalSection(&g_cs);
	_snprintf(s, sizeof s, "%s\n\n%s\n\n이미지 %d개 (받음 %d, 실패 %d, 작은 이미지 %d)\n다음 화: %s\n이전 화: %s%s\n\nM480 만화 뷰어 " VERSION,
	          g_title ? g_title : "(제목 없음)", g_url ? g_url : "-", g_nitems, ready, fail, nsmall,
	          g_next ? "있음" : "없음", g_prev ? "있음" : "없음", g_nav_guess ? "\n(주소의 숫자로 추정)" : "");
	msgbox(s);
}

static void show_menu(void)
{
	HMENU m = CreatePopupMenu(), vm = CreatePopupMenu(), bm = CreatePopupMenu(), gm = CreatePopupMenu(),
	      em = CreatePopupMenu(), um = CreatePopupMenu();
	RECT r;
	POINT pt;
	int cmd;
	UINT zf = g_mode == 0 ? 0 : MF_GRAYED;
	if (g_list) {
		if (g_list_kind == L_HIST || g_list_kind == L_BM) AppendMenuW(m, MF_STRING, M_DELITEM, L"선택한 항목 삭제");
		AppendMenuW(m, MF_STRING, M_CLOSELIST, L"목록 닫기");
		AppendMenuW(m, MF_SEPARATOR, 0, NULL);
	}
	/* view */
	AppendMenuW(vm, MF_STRING | (g_mode == 0 ? MF_CHECKED : 0), M_MODE0, L"세로 스크롤 (가로 맞춤)");
	AppendMenuW(vm, MF_STRING | (g_mode == 1 ? MF_CHECKED : 0), M_MODE1, L"한 장씩 (화면 맞춤)");
	AppendMenuW(vm, MF_SEPARATOR, 0, NULL);
	AppendMenuW(vm, MF_STRING | zf | (g_zoom == 100 ? MF_CHECKED : 0), M_Z100, L"확대 100% (화면 폭)");
	AppendMenuW(vm, MF_STRING | zf | (g_zoom == 150 ? MF_CHECKED : 0), M_Z150, L"확대 150%");
	AppendMenuW(vm, MF_STRING | zf | (g_zoom == 200 ? MF_CHECKED : 0), M_Z200, L"확대 200%");
	AppendMenuW(vm, MF_SEPARATOR, 0, NULL);
	AppendMenuW(vm, MF_STRING | (g_show_small ? MF_CHECKED : 0), M_SMALL, L"작은 이미지도 표시");
	/* bookmarks */
	AppendMenuW(bm, MF_STRING, M_BMLIST, L"즐겨찾기 목록");
	AppendMenuW(bm, MF_STRING | (g_url ? 0 : MF_GRAYED), M_ADDBM, L"지금 페이지 추가");
	/* jump */
	AppendMenuW(gm, MF_STRING, M_TOP, L"맨 처음으로");
	AppendMenuW(gm, MF_STRING, M_BOTTOM, L"맨 끝으로");
	/* extra */
	AppendMenuW(um, MF_STRING | (g_ua == 0 ? MF_CHECKED : 0), M_UA0, L"모바일 (안드로이드)");
	AppendMenuW(um, MF_STRING | (g_ua == 1 ? MF_CHECKED : 0), M_UA1, L"PC");
	AppendMenuW(um, MF_STRING | (g_ua == 2 ? MF_CHECKED : 0), M_UA2, L"아이폰");
	AppendMenuW(em, MF_STRING | (g_nitems ? 0 : MF_GRAYED), M_SAVE, L"지금 이미지 저장");
	AppendMenuW(em, MF_POPUP, (UINT_PTR)um, L"사이트에 보일 기기");
	AppendMenuW(em, MF_STRING, M_COOKIES, L"쿠키 삭제");
	AppendMenuW(em, MF_STRING, M_INFO, L"페이지 정보");

	AppendMenuW(m, MF_STRING | (g_next ? 0 : MF_GRAYED), M_NEXT, g_nav_guess ? L"다음 화 ▶ (주소 추정)" : L"다음 화 ▶");
	AppendMenuW(m, MF_STRING | (g_prev ? 0 : MF_GRAYED), M_PREV, g_nav_guess ? L"◀ 이전 화 (주소 추정)" : L"◀ 이전 화");
	AppendMenuW(m, MF_STRING | (g_nback ? 0 : MF_GRAYED), M_BACK, L"뒤로");
	AppendMenuW(m, MF_STRING, M_RELOAD, L"새로고침");
	AppendMenuW(m, MF_STRING, M_ADDR, L"주소 입력");
	AppendMenuW(m, MF_SEPARATOR, 0, NULL);
	AppendMenuW(m, MF_STRING, M_HIST, L"최근 본 만화");
	AppendMenuW(m, MF_POPUP, (UINT_PTR)bm, L"즐겨찾기");
	AppendMenuW(m, MF_STRING | (g_nlinks ? 0 : MF_GRAYED), M_LINKS, L"페이지 링크 목록");
	AppendMenuW(m, MF_SEPARATOR, 0, NULL);
	AppendMenuW(m, MF_POPUP, (UINT_PTR)vm, L"보기 방식 / 확대");
	AppendMenuW(m, MF_STRING | (g_full ? MF_CHECKED : 0), M_FULL, L"전체 화면");
	AppendMenuW(m, MF_POPUP, (UINT_PTR)gm, L"건너뛰기");
	AppendMenuW(m, MF_POPUP, (UINT_PTR)em, L"기타");
	AppendMenuW(m, MF_STRING, M_EXIT, L"종료");
	GetClientRect(g_hwnd, &r);
	pt.x = r.right; pt.y = g_full ? 0 : g_tb_h;
	ClientToScreen(g_hwnd, &pt);
	cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, pt.x, pt.y, g_hwnd, NULL);
	DestroyMenu(m);
	if (cmd) SendMessageW(g_hwnd, WM_COMMAND, cmd, 0);
}

static void do_command(int id)
{
	switch (id) {
	case M_ADDR:
		if (g_full) set_full(0);
		SetFocus(g_addr);
		SendMessageW(g_addr, EM_SETSEL, 0, -1);
		sip_show(1);
		break;
	case M_RELOAD: if (g_url) { char *u = xstrdup(g_url); int f = focus_item(); navigate(u, 0, f); free(u); } break;
	case M_NEXT: if (g_next) { char *u = xstrdup(g_next); navigate(u, 1, 0); free(u); } break;
	case M_PREV: if (g_prev) { char *u = xstrdup(g_prev); navigate(u, 1, 0); free(u); } break;
	case M_BACK: go_back(); break;
	case M_MODE0: set_mode(0); break;
	case M_MODE1: set_mode(1); break;
	case M_Z100: set_zoom(100); break;
	case M_Z150: set_zoom(150); break;
	case M_Z200: set_zoom(200); break;
	case M_FULL: set_full(!g_full); break;
	case M_SMALL: g_show_small = !g_show_small; relayout(); update_want(); view_invalidate(); status_invalidate(); break;
	case M_ADDBM: add_bookmark(); break;
	case M_BMLIST: list_show(L_BM); break;
	case M_HIST: save_position(); list_show(L_HIST); break;
	case M_LINKS: list_show(L_LINKS); break;
	case M_TOP: if (g_mode == 0) set_scroll(0); else set_page(0); break;
	case M_BOTTOM: if (g_mode == 0) set_scroll(g_content_h); else set_page(g_npages); break;
	case M_SAVE: save_image(); break;
	case M_UA0: case M_UA1: case M_UA2: g_ua = id - M_UA0; net_set_ua(g_uas[g_ua]); break;
	case M_COOKIES: cookies_clear(); msgbox("쿠키를 삭제했습니다."); break;
	case M_INFO: show_info(); break;
	case M_DELITEM: list_delete_sel(); break;
	case M_CLOSELIST: list_close(); break;
	case M_EXIT: DestroyWindow(g_hwnd); break;
	}
}

/* ------------------------------------------------------------------ */
/* view window                                                         */
/* ------------------------------------------------------------------ */
static int hit_item(int x, int y)
{
	int i;
	(void)x;
	if (g_mode == 1) return g_page < g_npages ? g_pages[g_page] : -1;
	for (i = 0; i < g_nitems; i++) {
		int t = g_items[i].y - g_scroll;
		if (g_items[i].dh > 0 && y >= t && y < t + g_items[i].dh) return i;
	}
	return -1;
}

static void on_tap(int x, int y)
{
	POINT pt;
	int vw = view_w(), vh = view_h(), i;
	pt.x = x; pt.y = y;
	if (!g_nitems) {
		if (g_full) set_full(0);
		return;
	}
	if (PtInRect(&g_btn_prev, pt)) { if (g_prev) do_command(M_PREV); return; }
	if (PtInRect(&g_btn_next, pt)) { if (g_next) do_command(M_NEXT); return; }
	i = hit_item(x, y);
	if (i >= 0 && g_items[i].state == IS_FAIL) { retry_item(i); return; }
	if (g_mode == 1) {
		if (x < vw * 35 / 100) page_step(-1);
		else if (x > vw * 65 / 100) page_step(1);
		else set_full(!g_full);
	} else {
		if (y < vh * 30 / 100) page_step(-1);
		else if (y > vh * 70 / 100) page_step(1);
		else set_full(!g_full);
	}
}

static LRESULT CALLBACK view_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch (m) {
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(h, &ps);
		paint_view(hdc);
		EndPaint(h, &ps);
		return 0;
	}
	case WM_ERASEBKGND: return 1;
	case WM_SIZE:
		relayout();
		update_want();
		return 0;
	case WM_LBUTTONDOWN:
		SetFocus(h);
		SetCapture(h);
		KillTimer(h, TM_KINETIC);
		g_vel = 0;
		g_down = 1; g_dragging = 0;
		g_down_x = g_last_x = (short)LOWORD(l);
		g_down_y = g_last_y = (short)HIWORD(l);
		g_down_scroll = g_scroll; g_down_scrollx = g_scrollx;
		g_last_t = GetTickCount();
		return 0;
	case WM_MOUSEMOVE:
		if (g_down) {
			int x = (short)LOWORD(l), y = (short)HIWORD(l);
			DWORD t = GetTickCount();
			if (!g_dragging && (abs(y - g_down_y) > SC(8) || abs(x - g_down_x) > SC(8))) g_dragging = 1;
			if (g_dragging && g_mode == 0) {
				int dt = (int)(t - g_last_t);
				if (dt > 0) {
					int v = (g_last_y - y) * 16 * 25 / dt;
					g_vel = (g_vel + v) / 2;
				}
				set_scroll(g_down_scroll + (g_down_y - y));
				set_scrollx(g_down_scrollx + (g_down_x - x));
			}
			g_last_x = x; g_last_y = y; g_last_t = t;
		}
		return 0;
	case WM_LBUTTONUP:
		if (g_down) {
			int x = (short)LOWORD(l), y = (short)HIWORD(l);
			ReleaseCapture();
			g_down = 0;
			if (!g_dragging) on_tap(x, y);
			else if (g_mode == 1) {
				int dx = x - g_down_x, dy = y - g_down_y;
				if (abs(dx) > SC(30) && abs(dx) > abs(dy)) page_step(dx < 0 ? 1 : -1);
				else if (abs(dy) > SC(30)) page_step(dy < 0 ? 1 : -1);
			} else if (GetTickCount() - g_last_t < 120 && abs(g_vel) > 16 * 4) {
				SetTimer(h, TM_KINETIC, 25, NULL);
			}
		}
		return 0;
	case WM_TIMER:
		if (w == TM_KINETIC) {
			int old = g_scroll;
			set_scroll(g_scroll + g_vel / 16);
			g_vel = g_vel * 94 / 100;
			if (abs(g_vel) < 16 || g_scroll == old) { KillTimer(h, TM_KINETIC); g_vel = 0; }
		}
		return 0;
	case WM_KEYDOWN: {
		int line = SC(60);
		switch (w) {
		case VK_UP: if (g_mode == 0) set_scroll(g_scroll - line); else page_step(-1); return 0;
		case VK_DOWN: if (g_mode == 0) set_scroll(g_scroll + line); else page_step(1); return 0;
		case VK_LEFT: case VK_PRIOR: page_step(-1); return 0;
		case VK_RIGHT: case VK_NEXT: case VK_SPACE: case VK_RETURN: page_step(1); return 0;
		case VK_HOME: do_command(M_TOP); return 0;
		case VK_END: do_command(M_BOTTOM); return 0;
		case VK_ESCAPE: case VK_BACK: go_back(); return 0;
		case VK_F2: return 0;
		}
		break;
	}
	case WM_APP_PAGE:
		on_page((PageJob *)l);
		return 0;
	case WM_APP_ITEM:
		if ((LONG)w == g_gen) {
			if (!g_auto_small && !g_show_small) {
				int i, busy = 0, vis = 0;
				for (i = 0; i < g_nitems; i++) {
					if (g_items[i].state == IS_WAIT || g_items[i].state == IS_LOAD) busy = 1;
					if (g_items[i].state == IS_READY && !g_items[i].is_small) vis = 1;
				}
				if (!busy && !vis) g_auto_small = 1;
			}
			relayout();
			update_want();
			view_invalidate();
			status_invalidate();
		}
		return 0;
	case WM_APP_DEC:
		if ((LONG)w == g_gen) {
			if (g_mode == 0) {
				/* image might have failed to decode: layout changes */
				if ((int)l < g_nitems && g_items[(int)l].state == IS_FAIL) relayout();
			}
			view_invalidate();
			SetEvent(g_dec_ev);
		}
		return 0;
	case WM_APP_PROG:
		view_invalidate();
		status_invalidate();
		return 0;
	}
	return DefWindowProcW(h, m, w, l);
}

/* ------------------------------------------------------------------ */
/* main window                                                         */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK addr_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WM_KEYDOWN && w == VK_RETURN) { go_typed(); return 0; }
	if (m == WM_CHAR && (w == VK_RETURN || w == '\n')) return 0;
	return CallWindowProcW(g_addr_proc, h, m, w, l);
}

static void paint_status(HDC hdc, RECT *cr)
{
	RECT r = *cr;
	char s[300], right[80];
	int i, ready = 0, loading = 0;
	HBRUSH br;
	r.top = r.bottom - g_st_h;
	br = CreateSolidBrush(RGB(30, 30, 30));
	FillRect(hdc, &r, br);
	DeleteObject(br);
	SetBkMode(hdc, TRANSPARENT);
	SelectObject(hdc, g_font);
	right[0] = 0;
	EnterCriticalSection(&g_cs);
	for (i = 0; i < g_nitems; i++) {
		if (g_items[i].state == IS_READY || g_items[i].state == IS_FAIL) ready++;
		if (g_items[i].state == IS_LOAD) loading++;
	}
	LeaveCriticalSection(&g_cs);
	if (g_list) {
		_snprintf(s, sizeof s, "%s", g_list_kind == L_HIST ? "최근 본 만화 (누르면 이어 보기)" : g_list_kind == L_BM ? "즐겨찾기" : "페이지 링크");
	} else if (g_loading) {
		_snprintf(s, sizeof s, "불러오는 중...");
	} else if (g_nitems) {
		int f = focus_item(), pos = 0;
		for (i = 0; g_pages && i < g_npages; i++) if (g_pages[i] <= f) pos = i + 1;
		_snprintf(s, sizeof s, "%d/%d %s", pos, g_npages, g_title ? g_title : "");
		if (ready < g_nitems) _snprintf(right, sizeof right, "받는 중 %d/%d", ready, g_nitems);
	} else _snprintf(s, sizeof s, "%s", g_title ? g_title : "");
	if (right[0]) {
		RECT rr = r;
		WCHAR *w = U(right);
		rr.right -= SC(4);
		SetTextColor(hdc, RGB(120, 200, 255));
		DrawTextW(hdc, w, -1, &rr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		DrawTextW(hdc, w, -1, &rr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_CALCRECT);
		r.right = r.right - (rr.right - rr.left) - SC(10);
		free(w);
	}
	{
		WCHAR *w = U(s);
		r.left += SC(4);
		SetTextColor(hdc, RGB(220, 220, 220));
		DrawTextW(hdc, w, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		free(w);
	}
}

#ifdef _WIN32_WCE
typedef BOOL (WINAPI *PUnregisterFunc1)(UINT, UINT);
#endif
#ifdef _WIN32_WCE
static int g_softkey_on;
#endif
static void softkey_grab(int on)
{
#ifdef _WIN32_WCE
	static PUnregisterFunc1 unreg;
	static int looked;
	if (!looked) {
		HMODULE mm = LoadLibraryW(L"coredll.dll");
		looked = 1;
		if (mm) unreg = (PUnregisterFunc1)GetProcAddressW(mm, L"UnregisterFunc1");
	}
	if (on && !g_softkey_on) {
		if (unreg) unreg(MOD_KEYUP, VK_F2);
		if (RegisterHotKey(g_hwnd, HK_SOFT2, MOD_KEYUP, VK_F2)) g_softkey_on = 1;
	} else if (!on && g_softkey_on) {
		UnregisterHotKey(g_hwnd, HK_SOFT2);
		g_softkey_on = 0;
	}
#else
	(void)on;
#endif
}

static LRESULT CALLBACK main_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch (m) {
	case WM_CREATE: {
		WNDCLASSW wc;
		memset(&wc, 0, sizeof wc);
		wc.lpfnWndProc = view_proc;
		wc.hInstance = g_inst;
		wc.lpszClassName = VIEWCLASS;
		RegisterClassW(&wc);
		g_hwnd = h;
		g_menubtn = CreateWindowExW(0, L"BUTTON", L"≡", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10, h, (HMENU)ID_MENU, g_inst, NULL);
		g_addr = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 10, 10, h, (HMENU)ID_ADDR, g_inst, NULL);
		g_go = CreateWindowExW(0, L"BUTTON", L"이동", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10, h, (HMENU)ID_GO, g_inst, NULL);
		g_view = CreateWindowExW(0, VIEWCLASS, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, NULL, g_inst, NULL);
		SendMessageW(g_addr, WM_SETFONT, (WPARAM)g_font_big, TRUE);
		SendMessageW(g_menubtn, WM_SETFONT, (WPARAM)g_font_big, TRUE);
		SendMessageW(g_go, WM_SETFONT, (WPARAM)g_font_big, TRUE);
		g_addr_proc = SUBCLASS(g_addr, addr_proc);
		return 0;
	}
	case WM_SIZE:
		layout_children();
		return 0;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(h, &ps);
		RECT r;
		GetClientRect(h, &r);
		if (!g_full) {
			RECT t = r;
			HBRUSH br = CreateSolidBrush(RGB(45, 45, 45));
			t.bottom = g_tb_h;
			FillRect(hdc, &t, br);
			DeleteObject(br);
			paint_status(hdc, &r);
		}
		EndPaint(h, &ps);
		return 0;
	}
	case WM_ERASEBKGND: return 1;
	case WM_COMMAND:
		if (LOWORD(w) == ID_GO) { go_typed(); return 0; }
		if (LOWORD(w) == ID_MENU) { show_menu(); return 0; }
		if (LOWORD(w) == ID_LIST) {
			if (HIWORD(w) == 0x7777 || HIWORD(w) == LBN_DBLCLK) list_open_sel();
			return 0;
		}
		if (LOWORD(w) == ID_ADDR) {
			if (HIWORD(w) == EN_SETFOCUS) { sip_show(1); SendMessageW(g_addr, EM_SETSEL, 0, -1); }
			else if (HIWORD(w) == EN_KILLFOCUS) sip_show(0);
			return 0;
		}
		do_command(LOWORD(w));
		return 0;
	case WM_SETTINGCHANGE:
		if (!g_full && (w == 224 /* SPI_SETSIPINFO */ || w == SPI_SETWORKAREA)) fit_window();
		break;
	case WM_ACTIVATE:
		if (LOWORD(w) != WA_INACTIVE) { fit_window(); softkey_grab(1); }
		else softkey_grab(0);
		break;
	case WM_HOTKEY:
		if (w == HK_SOFT2) { if (LOWORD(l) & MOD_KEYUP) show_menu(); return 0; }
		break;
	case WM_KEYDOWN: case WM_KEYUP:
		return SendMessageW(g_view, m, w, l);
	case WM_CLOSE:
		DestroyWindow(h);
		return 0;
	case WM_DESTROY: {
		int i;
		save_position();
		g_quit = 1;
		g_pagectl.abort = 1;
		for (i = 0; i < 2; i++) g_dlctl[i].abort = 1;
		SetEvent(g_dl_ev); SetEvent(g_dl_ev); SetEvent(g_dec_ev);
		settings_save();
		marks_save(L"history.txt", g_hist, g_nhist);
		cookies_save();
		softkey_grab(0);
#ifdef _WIN32_WCE
		if (g_full) { g_full = 0; fit_window(); }
#endif
		PostQuitMessage(0);
		return 0;
	}
	}
	return DefWindowProcW(h, m, w, l);
}

#ifdef _WIN32_WCE
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cmd, int show)
#else
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cmd, int show)
#endif
{
	WNDCLASSW wc;
	MSG msg;
	HDC dc;
	HWND prev;
	HANDLE th;
	int i;
	(void)hp;
	prev = FindWindowW(APPNAME, NULL);
	if (prev) { SetForegroundWindow((HWND)((ULONG_PTR)prev | 1)); return 0; }
	g_inst = hi;
	dc = GetDC(NULL);
	g_scale100 = GetDeviceCaps(dc, LOGPIXELSY) * 100 / 96;
	ReleaseDC(NULL, dc);
	if (g_scale100 < 100) g_scale100 = 100;
	InitializeCriticalSection(&g_cs);
	settings_load();
	g_font = make_font(SC(12), 0);
	g_font_big = make_font(SC(15), 0);
	g_font_list = make_font(SC(17), 0);
	g_tb_h = SC(30);
	g_st_h = SC(18);
	net_init();
	net_allow_insecure_all(1);
	net_set_ua(g_uas[g_ua]);
	cookies_load();
	marks_load(L"history.txt", g_hist, &g_nhist, MAXHIST);
	marks_load(L"bookmarks.txt", g_bm, &g_nbm, MAXBM);
	entropy_stir(&hi, sizeof hi);
	cache_clean(-1);
	g_dl_ev = CreateEventW(NULL, FALSE, FALSE, NULL);
	g_dec_ev = CreateEventW(NULL, FALSE, FALSE, NULL);
	for (i = 0; i < 2; i++) {
		g_dljob[i].idx = -1;
		th = CreateThread(NULL, 0x40000, dl_thread, (LPVOID)(INT_PTR)i, THREAD_FLAGS, NULL);
		if (th) CloseHandle(th);
	}
	th = CreateThread(NULL, 0x20000, dec_thread, NULL, THREAD_FLAGS, NULL);
	if (th) CloseHandle(th);

	memset(&wc, 0, sizeof wc);
	wc.lpfnWndProc = main_proc;
	wc.hInstance = hi;
	wc.lpszClassName = APPNAME;
#ifdef _WIN32_WCE
	wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
#endif
	RegisterClassW(&wc);
#ifdef _WIN32_WCE
	g_hwnd = CreateWindowW(APPNAME, L"M480 만화", WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hi, NULL);
#else
	{
		int ww = 240, hh = 400;
		char *e = getenv("M480_SIZE");
		RECT r;
		if (e) sscanf(e, "%dx%d", &ww, &hh);
		r.left = 0; r.top = 0; r.right = ww; r.bottom = hh;
		AdjustWindowRect(&r, WS_POPUP | WS_BORDER, FALSE);
		g_hwnd = CreateWindowW(APPNAME, L"M480 만화", WS_POPUP | WS_BORDER | WS_VISIBLE, 0, 0, r.right - r.left, r.bottom - r.top, NULL, NULL, hi, NULL);
	}
#endif
	if (!g_hwnd) return 1;
	fit_window();
	ShowWindow(g_hwnd, show);
	UpdateWindow(g_hwnd);
	SetFocus(g_view);
	{
		char *u = NULL;
		if (cmd && cmd[0]) {
			char *a = w_to_utf8(cmd, -1), *s = a;
			while (*s == ' ' || *s == '"') s++;
			{ int n = (int)strlen(s); while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '"')) s[--n] = 0; }
			if (*s) u = url_fixup(s);
			free(a);
		}
		if (u) navigate(u, 0, -1);
		else if (g_nhist) list_show(L_HIST);
		free(u);
	}
	while (GetMessageW(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	g_quit = 1;
	Sleep(200);
	net_shutdown();
	return 0;
}
