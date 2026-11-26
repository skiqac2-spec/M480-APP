/* M480Web - a lightweight TLS 1.2 web browser for Windows Mobile 6.x
 * UI: toolbar + address bar, scrollable page view with touch drag,
 * background page/image loading. */
#include <winsock2.h>
#include "util.h"
#include "net.h"
#include "url.h"
#include "html.h"
#include "layout.h"
#include <stdio.h>

int img_decode(const unsigned char *data, int len, int maxw, Img *im);

#define APPNAME   L"M480Web"
#define VIEWCLASS L"M480WebView"

#define WM_APP_LOADED   (WM_APP + 1)
#define WM_APP_PROGRESS (WM_APP + 2)
#define WM_APP_IMGDONE  (WM_APP + 3)
#define WM_APP_TRDONE   (WM_APP + 4)
#define WM_APP_TRPROG   (WM_APP + 5)

#define ID_ADDR     100
#define ID_FIELD    101
#define ID_PICKER   102

#define TM_KINETIC  1
#define TM_RELAYOUT 2
#define TM_REFRESH  3
#define TM_HOLD     4

#ifdef _WIN32_WCE
#define THREAD_STACK 0x40000
#define THREAD_FLAGS 0x00010000 /* STACK_SIZE_PARAM_IS_A_RESERVATION */
#else
#define THREAD_STACK 0x40000
#define THREAD_FLAGS 0
#endif

enum { M_HOME = 1000, M_RELOAD, M_ADDBM, M_BOOKMARKS, M_HISTORY, M_SIMPLE, M_IMAGES, M_INFO, M_COOKIES, M_EXIT,
       M_FONT0, M_FONT1, M_FONT2, M_FONT3, M_UA0, M_UA1, M_UA2, M_UA3, M_TOP, M_BOTTOM,
       M_LOPEN, M_LCOPY, M_LSIMPLE, M_LSAVEIMG, M_ADDSC, M_EDITSC, M_FILES, M_TRANSLATE, M_AUTOTR, M_DARK };

static const char *g_uas[4] = {
	"Opera/9.80 (J2ME/MIDP; Opera Mini/9.80 (S60; SymbOS; Opera Mobi/23.348; U; ko) Presto/2.5.25 Version/10.54",
	"Mozilla/5.0 (Linux; Android 4.4.2; SHV-E330S) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/38.0.2125.102 Mobile Safari/537.36",
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows CE; IEMobile 8.12; MSIEMobile 6.0)"
};
static const int g_fontsz[4] = { 12, 14, 16, 19 };

/* ---------------- state ---------------- */
static HINSTANCE g_inst;
static HWND g_hwnd, g_addr, g_view, g_field, g_picker;
static WNDPROC g_addr_proc, g_field_proc;
static int g_tb_h, g_status_h;

static Doc *g_doc;
static Layout g_lay;
static int g_scroll;
static char g_tlsinfo[96];
static char g_status[200];
static int g_loading;
static volatile LONG g_gen;
static int g_set_font = 1, g_set_ua = 0;
static int g_autotr;   /* translate foreign pages automatically */
static HBRUSH g_ctl_br;  /* edit-control background brush for dark mode */
#define DEFAULT_REPO "https://raw.githubusercontent.com/skiqac2-spec/M480-APP/main/downloads/files.txt"
static char g_repo[400] = DEFAULT_REPO;
static char g_token[160];

typedef struct { char *url; int scroll; } HistEnt;
#define MAXHIST 60
static HistEnt g_hist[MAXHIST];
static int g_nhist, g_hpos = -1;

typedef struct {
	char *url, *post, *ctype, *referer;
	int postlen;
	int mode;       /* NAV_* */
	LONG gen;
	NetCtl ctl;
	HttpResp resp;
	Doc *doc;
	int download;
	int files;          /* fetching the file-box manifest */
	int ok;
	int streamed;       /* body was written to dlpath */
	int qidx;           /* manifest entry index for file-box downloads, -1 otherwise */
	WCHAR dlpath[MAX_PATH];
} Job;
enum { NAV_NEW, NAV_HIST, NAV_RELOAD };
static Job *g_job;

/* image loader */
static NetCtl *g_imgctl;
static NetCtl *g_trctl;       /* running translation */
static int g_tr_skip_auto;    /* next page load: don't auto-translate ("원문 보기") */
static void translate_page(void);
static int doc_is_foreign(Doc *d);

/* view interaction */
static int g_down, g_dragging, g_down_x, g_down_y, g_last_y, g_press_link = -1, g_press_ctl = -1;
static DWORD g_last_t;
static int g_vel;      /* px per 25ms * 16 */
static int g_sbdrag, g_sboff;   /* dragging the scroll handle on the right */
static int g_nofire;            /* this touch stopped a fling: don't open anything */
#define SB_W  30                /* scroll handle touch width (like M480Tube) */
#define SB_TH 52                /* scroll handle height */
static int g_field_ctl = -1;
static int g_hold_link = -1;
static HBITMAP g_back_bm;
static int g_back_w, g_back_h;

/* ---------------- forward ---------------- */
static void navigate(const char *url, const char *post, int postlen, const char *ctype, int mode);
static void relayout(int keep_anchor);
static void set_scroll(int y);
static void field_commit(int destroy);
static void picker_close(void);
static void make_dirs_for(const WCHAR *file);

/* ---------------- settings / files ---------------- */
static void settings_load(void)
{
	WCHAR p[MAX_PATH];
	FILE *f;
	char line[600];
	app_path(p, L"settings.ini");
	f = _wfopen(p, L"rb");
	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		if (!strncmp(line, "font=", 5)) g_set_font = atoi(line + 5) & 3;
		else if (!strncmp(line, "images=", 7)) g_images_on = atoi(line + 7) ? 1 : 0;
		else if (!strncmp(line, "ua=", 3)) g_set_ua = atoi(line + 3) & 3;
		else if (!strncmp(line, "autotr=", 7)) g_autotr = atoi(line + 7) ? 1 : 0;
		else if (!strncmp(line, "dark=", 5)) g_dark = atoi(line + 5) ? 1 : 0;
		else if (!strncmp(line, "repo=", 5) || !strncmp(line, "token=", 6)) {
			char *v = strchr(line, '=') + 1;
			int n = (int)strlen(v);
			while (n > 0 && (v[n - 1] == '\n' || v[n - 1] == '\r' || v[n - 1] == ' ')) v[--n] = 0;
			if (line[0] == 'r') { if (*v) { strncpy(g_repo, v, sizeof g_repo - 1); g_repo[sizeof g_repo - 1] = 0; } }
			else { strncpy(g_token, v, sizeof g_token - 1); g_token[sizeof g_token - 1] = 0; }
		}
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
	fprintf(f, "font=%d\nimages=%d\nua=%d\nautotr=%d\ndark=%d\nrepo=%s\ntoken=%s\n", g_set_font, g_images_on, g_set_ua, g_autotr, g_dark, g_repo, g_token);
	fclose(f);
}

typedef struct { char *url; char *title; } Mark;
static Mark *g_bm; static int g_nbm;
static Mark *g_vis; static int g_nvis;
static Mark *g_sc; static int g_nsc;   /* home page shortcuts (editable) */

static const char *g_sc_default[][2] = {
	{ "https://namu.wiki/w/%EB%82%98%EB%AC%B4%EC%9C%84%ED%82%A4:%EB%8C%80%EB%AC%B8", "나무위키" },
	{ "https://ko.m.wikipedia.org/", "위키백과" },
	{ "https://m.naver.com/", "네이버" },
	{ "https://m.daum.net/", "다음" },
	{ "https://lite.duckduckgo.com/lite/", "DuckDuckGo Lite" },
	{ "http://68k.news/", "68k.news (가벼운 뉴스)" },
	{ "https://lite.cnn.com/", "CNN Lite" },
	{ "https://text.npr.org/", "NPR Text" },
	{ "https://news.ycombinator.com/", "Hacker News" },
	{ "http://frogfind.com/", "FrogFind (옛날 기기용 검색)" },
	{ NULL, NULL }
};

static void marks_save(const WCHAR *file, Mark *arr, int n);
static void marks_add(Mark **arr, int *n, const char *url, const char *title)
{
	char *t = xstrdup(title && *title ? title : url);
	char *c;
	for (c = t; *c; c++) if (*c == '\t' || *c == '\n' || *c == '\r') *c = ' ';
	*arr = (Mark *)realloc(*arr, (*n + 1) * sizeof(Mark));
	(*arr)[*n].url = xstrdup(url);
	(*arr)[*n].title = t;
	(*n)++;
}
static void marks_del(Mark *arr, int *n, int i)
{
	if (i < 0 || i >= *n) return;
	free(arr[i].url); free(arr[i].title);
	memmove(&arr[i], &arr[i + 1], (*n - i - 1) * sizeof(Mark));
	(*n)--;
}
static void sc_reset(void)
{
	int i;
	while (g_nsc) marks_del(g_sc, &g_nsc, g_nsc - 1);
	for (i = 0; g_sc_default[i][0]; i++) marks_add(&g_sc, &g_nsc, g_sc_default[i][0], g_sc_default[i][1]);
}

static void marks_load(const WCHAR *file, Mark **arr, int *n)
{
	WCHAR p[MAX_PATH];
	FILE *f;
	char line[2048];
	app_path(p, file);
	f = _wfopen(p, L"rb");
	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		char *tab = strchr(line, '\t');
		int l = (int)strlen(line);
		while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
		if (!tab || !line[0]) continue;
		*tab = 0;
		*arr = (Mark *)realloc(*arr, (*n + 1) * sizeof(Mark));
		(*arr)[*n].url = xstrdup(line);
		(*arr)[*n].title = xstrdup(tab + 1);
		(*n)++;
	}
	fclose(f);
}
static void marks_save(const WCHAR *file, Mark *arr, int n)
{
	WCHAR p[MAX_PATH];
	FILE *f;
	int i;
	app_path(p, file);
	f = _wfopen(p, L"wb");
	if (!f) return;
	for (i = 0; i < n; i++) fprintf(f, "%s\t%s\n", arr[i].url, arr[i].title);
	fclose(f);
}
static void visit_add(const char *url, const WCHAR *title)
{
	int i;
	char *t;
	if (!strncmp(url, "about:", 6)) return;
	t = title ? w_to_utf8(title, -1) : xstrdup(url);
	{ char *c; for (c = t; *c; c++) if (*c == '\t' || *c == '\n' || *c == '\r') *c = ' '; }
	for (i = 0; i < g_nvis; i++) if (!strcmp(g_vis[i].url, url)) {
		free(g_vis[i].url); free(g_vis[i].title);
		memmove(&g_vis[i], &g_vis[i + 1], (g_nvis - i - 1) * sizeof(Mark));
		g_nvis--;
		break;
	}
	if (g_nvis >= 100) { free(g_vis[0].url); free(g_vis[0].title); memmove(&g_vis[0], &g_vis[1], (g_nvis - 1) * sizeof(Mark)); g_nvis--; }
	g_vis = (Mark *)realloc(g_vis, (g_nvis + 1) * sizeof(Mark));
	g_vis[g_nvis].url = xstrdup(url);
	g_vis[g_nvis].title = t;
	g_nvis++;
}

/* ---------------- SIP (soft keyboard) ---------------- */
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
	RECT r;
#ifdef _WIN32_WCE
	static PSipGetInfo f;
	M_SIPINFO si;
	if (!f) { HMODULE h = LoadLibraryW(L"coredll.dll"); if (h) f = (PSipGetInfo)GetProcAddressW(h, L"SipGetInfo"); }
	memset(&si, 0, sizeof si);
	si.cbSize = sizeof si;
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0);
	if (f && f(&si) && (si.fdwFlags & 1 /* SIPF_ON */) && si.rcSipRect.top > r.top + 40 && si.rcSipRect.top < r.bottom)
		r.bottom = si.rcSipRect.top;
	MoveWindow(g_hwnd, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
#else
	(void)r;
#endif
}

/* ---------------- status ---------------- */
static void status_set(const char *s)
{
	RECT r;
	strncpy(g_status, s, sizeof g_status - 1);
	g_status[sizeof g_status - 1] = 0;
	GetClientRect(g_hwnd, &r);
	r.top = r.bottom - g_status_h;
	InvalidateRect(g_hwnd, &r, FALSE);
}
static void toolbar_invalidate(void)
{
	RECT r;
	GetClientRect(g_hwnd, &r);
	r.bottom = g_tb_h;
	InvalidateRect(g_hwnd, &r, FALSE);
}
static void status_title(void)
{
	if (g_doc && g_doc->title && g_doc->title[0]) {
		char *t = w_to_utf8(g_doc->title, -1);
		status_set(t);
		free(t);
	} else if (g_doc) status_set(g_doc->url);
	else status_set("");
}

static void addr_set(const char *url)
{
	WCHAR *w = utf8_to_w(url, -1, NULL);
	SetWindowTextW(g_addr, w);
	free(w);
}

/* ---------------- HTML generation helpers ---------------- */
static void h_esc(Buf *b, const char *s)
{
	for (; *s; s++) {
		if (*s == '<') buf_adds(b, "&lt;");
		else if (*s == '>') buf_adds(b, "&gt;");
		else if (*s == '&') buf_adds(b, "&amp;");
		else if (*s == '"') buf_adds(b, "&quot;");
		else buf_addc(b, *s);
	}
}

static char *query_param(const char *q, const char *name)
{
	int nl = (int)strlen(name);
	while (q && *q) {
		const char *e = strchr(q, '&');
		if (!e) e = q + strlen(q);
		if (!strncmp(q, name, nl) && q[nl] == '=') {
			Buf b;
			const char *p;
			buf_init(&b);
			for (p = q + nl + 1; p < e; p++) {
				if (*p == '+') buf_addc(&b, ' ');
				else if (*p == '%' && p + 2 < e) {
					char hx[3] = { p[1], p[2], 0 };
					buf_addc(&b, (char)strtol(hx, NULL, 16));
					p += 2;
				} else buf_addc(&b, *p);
			}
			return buf_detach(&b);
		}
		q = *e ? e + 1 : e;
	}
	return NULL;
}

static Doc *about_page(const char *url)
{
	Buf b;
	Doc *d;
	int i;
	buf_init(&b);
	if (!strncmp(url, "about:bookmarks", 15)) {
		buf_adds(&b, "<title>\xEC\xA6\x90\xEA\xB2\xA8\xEC\xB0\xBE\xEA\xB8\xB0</title><h2>\xEC\xA6\x90\xEA\xB2\xA8\xEC\xB0\xBE\xEA\xB8\xB0</h2>");
		if (!g_nbm) buf_adds(&b, "<p>\xEC\xA0\x80\xEC\x9E\xA5\xEB\x90\x9C \xEC\xA6\x90\xEA\xB2\xA8\xEC\xB0\xBE\xEA\xB8\xB0\xEA\xB0\x80 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4. \xEB\xA9\x94\xEB\x89\xB4(\xE2\x89\xA1)\xEC\x97\x90\xEC\x84\x9C \xEC\xB6\x94\xEA\xB0\x80\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94.</p>");
		buf_adds(&b, "<ul>");
		for (i = 0; i < g_nbm; i++) {
			buf_adds(&b, "<li><a href=\""); h_esc(&b, g_bm[i].url); buf_adds(&b, "\">"); h_esc(&b, g_bm[i].title);
			buf_addf(&b, "</a> &nbsp; <small><a href=\"about:delbm?%d\">[\xEC\x82\xAD\xEC\xA0\x9C]</a></small>", i);
		}
		buf_adds(&b, "</ul>");
	} else if (!strncmp(url, "about:shortcuts", 15)) {
		buf_adds(&b, "<title>바로가기 편집</title><h2>바로가기 편집</h2>"
			"<p><small>이름이나 주소를 고친 뒤 [저장]을 누르세요. 여기서 고친 목록이 홈 화면에 나옵니다.</small></p>");
		for (i = 0; i < g_nsc; i++) {
			buf_addf(&b, "<hr><form action=\"about:editsc\"><input type=hidden name=i value=%d>", i);
			buf_adds(&b, "이름 <input type=text name=t size=20 value=\""); h_esc(&b, g_sc[i].title);
			buf_adds(&b, "\"><br>주소 <input type=text name=u size=20 value=\""); h_esc(&b, g_sc[i].url);
			buf_adds(&b, "\"><br><input type=submit value=\"저장\"> ");
			if (i > 0) buf_addf(&b, "<a href=\"about:upsc?%d\">[위로]</a> ", i);
			if (i < g_nsc - 1) buf_addf(&b, "<a href=\"about:downsc?%d\">[아래로]</a> ", i);
			buf_addf(&b, "<a href=\"about:delsc?%d\">[삭제]</a></form>", i);
		}
		buf_adds(&b, "<hr><h3>새 바로가기 추가</h3><form action=\"about:addsc\">"
			"이름 <input type=text name=t size=20 placeholder=\"예: 날씨\"><br>"
			"주소 <input type=text name=u size=20 placeholder=\"예: m.weather.com\"><br>"
			"<input type=submit value=\"추가\"></form>"
			"<p><small>보고 있는 페이지는 메뉴(≡) &gt; '바로가기에 추가'로도 넣을 수 있습니다.</small></p>"
			"<hr><p><a href=\"about:resetsc\">기본 목록으로 되돌리기</a> | <a href=\"about:home\">홈으로</a></p>");
	} else if (!strncmp(url, "about:history", 13)) {
		buf_adds(&b, "<title>\xEB\xB0\xA9\xEB\xAC\xB8 \xEA\xB8\xB0\xEB\xA1\x9D</title><h2>\xEB\xB0\xA9\xEB\xAC\xB8 \xEA\xB8\xB0\xEB\xA1\x9D</h2><ul>");
		for (i = g_nvis - 1; i >= 0; i--) {
			buf_adds(&b, "<li><a href=\""); h_esc(&b, g_vis[i].url); buf_adds(&b, "\">"); h_esc(&b, g_vis[i].title[0] ? g_vis[i].title : g_vis[i].url); buf_adds(&b, "</a>");
		}
		buf_adds(&b, "</ul><p><a href=\"about:clearhistory\">\xEA\xB8\xB0\xEB\xA1\x9D \xEC\xA7\x80\xEC\x9A\xB0\xEA\xB8\xB0</a></p>");
	} else {
		/* home */
		buf_adds(&b,
			"<title>M480Web</title>"
			"<h2>위키 검색</h2>"
			"<form action=\"about:search\">"
			"<input type=text name=q size=14 placeholder=\"문서 이름 입력\"><br>"
			"<input type=submit name=s value=\"나무위키\"> <input type=submit name=s value=\"위키백과\"></form>"
			"<p><a href=\"https://namu.wiki/w/%EB%82%98%EB%AC%B4%EC%9C%84%ED%82%A4:%EB%8C%80%EB%AC%B8\"><b>나무위키 대문</b></a> · "
			"<a href=\"https://namu.wiki/random\">아무 문서</a> · "
			"<a href=\"https://ko.m.wikipedia.org/\">위키백과 대문</a></p>"
			"<p><small>입력 후 Enter를 누르면 나무위키에서 찾습니다. 주소창에 단어만 쳐도 됩니다.</small></p>");
		if (g_nbm) {
			buf_adds(&b, "<h3>\xEC\xA6\x90\xEA\xB2\xA8\xEC\xB0\xBE\xEA\xB8\xB0</h3><ul>");
			for (i = 0; i < g_nbm && i < 12; i++) { buf_adds(&b, "<li><a href=\""); h_esc(&b, g_bm[i].url); buf_adds(&b, "\">"); h_esc(&b, g_bm[i].title); buf_adds(&b, "</a>"); }
			buf_adds(&b, "</ul>");
		}
		buf_adds(&b,
			"<p><a href=\"about:files\"><b>[내 파일함]</b></a> <small>Claude가 만든 파일 받기</small></p>"
			"<h3>바로가기 <small><a href=\"about:shortcuts\">[편집]</a></small></h3><ul>");
		for (i = 0; i < g_nsc; i++) { buf_adds(&b, "<li><a href=\""); h_esc(&b, g_sc[i].url); buf_adds(&b, "\">"); h_esc(&b, g_sc[i].title); buf_adds(&b, "</a>"); }
		if (!g_nsc) buf_adds(&b, "<li><a href=\"about:shortcuts\">바로가기 추가하기</a>");
		buf_adds(&b, "</ul>"
			"<p><small>\xED\x8E\x98\xEC\x9D\xB4\xEC\xA7\x80\xEA\xB0\x80 \xEA\xB9\xA8\xEC\xA7\x80\xEB\xA9\xB4 \xEB\xA9\x94\xEB\x89\xB4\xEC\x9D\x98 '\xEB\x8B\xA8\xEC\x88\x9C\xED\x99\x94 \xEB\xB3\xB4\xEA\xB8\xB0'\xEB\xA5\xBC \xEC\x8D\xA8\xEB\xB3\xB4\xEC\x84\xB8\xEC\x9A\x94. "
			"\xEC\x9E\x90\xEB\xB0\x94\xEC\x8A\xA4\xED\x81\xAC\xEB\xA6\xBD\xED\x8A\xB8\xEB\x8A\x94 \xEC\xA7\x80\xEC\x9B\x90\xED\x95\x98\xEC\xA7\x80 \xEC\x95\x8A\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4.</small></p>");
	}
	d = html_parse(b.p, b.len, url, "utf-8", 0);
	buf_free(&b);
	return d;
}

static Doc *error_page(const char *url, const char *msg)
{
	Buf b;
	Doc *d;
	char *ue = url_encode(url);
	buf_init(&b);
	buf_adds(&b, "<title>\xEC\x98\xA4\xEB\xA5\x98</title><h3>\xED\x8E\x98\xEC\x9D\xB4\xEC\xA7\x80\xEB\xA5\xBC \xEC\x97\xB4 \xEC\x88\x98 \xEC\x97\x86\xEC\x9D\x8C</h3><p>");
	h_esc(&b, msg);
	buf_adds(&b, "</p><p><small>");
	h_esc(&b, url);
	buf_adds(&b, "</small></p><ul><li><a href=\"");
	h_esc(&b, url);
	buf_adds(&b, "\">\xEB\x8B\xA4\xEC\x8B\x9C \xEC\x8B\x9C\xEB\x8F\x84</a>");
	if (strncmp(url, "about:", 6)) {
		buf_adds(&b, "<li><a href=\"http://frogfind.com/read.php?a=");
		buf_adds(&b, ue);
		buf_adds(&b, "\">\xEB\x8B\xA8\xEC\x88\x9C\xED\x99\x94 \xEB\xB3\xB4\xEA\xB8\xB0(FrogFind)\xEB\xA1\x9C \xEC\x97\xB4\xEA\xB8\xB0</a>");
	}
	buf_adds(&b, "<li><a href=\"about:home\">\xED\x99\x88</a></ul>");
	free(ue);
	d = html_parse(b.p, b.len, url, "utf-8", 0);
	buf_free(&b);
	return d;
}

/* ---------------- loading threads ---------------- */
static void job_free(Job *j)
{
	if (!j) return;
	free(j->url); free(j->post); free(j->ctype); free(j->referer);
	http_resp_free(&j->resp);
	if (j->doc) doc_release(j->doc);
	free(j);
}

static void progress_cb(void *ctx, int got, int total)
{
	static DWORD last;
	Job *j = (Job *)ctx;
	if (GetTickCount() - last < 250) return;
	last = GetTickCount();
	if (j->gen == g_gen) PostMessageW(g_hwnd, WM_APP_PROGRESS, (WPARAM)got, (LPARAM)total);
}

static int is_html_type(const char *ct)
{
	return !ct || !strcmp(ct, "text/html") || !strcmp(ct, "application/xhtml+xml") || !strcmp(ct, "application/xml") || !strcmp(ct, "text/xml");
}

static int view_width(void)
{
	RECT r;
	GetClientRect(g_view, &r);
	return r.right - r.left;
}

static int is_namu(const char *url)
{
	Url u;
	int r = 0;
	if (url && url_parse(url, &u)) {
		int n = (int)strlen(u.host);
		r = !strcmp(u.host, "namu.wiki") || (n > 10 && !strcmp(u.host + n - 10, ".namu.wiki"));
#ifdef M480_DEBUG
		if (getenv("M480_NAMU_TEST") && strstr(u.path, "/namu/")) r = 1;
#endif
		url_free(&u);
	}
	return r;
}

/* search bar shown on top of every namu.wiki page (the site's own one needs JavaScript) */
static void namu_toolbar(Buf *b, const char *url)
{
	const char *qs = strchr(url, '?');
	buf_adds(b, "<form action=\"https://namu.wiki/Go\" data-m480=\"1\"><input type=text name=q size=13 placeholder=\"나무위키 검색\"> <input type=submit value=\"검색\"></form>");
	if (strstr(url, "/Search?") && qs) {
		char *q = query_param(qs + 1, "q");
		if (q && *q) {
			char *e = url_encode(q);
			buf_adds(b, "<p><b>");
			h_esc(b, q);
			buf_adds(b, "</b> 문서를 바로 찾지 못했습니다.<br><a href=\"https://lite.duckduckgo.com/lite/?kl=kr-kr&q=site%3Anamu.wiki+");
			buf_adds(b, e);
			buf_adds(b, "\">비슷한 나무위키 문서 찾기</a></p>");
			free(e);
		}
		free(q);
	}
	buf_adds(b, "<hr>");
}


char *namumark_to_html(const char *src, int len, const char *title);

/* namu.wiki sometimes answers with its JavaScript shell only ("Loading...") */
static int namu_is_shell(Doc *d)
{
	int i, n = 0;
	for (i = 0; i < d->nitems; i++) {
		Item *it = &d->items[i];
		int k;
		if (it->type != IT_TEXT) continue;
		for (k = 0; k < it->len; k++) if (d->text.p[it->text + k] != ' ') n++;
		if (n > 60) return 0;
	}
	return 1;
}

static char *pct_decode(const char *s, int n)
{
	Buf b;
	int i;
	buf_init(&b);
	for (i = 0; i < n; i++) {
		if (s[i] == '%' && i + 2 < n) { char hx[3] = { s[i + 1], s[i + 2], 0 }; buf_addc(&b, (char)strtol(hx, NULL, 16)); i += 2; }
		else buf_addc(&b, s[i]);
	}
	return buf_detach(&b);
}

/* document part of a namu.wiki /w/ URL (still %-encoded); NULL if not a document */
static char *namu_doc_part(const char *url)
{
	const char *w = strstr(url, "namu.wiki/w/");
	const char *e;
	if (!w) return NULL;
	w += 12;
	e = w;
	while (*e && *e != '?' && *e != '#') e++;
	return e > w ? xstrndup(w, (int)(e - w)) : NULL;
}

static void html_unescape(Buf *o, const char *s, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		if (s[i] == '&') {
			if (!strncmp(s + i, "&lt;", 4)) { buf_addc(o, '<'); i += 3; continue; }
			if (!strncmp(s + i, "&gt;", 4)) { buf_addc(o, '>'); i += 3; continue; }
			if (!strncmp(s + i, "&amp;", 5)) { buf_addc(o, '&'); i += 4; continue; }
			if (!strncmp(s + i, "&quot;", 6)) { buf_addc(o, '"'); i += 5; continue; }
			if (!strncmp(s + i, "&#39;", 5)) { buf_addc(o, '\''); i += 4; continue; }
			if (!strncmp(s + i, "&#x27;", 6)) { buf_addc(o, '\''); i += 5; continue; }
		}
		buf_addc(o, s[i]);
	}
}

/* Try harder for a namu.wiki document that came back empty:
   1) fetch it again (the shell is usually a temporary server-side fallback),
   2) fetch the raw markup (/raw/) and render it ourselves.
   Returns a new Doc or NULL. Runs in the loader thread. */
static Doc *namu_rescue(Job *j)
{
	char *part = namu_doc_part(j->resp.url);
	int tries;
	Doc *d = NULL;
	if (!part) return NULL;
	for (tries = 1; tries <= 2 && !j->ctl.abort; tries++) {
		HttpResp r;
		Sleep(800 * tries);
		if (j->ctl.abort) break;
		PostMessageW(g_hwnd, WM_APP_PROGRESS, 0, -2 - tries);
		if (http_request(j->resp.url, "GET", NULL, 0, NULL, NULL, &r, &j->ctl, 8 * 1024 * 1024) && r.status == 200 && r.body.p) {
			Buf nb;
			buf_init(&nb);
			namu_toolbar(&nb, r.url);
			buf_add(&nb, r.body.p, r.body.len);
			d = html_parse_ex(nb.p, nb.len, r.url, "utf-8", 0, HP_NAMU | HP_NOIMG);
			buf_free(&nb);
			if (!namu_is_shell(d)) { http_resp_free(&r); free(part); return d; }
			doc_release(d);
			d = NULL;
		}
		http_resp_free(&r);
	}
	if (!j->ctl.abort) {
		HttpResp r;
		Buf u;
		buf_init(&u);
		buf_adds(&u, "https://namu.wiki/raw/");
		buf_adds(&u, part);
		PostMessageW(g_hwnd, WM_APP_PROGRESS, 0, -9);
		if (http_request(u.p, "GET", NULL, 0, NULL, NULL, &r, &j->ctl, 8 * 1024 * 1024) && r.status == 200 && r.body.p) {
			Buf mk;
			const char *b = r.body.p;
			int n = r.body.len;
			buf_init(&mk);
			if (r.ctype && !strcmp(r.ctype, "text/plain")) buf_add(&mk, b, n);
			else {
				const char *ta = stristr(b, "<textarea");
				if (ta) {
					const char *st = strchr(ta, '>');
					const char *en = st ? stristr(st, "</textarea>") : NULL;
					if (st && en) html_unescape(&mk, st + 1, (int)(en - st - 1));
				}
			}
			if (mk.len > 0) {
				char *title = pct_decode(part, (int)strlen(part));
				char *html = namumark_to_html(mk.p, mk.len, title);
				Buf nb;
				buf_init(&nb);
				namu_toolbar(&nb, j->resp.url);
				buf_adds(&nb, "<p><small>(나무위키가 본문을 보내지 않아 원본 글로 표시합니다)</small></p>");
				buf_adds(&nb, html);
				d = html_parse_ex(nb.p, nb.len, j->resp.url, "utf-8", 0, HP_NAMU | HP_NOIMG);
				buf_free(&nb);
				free(html);
				free(title);
			}
			buf_free(&mk);
		}
		http_resp_free(&r);
		buf_free(&u);
	}
	if (!d && !j->ctl.abort) {
		/* explain instead of an empty page */
		char *title = pct_decode(part, (int)strlen(part));
		char *te = url_encode(title);
		Buf nb;
		buf_init(&nb);
		namu_toolbar(&nb, j->resp.url);
		buf_adds(&nb, "<h3>문서를 불러오지 못했습니다</h3><p>나무위키가 지금 이 문서의 본문을 보내 주지 않습니다 "
			"(자바스크립트로만 보여 주는 상태). 잠시 뒤 다시 시도하면 열리는 경우가 많습니다.</p><ul><li><a href=\"");
		h_esc(&nb, j->resp.url);
		buf_adds(&nb, "\">다시 시도</a><li><a href=\"https://ko.m.wikipedia.org/w/index.php?title=%ED%8A%B9%EC%88%98:%EA%B2%80%EC%83%89&go=1&search=");
		buf_adds(&nb, te);
		buf_adds(&nb, "\">위키백과에서 '");
		h_esc(&nb, title);
		buf_adds(&nb, "' 찾기</a></ul>");
		d = html_parse_ex(nb.p, nb.len, j->resp.url, "utf-8", 0, HP_NAMU | HP_NOIMG);
		buf_free(&nb);
		free(te);
		free(title);
	}
	free(part);
	return d;
}

static DWORD WINAPI load_thread(LPVOID p)
{
	Job *j = (Job *)p;
	int ok = http_request(j->url, j->post ? "POST" : "GET", j->post, j->postlen, j->ctype, j->referer, &j->resp, &j->ctl, 8 * 1024 * 1024);
	j->ok = ok;
	if (j->resp.sink) {
		fclose((FILE *)j->resp.sink);
		j->resp.sink = NULL;
		if (!ok || j->ctl.abort || j->resp.status < 200 || j->resp.status >= 300) DeleteFileW(j->dlpath);
	}
	if (ok && !j->ctl.abort && j->qidx >= 0 && !j->streamed && j->resp.status >= 200 && j->resp.status < 300) {
		/* server compressed it, so it came into memory: write it out now */
		FILE *f;
		make_dirs_for(j->dlpath);
		f = _wfopen(j->dlpath, L"wb");
		if (f) {
			if (fwrite(j->resp.body.p, 1, j->resp.body.len, f) == (size_t)j->resp.body.len) j->streamed = 1;
			fclose(f);
		}
		if (!j->streamed) { DeleteFileW(j->dlpath); strcpy(j->resp.err, "파일을 저장할 수 없음"); }
	}
	if (ok && !j->ctl.abort && !j->files && !j->streamed && j->qidx < 0) {
		const char *ct = j->resp.ctype;
		if (is_html_type(ct) || (ct && !strncmp(ct, "text/", 5))) {
			int is_text = ct && !is_html_type(ct);
			if (!is_text && is_namu(j->resp.url)) {
				Buf nb;
				buf_init(&nb);
				namu_toolbar(&nb, j->resp.url);
				if (j->resp.body.p) buf_add(&nb, j->resp.body.p, j->resp.body.len);
				j->doc = html_parse_ex(nb.p, nb.len, j->resp.url, "utf-8", 0, HP_NAMU | HP_NOIMG);
				buf_free(&nb);
				if (namu_is_shell(j->doc)) {
					char *pp = namu_doc_part(j->resp.url);
					if (pp) {
						Doc *nd;
						free(pp);
						nd = namu_rescue(j);
						if (nd) { doc_release(j->doc); j->doc = nd; }
					}
				}
			} else
			j->doc = html_parse(j->resp.body.p ? j->resp.body.p : "", j->resp.body.len, j->resp.url, j->resp.charset, is_text);
		} else if (ct && !strncmp(ct, "image/", 6)) {
			Buf b;
			buf_init(&b);
			buf_adds(&b, "<center><img src=\"\"></center>");
			j->doc = html_parse(b.p, b.len, j->resp.url, "utf-8", 0);
			buf_free(&b);
			if (j->doc->nimgs) {
				Img *im = &j->doc->imgs[0];
				free(im->url);
				im->url = xstrdup(j->resp.url);
				if (img_decode((unsigned char *)j->resp.body.p, j->resp.body.len, view_width() - SB_W - SC(4), im)) im->state = 2;
				else im->state = 3;
			}
		} else {
			j->download = 1;
		}
	}
	PostMessageW(g_hwnd, WM_APP_LOADED, 0, (LPARAM)j);
	return 0;
}

static unsigned char *b64_decode(const char *s, int *outlen)
{
	int n = (int)strlen(s), i, k = 0;
	unsigned long acc = 0;
	int bits = 0;
	unsigned char *o = (unsigned char *)malloc(n * 3 / 4 + 4);
	for (i = 0; i < n; i++) {
		int c = s[i], v;
		if (c >= 'A' && c <= 'Z') v = c - 'A';
		else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
		else if (c >= '0' && c <= '9') v = c - '0' + 52;
		else if (c == '+' || c == '-') v = 62;
		else if (c == '/' || c == '_') v = 63;
		else if (c == '%' && i + 2 < n && s[i + 1] == '3' && (s[i + 2] == 'D' || s[i + 2] == 'd')) { i += 2; continue; }
		else continue;
		acc = (acc << 6) | v;
		bits += 6;
		if (bits >= 8) { bits -= 8; o[k++] = (unsigned char)(acc >> bits); }
	}
	*outlen = k;
	return o;
}

typedef struct { Doc *doc; LONG gen; NetCtl ctl; int maxw; } ImgJob;

static DWORD WINAPI img_thread(LPVOID p)
{
	ImgJob *ij = (ImgJob *)p;
	Doc *d = ij->doc;
	int i, done = 0;
	for (i = 0; i < d->nimgs && done < 120; i++) {
		Img *im = &d->imgs[i];
		int ok = 0;
		if (ij->gen != g_gen || ij->ctl.abort) break;
		if (im->state != 0) continue;
		im->state = 1;
		/* identical URL already decoded? reuse dimensions by re-decoding is wasteful; just fetch */
		if (!strncmp(im->url, "data:", 5)) {
			const char *c = strstr(im->url, ";base64,");
			if (c) {
				int n;
				unsigned char *bin = b64_decode(c + 8, &n);
				ok = img_decode(bin, n, ij->maxw, im);
				free(bin);
			}
		} else {
			HttpResp r;
			if (http_request(im->url, "GET", NULL, 0, NULL, d->url, &r, &ij->ctl, 2 * 1024 * 1024) && r.status == 200 && r.body.len > 0)
				ok = img_decode((unsigned char *)r.body.p, r.body.len, ij->maxw, im);
			http_resp_free(&r);
		}
		im->state = ok ? 2 : 3;
		done++;
		if (ij->gen == g_gen) PostMessageW(g_hwnd, WM_APP_IMGDONE, (WPARAM)ij->gen, 0);
	}
	doc_release(d);
	free(ij);
	return 0;
}

static void start_images(void)
{
	ImgJob *ij;
	HANDLE th;
	if (!g_doc || !g_images_on || !g_doc->nimgs) return;
	ij = (ImgJob *)calloc(1, sizeof(ImgJob));
	ij->doc = g_doc;
	doc_addref(g_doc);
	ij->gen = g_gen;
	ij->maxw = view_width() - SB_W - SC(4);
	g_imgctl = &ij->ctl;
	th = CreateThread(NULL, THREAD_STACK, img_thread, ij, THREAD_FLAGS, NULL);
	if (th) CloseHandle(th);
	else { doc_release(g_doc); free(ij); g_imgctl = NULL; }
}

static void stop_all(void)
{
	InterlockedIncrement((LONG *)&g_gen);
	if (g_job) { g_job->ctl.abort = 1; g_job = NULL; }
	if (g_imgctl) { g_imgctl->abort = 1; g_imgctl = NULL; }
	if (g_trctl) { g_trctl->abort = 1; g_trctl = NULL; }
	g_loading = 0;
	KillTimer(g_hwnd, TM_REFRESH);
	toolbar_invalidate();
}

/* ---------------- document install ---------------- */
static int find_anchor_y(const char *url)
{
	const char *h = strchr(url, '#');
	int i;
	if (!h || !h[1] || !g_doc) return -1;
	{
		WCHAR *name = utf8_to_w(h + 1, -1, NULL);
		for (i = 0; i < g_doc->nanchors; i++) {
			if (!wcscmp(g_doc->anchors[i].name, name)) { free(name); return layout_item_y(&g_lay, g_doc->anchors[i].item) - SC(4); }
		}
		free(name);
	}
	return -1;
}

static void hist_push(const char *url)
{
	int i;
	if (g_hpos >= 0 && g_hpos < g_nhist && !strcmp(g_hist[g_hpos].url, url)) return;
	for (i = g_hpos + 1; i < g_nhist; i++) free(g_hist[i].url);
	g_nhist = g_hpos + 1;
	if (g_nhist >= MAXHIST) { free(g_hist[0].url); memmove(g_hist, g_hist + 1, (MAXHIST - 1) * sizeof(HistEnt)); g_nhist--; }
	g_hist[g_nhist].url = xstrdup(url);
	g_hist[g_nhist].scroll = 0;
	g_hpos = g_nhist++;
}

static void install_doc(Doc *d, int mode, const char *tls)
{
	int restore = 0;
	field_commit(1);
	picker_close();
	if (g_hpos >= 0 && g_hpos < g_nhist) g_hist[g_hpos].scroll = g_scroll;
	if (g_doc) doc_release(g_doc);
	g_doc = d;
	strncpy(g_tlsinfo, tls ? tls : "", sizeof g_tlsinfo - 1);
	if (mode == NAV_NEW) hist_push(d->url);
	else if (mode == NAV_HIST && g_hpos >= 0) restore = g_hist[g_hpos].scroll;
	else if (mode == NAV_RELOAD) restore = g_scroll;
	if (g_hpos >= 0 && strcmp(g_hist[g_hpos].url, d->url)) { free(g_hist[g_hpos].url); g_hist[g_hpos].url = xstrdup(d->url); }
	g_scroll = 0;
	relayout(0);
	{
		int ay = find_anchor_y(d->url);
		set_scroll(ay >= 0 ? ay : restore);
	}
	addr_set(d->url);
	status_title();
	visit_add(d->url, d->title);
	if (d->refresh_url && d->refresh_delay <= 5) SetTimer(g_hwnd, TM_REFRESH, d->refresh_delay * 1000 + 100, NULL);
	start_images();
	toolbar_invalidate();
	if (GetFocus() != g_addr && !g_field) SetFocus(g_view);
	if (g_tr_skip_auto) g_tr_skip_auto = 0;
	else if (g_autotr && strncmp(d->url, "about:", 6) && doc_is_foreign(d)) translate_page();
}

static void save_download(Job *j)
{
	WCHAR path[MAX_PATH], msg[400];
	char name[128];
	const char *p = j->resp.url, *q;
	FILE *f;
	int i;
	/* filename from URL */
	q = strchr(p, '?');
	if (!q) q = p + strlen(p);
	for (i = (int)(q - p) - 1; i >= 0 && p[i] != '/'; i--);
	_snprintf(name, sizeof name - 1, "%.*s", (int)(q - p - i - 1) > 100 ? 100 : (int)(q - p - i - 1), p + i + 1);
	name[sizeof name - 1] = 0;
	if (!name[0]) strcpy(name, "download.bin");
	for (i = 0; name[i]; i++) if (strchr("\\/:*?\"<>|", name[i])) name[i] = '_';
	{
		WCHAR *wn = utf8_to_w(name, -1, NULL);
#ifdef _WIN32_WCE
		wcscpy(path, L"\\My Documents\\");
		wcscat(path, wn);
#else
		app_path(path, wn);
#endif
		free(wn);
	}
	wsprintfW(msg, L"\xC774 \xD30C\xC77C\xC744 \xC800\xC7A5\xD560\xAE4C\xC694?\n%s\n(%d KB)", path, j->resp.body.len / 1024);
	if (MessageBoxW(g_hwnd, msg, APPNAME, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
	f = _wfopen(path, L"wb");
	if (!f) { MessageBoxW(g_hwnd, L"\xC800\xC7A5 \xC2E4\xD328", APPNAME, MB_OK); return; }
	fwrite(j->resp.body.p, 1, j->resp.body.len, f);
	fclose(f);
	wsprintfW(msg, L"\xC800\xC7A5\xB428: %s", path);
	MessageBoxW(g_hwnd, msg, APPNAME, MB_OK);
}


/* ---------------- file box (GitHub repo downloads) ---------------- */
typedef struct { char *path; long size; char *desc; char *local; int group; } FEnt;
typedef struct { char *id, *title, *date; } FGrp;
static FEnt *g_fe; static int g_nfe;
static FGrp *g_fg; static int g_nfg;
static int g_q[64], g_qn, g_qpos;     /* download queue (manifest indices) */
static int g_qfail;

static void fb_clear(void)
{
	int i;
	for (i = 0; i < g_nfe; i++) { free(g_fe[i].path); free(g_fe[i].desc); free(g_fe[i].local); }
	for (i = 0; i < g_nfg; i++) { free(g_fg[i].id); free(g_fg[i].title); free(g_fg[i].date); }
	free(g_fe); free(g_fg);
	g_fe = NULL; g_fg = NULL; g_nfe = g_nfg = 0;
}

static void fb_parse(const char *t, int len)
{
	char *txt = xstrndup(t, len), *line = txt;
	fb_clear();
	if ((unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB) line += 3;
	while (line && *line) {
		char *nl = strchr(line, '\n'), *f[4];
		int n = 0, k;
		if (nl) *nl = 0;
		k = (int)strlen(line);
		while (k > 0 && (line[k - 1] == '\r' || line[k - 1] == ' ')) line[--k] = 0;
		if (line[0] && line[0] != '#') {
			char *c = line;
			f[n++] = c;
			for (; *c && n < 4; c++) if (*c == '\t') { *c = 0; f[n++] = c + 1; }
			if (line[0] == '@') {
				g_fg = (FGrp *)realloc(g_fg, (g_nfg + 1) * sizeof(FGrp));
				g_fg[g_nfg].id = xstrdup(f[0] + 1);
				g_fg[g_nfg].title = xstrdup(n > 1 ? f[1] : f[0] + 1);
				g_fg[g_nfg].date = xstrdup(n > 2 ? f[2] : "");
				g_nfg++;
			} else {
				g_fe = (FEnt *)realloc(g_fe, (g_nfe + 1) * sizeof(FEnt));
				g_fe[g_nfe].path = xstrdup(f[0]);
				g_fe[g_nfe].size = n > 1 ? atol(f[1]) : -1;
				g_fe[g_nfe].desc = xstrdup(n > 2 ? f[2] : "");
				g_fe[g_nfe].local = xstrdup(n > 3 && f[3][0] ? f[3] : f[0]);
				g_fe[g_nfe].group = g_nfg - 1;
				g_nfe++;
			}
		}
		line = nl ? nl + 1 : NULL;
	}
	free(txt);
}

/* folder where file-box downloads go */
static void fb_root(WCHAR *out)
{
#ifdef _WIN32_WCE
	static const WCHAR *cards[] = { L"\\Storage Card", L"\\SD Card", L"\\Storage Card2", L"\\SDMMC", NULL };
	int i;
	for (i = 0; cards[i]; i++) {
		DWORD a = GetFileAttributesW(cards[i]);
		if (a != 0xFFFFFFFF && (a & FILE_ATTRIBUTE_DIRECTORY)) { wcscpy(out, cards[i]); wcscat(out, L"\\M480-APP"); return; }
	}
	wcscpy(out, L"\\My Documents\\M480-APP");
#else
	app_path(out, L"M480-APP");
#endif
}

static void fb_local_path(int i, WCHAR *out)
{
	WCHAR *w = utf8_to_w(g_fe[i].local, -1, NULL);
	WCHAR *c;
	fb_root(out);
	wcscat(out, L"\\");
	for (c = w; *c; c++) {
		if (*c == '/') *c = '\\';
		else if (*c == ':' || (*c == '.' && c[1] == '.')) *c = '_';
	}
	if (w[0] == '\\') w[0] = '_';
	wcsncat(out, w, MAX_PATH - wcslen(out) - 1);
	free(w);
}

static void make_dirs_for(const WCHAR *file)
{
	WCHAR tmp[MAX_PATH];
	int i;
	wcsncpy(tmp, file, MAX_PATH - 1);
	tmp[MAX_PATH - 1] = 0;
	for (i = 1; tmp[i]; i++) {
		if (tmp[i] == '\\') {
			tmp[i] = 0;
			CreateDirectoryW(tmp, NULL);
			tmp[i] = '\\';
		}
	}
}

static long file_size_w(const WCHAR *path)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(path, &fd);
	if (h == INVALID_HANDLE_VALUE) return -1;
	FindClose(h);
	return (long)fd.nFileSizeLow;
}

static char *fb_url(int i)
{
	return url_resolve(g_repo, g_fe[i].path);
}

static void fmt_size(char *out, long sz)
{
	if (sz < 0) strcpy(out, "?");
	else if (sz >= 1024 * 1024) sprintf(out, "%ld.%ld MB", sz / (1024 * 1024), (sz % (1024 * 1024)) * 10 / (1024 * 1024));
	else sprintf(out, "%ld KB", (sz + 1023) / 1024);
}

/* state of one app (group): 0 not downloaded, 1 partly, 2 up to date, 3 update available */
static int fb_group_state(int g)
{
	int i, n = 0, have = 0, stale = 0;
	for (i = 0; i < g_nfe; i++) {
		WCHAR lp[MAX_PATH];
		long sz;
		if (g_fe[i].group != g) continue;
		n++;
		fb_local_path(i, lp);
		sz = file_size_w(lp);
		if (sz >= 0) { have++; if (g_fe[i].size >= 0 && sz != g_fe[i].size) stale++; }
	}
	if (!have) return 0;
	if (stale) return 3;
	return have == n ? 2 : 1;
}

static void fb_title(Buf *b, const char *t, int big)
{
	/* "M480 뮤직 2.4 (블루투스 잡음 수정 2)" -> bold name + small note */
	const char *p = strstr(t, " (");
	if (p && big) {
		buf_adds(b, "<b>"); { char *x = xstrndup(t, (int)(p - t)); h_esc(b, x); free(x); } buf_adds(b, "</b>");
		buf_adds(b, "<br><small>"); h_esc(b, p + 1); buf_adds(b, "</small>");
	} else { buf_adds(b, big ? "<b>" : ""); h_esc(b, t); buf_adds(b, big ? "</b>" : ""); }
}

/* level 1: list of apps, grouped by state */
static Doc *files_page(void)
{
	Buf b;
	Doc *d;
	int g, pass, i;
	static const char *heads[3] = { "업데이트 있음", "받은 앱", "받지 않은 앱" };
	int *st;
	buf_init(&b);
	buf_adds(&b, "<title>내 파일함</title><h2>내 파일함</h2>"
		"<p><small>앱을 누르면 파일 목록이 나옵니다.</small></p>");
	if (!g_nfe) buf_adds(&b, "<p>올라온 파일이 없습니다.</p>");
	st = (int *)malloc((g_nfg + 2) * sizeof(int));
	for (g = -1; g < g_nfg; g++) st[g + 1] = fb_group_state(g);
	for (pass = 0; pass < 3; pass++) {
		int any = 0;
		for (g = -1; g < g_nfg; g++) {
			int s1 = st[g + 1], k = pass == 0 ? (s1 == 3) : pass == 1 ? (s1 == 1 || s1 == 2) : (s1 == 0);
			int nf = 0;
			for (i = 0; i < g_nfe; i++) if (g_fe[i].group == g) nf++;
			if (!k || !nf) continue;
			if (!any) { buf_addf(&b, "<h3>%s</h3><ul>", heads[pass]); any = 1; }
			buf_addf(&b, "<li><a href=\"about:app?%d\">", g);
			fb_title(&b, g >= 0 ? g_fg[g].title : "기타", 1);
			buf_adds(&b, "</a><br><small>");
			if (s1 == 3) buf_adds(&b, "<b>새 버전 있음</b> · ");
			else if (s1 == 2) buf_adds(&b, "최신 · ");
			else if (s1 == 1) buf_adds(&b, "일부 받음 · ");
			buf_addf(&b, "파일 %d개", nf);
			if (g >= 0 && g_fg[g].date[0]) { buf_adds(&b, " · "); h_esc(&b, g_fg[g].date); }
			buf_adds(&b, "</small>");
		}
		if (any) buf_adds(&b, "</ul>");
	}
	free(st);
	buf_adds(&b, "<hr><p><a href=\"about:files\">새로고침</a> | <a href=\"about:settings\">파일함 설정</a> | <a href=\"about:home\">홈</a></p>");
	d = html_parse(b.p, b.len, "about:files", "utf-8", 0);
	buf_free(&b);
	return d;
}

/* level 2: files of one app */
static Doc *app_page(int g)
{
	Buf b;
	Doc *d;
	int i, s1 = fb_group_state(g);
	char url[40];
	WCHAR root[MAX_PATH];
	char *rootu;
	if (g < -1 || g >= g_nfg) return files_page();
	fb_root(root);
	rootu = w_to_utf8(root, -1);
	buf_init(&b);
	buf_adds(&b, "<title>");
	h_esc(&b, g >= 0 ? g_fg[g].title : "기타");
	buf_adds(&b, "</title><p><a href=\"about:files\">◀ 앱 목록</a></p><h3>");
	fb_title(&b, g >= 0 ? g_fg[g].title : "기타", 1);
	buf_adds(&b, "</h3><small>");
	if (g >= 0 && g_fg[g].date[0]) { buf_adds(&b, "업데이트: "); h_esc(&b, g_fg[g].date); buf_adds(&b, "<br>"); }
	buf_adds(&b, s1 == 3 ? "<b>새 버전이 있습니다</b>" : s1 == 2 ? "최신 버전을 받아 두었습니다" : s1 == 1 ? "일부 파일만 받았습니다" : "아직 받지 않았습니다");
	buf_adds(&b, "</small><p>");
	buf_addf(&b, "<a href=\"about:getall?%d\"><b>[%s]</b></a></p><ul>", g, s1 == 3 ? "전체 업데이트 받기" : "전체 받기");
	for (i = 0; i < g_nfe; i++) {
		WCHAR lp[MAX_PATH];
		long have;
		char sz[32];
		const char *name;
		if (g_fe[i].group != g) continue;
		fb_local_path(i, lp);
		have = file_size_w(lp);
		fmt_size(sz, g_fe[i].size);
		name = strrchr(g_fe[i].local, '/');
		name = name ? name + 1 : g_fe[i].local;
		buf_addf(&b, "<li><a href=\"about:get?%d\">", i);
		h_esc(&b, name);
		buf_adds(&b, "</a> <small>(");
		buf_adds(&b, sz);
		buf_adds(&b, ")");
		if (have >= 0 && (g_fe[i].size < 0 || have == g_fe[i].size)) buf_adds(&b, " 받음");
		else if (have >= 0) buf_adds(&b, " <b>새 버전</b>");
		buf_adds(&b, "</small>");
		if (have >= 0) buf_addf(&b, " <small><a href=\"about:open?%d\">[열기]</a></small>", i);
		if (g_fe[i].desc[0]) { buf_adds(&b, "<br><small>"); h_esc(&b, g_fe[i].desc); buf_adds(&b, "</small>"); }
	}
	buf_adds(&b, "</ul><p><small>저장 위치: ");
	h_esc(&b, rootu);
	buf_adds(&b, "</small></p><hr><p><a href=\"about:files\">◀ 앱 목록</a> | <a href=\"about:home\">홈</a></p>");
	free(rootu);
	_snprintf(url, sizeof url, "about:app?%d", g);
	d = html_parse(b.p, b.len, url, "utf-8", 0);
	buf_free(&b);
	return d;
}

static Doc *settings_page(void)
{
	Buf b;
	Doc *d;
	int n = (int)strlen(g_token);
	buf_init(&b);
	buf_adds(&b, "<title>파일함 설정</title><h2>파일함 설정</h2>"
		"<form action=\"about:savesettings\">"
		"<b>파일 목록 주소</b><br><input type=text name=m size=24 value=\"");
	h_esc(&b, g_repo);
	buf_adds(&b, "\"><br><br><b>GitHub 토큰</b> <small>(비공개 저장소일 때만 필요)</small><br>");
	if (n) buf_addf(&b, "<small>현재: 설정됨 (끝자리 ...%s)</small><br>", n > 4 ? g_token + n - 4 : g_token);
	else buf_adds(&b, "<small>현재: 없음</small><br>");
	buf_adds(&b, "<input type=password name=t size=24 placeholder=\"바꿀 때만 입력\"><br>"
		"<input type=checkbox name=clear value=1> 토큰 지우기<br><br>"
		"<input type=submit value=\"저장\"></form>"
		"<p><small>토큰은 PC에서 만든 뒤 github_token.txt 파일로 M480Web.exe와 같은 폴더에 넣어도 자동으로 읽습니다.</small></p>"
		"<p><a href=\"about:resetrepo\">목록 주소를 기본값으로</a> | <a href=\"about:files\">내 파일함</a></p>");
	d = html_parse(b.p, b.len, "about:settings", "utf-8", 0);
	buf_free(&b);
	return d;
}

static void apply_auth(void)
{
	/* the token is only ever sent to the host the file list lives on */
	Url u;
	if (url_parse(g_repo, &u)) { net_set_auth(u.host, g_token); url_free(&u); }
	else net_set_auth("", "");
}

static void load_token_file(void)
{
	WCHAR p[MAX_PATH];
	FILE *f;
	char line[200];
	app_path(p, L"github_token.txt");
	f = _wfopen(p, L"rb");
	if (!f) return;
	if (fgets(line, sizeof line, f)) {
		char *s = line;
		int n;
		if ((unsigned char)s[0] == 0xEF) s += 3;
		while (*s == ' ') s++;
		n = (int)strlen(s);
		while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = 0;
		if (n > 10) { strncpy(g_token, s, sizeof g_token - 1); g_token[sizeof g_token - 1] = 0; }
	}
	fclose(f);
}

/* run / install a file through the shell */
typedef struct {
	DWORD cbSize; ULONG fMask; HWND hwnd; LPCWSTR lpVerb, lpFile, lpParameters, lpDirectory; int nShow;
	HINSTANCE hInstApp; void *lpIDList; LPCWSTR lpClass; HKEY hkeyClass; DWORD dwHotKey; HANDLE hIcon; HANDLE hProcess;
} M_SEI;
typedef BOOL (WINAPI *PShellExecuteEx)(M_SEI *);

static int shell_open(const WCHAR *path)
{
	static PShellExecuteEx f;
	M_SEI sei;
	if (!f) {
#ifdef _WIN32_WCE
		HMODULE h = LoadLibraryW(L"coredll.dll");
		if (h) f = (PShellExecuteEx)GetProcAddressW(h, L"ShellExecuteEx");
#else
		HMODULE h = LoadLibraryW(L"shell32.dll");
		if (h) f = (PShellExecuteEx)GetProcAddress(h, "ShellExecuteExW");
#endif
	}
	if (!f) return 0;
	memset(&sei, 0, sizeof sei);
	sei.cbSize = sizeof sei;
	sei.hwnd = g_hwnd;
	sei.lpVerb = L"open";
	sei.lpFile = path;
	sei.nShow = SW_SHOWNORMAL;
	return f(&sei) ? 1 : 0;
}

static int ends_with_i(const WCHAR *s, const WCHAR *suf)
{
	int a = (int)wcslen(s), b = (int)wcslen(suf), i;
	if (a < b) return 0;
	for (i = 0; i < b; i++) {
		WCHAR x = s[a - b + i], y = suf[i];
		if (x >= 'A' && x <= 'Z') x += 32;
		if (y >= 'A' && y <= 'Z') y += 32;
		if (x != y) return 0;
	}
	return 1;
}

/* after a file is saved: offer to install / run it */
static void offer_open(const WCHAR *path)
{
	WCHAR msg[MAX_PATH + 200];
	const WCHAR *name = wcsrchr(path, '\\');
	int self;
	name = name ? name + 1 : path;
	self = !_wcsnicmp(name, L"M480Web", 7);
	if (ends_with_i(path, L".cab")) {
		wsprintfW(msg, L"받기 완료:\n%s\n\n지금 설치할까요?%s", path, self ? L"\n(설치를 위해 M480Web이 종료됩니다)" : L"");
		if (MessageBoxW(g_hwnd, msg, APPNAME, MB_YESNO | MB_ICONQUESTION) == IDYES) {
			if (!shell_open(path)) MessageBoxW(g_hwnd, L"설치 프로그램을 실행하지 못했습니다. 파일 탐색기에서 직접 눌러 주세요.", APPNAME, MB_OK);
			else if (self) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
		}
	} else if (ends_with_i(path, L".exe") && !self) {
		wsprintfW(msg, L"받기 완료:\n%s\n\n지금 실행할까요?", path);
		if (MessageBoxW(g_hwnd, msg, APPNAME, MB_YESNO | MB_ICONQUESTION) == IDYES) shell_open(path);
	} else {
		wsprintfW(msg, L"저장했습니다:\n%s", path);
		MessageBoxW(g_hwnd, msg, APPNAME, MB_OK);
	}
}

/* streaming target chosen once response headers are known (worker thread) */
static void *dl_on_headers(void *ctx, HttpResp *r)
{
	Job *j = (Job *)ctx;
	const char *ct = r->ctype;
	FILE *f;
	if (j->files) return NULL;
	if (j->qidx < 0) {
		/* ordinary link: only stream things we can't display */
		const char *p, *q;
		char name[128];
		int i;
		WCHAR *wn;
		if (!ct || is_html_type(ct) || !strncmp(ct, "text/", 5) || (!strncmp(ct, "image/", 6) && !strstr(ct, "icon"))) return NULL;
		p = j->url;
		q = strchr(p, '?');
		if (!q) q = p + strlen(p);
		for (i = (int)(q - p) - 1; i >= 0 && p[i] != '/'; i--);
		_snprintf(name, sizeof name - 1, "%.*s", (int)(q - p - i - 1) > 100 ? 100 : (int)(q - p - i - 1), p + i + 1);
		name[sizeof name - 1] = 0;
		if (!name[0]) strcpy(name, "download.bin");
		for (i = 0; name[i]; i++) if (strchr("\\/:*?\"<>|%", name[i])) name[i] = '_';
		wn = utf8_to_w(name, -1, NULL);
#ifdef _WIN32_WCE
		wcscpy(j->dlpath, L"\\My Documents\\");
		wcscat(j->dlpath, wn);
#else
		app_path(j->dlpath, wn);
#endif
		free(wn);
	}
	make_dirs_for(j->dlpath);
	f = _wfopen(j->dlpath, L"wb");
	if (!f) { _snprintf(r->err, sizeof r->err, "파일을 만들 수 없음 (사용 중이거나 공간 부족)"); return NULL; }
	j->streamed = 1;
	return f;
}

static void start_dl(int idx);

static void queue_next(void)
{
	char s[120];
	if (g_qpos >= g_qn) {
		int n = g_qn, fail = g_qfail;
		g_qn = g_qpos = 0;
		g_qfail = 0;
		if (g_doc && !strcmp(g_doc->url, "about:files")) install_doc(files_page(), NAV_RELOAD, NULL);
		else if (g_doc && !strncmp(g_doc->url, "about:app?", 10)) install_doc(app_page(atoi(g_doc->url + 10)), NAV_RELOAD, NULL);
		if (n > 1) {
			sprintf(s, fail ? "%d개 중 %d개 받기 실패" : "%d개 파일 받기 완료", n, fail);
			status_set(s);
		}
		return;
	}
	start_dl(g_q[g_qpos++]);
}

static void on_loaded(Job *j)
{
	if (j->gen != g_gen) { job_free(j); return; }
	g_job = NULL;
	g_loading = 0;
	toolbar_invalidate();
	if (j->ctl.abort) { job_free(j); status_title(); g_qn = g_qpos = 0; return; }
	if (j->files) {
		if (j->ok && j->resp.status == 200) {
			fb_parse(j->resp.body.p ? j->resp.body.p : "", j->resp.body.len);
			install_doc(files_page(), j->mode, j->resp.tlsinfo);
		} else {
			char msg[400];
			if (j->resp.status == 404)
				strcpy(msg, "파일 목록을 찾을 수 없습니다 (404). 저장소가 비공개라면 '파일함 설정'에서 GitHub 토큰을 넣어 주세요.");
			else if (j->resp.err[0]) _snprintf(msg, sizeof msg, "%s", j->resp.err);
			else _snprintf(msg, sizeof msg, "HTTP %d", j->resp.status);
			install_doc(error_page("about:files", msg), j->mode == NAV_NEW ? NAV_NEW : NAV_RELOAD, NULL);
		}
		job_free(j);
		return;
	}
	if (j->qidx >= 0 || j->streamed) {
		int ok = j->ok && j->streamed && j->resp.status >= 200 && j->resp.status < 300;
		WCHAR path[MAX_PATH];
		int single = (g_qn <= 1);
		wcscpy(path, j->dlpath);
		if (!ok) {
			char msg[300];
			WCHAR *w;
			if (j->resp.status == 404) strcpy(msg, "파일이 저장소에 없습니다 (404)");
			else if (j->resp.err[0]) strcpy(msg, j->resp.err);
			else _snprintf(msg, sizeof msg, "HTTP %d", j->resp.status);
			if (j->qidx >= 0) { g_qfail++; }
			w = utf8_to_w(msg, -1, NULL);
			if (single || j->qidx < 0) MessageBoxW(g_hwnd, w, L"받기 실패", MB_OK | MB_ICONEXCLAMATION);
			free(w);
		}
		job_free(j);
		status_title();
		if (g_doc) addr_set(g_doc->url);
		if (ok && single) offer_open(path);
		if (g_qn) queue_next();
		return;
	}
	if (j->doc) {
		Doc *d = j->doc;
		j->doc = NULL;
		install_doc(d, j->mode, j->resp.tlsinfo);
		job_free(j);
		return;
	}
	if (j->download && j->resp.status == 200) {
		save_download(j);
		job_free(j);
		status_title();
		if (g_doc) addr_set(g_doc->url);
		return;
	}
	if (j->resp.certerr) {
		WCHAR msg[600];
		WCHAR *e = utf8_to_w(j->resp.err, -1, NULL);
		WCHAR *h = utf8_to_w(j->resp.certhost, -1, NULL);
		wsprintfW(msg, L"%s\n\n\xC0AC\xC774\xD2B8: %s\n\n\xC704\xD5D8\xC744 \xC54C\xACE0 \xACC4\xC18D \xC5F0\xACB0\xD560\xAE4C\xC694? (\xC774 \xC0AC\xC774\xD2B8\xB294 \xC774\xBC88 \xC2E4\xD589 \xB3D9\xC548 \xD5C8\xC6A9\xB429\xB2C8\xB2E4)", e, h);
		free(e); free(h);
		if (MessageBoxW(g_hwnd, msg, L"\xBCF4\xC548 \xACBD\xACE0", MB_YESNO | MB_ICONEXCLAMATION | MB_DEFBUTTON2) == IDYES) {
			net_allow_insecure(j->resp.certhost);
			navigate(j->url, j->post, j->postlen, j->ctype, j->mode);
			job_free(j);
			return;
		}
	}
	{
		char msg[300];
		if (j->resp.err[0]) strcpy(msg, j->resp.err);
		else _snprintf(msg, sizeof msg, "HTTP %d", j->resp.status);
		install_doc(error_page(j->url, msg), j->mode == NAV_NEW ? NAV_NEW : NAV_RELOAD, NULL);
	}
	job_free(j);
}

static void navigate(const char *url0, const char *post, int postlen, const char *ctype, int mode)
{
	Job *j;
	HANDLE th;
	char *url = xstrdup(url0);
#ifdef M480_DEBUG
	{ FILE *lf = fopen("C:\\nav.log", "a"); if (lf) { fprintf(lf, "%s\n", url0); fclose(lf); } }
#endif
	field_commit(1);
	picker_close();
	/* home page search box: one field, two buttons */
	if (!strncmp(url, "about:search?", 13)) {
		char *q = query_param(url + 13, "q"), *sb = query_param(url + 13, "s");
		char *target = NULL;
		if (q && *q) {
			char *e = url_encode(q), *c;
			Buf b;
			buf_init(&b);
			if (sb && !strcmp(sb, "위키백과")) buf_adds(&b, "https://ko.m.wikipedia.org/w/index.php?title=%ED%8A%B9%EC%88%98:%EA%B2%80%EC%83%89&go=1&search=");
			else buf_adds(&b, "https://namu.wiki/Go?q=");
			for (c = e; *c; c++) { if (*c == '+') buf_adds(&b, "%20"); else buf_addc(&b, *c); }
			free(e);
			target = buf_detach(&b);
		}
		free(q); free(sb);
		free(url);
		if (target) { navigate(target, NULL, 0, NULL, NAV_NEW); free(target); }
		return;
	}
	/* file box commands */
	if (!strncmp(url, "about:get?", 10) || !strncmp(url, "about:getall?", 13) || !strncmp(url, "about:open?", 11)) {
		int n = atoi(strchr(url, '?') + 1), i;
		if (!strncmp(url, "about:get?", 10) && n >= 0 && n < g_nfe) {
			stop_all();
			g_qn = 0; g_qpos = 0; g_qfail = 0;
			g_q[g_qn++] = n;
			queue_next();
		} else if (!strncmp(url, "about:getall?", 13)) {
			stop_all();
			g_qn = 0; g_qpos = 0; g_qfail = 0;
			for (i = 0; i < g_nfe && g_qn < 64; i++) if (g_fe[i].group == n) g_q[g_qn++] = i;
			if (g_qn) queue_next();
		} else if (!strncmp(url, "about:open?", 11) && n >= 0 && n < g_nfe) {
			WCHAR lp[MAX_PATH];
			fb_local_path(n, lp);
			if (ends_with_i(lp, L".cab") || ends_with_i(lp, L".exe")) offer_open(lp);
			else if (!shell_open(lp)) MessageBoxW(g_hwnd, lp, L"이 파일은 여기서 열 수 없습니다", MB_OK);
		}
		free(url);
		return;
	}
	if (!strcmp(url, "about:files")) goto network;
	if (!strncmp(url, "about:app?", 10)) {
		int g = atoi(url + 10);
		free(url);
		if (!g_nfe) { navigate("about:files", NULL, 0, NULL, mode); return; }   /* list not loaded yet */
		stop_all();
		install_doc(app_page(g), mode, NULL);
		return;
	}
	/* about: pages */
	if (!strncmp(url, "about:", 6)) {
		stop_all();
		if (!strncmp(url, "about:savesettings", 18) || !strcmp(url, "about:resetrepo")) {
			char *q = strchr(url, '?');
			char *pm = q ? query_param(q + 1, "m") : NULL, *pt = q ? query_param(q + 1, "t") : NULL, *pc = q ? query_param(q + 1, "clear") : NULL;
			if (!strcmp(url, "about:resetrepo")) strcpy(g_repo, DEFAULT_REPO);
			if (pm && !strncmp(pm, "http", 4)) { strncpy(g_repo, pm, sizeof g_repo - 1); g_repo[sizeof g_repo - 1] = 0; }
			if (pt && *pt) { char *c = pt; while (*c == ' ') c++; strncpy(g_token, c, sizeof g_token - 1); g_token[sizeof g_token - 1] = 0; }
			if (pc) g_token[0] = 0;
			free(pm); free(pt); free(pc);
			apply_auth();
			settings_save();
			status_set("설정을 저장했습니다");
			install_doc(settings_page(), NAV_RELOAD, NULL);
			free(url);
			return;
		}
		if (!strcmp(url, "about:settings")) { install_doc(settings_page(), mode, NULL); free(url); return; }
		if (!strncmp(url, "about:editsc?", 13) || !strncmp(url, "about:addsc?", 12) || !strncmp(url, "about:delsc?", 12) ||
		    !strncmp(url, "about:upsc?", 11) || !strncmp(url, "about:downsc?", 13) || !strcmp(url, "about:resetsc")) {
			char *q = strchr(url, '?');
			char *pi = q ? query_param(q + 1, "i") : NULL, *pt = q ? query_param(q + 1, "t") : NULL, *pu = q ? query_param(q + 1, "u") : NULL;
			int idx = pi ? atoi(pi) : (q ? atoi(q + 1) : -1);
			char *fixed = (pu && *pu) ? url_fixup(pu) : NULL;
			if (!strncmp(url, "about:editsc", 12) && idx >= 0 && idx < g_nsc) {
				if (fixed && strncmp(fixed, "https://lite.duckduckgo.com/lite/?kl", 36)) { free(g_sc[idx].url); g_sc[idx].url = xstrdup(fixed); }
				if (pt && *pt) { char *c; free(g_sc[idx].title); g_sc[idx].title = xstrdup(pt); for (c = g_sc[idx].title; *c; c++) if (*c == '\t') *c = ' '; }
			} else if (!strncmp(url, "about:addsc", 11) && fixed && strncmp(fixed, "https://lite.duckduckgo.com/lite/?kl", 36)) {
				marks_add(&g_sc, &g_nsc, fixed, pt);
			} else if (!strncmp(url, "about:delsc", 11)) {
				marks_del(g_sc, &g_nsc, idx);
			} else if (!strncmp(url, "about:upsc", 10) && idx > 0 && idx < g_nsc) {
				Mark t = g_sc[idx]; g_sc[idx] = g_sc[idx - 1]; g_sc[idx - 1] = t;
			} else if (!strncmp(url, "about:downsc", 12) && idx >= 0 && idx < g_nsc - 1) {
				Mark t = g_sc[idx]; g_sc[idx] = g_sc[idx + 1]; g_sc[idx + 1] = t;
			} else if (!strcmp(url, "about:resetsc")) {
				if (MessageBoxW(g_hwnd, L"바로가기를 기본 목록으로 되돌릴까요?", APPNAME, MB_YESNO) == IDYES) sc_reset();
			}
			free(pi); free(pt); free(pu); free(fixed);
			marks_save(L"shortcuts.txt", g_sc, g_nsc);
			free(url);
			url = xstrdup("about:shortcuts");
			if (g_doc && !strncmp(g_doc->url, "about:shortcuts", 15)) mode = NAV_RELOAD;
		} else if (!strncmp(url, "about:delbm?", 12)) {
			int i = atoi(url + 12);
			if (i >= 0 && i < g_nbm) {
				free(g_bm[i].url); free(g_bm[i].title);
				memmove(&g_bm[i], &g_bm[i + 1], (g_nbm - i - 1) * sizeof(Mark));
				g_nbm--;
				marks_save(L"bookmarks.txt", g_bm, g_nbm);
			}
			free(url);
			url = xstrdup("about:bookmarks");
			mode = NAV_RELOAD;
		} else if (!strcmp(url, "about:clearhistory")) {
			int i;
			for (i = 0; i < g_nvis; i++) { free(g_vis[i].url); free(g_vis[i].title); }
			g_nvis = 0;
			marks_save(L"history.txt", g_vis, 0);
			free(url);
			url = xstrdup("about:history");
			mode = NAV_RELOAD;
		}
		install_doc(about_page(url), mode, NULL);
		free(url);
		return;
	}
	/* same-document fragment navigation */
	if (g_doc && !post && strchr(url, '#')) {
		char *a = url_strip_fragment(url), *b = url_strip_fragment(g_doc->url);
		int same = !strcmp(a, b);
		free(a); free(b);
		if (same) {
			int y = find_anchor_y(url);
			if (mode == NAV_NEW) {
				if (g_hpos >= 0) g_hist[g_hpos].scroll = g_scroll;
				hist_push(url);
			}
			free(g_doc->url);
			g_doc->url = xstrdup(url);
			addr_set(url);
			if (mode == NAV_HIST && g_hpos >= 0) set_scroll(g_hist[g_hpos].scroll);
			else if (y >= 0) set_scroll(y);
			free(url);
			toolbar_invalidate();
			return;
		}
	}
network:
	stop_all();
	j = (Job *)calloc(1, sizeof(Job));
	j->url = url;
	if (post) { j->post = (char *)malloc(postlen + 1); memcpy(j->post, post, postlen); j->post[postlen] = 0; j->postlen = postlen; }
	j->ctype = ctype ? xstrdup(ctype) : NULL;
	j->referer = (g_doc && strncmp(g_doc->url, "about:", 6)) ? xstrdup(g_doc->url) : NULL;
	j->mode = mode;
	j->gen = g_gen;
	j->ctl.progress = progress_cb;
	j->ctl.on_headers = dl_on_headers;
	j->ctl.ctx = j;
	j->ctl.sock = (unsigned int)INVALID_SOCKET;
	j->qidx = -1;
	if (!strcmp(url, "about:files") || !strcmp(url, g_repo)) {
		free(j->url);
		j->url = xstrdup(g_repo);
		j->files = 1;
		free(j->referer);
		j->referer = NULL;
	}
	g_job = j;
	g_loading = 1;
	if (!j->files) addr_set(url);
	status_set("\xEC\x97\xB0\xEA\xB2\xB0 \xEC\xA4\x91...");
	toolbar_invalidate();
	th = CreateThread(NULL, THREAD_STACK, load_thread, j, THREAD_FLAGS, NULL);
	if (th) CloseHandle(th);
	else { g_job = NULL; g_loading = 0; job_free(j); status_set("\xEC\x8A\xA4\xEB\xA0\x88\xEB\x93\x9C \xEC\x83\x9D\xEC\x84\xB1 \xEC\x8B\xA4\xED\x8C\xA8"); }
}

static void start_dl(int idx)
{
	Job *j;
	HANDLE th;
	char s[200];
	const char *name = strrchr(g_fe[idx].local, '/');
	name = name ? name + 1 : g_fe[idx].local;
	j = (Job *)calloc(1, sizeof(Job));
	j->url = fb_url(idx);
	j->mode = NAV_RELOAD;
	j->gen = g_gen;
	j->qidx = idx;
	fb_local_path(idx, j->dlpath);
	j->ctl.progress = progress_cb;
	j->ctl.on_headers = dl_on_headers;
	j->ctl.ctx = j;
	j->ctl.sock = (unsigned int)INVALID_SOCKET;
	g_job = j;
	g_loading = 1;
	if (g_qn > 1) _snprintf(s, sizeof s, "받는 중 (%d/%d) %s", g_qpos, g_qn, name);
	else _snprintf(s, sizeof s, "받는 중: %s", name);
	status_set(s);
	toolbar_invalidate();
	th = CreateThread(NULL, THREAD_STACK, load_thread, j, THREAD_FLAGS, NULL);
	if (th) CloseHandle(th);
	else { g_job = NULL; g_loading = 0; job_free(j); g_qn = g_qpos = 0; }
}

/* ---------------- forms ---------------- */
static void add_pair(Buf *b, const WCHAR *name, const WCHAR *value, UINT cp)
{
	char *n = w_to_cp(name, -1, cp), *v = w_to_cp(value ? value : L"", -1, cp);
	char *ne = url_encode(n), *ve = url_encode(v);
	if (b->len) buf_addc(b, '&');
	buf_adds(b, ne); buf_addc(b, '='); buf_adds(b, ve);
	free(n); free(v); free(ne); free(ve);
}

static void submit_form(int form, int submitter)
{
	Doc *d = g_doc;
	Buf b;
	int i;
	UINT cp;
	char *action;
	int post;
	if (!d) return;
	cp = d->cp ? d->cp : CP_UTF8;
	buf_init(&b);
	for (i = 0; i < d->nctls; i++) {
		Ctl *c = &d->ctls[i];
		if (c->form != form || !c->name || !c->name[0]) continue;
		switch (c->type) {
		case CT_SUBMIT: case CT_IMAGE:
			if (i != submitter) break;
			if (c->type == CT_IMAGE) {
				WCHAR nx[140];
				wsprintfW(nx, L"%.120s.x", c->name); add_pair(&b, nx, L"1", cp);
				wsprintfW(nx, L"%.120s.y", c->name); add_pair(&b, nx, L"1", cp);
			} else add_pair(&b, c->name, c->value, cp);
			break;
		case CT_RESET: case CT_BUTTON: case CT_FILE: break;
		case CT_CHECK: case CT_RADIO:
			if (c->checked) add_pair(&b, c->name, c->value, cp);
			break;
		case CT_SELECT:
			if (c->nopt > 0) add_pair(&b, c->name, c->opt_value[c->sel], cp);
			break;
		default:
			add_pair(&b, c->name, c->value, cp);
		}
	}
	if (form >= 0 && form < d->nforms) { action = xstrdup(d->forms[form].action); post = d->forms[form].post; }
	else { action = xstrdup(d->url); post = 0; }
	if (post) {
		navigate(action, b.p ? b.p : "", b.len, "application/x-www-form-urlencoded", NAV_NEW);
	} else {
		Buf u;
		char *q;
		char *a2 = url_strip_fragment(action);
		buf_init(&u);
		q = strchr(a2, '?');
		if (q) *q = 0;
		buf_adds(&u, a2);
		buf_addc(&u, '?');
		if (b.p) buf_adds(&u, b.p);
		navigate(u.p, NULL, 0, NULL, NAV_NEW);
		buf_free(&u);
		free(a2);
	}
	free(action);
	buf_free(&b);
}

static void reset_form(int form)
{
	(void)form;
	navigate(g_doc->url, NULL, 0, NULL, NAV_RELOAD);
}

/* inline edit field for text inputs */
static LRESULT CALLBACK field_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WM_KEYDOWN && w == VK_RETURN && g_field_ctl >= 0 && g_doc && g_doc->ctls[g_field_ctl].type != CT_TEXTAREA) {
		int ctl = g_field_ctl;
		int form = g_doc->ctls[ctl].form;
		field_commit(1);
		submit_form(form, -1);
		return 0;
	}
	if (m == WM_CHAR && w == VK_RETURN && g_field_ctl >= 0 && g_doc && g_doc->ctls[g_field_ctl].type != CT_TEXTAREA) return 0;
	if (m == WM_KILLFOCUS) {
		LRESULT r = CallWindowProcW(g_field_proc, h, m, w, l);
		PostMessageW(g_hwnd, WM_COMMAND, MAKEWPARAM(ID_FIELD, 0xFFFF), 0);
		return r;
	}
	return CallWindowProcW(g_field_proc, h, m, w, l);
}

static void field_commit(int destroy)
{
	if (!g_field) return;
	if (g_field_ctl >= 0 && g_doc && g_field_ctl < g_doc->nctls) {
		Ctl *c = &g_doc->ctls[g_field_ctl];
		int n = GetWindowTextLengthW(g_field);
		WCHAR *t = (WCHAR *)malloc((n + 1) * sizeof(WCHAR));
		GetWindowTextW(g_field, t, n + 1);
		if (c->type == CT_TEXTAREA) {
			/* convert \r\n to \n */
			int i, k = 0;
			for (i = 0; t[i]; i++) if (t[i] != '\r') t[k++] = t[i];
			t[k] = 0;
		}
		free(c->value);
		c->value = t;
	}
	if (destroy) {
		HWND f = g_field;
		g_field = NULL;
		g_field_ctl = -1;
		DestroyWindow(f);
		sip_show(0);
		InvalidateRect(g_view, NULL, FALSE);
	}
}

static void field_open(int ctl)
{
	RECT r;
	Ctl *c = &g_doc->ctls[ctl];
	DWORD st = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
	WCHAR *val;
	field_commit(1);
	if (!layout_ctl_box(&g_lay, ctl, &r)) return;
	if (c->type == CT_PASSWORD) st |= ES_PASSWORD;
	if (c->type == CT_TEXTAREA) st = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL;
	/* make sure field is visible */
	{
		RECT vr;
		GetClientRect(g_view, &vr);
		if (r.top - g_scroll < 0 || r.bottom - g_scroll > vr.bottom) set_scroll(r.top - SC(20));
	}
	g_field = CreateWindowExW(0, L"EDIT", L"", st, r.left, r.top - g_scroll, r.right - r.left, r.bottom - r.top,
	                          g_view, (HMENU)ID_FIELD, g_inst, NULL);
	if (!g_field) return;
	g_field_ctl = ctl;
	SendMessageW(g_field, WM_SETFONT, (WPARAM)font_for(1 << S_SZ_SH), TRUE);
	/* textarea: expand \n to \r\n */
	if (c->type == CT_TEXTAREA) {
		WBuf wb; int i;
		wbuf_init(&wb);
		for (i = 0; c->value && c->value[i]; i++) { if (c->value[i] == '\n') wbuf_addc(&wb, '\r'); wbuf_addc(&wb, c->value[i]); }
		val = wb.p ? wb.p : xwcsdup(L"");
	} else val = xwcsdup(c->value ? c->value : L"");
	SetWindowTextW(g_field, val);
	free(val);
	g_field_proc = (WNDPROC)SetWindowLongW(g_field, GWL_WNDPROC, (LONG)field_proc);
	SetFocus(g_field);
	SendMessageW(g_field, EM_SETSEL, 0, -1);
	sip_show(1);
}

/* select picker: a listbox covering the view */
static void picker_close(void)
{
	if (g_picker) { HWND p = g_picker; g_picker = NULL; DestroyWindow(p); InvalidateRect(g_view, NULL, FALSE); }
}
static int g_picker_ctl = -1;
static void picker_open(int ctl)
{
	RECT r;
	Ctl *c = &g_doc->ctls[ctl];
	int i;
	picker_close();
	field_commit(1);
	GetClientRect(g_view, &r);
	g_picker = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
	                           SC(8), SC(8), r.right - SC(16), r.bottom - SC(16), g_view, (HMENU)ID_PICKER, g_inst, NULL);
	if (!g_picker) return;
	SendMessageW(g_picker, WM_SETFONT, (WPARAM)font_for((2 << S_SZ_SH)), TRUE);
	for (i = 0; i < c->nopt; i++) SendMessageW(g_picker, LB_ADDSTRING, 0, (LPARAM)(c->opt_label[i][0] ? c->opt_label[i] : L" "));
	SendMessageW(g_picker, LB_SETCURSEL, c->sel, 0);
	{
		int ih = (int)SendMessageW(g_picker, LB_GETITEMHEIGHT, 0, 0);
		int hh = ih * c->nopt + SC(4);
		RECT cr;
		if (ih > 0 && hh < r.bottom - SC(16)) {
			RECT b;
			int top = SC(8);
			if (layout_ctl_box(&g_lay, ctl, &b)) {
				top = b.bottom - g_scroll;
				if (top + hh > r.bottom - SC(4)) top = b.top - g_scroll - hh;
				if (top < SC(4)) top = SC(4);
			}
			MoveWindow(g_picker, SC(8), top, r.right - SC(16), hh, TRUE);
		}
		(void)cr;
	}
	g_picker_ctl = ctl;
	SetFocus(g_picker);
}

static void activate_ctl(int ctl)
{
	Ctl *c;
	int i;
	if (!g_doc || ctl < 0 || ctl >= g_doc->nctls) return;
	c = &g_doc->ctls[ctl];
	switch (c->type) {
	case CT_TEXT: case CT_PASSWORD: case CT_TEXTAREA:
		field_open(ctl);
		break;
	case CT_CHECK:
		c->checked = !c->checked;
		InvalidateRect(g_view, NULL, FALSE);
		break;
	case CT_RADIO:
		for (i = 0; i < g_doc->nctls; i++) {
			Ctl *o = &g_doc->ctls[i];
			if (o->type == CT_RADIO && o->form == c->form && o->name && c->name && !wcscmp(o->name, c->name)) o->checked = 0;
		}
		c->checked = 1;
		InvalidateRect(g_view, NULL, FALSE);
		break;
	case CT_SELECT:
		if (c->nopt > 0) picker_open(ctl);
		break;
	case CT_SUBMIT: case CT_IMAGE:
		submit_form(c->form, ctl);
		break;
	case CT_RESET:
		reset_form(c->form);
		break;
	}
}

/* ---------------- view ---------------- */
static int view_height(void)
{
	RECT r;
	GetClientRect(g_view, &r);
	return r.bottom;
}

static void update_scrollbar(void)
{
	SCROLLINFO si;
	si.cbSize = sizeof si;
	si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	si.nMin = 0;
	si.nMax = g_lay.height > 0 ? g_lay.height - 1 : 0;
	si.nPage = view_height();
	si.nPos = g_scroll;
	(void)si; /* no system scroll bar: the page draws its own scroll handle */
}

static void set_scroll(int y)
{
	int maxs = g_lay.height - view_height();
	if (maxs < 0) maxs = 0;
	if (y > maxs) y = maxs;
	if (y < 0) y = 0;
	if (y == g_scroll) { update_scrollbar(); return; }
	if (g_field) field_commit(1);
	g_scroll = y;
	update_scrollbar();
	InvalidateRect(g_view, NULL, FALSE);
	UpdateWindow(g_view);
}

static void relayout(int keep_anchor)
{
	HDC hdc;
	int anchor_item = -1, anchor_off = 0;
	if (!g_doc) return;
	if (keep_anchor && g_lay.nboxes) {
		int lo = 0, hi = g_lay.nboxes;
		while (lo < hi) { int mid = (lo + hi) / 2; if (g_lay.boxes[mid].lb < g_scroll) lo = mid + 1; else hi = mid; }
		if (lo < g_lay.nboxes) { anchor_item = g_lay.boxes[lo].item; anchor_off = g_lay.boxes[lo].y - g_scroll; }
	}
	hdc = GetDC(g_view);
	layout_doc(&g_lay, hdc, g_doc, view_width() - SB_W + 4);   /* right strip kept for the scroll handle */
	ReleaseDC(g_view, hdc);
	if (anchor_item >= 0 && g_scroll > 0) {
		int ny = layout_item_y(&g_lay, anchor_item) - anchor_off;
		g_scroll = -1;
		set_scroll(ny);
	} else {
		int s = g_scroll;
		g_scroll = -1;
		set_scroll(s);
	}
	if (g_field && g_field_ctl >= 0) {
		RECT r;
		if (layout_ctl_box(&g_lay, g_field_ctl, &r)) MoveWindow(g_field, r.left, r.top - g_scroll, r.right - r.left, r.bottom - r.top, TRUE);
	}
	InvalidateRect(g_view, NULL, FALSE);
}

static void copy_text(const char *s)
{
	WCHAR *w = utf8_to_w(s, -1, NULL);
	int n = (int)wcslen(w);
	HGLOBAL h;
	if (!OpenClipboard(g_hwnd)) { free(w); return; }
	EmptyClipboard();
	h = (HGLOBAL)LocalAlloc(LMEM_FIXED, (n + 1) * sizeof(WCHAR));
	if (h) { memcpy((void *)h, w, (n + 1) * sizeof(WCHAR)); SetClipboardData(CF_UNICODETEXT, h); }
	CloseClipboard();
	free(w);
}

static void open_simplified(const char *url)
{
	char *e = url_encode(url);
	Buf b;
	buf_init(&b);
	buf_adds(&b, "http://frogfind.com/read.php?a=");
	buf_adds(&b, e);
	navigate(b.p, NULL, 0, NULL, NAV_NEW);
	buf_free(&b);
	free(e);
}

static void link_menu(int link, int x, int y)
{
	HMENU m = CreatePopupMenu();
	POINT pt;
	int cmd;
	AppendMenuW(m, MF_STRING, M_LOPEN, L"\xB9C1\xD06C \xC5F4\xAE30");
	AppendMenuW(m, MF_STRING, M_LSIMPLE, L"\xB2E8\xC21C\xD654 \xBCF4\xAE30\xB85C \xC5F4\xAE30");
	AppendMenuW(m, MF_STRING, M_LCOPY, L"\xB9C1\xD06C \xC8FC\xC18C \xBCF5\xC0AC");
	pt.x = x; pt.y = y;
	ClientToScreen(g_view, &pt);
	cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, g_hwnd, NULL);
	DestroyMenu(m);
	if (!g_doc || link < 0 || link >= g_doc->nlinks) return;
	if (cmd == M_LOPEN) navigate(g_doc->links[link], NULL, 0, NULL, NAV_NEW);
	else if (cmd == M_LSIMPLE) open_simplified(g_doc->links[link]);
	else if (cmd == M_LCOPY) copy_text(g_doc->links[link]);
}

/* ---------------- hardware keys ---------------- */
#define M_SOFTMENU 1990
#define M_GOBACK   1991
#define HK_SOFT2   0xB002
#ifndef MOD_KEYUP
#define MOD_KEYUP 0x1000
#endif
typedef BOOL (WINAPI *PUnregisterFunc1)(UINT, UINT);
typedef HANDLE (WINAPI *PImmAssoc)(HWND, HANDLE);
typedef UINT (WINAPI *PImmGetVK)(HWND);
static PImmGetVK g_immgetvk;
static int g_softkey_on;

static void keys_init(void)
{
	HMODULE m;
	PImmAssoc a = NULL;
#ifdef _WIN32_WCE
	m = LoadLibraryW(L"coredll.dll");
	if (m) {
		a = (PImmAssoc)GetProcAddressW(m, L"ImmAssociateContext");
		g_immgetvk = (PImmGetVK)GetProcAddressW(m, L"ImmGetVirtualKey");
	}
#else
	m = LoadLibraryW(L"imm32.dll");
	if (m) {
		a = (PImmAssoc)GetProcAddress(m, "ImmAssociateContext");
		g_immgetvk = (PImmGetVK)GetProcAddress(m, "ImmGetVirtualKey");
	}
#endif
	/* page view needs plain keys (Backspace etc.), not Korean IME composition */
	if (a) { a(g_view, NULL); a(g_hwnd, NULL); }
}

/* right soft key (above the End key): take it over while M480Web is in front */
static void softkey_grab(int on)
{
#ifdef _WIN32_WCE
	static PUnregisterFunc1 unreg;
	static int looked;
	if (!looked) {
		HMODULE m = LoadLibraryW(L"coredll.dll");
		looked = 1;
		if (m) unreg = (PUnregisterFunc1)GetProcAddressW(m, L"UnregisterFunc1");
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

static WPARAM real_vk(HWND h, WPARAM w)
{
	if (w == 0xE5 && g_immgetvk) { UINT v = g_immgetvk(h); if (v) return v; }  /* VK_PROCESSKEY */
	return w;
}

static LRESULT CALLBACK view_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch (m) {
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(h, &ps);
		RECT r;
		HDC mdc;
		HGDIOBJ old;
		GetClientRect(h, &r);
		if (!g_back_bm || g_back_w != r.right || g_back_h != r.bottom) {
			if (g_back_bm) DeleteObject(g_back_bm);
			g_back_bm = CreateCompatibleBitmap(hdc, r.right, r.bottom);
			g_back_w = r.right; g_back_h = r.bottom;
		}
		mdc = CreateCompatibleDC(hdc);
		old = SelectObject(mdc, g_back_bm);
		{ HBRUSH bgb = CreateSolidBrush(C_BG); FillRect(mdc, &r, bgb); DeleteObject(bgb); }
		layout_paint(&g_lay, mdc, g_scroll, r.bottom, g_press_link, g_press_ctl);
		if (g_lay.height > r.bottom && r.bottom > SB_TH) {
			/* scroll handle on the right: drag it to move through long pages quickly */
			int w = r.right, hgt = r.bottom;
			int by = (int)((long)g_scroll * (hgt - SB_TH) / (g_lay.height - hgt));
			int x0 = w - SB_W + 6, x1 = w - 2, cx = (x0 + x1) / 2, k;
			COLORREF hc = g_sbdrag ? DK(RGB(120, 150, 210), RGB(80, 105, 160)) : DK(RGB(200, 208, 222), RGB(62, 66, 78)), fg = DK(RGB(60, 70, 90), RGB(185, 190, 205));
			RECT rr;
			HBRUSH br;
#define SBFILL(L, T, R, B, C) do { rr.left = (L); rr.top = (T); rr.right = (R); rr.bottom = (B); br = CreateSolidBrush(C); FillRect(mdc, &rr, br); DeleteObject(br); } while (0)
			SBFILL(w - 3, 0, w - 1, hgt, DK(RGB(215, 218, 225), RGB(55, 58, 66)));                 /* track */
			SBFILL(x0 - 1, by - 1, x1 + 1, by + SB_TH + 1, DK(RGB(150, 155, 170), RGB(95, 100, 115))); /* border */
			SBFILL(x0, by, x1, by + SB_TH, hc);
			for (k = 0; k < 6; k++) {                                             /* up / down arrows */
				SBFILL(cx - k, by + 8 + k, cx + k + 1, by + 9 + k, fg);
				SBFILL(cx - k, by + SB_TH - 9 - k, cx + k + 1, by + SB_TH - 8 - k, fg);
			}
			SBFILL(x0 + 5, by + SB_TH / 2 - 3, x1 - 5, by + SB_TH / 2 - 2, fg); /* grip */
			SBFILL(x0 + 5, by + SB_TH / 2 + 1, x1 - 5, by + SB_TH / 2 + 2, fg);
#undef SBFILL
		}
		BitBlt(hdc, 0, 0, r.right, r.bottom, mdc, 0, 0, SRCCOPY);
		SelectObject(mdc, old);
		DeleteDC(mdc);
		EndPaint(h, &ps);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_SIZE:
		picker_close();
		if (g_doc) {
			static int lastw;
			if (LOWORD(l) != lastw) { lastw = LOWORD(l); relayout(1); }
			else if (g_field && g_field_ctl >= 0) {
				/* keyboard appeared/disappeared: keep the edited field visible */
				RECT r;
				int vh = HIWORD(l);
				if (layout_ctl_box(&g_lay, g_field_ctl, &r) && (r.bottom - g_scroll > vh || r.top - g_scroll < 0)) {
					g_scroll = r.top - SC(8);
					if (g_scroll < 0) g_scroll = 0;
				}
				update_scrollbar();
				if (layout_ctl_box(&g_lay, g_field_ctl, &r)) MoveWindow(g_field, r.left, r.top - g_scroll, r.right - r.left, r.bottom - r.top, TRUE);
				InvalidateRect(h, NULL, FALSE);
			} else { int s = g_scroll; g_scroll = -1; set_scroll(s); }
		}
		return 0;
	case WM_VSCROLL: {
		SCROLLINFO si;
		int y = g_scroll, page = view_height() - SC(30), line = font_height(1 << S_SZ_SH) * 2;
		si.cbSize = sizeof si;
		si.fMask = SIF_TRACKPOS;
		GetScrollInfo(h, SB_VERT, &si);
		switch (LOWORD(w)) {
		case SB_LINEUP: y -= line; break;
		case SB_LINEDOWN: y += line; break;
		case SB_PAGEUP: y -= page; break;
		case SB_PAGEDOWN: y += page; break;
		case SB_THUMBTRACK: case SB_THUMBPOSITION: y = si.nTrackPos; break;
		case SB_TOP: y = 0; break;
		case SB_BOTTOM: y = g_lay.height; break;
		}
		KillTimer(g_hwnd, TM_KINETIC);
		set_scroll(y);
		return 0;
	}
	case WM_LBUTTONDOWN: {
		int x = (short)LOWORD(l), y = (short)HIWORD(l);
		if (g_picker) { picker_close(); return 0; }
		SetCapture(h);
		if (GetFocus() != h && !g_picker) SetFocus(h);
		/* a touch that stops a moving page only stops it */
		g_nofire = g_vel != 0;
		KillTimer(g_hwnd, TM_KINETIC);
		g_vel = 0;
		{
			RECT cr;
			int vh;
			GetClientRect(h, &cr);
			vh = cr.bottom;
			if (x >= cr.right - SB_W && g_lay.height > vh && vh > SB_TH) {
				int by = (int)((long)g_scroll * (vh - SB_TH) / (g_lay.height - vh));
				int on = (y >= by && y < by + SB_TH);
				if (g_field) field_commit(1);
				g_sbdrag = 1;
				g_sboff = on ? y - by : SB_TH / 2;   /* tap the track: jump there */
				g_down = 0;
				g_press_link = g_press_ctl = -1;
				if (!on) set_scroll((int)((long)(y - g_sboff) * (g_lay.height - vh) / (vh - SB_TH)));
				InvalidateRect(h, NULL, FALSE);
				return 0;
			}
		}
		g_down = 1; g_dragging = 0;
		g_down_x = x; g_down_y = y; g_last_y = y; g_last_t = GetTickCount();
		if (g_nofire) g_press_link = g_press_ctl = -1;
		else layout_hit(&g_lay, x, y + g_scroll, &g_press_link, &g_press_ctl);
#ifdef M480_DEBUG
		{ FILE *lf = fopen("C:\\nav.log", "a"); if (lf) { fprintf(lf, "down %d %d link=%d ctl=%d\n", x, y + g_scroll, g_press_link, g_press_ctl); { int q; for (q = 0; q < g_lay.nboxes && q < 30; q++) fprintf(lf, " box%d it=%d type=%d x=%d y=%d w=%d h=%d lb=%d\n", q, g_lay.boxes[q].item, g_doc->items[g_lay.boxes[q].item].type, g_lay.boxes[q].x, g_lay.boxes[q].y, g_lay.boxes[q].w, g_lay.boxes[q].h, g_lay.boxes[q].lb); } fclose(lf); } }
#endif
		g_hold_link = g_press_link;
		if (g_press_link >= 0) SetTimer(g_hwnd, TM_HOLD, 700, NULL);
		if (g_press_link >= 0 || g_press_ctl >= 0) InvalidateRect(h, NULL, FALSE);
		entropy_stir(&l, sizeof l);
		return 0;
	}
	case WM_MOUSEMOVE: {
		int x = (short)LOWORD(l), y = (short)HIWORD(l);
		DWORD t = GetTickCount();
		if (g_sbdrag) {
			RECT cr;
			GetClientRect(h, &cr);
			if (g_lay.height > cr.bottom && cr.bottom > SB_TH)
				set_scroll((int)((long)(y - g_sboff) * (g_lay.height - cr.bottom) / (cr.bottom - SB_TH)));
			return 0;
		}
		if (!g_down) return 0;
		/* any real movement means scrolling, never a tap */
		if (!g_dragging && (abs(y - g_down_y) > 6 || abs(x - g_down_x) > 10)) {
			g_dragging = 1;
			KillTimer(g_hwnd, TM_HOLD);
			if (g_press_link >= 0 || g_press_ctl >= 0) { g_press_link = g_press_ctl = -1; InvalidateRect(h, NULL, FALSE); }
		}
		if (g_dragging) {
			int dy = g_last_y - y;
			DWORD dt = t - g_last_t;
			if (dt > 0) {
				int v = dy * 25 * 16 / (int)(dt < 5 ? 5 : dt);
				g_vel = (g_vel + v * 3) / 4;
			}
			set_scroll(g_scroll + dy);
			g_last_y = y; g_last_t = t;
		}
		return 0;
	}
	case WM_LBUTTONUP: {
		int link = g_press_link, ctl = g_press_ctl;
		KillTimer(g_hwnd, TM_HOLD);
		ReleaseCapture();
		if (g_sbdrag) { g_sbdrag = 0; InvalidateRect(h, NULL, FALSE); return 0; }
		if (!g_down) return 0;
		g_down = 0;
		g_press_link = g_press_ctl = -1;
		if (g_dragging) {
			if (GetTickCount() - g_last_t > 120) g_vel = 0;
			if (abs(g_vel) > 16 * 3) SetTimer(g_hwnd, TM_KINETIC, 25, NULL);
			else g_vel = 0;
			return 0;
		}
		InvalidateRect(h, NULL, FALSE);
		if (g_nofire) return 0;
		if (ctl >= 0) activate_ctl(ctl);
		else if (link >= 0 && g_doc && link < g_doc->nlinks) navigate(g_doc->links[link], NULL, 0, NULL, NAV_NEW);
		return 0;
	}
	case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORSTATIC:
		if (g_dark) {
			if (!g_ctl_br) g_ctl_br = CreateSolidBrush(RGB(40, 42, 48));
			SetTextColor((HDC)w, RGB(225, 225, 222));
			SetBkColor((HDC)w, RGB(40, 42, 48));
			return (LRESULT)g_ctl_br;
		}
		break;
	case WM_KEYUP:
		if (real_vk(h, w) == VK_F2) { PostMessageW(g_hwnd, WM_COMMAND, M_SOFTMENU, 0); return 0; }
		break;
	case WM_KEYDOWN: {
		int line = font_height(1 << S_SZ_SH) * 3, page = view_height() - SC(30);
		w = real_vk(h, w);
		switch (w) {
		case VK_ESCAPE: PostMessageW(g_hwnd, WM_COMMAND, M_GOBACK, 0); return 0;
		case VK_F2: return 0;
		case VK_UP: set_scroll(g_scroll - line); return 0;
		case VK_DOWN: set_scroll(g_scroll + line); return 0;
		case VK_LEFT: case VK_PRIOR: set_scroll(g_scroll - page); return 0;
		case VK_RIGHT: case VK_NEXT: case VK_SPACE: set_scroll(g_scroll + page); return 0;
		case VK_HOME: set_scroll(0); return 0;
		case VK_END: set_scroll(g_lay.height); return 0;
		case VK_BACK: PostMessageW(g_hwnd, WM_COMMAND, M_GOBACK, 0); return 0;
		}
		break;
	}
	case WM_COMMAND:
		if (LOWORD(w) == ID_PICKER && (HIWORD(w) == LBN_SELCHANGE || HIWORD(w) == LBN_DBLCLK)) {
			int sel = (int)SendMessageW(g_picker, LB_GETCURSEL, 0, 0);
			if (g_doc && g_picker_ctl >= 0 && g_picker_ctl < g_doc->nctls && sel >= 0) {
				g_doc->ctls[g_picker_ctl].sel = sel;
				relayout(1);
			}
			PostMessageW(g_hwnd, WM_COMMAND, MAKEWPARAM(ID_PICKER, 0xFFFF), 0);
			return 0;
		}
		break;
	}
	return DefWindowProcW(h, m, w, l);
}

/* ---------------- toolbar ---------------- */
enum { TB_BACK, TB_FWD, TB_RELOAD, TB_MENU };
static int tb_btn_w(void) { return g_tb_h; }

static void tb_rect(int which, RECT *r, int cw)
{
	int bw = tb_btn_w();
	r->top = 0; r->bottom = g_tb_h;
	switch (which) {
	case TB_BACK: r->left = 0; break;
	case TB_FWD: r->left = bw; break;
	case TB_RELOAD: r->left = bw * 2; break;
	default: r->left = cw - bw; break;
	}
	r->right = r->left + bw;
}

static void draw_poly(HDC hdc, POINT *p, int n, COLORREF c)
{
	HBRUSH br = CreateSolidBrush(c);
	HPEN pen = CreatePen(PS_SOLID, 1, c);
	HGDIOBJ ob = SelectObject(hdc, br), op = SelectObject(hdc, pen);
	Polygon(hdc, p, n);
	SelectObject(hdc, ob); SelectObject(hdc, op);
	DeleteObject(br); DeleteObject(pen);
}

static void draw_toolbar(HDC hdc, int cw)
{
	RECT r;
	int i, bw = tb_btn_w();
	COLORREF on = DK(RGB(40, 60, 90), RGB(190, 200, 220)), off = DK(RGB(170, 175, 185), RGB(85, 90, 100));
	HBRUSH bg = CreateSolidBrush(DK(RGB(222, 228, 238), RGB(38, 41, 49)));
	r.left = 0; r.top = 0; r.right = cw; r.bottom = g_tb_h;
	FillRect(hdc, &r, bg);
	DeleteObject(bg);
	for (i = 0; i < 4; i++) {
		int cx, cy, s;
		POINT p[8];
		tb_rect(i, &r, cw);
		cx = (r.left + r.right) / 2; cy = (r.top + r.bottom) / 2; s = bw / 4;
		if (i == TB_BACK) {
			p[0].x = cx - s; p[0].y = cy; p[1].x = cx + s * 2 / 3; p[1].y = cy - s; p[2].x = cx + s * 2 / 3; p[2].y = cy + s;
			draw_poly(hdc, p, 3, g_hpos > 0 ? on : off);
		} else if (i == TB_FWD) {
			p[0].x = cx + s; p[0].y = cy; p[1].x = cx - s * 2 / 3; p[1].y = cy - s; p[2].x = cx - s * 2 / 3; p[2].y = cy + s;
			draw_poly(hdc, p, 3, g_hpos < g_nhist - 1 ? on : off);
		} else if (i == TB_RELOAD) {
			if (g_loading) {
				/* X */
				int t = s / 3 + 1;
				p[0].x = cx - s; p[0].y = cy - s + t; p[1].x = cx - s + t; p[1].y = cy - s; p[2].x = cx + s; p[2].y = cy + s - t; p[3].x = cx + s - t; p[3].y = cy + s;
				draw_poly(hdc, p, 4, RGB(190, 40, 40));
				p[0].x = cx + s; p[0].y = cy - s + t; p[1].x = cx + s - t; p[1].y = cy - s; p[2].x = cx - s; p[2].y = cy + s - t; p[3].x = cx - s + t; p[3].y = cy + s;
				draw_poly(hdc, p, 4, RGB(190, 40, 40));
			} else {
				/* circular arrow: ring made of a polygon */
				static const int cs[13][2] = { {0,-100},{50,-87},{87,-50},{100,0},{87,50},{50,87},{0,100},{-50,87},{-87,50},{-100,0},{-87,-50},{-50,-87},{-20,-98} };
				POINT ring[26];
				int k, ro = s, ri = s * 6 / 10;
				for (k = 0; k < 12; k++) { ring[k].x = cx + cs[k + 1][0] * ro / 100; ring[k].y = cy + cs[k + 1][1] * ro / 100; }
				for (k = 0; k < 12; k++) { ring[12 + k].x = cx + cs[12 - k][0] * ri / 100; ring[12 + k].y = cy + cs[12 - k][1] * ri / 100; }
				draw_poly(hdc, ring, 24, on);
				p[0].x = cx - s / 4; p[0].y = cy - s - s / 2; p[1].x = cx + s / 2; p[1].y = cy - (ro + ri) / 2; p[2].x = cx - s / 4; p[2].y = cy - s / 3;
				draw_poly(hdc, p, 3, on);
			}
		} else {
			int k, t = s / 3 + 1;
			for (k = -1; k <= 1; k++) {
				RECT rr;
				HBRUSH b2 = CreateSolidBrush(on);
				rr.left = cx - s; rr.right = cx + s; rr.top = cy + k * s * 2 / 3 - t / 2; rr.bottom = rr.top + t;
				FillRect(hdc, &rr, b2);
				DeleteObject(b2);
			}
		}
	}
}

static void draw_status(HDC hdc, RECT *cr)
{
	RECT r = *cr;
	HBRUSH bg = CreateSolidBrush(g_loading ? DK(RGB(255, 246, 214), RGB(70, 60, 30)) : DK(RGB(236, 238, 242), RGB(34, 36, 42)));
	WCHAR *w;
	r.top = r.bottom - g_status_h;
	FillRect(hdc, &r, bg);
	DeleteObject(bg);
	SelectObject(hdc, font_for(0));
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, DK(RGB(60, 60, 70), RGB(175, 178, 188)));
	r.left += SC(3); r.right -= SC(3);
	w = utf8_to_w(g_status, -1, NULL);
	if (g_doc && !g_loading && g_tlsinfo[0]) {
		RECT r2 = r;
		SetTextColor(hdc, DK(RGB(0, 120, 40), RGB(90, 200, 120)));
		DrawTextW(hdc, L"\x25A0", 1, &r2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		r.left += SC(12);
		SetTextColor(hdc, DK(RGB(60, 60, 70), RGB(175, 178, 188)));
	}
	DrawTextW(hdc, w, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	free(w);
}

static void layout_children(void)
{
	RECT r;
	int bw = tb_btn_w(), pad = SC(3);
	GetClientRect(g_hwnd, &r);
	MoveWindow(g_addr, bw * 3 + pad, pad, r.right - bw * 4 - pad * 2, g_tb_h - pad * 2, TRUE);
	MoveWindow(g_view, 0, g_tb_h, r.right, r.bottom - g_tb_h - g_status_h, TRUE);
}

static void apply_font_setting(void)
{
	fonts_set_base(SC(g_fontsz[g_set_font]));
	SendMessageW(g_addr, WM_SETFONT, (WPARAM)font_for(1 << S_SZ_SH), TRUE);
	g_tb_h = font_height(1 << S_SZ_SH) + SC(12);
	if (g_tb_h < SC(28)) g_tb_h = SC(28);
	g_status_h = font_height(0) + SC(4);
	layout_children();
	relayout(1);
	InvalidateRect(g_hwnd, NULL, TRUE);
}

static void go_hist(int delta)
{
	int np = g_hpos + delta;
	if (np < 0 || np >= g_nhist) return;
	if (g_hpos >= 0) g_hist[g_hpos].scroll = g_scroll;
	g_hpos = np;
	navigate(g_hist[np].url, NULL, 0, NULL, NAV_HIST);
	toolbar_invalidate();
}


/* ---------------- translation (Google Translate, page text -> Korean) ---------------- */
typedef struct {
	Doc *doc;            /* reference held */
	LONG gen;
	NetCtl ctl;
	int nrun;
	int *first, *count;  /* item range of each run */
	char **src;          /* UTF-8 source text per run (NULL = leave as is) */
	char **dst;          /* translated text per run */
	char err[160];
} TrJob;
static TrJob *g_tr;      /* running translation, or NULL */

static int tr_is_hangul(WCHAR c) { return (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0x3131 && c <= 0x318E); }
static int tr_is_latin(WCHAR c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0x24F) || (c >= 0x400 && c <= 0x4FF); }

/* does the page look foreign (mostly non-Korean letters)? */
static int doc_is_foreign(Doc *d)
{
	int i, lat = 0, han = 0, other = 0;
	for (i = 0; i < d->text.len; i++) {
		WCHAR c = d->text.p[i];
		if (tr_is_hangul(c)) han++;
		else if (tr_is_latin(c)) lat++;
		else if ((c >= 0x3040 && c <= 0x30FF) || (c >= 0x4E00 && c <= 0x9FFF)) other++;
	}
	return (lat + other) > 150 && han * 8 < lat + other;
}

/* parse Google's [[["번역","original",...],...],...] reply: concatenate the translations */
static char *tr_parse(const char *js, int n)
{
	Buf out;
	int i = 0, depth = 0, seg_first = 0;
	buf_init(&out);
	while (i < n) {
		char c = js[i];
		if (c == '[') { depth++; if (depth == 3) seg_first = 1; i++; continue; }
		if (c == ']') { depth--; i++; if (depth == 1) break; continue; }
		if (c == ',') { if (depth == 3) seg_first = 0; i++; continue; }
		if (c == '"') {
			Buf str;
			buf_init(&str);
			i++;
			while (i < n && js[i] != '"') {
				if (js[i] == '\\' && i + 1 < n) {
					char e = js[i + 1];
					i += 2;
					if (e == 'n') buf_addc(&str, '\n');
					else if (e == 't') buf_addc(&str, '\t');
					else if (e == 'u' && i + 4 <= n) {
						char hx[5]; unsigned v; WCHAR w[2]; char *u8;
						memcpy(hx, js + i, 4); hx[4] = 0;
						v = (unsigned)strtoul(hx, NULL, 16);
						i += 4;
						w[0] = (WCHAR)v; w[1] = 0;
						u8 = w_to_utf8(w, 1);
						buf_adds(&str, u8);
						free(u8);
					} else buf_addc(&str, e);
				} else buf_addc(&str, js[i++]);
			}
			i++;
			if (depth == 3 && seg_first && str.p) buf_adds(&out, str.p);
			if (depth == 3) seg_first = 0;
			buf_free(&str);
			continue;
		}
		i++;
	}
	return out.len ? buf_detach(&out) : (buf_free(&out), (char *)NULL);
}

static char *tr_request(const char *text, NetCtl *ctl, char *err)
{
	HttpResp r;
	char *enc = url_encode(text), *res = NULL;
	Buf body;
	buf_init(&body);
	buf_adds(&body, "q=");
	buf_adds(&body, enc);
	free(enc);
	if (http_request("https://translate.googleapis.com/translate_a/single?client=gtx&sl=auto&tl=ko&dt=t&ie=UTF-8&oe=UTF-8",
	                 "POST", body.p, body.len, "application/x-www-form-urlencoded", NULL, &r, ctl, 2 * 1024 * 1024)) {
		if (r.status == 200 && r.body.p) res = tr_parse(r.body.p, r.body.len);
		else if (r.status == 429 || (r.url && strstr(r.url, "/sorry"))) strcpy(err, "번역 서버가 잠시 요청을 막았습니다. 조금 뒤 다시 해 보세요.");
		else _snprintf(err, 150, "번역 실패 (HTTP %d)", r.status);
	} else _snprintf(err, 150, "번역 서버 연결 실패: %.100s", r.err);
	http_resp_free(&r);
	buf_free(&body);
	return res;
}

static int count_lines(const char *s) { int n = 1; for (; *s; s++) if (*s == '\n') n++; return n; }

static DWORD WINAPI tr_thread(LPVOID p)
{
	TrJob *t = (TrJob *)p;
	int i = 0, done = 0, total = 0;
	for (i = 0; i < t->nrun; i++) if (t->src[i]) total++;
	i = 0;
	while (i < t->nrun && !t->ctl.abort && t->gen == g_gen) {
		/* batch: runs joined by newlines, up to ~1800 characters */
		Buf b;
		int start = i, k, nb = 0;
		char *res;
		buf_init(&b);
		while (i < t->nrun && (b.len < 1800 || nb == 0)) {
			if (t->src[i]) {
				if (nb) buf_addc(&b, '\n');
				buf_adds(&b, t->src[i]);
				nb++;
			}
			i++;
		}
		if (!nb) { buf_free(&b); continue; }
		res = tr_request(b.p, &t->ctl, t->err);
		buf_free(&b);
		if (!res) break;
		if (count_lines(res) == nb) {
			char *line = res;
			for (k = start; k < i; k++) {
				char *nl;
				if (!t->src[k]) continue;
				nl = strchr(line, '\n');
				t->dst[k] = nl ? xstrndup(line, (int)(nl - line)) : xstrdup(line);
				line = nl ? nl + 1 : line + strlen(line);
			}
		} else {
			/* line structure got lost: translate those runs one by one */
			for (k = start; k < i && !t->ctl.abort; k++) {
				char *one;
				if (!t->src[k]) continue;
				one = tr_request(t->src[k], &t->ctl, t->err);
				if (one) { char *c; for (c = one; *c; c++) if (*c == '\n') *c = ' '; t->dst[k] = one; }
			}
		}
		free(res);
		done += nb;
		if (t->gen == g_gen) PostMessageW(g_hwnd, WM_APP_TRPROG, (WPARAM)done, (LPARAM)total);
	}
	PostMessageW(g_hwnd, WM_APP_TRDONE, 0, (LPARAM)t);
	return 0;
}

static void tr_free(TrJob *t)
{
	int i;
	for (i = 0; i < t->nrun; i++) { free(t->src[i]); free(t->dst[i]); }
	free(t->src); free(t->dst); free(t->first); free(t->count);
	doc_release(t->doc);
	free(t);
}

static void translate_page(void)
{
	Doc *d = g_doc;
	TrJob *t;
	int i, cap = 64;
	HANDLE th;
	if (!d || (g_tr && g_tr->gen == g_gen) || d->translated || !strncmp(d->url, "about:", 6)) return;
	t = (TrJob *)calloc(1, sizeof(TrJob));
	t->first = (int *)malloc(cap * sizeof(int));
	t->count = (int *)malloc(cap * sizeof(int));
	t->src = (char **)malloc(cap * sizeof(char *));
	/* runs = consecutive text items (one paragraph / line of text) */
	for (i = 0; i < d->nitems; ) {
		int j = i, lat = 0, han = 0, k;
		WBuf w;
		if (d->items[i].type != IT_TEXT) { i++; continue; }
		while (j < d->nitems && (d->items[j].type == IT_TEXT || d->items[j].type == IT_ANCHOR)) j++;
		wbuf_init(&w);
		for (k = i; k < j; k++) if (d->items[k].type == IT_TEXT) {
			int q;
			for (q = 0; q < d->items[k].len; q++) {
				WCHAR c = d->text.p[d->items[k].text + q];
				if (c == '\n' || c == '\r') c = ' ';
				if (tr_is_hangul(c)) han++; else if (tr_is_latin(c)) lat++;
				wbuf_addc(&w, c);
			}
		}
		if (t->nrun == cap) {
			cap *= 2;
			t->first = (int *)realloc(t->first, cap * sizeof(int));
			t->count = (int *)realloc(t->count, cap * sizeof(int));
			t->src = (char **)realloc(t->src, cap * sizeof(char *));
		}
		t->first[t->nrun] = i;
		t->count[t->nrun] = j - i;
		t->src[t->nrun] = (lat >= 2 && han * 3 < lat && w.len) ? w_to_utf8(w.p, w.len) : NULL;
		t->nrun++;
		wbuf_free(&w);
		i = j;
	}
	t->dst = (char **)calloc(t->nrun ? t->nrun : 1, sizeof(char *));
	t->doc = d;
	doc_addref(d);
	t->gen = g_gen;
	t->ctl.sock = (unsigned int)INVALID_SOCKET;
	g_tr = t;
	g_trctl = &t->ctl;
	status_set("번역 중...");
	th = CreateThread(NULL, THREAD_STACK, tr_thread, t, THREAD_FLAGS, NULL);
	if (th) CloseHandle(th);
	else { g_tr = NULL; g_trctl = NULL; tr_free(t); status_set("번역을 시작하지 못했습니다"); }
}

static void tr_apply(TrJob *t)
{
	Doc *d = t->doc;
	int r, k, n = 0;
	if (g_tr == t) g_tr = NULL;
	if (g_trctl == &t->ctl) g_trctl = NULL;
	if (d != g_doc || t->gen != g_gen) { tr_free(t); return; }
	for (r = 0; r < t->nrun; r++) {
		WCHAR *w;
		int wl, link = -2, first = -1;
		unsigned short style = 0;
		if (!t->dst[r]) continue;
		w = utf8_to_w(t->dst[r], -1, &wl);
		if (!w) continue;
		for (k = t->first[r]; k < t->first[r] + t->count[r]; k++) {
			Item *it = &d->items[k];
			if (it->type != IT_TEXT) continue;
			if (first < 0) { first = k; style = it->style; }
			if (it->len > 0 && !(it->len == 1 && d->text.p[it->text] == ' ')) {
				if (link == -2) link = it->link; else if (link != it->link) link = -1;
			}
		}
		if (first < 0) { free(w); continue; }
		for (k = t->first[r]; k < t->first[r] + t->count[r]; k++) if (d->items[k].type == IT_TEXT) d->items[k].len = 0;
		{
			Item *it = &d->items[first];
			it->text = d->text.len;
			wbuf_add(&d->text, w, wl);
			it->len = wl;
			it->link = link < 0 ? -1 : link;
			it->style = it->link >= 0 ? style : (unsigned short)(style & ~(S_LINK | S_U));
		}
		free(w);
		n++;
	}
	if (n) {
		d->translated = 1;
		relayout(1);
		status_set(t->err[0] ? "일부만 번역됨 - 메뉴 > 원문 보기로 되돌릴 수 있음" : "번역 완료 (메뉴 > 원문 보기로 되돌리기)");
	} else if (t->err[0]) {
		WCHAR *e = utf8_to_w(t->err, -1, NULL);
		status_set(t->err);
		MessageBoxW(g_hwnd, e, L"번역", MB_OK | MB_ICONEXCLAMATION);
		free(e);
	} else status_set("번역할 외국어 글이 없습니다");
	tr_free(t);
}

static void show_menu(void)
{
	HMENU m = CreatePopupMenu(), fm = CreatePopupMenu(), um = CreatePopupMenu();
	RECT r;
	POINT pt;
	int cmd, i;
	static const WCHAR *fonts[4] = { L"\xC791\xAC8C", L"\xBCF4\xD1B5", L"\xD06C\xAC8C", L"\xC544\xC8FC \xD06C\xAC8C" };
	static const WCHAR *uas[4] = { L"\xD53C\xCC98\xD3F0 (\xAC00\xC7A5 \xAC00\xBCBC\xC6C0)", L"\xBAA8\xBC14\xC77C (\xC548\xB4DC\xB85C\xC774\xB4DC)", L"\xB370\xC2A4\xD06C\xD1B1 (PC)", L"IE Mobile" };
	for (i = 0; i < 4; i++) AppendMenuW(fm, MF_STRING | (g_set_font == i ? MF_CHECKED : 0), M_FONT0 + i, fonts[i]);
	for (i = 0; i < 4; i++) AppendMenuW(um, MF_STRING | (g_set_ua == i ? MF_CHECKED : 0), M_UA0 + i, uas[i]);
	AppendMenuW(m, MF_STRING, M_HOME, L"\xD648");
	AppendMenuW(m, MF_STRING, M_RELOAD, L"\xC0C8\xB85C\xACE0\xCE68");
	if (g_doc && g_doc->translated) AppendMenuW(m, MF_STRING, M_TRANSLATE, L"원문 보기 (번역 끄기)");
	else AppendMenuW(m, MF_STRING | (g_trctl ? MF_GRAYED : 0), M_TRANSLATE, g_trctl ? L"번역 중..." : L"한국어로 번역");
	AppendMenuW(m, MF_STRING | (g_autotr ? MF_CHECKED : 0), M_AUTOTR, L"외국어 페이지 자동 번역");
	AppendMenuW(m, MF_SEPARATOR, 0, NULL);
	AppendMenuW(m, MF_STRING, M_ADDBM, L"\xC990\xACA8\xCC3E\xAE30 \xCD94\xAC00");
	AppendMenuW(m, MF_STRING, M_FILES, L"내 파일함 (GitHub)");
	AppendMenuW(m, MF_STRING, M_ADDSC, L"바로가기에 추가");
	AppendMenuW(m, MF_STRING, M_EDITSC, L"바로가기 편집");
	AppendMenuW(m, MF_STRING, M_BOOKMARKS, L"\xC990\xACA8\xCC3E\xAE30 \xBAA9\xB85D");
	AppendMenuW(m, MF_STRING, M_HISTORY, L"\xBC29\xBB38 \xAE30\xB85D");
	AppendMenuW(m, MF_SEPARATOR, 0, NULL);
	AppendMenuW(m, MF_STRING, M_SIMPLE, L"\xB2E8\xC21C\xD654 \xBCF4\xAE30 (FrogFind)");
	AppendMenuW(m, MF_STRING | (g_dark ? MF_CHECKED : 0), M_DARK, L"다크 모드 (어두운 화면)");
	AppendMenuW(m, MF_STRING | (g_images_on ? MF_CHECKED : 0), M_IMAGES, L"\xC774\xBBF8\xC9C0 \xD45C\xC2DC");
	AppendMenuW(m, MF_POPUP, (UINT_PTR)fm, L"\xAE00\xC790 \xD06C\xAE30");
	AppendMenuW(m, MF_POPUP, (UINT_PTR)um, L"\xBE0C\xB77C\xC6B0\xC800 \xC2DD\xBCC4");
	AppendMenuW(m, MF_STRING, M_TOP, L"\xB9E8 \xC704\xB85C");
	AppendMenuW(m, MF_STRING, M_BOTTOM, L"\xB9E8 \xC544\xB798\xB85C");
	AppendMenuW(m, MF_SEPARATOR, 0, NULL);
	AppendMenuW(m, MF_STRING, M_INFO, L"\xD398\xC774\xC9C0 \xC815\xBCF4");
	AppendMenuW(m, MF_STRING, M_COOKIES, L"\xCFE0\xD0A4 \xC0AD\xC81C");
	AppendMenuW(m, MF_STRING, M_EXIT, L"\xC885\xB8CC");
	GetClientRect(g_hwnd, &r);
	pt.x = r.right; pt.y = g_tb_h;
	ClientToScreen(g_hwnd, &pt);
	cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, pt.x, pt.y, g_hwnd, NULL);
	DestroyMenu(m);
	if (cmd) SendMessageW(g_hwnd, WM_COMMAND, cmd, 0);
}

static LRESULT CALLBACK addr_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WM_KEYDOWN && w == VK_RETURN) {
		int n = GetWindowTextLengthW(h);
		WCHAR *t = (WCHAR *)malloc((n + 1) * sizeof(WCHAR));
		char *u, *fx;
		GetWindowTextW(h, t, n + 1);
		u = w_to_utf8(t, -1);
		fx = url_fixup(u);
		free(t); free(u);
		sip_show(0);
		SetFocus(g_view);
		navigate(fx, NULL, 0, NULL, NAV_NEW);
		free(fx);
		return 0;
	}
	if (m == WM_CHAR && (w == VK_RETURN || w == '\n')) return 0;
	if (m == WM_SETFOCUS) {
		LRESULT r = CallWindowProcW(g_addr_proc, h, m, w, l);
		PostMessageW(h, EM_SETSEL, 0, -1);
		sip_show(1);
		return r;
	}
	if (m == WM_KILLFOCUS) {
		sip_show(0);
		if (g_doc && !g_loading) addr_set(g_doc->url);
	}
	return CallWindowProcW(g_addr_proc, h, m, w, l);
}

static void page_info(void)
{
	WCHAR msg[1200];
	WCHAR *u, *t, *tl;
	char ua[80];
	if (!g_doc) return;
	u = utf8_to_w(g_doc->url, -1, NULL);
	t = g_doc->title ? xwcsdup(g_doc->title) : xwcsdup(L"");
	tl = utf8_to_w(g_tlsinfo[0] ? g_tlsinfo : "\xEC\x95\x94\xED\x98\xB8\xED\x99\x94 \xEC\x95\x88 \xEB\x90\xA8", -1, NULL);
	_snprintf(ua, sizeof ua, "%d", g_doc->cp);
	wsprintfW(msg, L"\xC81C\xBAA9: %.200s\n\n\xC8FC\xC18C: %.500s\n\n\xBCF4\xC548: %s\n\xBB38\xC790\xC14B: %d\n\xB9C1\xD06C %d\xAC1C, \xC774\xBBF8\xC9C0 %d\xAC1C",
	          t, u, tl, g_doc->cp, g_doc->nlinks, g_doc->nimgs);
	MessageBoxW(g_hwnd, msg, L"\xD398\xC774\xC9C0 \xC815\xBCF4", MB_OK);
	free(u); free(t); free(tl);
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
		wc.hbrBackground = NULL;
		RegisterClassW(&wc);
		g_hwnd = h;
		g_addr = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 10, 10, h, (HMENU)ID_ADDR, g_inst, NULL);
		g_addr_proc = (WNDPROC)SetWindowLongW(g_addr, GWL_WNDPROC, (LONG)addr_proc);
		g_view = CreateWindowExW(0, VIEWCLASS, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, NULL, g_inst, NULL);
		return 0;
	}
	case WM_SIZE:
		layout_children();
		InvalidateRect(h, NULL, FALSE);
		return 0;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(h, &ps);
		RECT r;
		GetClientRect(h, &r);
		draw_toolbar(hdc, r.right);
		draw_status(hdc, &r);
		EndPaint(h, &ps);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_LBUTTONDOWN: {
		RECT r, cr;
		POINT pt;
		int i;
		pt.x = (short)LOWORD(l); pt.y = (short)HIWORD(l);
		GetClientRect(h, &cr);
		for (i = 0; i < 4; i++) {
			tb_rect(i, &r, cr.right);
			if (PtInRect(&r, pt)) {
				if (i == TB_BACK) go_hist(-1);
				else if (i == TB_FWD) go_hist(1);
				else if (i == TB_RELOAD) { if (g_loading) { stop_all(); status_set("\xEC\xB7\xA8\xEC\x86\x8C\xEB\x90\xA8"); } else if (g_doc) navigate(g_doc->url, NULL, 0, NULL, NAV_RELOAD); }
				else show_menu();
				return 0;
			}
		}
		return 0;
	}
	case WM_COMMAND:
		switch (LOWORD(w)) {
		case ID_FIELD:
			if (HIWORD(w) == 0xFFFF && g_field && GetFocus() != g_field) field_commit(1);
			return 0;
		case ID_PICKER:
			if (HIWORD(w) == 0xFFFF) picker_close();
			return 0;
		case M_HOME: navigate("about:home", NULL, 0, NULL, NAV_NEW); return 0;
		case M_RELOAD: if (g_doc) navigate(g_doc->url, NULL, 0, NULL, NAV_RELOAD); return 0;
		case M_ADDBM:
			if (g_doc && strncmp(g_doc->url, "about:", 6)) {
				char *t = g_doc->title ? w_to_utf8(g_doc->title, -1) : xstrdup(g_doc->url);
				{ char *c; for (c = t; *c; c++) if (*c == '\t' || *c == '\n' || *c == '\r') *c = ' '; }
				g_bm = (Mark *)realloc(g_bm, (g_nbm + 1) * sizeof(Mark));
				g_bm[g_nbm].url = xstrdup(g_doc->url);
				g_bm[g_nbm].title = t;
				g_nbm++;
				marks_save(L"bookmarks.txt", g_bm, g_nbm);
				status_set("\xEC\xA6\x90\xEA\xB2\xA8\xEC\xB0\xBE\xEA\xB8\xB0\xEC\x97\x90 \xEC\xB6\x94\xEA\xB0\x80\xED\x95\x98\xEC\x98\x80\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4");
			}
			return 0;
		case M_ADDSC:
			if (g_doc && strncmp(g_doc->url, "about:", 6)) {
				char *t = g_doc->title && g_doc->title[0] ? w_to_utf8(g_doc->title, -1) : xstrdup(g_doc->url);
				marks_add(&g_sc, &g_nsc, g_doc->url, t);
				free(t);
				marks_save(L"shortcuts.txt", g_sc, g_nsc);
				status_set("바로가기에 추가했습니다 (홈 화면에 표시)");
			}
			return 0;
		case M_EDITSC: navigate("about:shortcuts", NULL, 0, NULL, NAV_NEW); return 0;
		case M_FILES: navigate("about:files", NULL, 0, NULL, NAV_NEW); return 0;
		case M_TRANSLATE:
			if (g_doc && g_doc->translated) { g_tr_skip_auto = 1; navigate(g_doc->url, NULL, 0, NULL, NAV_RELOAD); }
			else translate_page();
			return 0;
		case M_DARK:
			g_dark = !g_dark;
			settings_save();
			if (g_ctl_br) { DeleteObject(g_ctl_br); g_ctl_br = NULL; }
			InvalidateRect(h, NULL, TRUE);
			InvalidateRect(g_view, NULL, FALSE);
			InvalidateRect(g_addr, NULL, TRUE);
			return 0;
		case M_AUTOTR:
			g_autotr = !g_autotr;
			settings_save();
			if (g_autotr && g_doc && !g_doc->translated && strncmp(g_doc->url, "about:", 6) && doc_is_foreign(g_doc)) translate_page();
			return 0;
		case M_BOOKMARKS: navigate("about:bookmarks", NULL, 0, NULL, NAV_NEW); return 0;
		case M_HISTORY: navigate("about:history", NULL, 0, NULL, NAV_NEW); return 0;
		case M_SIMPLE: if (g_doc && strncmp(g_doc->url, "about:", 6)) open_simplified(g_doc->url); return 0;
		case M_IMAGES:
			g_images_on = !g_images_on;
			settings_save();
			relayout(1);
			if (g_images_on) start_images();
			return 0;
		case M_FONT0: case M_FONT1: case M_FONT2: case M_FONT3:
			g_set_font = LOWORD(w) - M_FONT0;
			settings_save();
			apply_font_setting();
			return 0;
		case M_UA0: case M_UA1: case M_UA2: case M_UA3:
			g_set_ua = LOWORD(w) - M_UA0;
			net_set_ua(g_uas[g_set_ua]);
			settings_save();
			if (g_doc && strncmp(g_doc->url, "about:", 6)) navigate(g_doc->url, NULL, 0, NULL, NAV_RELOAD);
			return 0;
		case M_TOP: set_scroll(0); return 0;
		case M_TOP - 100: case M_GOBACK: go_hist(-1); return 0;
		case M_SOFTMENU: show_menu(); return 0;
		case M_BOTTOM: set_scroll(g_lay.height); return 0;
		case M_INFO: page_info(); return 0;
		case M_COOKIES:
			if (MessageBoxW(h, L"\xBAA8\xB4E0 \xCFE0\xD0A4(\xB85C\xADF8\xC778 \xC815\xBCF4)\xB97C \xC0AD\xC81C\xD560\xAE4C\xC694?", APPNAME, MB_YESNO) == IDYES) cookies_clear();
			return 0;
		case M_EXIT: DestroyWindow(h); return 0;
		}
		break;
	case WM_TIMER:
		if (w == TM_KINETIC) {
			if (abs(g_vel) < 16 || g_down) { KillTimer(h, TM_KINETIC); g_vel = 0; return 0; }
			{
				int before = g_scroll;
				set_scroll(g_scroll + g_vel / 16);
				if (g_scroll == before) { KillTimer(h, TM_KINETIC); g_vel = 0; }
			}
			g_vel = g_vel * 92 / 100;
		} else if (w == TM_RELAYOUT) {
			KillTimer(h, TM_RELAYOUT);
			relayout(1);
		} else if (w == TM_REFRESH) {
			KillTimer(h, TM_REFRESH);
			if (g_doc && g_doc->refresh_url && strcmp(g_doc->refresh_url, g_doc->url))
				navigate(g_doc->refresh_url, NULL, 0, NULL, NAV_RELOAD);
		} else if (w == TM_HOLD) {
			KillTimer(h, TM_HOLD);
			if (g_down && !g_dragging && g_hold_link >= 0) {
				int link = g_hold_link;
				g_down = 0;
				ReleaseCapture();
				g_press_link = g_press_ctl = -1;
				InvalidateRect(g_view, NULL, FALSE);
				link_menu(link, g_down_x, g_down_y);
			}
		}
		return 0;
	case WM_APP_LOADED:
		on_loaded((Job *)l);
		return 0;
	case WM_APP_PROGRESS: {
		char s[80];
		if ((int)l == -9) _snprintf(s, sizeof s, "본문이 비어 있어 원본 글을 받는 중...");
		else if ((int)l <= -3) _snprintf(s, sizeof s, "본문이 비어 있어 다시 받는 중... (%d)", -2 - (int)l);
		else if ((int)l > 0) _snprintf(s, sizeof s, "\xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEB\x8A\x94 \xEC\xA4\x91... %d / %d KB", (int)w / 1024, (int)l / 1024);
		else _snprintf(s, sizeof s, "\xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEB\x8A\x94 \xEC\xA4\x91... %d KB", (int)w / 1024);
		status_set(s);
		return 0;
	}
	case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORSTATIC:
		if (g_dark) {
			if (!g_ctl_br) g_ctl_br = CreateSolidBrush(RGB(40, 42, 48));
			SetTextColor((HDC)w, RGB(225, 225, 222));
			SetBkColor((HDC)w, RGB(40, 42, 48));
			return (LRESULT)g_ctl_br;
		}
		break;
	case WM_APP_TRPROG: {
		char st[80];
		if (g_tr) { _snprintf(st, sizeof st, "번역 중... %d / %d", (int)w, (int)l); status_set(st); }
		return 0;
	}
	case WM_APP_TRDONE:
		tr_apply((TrJob *)l);
		return 0;
	case WM_APP_IMGDONE:
		if ((LONG)w == g_gen) SetTimer(h, TM_RELAYOUT, 300, NULL);
		return 0;
	case WM_SETTINGCHANGE:
		if (w == 224 /* SPI_SETSIPINFO */ || w == SPI_SETWORKAREA) fit_window();
		break;
	case WM_ACTIVATE:
		if (LOWORD(w) != WA_INACTIVE) { fit_window(); softkey_grab(1); }
		else softkey_grab(0);
		break;
	case WM_HOTKEY:
		if (w == HK_SOFT2) {
			if (LOWORD(l) & MOD_KEYUP) show_menu();
			return 0;
		}
		break;
	case WM_KEYDOWN: case WM_KEYUP:
		/* keys that reach the main window (focus not in the page) work like in the page */
		return SendMessageW(g_view, m, w, l);
	case WM_CLOSE:
		DestroyWindow(h);
		return 0;
	case WM_DESTROY:
		stop_all();
		settings_save();
		marks_save(L"history.txt", g_vis, g_nvis);
		PostQuitMessage(0);
		return 0;
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
	(void)hp;
	prev = FindWindowW(APPNAME, NULL);
	if (prev) { SetForegroundWindow((HWND)((ULONG_PTR)prev | 1)); return 0; }
	g_inst = hi;
	dc = GetDC(NULL);
	g_scale100 = GetDeviceCaps(dc, LOGPIXELSY) * 100 / 96;
	ReleaseDC(NULL, dc);
	if (g_scale100 < 100) g_scale100 = 100;
#ifndef _WIN32_WCE
	{
		char *env = getenv("M480_SCALE");
		if (env) g_scale100 = atoi(env);
	}
#endif
	settings_load();
	if (!g_token[0]) load_token_file();
	fonts_set_base(SC(g_fontsz[g_set_font]));
	g_tb_h = font_height(1 << S_SZ_SH) + SC(12);
	if (g_tb_h < SC(28)) g_tb_h = SC(28);
	g_status_h = font_height(0) + SC(4);
	net_init();
	net_set_ua(g_uas[g_set_ua]);
	apply_auth();
	marks_load(L"bookmarks.txt", &g_bm, &g_nbm);
	marks_load(L"history.txt", &g_vis, &g_nvis);
	{
		WCHAR p[MAX_PATH];
		FILE *f;
		app_path(p, L"shortcuts.txt");
		f = _wfopen(p, L"rb");
		if (f) { fclose(f); marks_load(L"shortcuts.txt", &g_sc, &g_nsc); }
		else sc_reset();
	}
	entropy_stir(&hi, sizeof hi);

	memset(&wc, 0, sizeof wc);
	wc.lpfnWndProc = main_proc;
	wc.hInstance = hi;
	wc.lpszClassName = APPNAME;
	wc.hbrBackground = NULL;
#ifdef _WIN32_WCE
	wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
#endif
	RegisterClassW(&wc);
#ifdef _WIN32_WCE
	g_hwnd = CreateWindowW(APPNAME, APPNAME, WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hi, NULL);
	fit_window();
#else
	{
		int w = 240, hgt = 400;
		char *e = getenv("M480_SIZE");
		RECT r;
		if (e) sscanf(e, "%dx%d", &w, &hgt);
		r.left = 0; r.top = 0; r.right = w; r.bottom = hgt;
		AdjustWindowRect(&r, WS_POPUP | WS_BORDER, FALSE);
		g_hwnd = CreateWindowW(APPNAME, APPNAME, WS_POPUP | WS_BORDER | WS_VISIBLE, 0, 0, r.right - r.left, r.bottom - r.top, NULL, NULL, hi, NULL);
	}
#endif
	if (!g_hwnd) return 1;
	SendMessageW(g_addr, WM_SETFONT, (WPARAM)font_for(1 << S_SZ_SH), TRUE);
	layout_children();
	ShowWindow(g_hwnd, show);
	UpdateWindow(g_hwnd);
	SetFocus(g_view);
	keys_init();
	{
		/* start page: command line URL or home */
		char *u = NULL;
		if (cmd && cmd[0]) {
			char *a = w_to_utf8(cmd, -1);
			char *s = a;
			while (*s == ' ' || *s == '"') s++;
			{ int n = (int)strlen(s); while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '"')) s[--n] = 0; }
			if (*s) u = url_fixup(s);
			free(a);
		}
		navigate(u ? u : "about:home", NULL, 0, NULL, NAV_NEW);
		free(u);
	}
	while (GetMessageW(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	net_shutdown();
	return 0;
}
