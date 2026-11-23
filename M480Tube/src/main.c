/* M480Tube - YouTube viewer for Windows Mobile 6.x (SCH-M480)
 * Search / subscriptions / channel pages as list or grid, light or dark theme,
 * playback in a full-screen player window. */
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
#define VERSION   "2.9.2"

unsigned short *thumb_decode(const unsigned char *data, int len, int tw, int th);

#define WM_APP_JOB      (WM_APP + 1)
#define WM_APP_THUMB    (WM_APP + 2)
#define WM_APP_STREAMS  (WM_APP + 3)
#define WM_APP_CONFIG   (WM_APP + 4)
#define WM_APP_PROGRESS (WM_APP + 5)
#define WM_APP_PART     (WM_APP + 6)

#define ID_EDIT 100
#define TM_PLAYER 1
#define TM_KINETIC 2
#define TM_HOLD 3

#ifdef _WIN32_WCE
#define THREAD_FLAGS 0x00010000
#define GDIFLUSH()
#else
#define THREAD_FLAGS 0
#define GDIFLUSH() GdiFlush()
#endif

enum {
	M_Q240 = 1000, M_Q144, M_Q360, M_ORI_P, M_ORI_L, M_STATS, M_LIST, M_GRID, M_DARK,
	M_IMPORT, M_UPDATE, M_SELFTEST, M_ABOUT, M_EXIT,
	M_CLEARHIST, M_SUBMGR,
	M_SORT0 = 1050, M_SORT1, M_SORT2, M_SORT3, M_SORT4, M_SORT5,
	C_PLAY = 1100, C_CHANNEL, C_SUB, C_UNSUB, C_DELHIST, C_MGR_UNSUB
};

/* ---------------- theme ---------------- */
typedef struct {
	COLORREF bg, fg, sub, sep, press, bar, barbtn, bartxt, tabbg, tabsel, tabfg, stbg, stfg, ph, morebg, morefg, editbg, editfg;
} Theme;
static const Theme g_light = {
	RGB(255,255,255), RGB(10,10,10), RGB(100,100,100), RGB(225,225,225), RGB(220,230,255),
	RGB(200,20,20), RGB(150,10,10), RGB(255,255,255), RGB(245,245,245), RGB(200,20,20), RGB(70,70,70),
	RGB(235,235,235), RGB(60,60,60), RGB(200,200,200), RGB(245,245,245), RGB(30,80,200), RGB(255,255,255), RGB(0,0,0)
};
static const Theme g_darkt = {
	RGB(18,18,18), RGB(235,235,235), RGB(160,160,160), RGB(48,48,48), RGB(55,65,90),
	RGB(33,33,33), RGB(200,25,25), RGB(255,255,255), RGB(26,26,26), RGB(235,45,45), RGB(200,200,200),
	RGB(28,28,28), RGB(170,170,170), RGB(55,55,55), RGB(30,30,30), RGB(120,170,255), RGB(50,50,50), RGB(240,240,240)
};
static const Theme *T = &g_light;
static HBRUSH g_editbr;

/* ---------------- views ---------------- */
enum { V_SEARCH, V_SUBS, V_CHANNEL, V_HIST, V_MGR };
typedef struct {
	int kind, uid;
	YtList list;
	char *query;           /* search */
	char chid[32];         /* channel */
	char *title;           /* channel name */
	int scroll;
	int busy, busy_more;
	int sort;              /* 0 default, 1 newest, 2 oldest, 3 longest, 4 shortest, 5 most viewed */
	int ord_next;
	char msg[300];         /* shown when the list is empty */
} View;

static View g_vsearch, g_vsubs, g_vhist, g_vmgr;
static int g_mgr;          /* subscription manager open */
static void apply_sort(View *v);
static void number_items(View *v, int from);
static void mgr_close(void);
static void mgr_build(void);
static void mgr_open(void);
static View *g_chan[8];
static int g_nchan;
static int g_tab;          /* 0 = search, 1 = subscriptions, 2 = watch history */
static int g_next_uid = 1;

/* ---------------- state ---------------- */
static HINSTANCE g_inst;
static HWND g_hwnd, g_edit, g_play_hwnd;
static WNDPROC g_edit_proc;
static HFONT g_font, g_fontb, g_fonts;
static int g_topbar = 32, g_tabbar = 26, g_status_h = 18;
static char g_status[300];

static int g_quality = 0, g_start_ori = ORI_LAND_LEFT, g_stats = 1, g_grid = 0, g_dark = 0;

static CRITICAL_SECTION g_lcs;     /* protects views and the thumbnail cache */
static HANDLE g_thumb_ev;
static volatile int g_quit;

/* list layout */
#define ROW_H 64
#define LTW 96
#define LTH 54
#define MORE_H 44

/* subscriptions */
typedef struct { char chid[32]; char *name; DWORD fetched; int feed; } Sub;
static Sub *g_subs;
static int g_nsubs;
static int g_subs_loaded_once;

/* thumbnail cache */
typedef struct { char id[16]; unsigned short *px; char st; int w, h; DWORD used; } TEnt;
#define TMAX 120
static TEnt g_tc[TMAX];
static int g_tw = LTW, g_th = LTH;

/* list back buffer */
static HBITMAP g_bb, g_bb_old;
static HDC g_bbdc;
static unsigned short *g_bbits;
static int g_bbw, g_bbh, g_bbstride;

/* touch */
static int g_down, g_dragging, g_down_y, g_down_x, g_last_y, g_down_scroll;
static DWORD g_last_t;
static int g_vel;
static int g_pressed = -1, g_pressed_ch;

/* player */
static Player *g_player;
static volatile int g_opening;

static View *cur(void)
{
	if (g_nchan) return g_chan[g_nchan - 1];
	if (g_mgr) return &g_vmgr;
	return g_tab == 1 ? &g_vsubs : g_tab == 2 ? &g_vhist : &g_vsearch;
}

static View *view_by_uid(int uid)
{
	int i;
	if (g_vsearch.uid == uid) return &g_vsearch;
	if (g_vsubs.uid == uid) return &g_vsubs;
	if (g_vhist.uid == uid) return &g_vhist;
	for (i = 0; i < g_nchan; i++) if (g_chan[i]->uid == uid) return g_chan[i];
	return NULL;
}

static void view_clear(View *v)
{
	yt_list_free(&v->list);
	free(v->query); v->query = NULL;
	v->scroll = 0;
	v->msg[0] = 0;
}

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
		else if (!strncmp(line, "grid=", 5)) g_grid = atoi(line + 5);
		else if (!strncmp(line, "dark=", 5)) g_dark = atoi(line + 5);
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
	fprintf(f, "quality=%d\r\norientation=%d\r\nstats=%d\r\ngrid=%d\r\ndark=%d\r\n", g_quality, g_start_ori, g_stats, g_grid, g_dark);
	fclose(f);
}

/* ---------------- subscriptions storage ---------------- */
static void subs_save(void)
{
	WCHAR p[MAX_PATH];
	FILE *f;
	int i;
	app_path(p, L"m480tube_subs.txt");
	f = _wfopen(p, L"wb");
	if (!f) return;
	for (i = 0; i < g_nsubs; i++) fprintf(f, "%s\t%s\t%d\r\n", g_subs[i].chid, g_subs[i].name ? g_subs[i].name : "", g_subs[i].feed);
	fclose(f);
}

static int sub_index(const char *chid)
{
	int i;
	for (i = 0; i < g_nsubs; i++) if (!strcmp(g_subs[i].chid, chid)) return i;
	return -1;
}

static int sub_add(const char *chid, const char *name)
{
	if (!chid || strncmp(chid, "UC", 2) || strlen(chid) >= 32 || sub_index(chid) >= 0) return 0;
	g_subs = (Sub *)realloc(g_subs, (g_nsubs + 1) * sizeof(Sub));
	memset(&g_subs[g_nsubs], 0, sizeof(Sub));
	strcpy(g_subs[g_nsubs].chid, chid);
	g_subs[g_nsubs].name = xstrdup(name ? name : chid);
	g_subs[g_nsubs].feed = 1;
	g_nsubs++;
	return 1;
}

static void sub_remove(const char *chid)
{
	int i = sub_index(chid);
	if (i < 0) return;
	free(g_subs[i].name);
	memmove(&g_subs[i], &g_subs[i + 1], (g_nsubs - i - 1) * sizeof(Sub));
	g_nsubs--;
}

static void subs_load(void)
{
	WCHAR p[MAX_PATH];
	FILE *f;
	char line[400];
	app_path(p, L"m480tube_subs.txt");
	f = _wfopen(p, L"rb");
	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		char *tab = strchr(line, '\t'), *e, *t2;
		int feed = 1;
		if (!tab) continue;
		*tab = 0;
		e = tab + 1 + strlen(tab + 1);
		while (e > tab + 1 && (e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
		t2 = strchr(tab + 1, '\t');   /* third column: in the feed (1) or not (0) */
		if (t2) { *t2 = 0; feed = atoi(t2 + 1) != 0; }
		if (sub_add(line, tab + 1)) g_subs[g_nsubs - 1].feed = feed;
	}
	fclose(f);
}

/* Reads a subscription export and subscribes to every channel id in it.
   Accepts Google Takeout CSV (any column order / name / language), UTF-8 or UTF-16.
   returns channels newly added, -1 if unreadable; *found = channel ids seen */
static int is_idch(int ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_'; }

static int subs_import_file(const WCHAR *path, int *found)
{
	FILE *f = _wfopen(path, L"rb");
	long n;
	char *d, *text, *p;
	int added = 0;
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n <= 0 || n > 2 * 1024 * 1024) { fclose(f); return -1; }
	d = (char *)malloc(n + 2);
	if (!d) { fclose(f); return -1; }
	n = (long)fread(d, 1, n, f);
	fclose(f);
	d[n] = d[n + 1] = 0;
	if (n >= 2 && (unsigned char)d[0] == 0xFF && (unsigned char)d[1] == 0xFE) {
		text = w_to_utf8((const WCHAR *)(d + 2), (n - 2) / 2);   /* UTF-16 LE */
		free(d);
		if (!text) return -1;
	} else text = d;
	p = text;
	while (*p) {
		char *e = p, *q;
		while (*e && *e != '\n') e++;
		/* channel id: UC + 22 id chars, not part of a longer word */
		for (q = p; q + 24 <= e; q++) {
			int k;
			if (q[0] != 'U' || q[1] != 'C' || (q > p && is_idch((unsigned char)q[-1]))) continue;
			for (k = 2; k < 24 && is_idch((unsigned char)q[k]); k++) {}
			if (k == 24 && (q + 24 == e || !is_idch((unsigned char)q[24]))) break;
		}
		if (q + 24 <= e) {
			char id[32], title[300];
			memcpy(id, q, 24);
			id[24] = 0;
			title[0] = 0;
			/* CSV: the title is the last column (quote aware) */
			{
				const char *a = p, *last = p, *lastend = e;
				int inq = 0;
				for (; a < e; a++) {
					if (*a == '"') inq = !inq;
					else if (*a == ',' && !inq) last = a + 1;
				}
				a = last;
				lastend = e;
				while (lastend > a && (lastend[-1] == '\r' || lastend[-1] == ' ')) lastend--;
				while (a < lastend && *a == ' ') a++;
				if (a < lastend && *a == '"' && lastend[-1] == '"' && lastend - a >= 2) { a++; lastend--; }
				if (last != p && lastend - a > 0 && lastend - a < (int)sizeof title) {
					int k, bad = 0;
					for (k = 0; k + 4 <= lastend - a; k++) if (!strncmp(a + k, "http", 4)) bad = 1;
					if (lastend - a == 24 && !strncmp(a, id, 24)) bad = 1;
					if (!bad) {
						/* "" inside quotes = one quote */
						int o = 0;
						for (k = 0; a + k < lastend && o < (int)sizeof title - 1; k++) {
							if (a[k] == '"' && a + k + 1 < lastend && a[k + 1] == '"') k++;
							title[o++] = a[k];
						}
						title[o] = 0;
					}
				}
			}
			(*found)++;
			/* imported channels start outside the feed: pick them in the manager */
			if (sub_add(id, title[0] ? title : id)) { g_subs[g_nsubs - 1].feed = 0; added++; }
		}
		p = *e ? e + 1 : e;
	}
	free(text);
	return added;
}

/* ---------------- watch history ---------------- */
#define HIST_MAX 200
static void hist_field(FILE *f, const char *s)
{
	/* tabs / newlines would break the line format */
	for (; s && *s; s++) fputc((*s == '\t' || *s == '\r' || *s == '\n') ? ' ' : *s, f);
	fputc('\t', f);
}

static void hist_save(void)
{
	WCHAR p[MAX_PATH];
	FILE *f;
	int i;
	app_path(p, L"m480tube_history.txt");
	f = _wfopen(p, L"wb");
	if (!f) return;
	for (i = 0; i < g_vhist.list.n; i++) {
		YtItem *it = &g_vhist.list.items[i];
		hist_field(f, it->id);
		hist_field(f, it->chid);
		hist_field(f, it->title);
		hist_field(f, it->channel);
		hist_field(f, it->length);
		hist_field(f, it->meta);
		fputs("\r\n", f);
	}
	fclose(f);
}

static void hist_load(void)
{
	WCHAR p[MAX_PATH];
	FILE *f;
	char line[1200];
	app_path(p, L"m480tube_history.txt");
	f = _wfopen(p, L"rb");
	if (!f) return;
	while (fgets(line, sizeof line, f) && g_vhist.list.n < HIST_MAX) {
		char *fld[6], *s = line;
		int n = 0;
		YtItem *it;
		while (n < 6) {
			char *t = strchr(s, '\t');
			if (!t) break;
			*t = 0;
			fld[n++] = s;
			s = t + 1;
		}
		if (n < 3 || !fld[0][0] || strlen(fld[0]) > 15) continue;
		g_vhist.list.items = (YtItem *)realloc(g_vhist.list.items, (g_vhist.list.n + 1) * sizeof(YtItem));
		it = &g_vhist.list.items[g_vhist.list.n++];
		memset(it, 0, sizeof *it);
		strcpy(it->id, fld[0]);
		strncpy(it->chid, fld[1], sizeof it->chid - 1);
		it->title = xstrdup(fld[2]);
		if (n > 3 && fld[3][0]) it->channel = xstrdup(fld[3]);
		if (n > 4 && fld[4][0]) it->length = xstrdup(fld[4]);
		if (n > 5 && fld[5][0]) it->meta = xstrdup(fld[5]);
	}
	fclose(f);
	number_items(&g_vhist, 0);
}

/* put a watched video at the top of the history (copy of the list item) */
static void hist_add(const YtItem *src)
{
	YtList *l = &g_vhist.list;
	YtItem it;
	int i;
	memset(&it, 0, sizeof it);
	strcpy(it.id, src->id);
	strcpy(it.chid, src->chid);
	it.title = xstrdup(src->title);
	it.channel = xstrdup(src->channel);
	it.length = xstrdup(src->length);
	it.meta = xstrdup(src->meta);
	EnterCriticalSection(&g_lcs);
	for (i = 0; i < l->n; i++) {
		if (!strcmp(l->items[i].id, it.id)) {
			free(l->items[i].title); free(l->items[i].channel); free(l->items[i].length); free(l->items[i].meta);
			memmove(&l->items[i], &l->items[i + 1], (l->n - i - 1) * sizeof(YtItem));
			l->n--;
			break;
		}
	}
	if (l->n >= HIST_MAX) {
		YtItem *o = &l->items[l->n - 1];
		free(o->title); free(o->channel); free(o->length); free(o->meta);
		l->n--;
	}
	l->items = (YtItem *)realloc(l->items, (l->n + 1) * sizeof(YtItem));
	memmove(&l->items[1], &l->items[0], l->n * sizeof(YtItem));
	l->items[0] = it;
	l->n++;
	{
		/* newest watch first in the default order */
		int k, mn = 0;
		for (k = 1; k < l->n; k++) if (l->items[k].ord < mn) mn = l->items[k].ord;
		l->items[0].ord = mn - 1;
	}
	if (g_vhist.sort) apply_sort(&g_vhist);
	LeaveCriticalSection(&g_lcs);
	hist_save();
}

static void hist_remove(int idx)
{
	YtList *l = &g_vhist.list;
	EnterCriticalSection(&g_lcs);
	if (idx >= 0 && idx < l->n) {
		free(l->items[idx].title); free(l->items[idx].channel); free(l->items[idx].length); free(l->items[idx].meta);
		memmove(&l->items[idx], &l->items[idx + 1], (l->n - idx - 1) * sizeof(YtItem));
		l->n--;
	}
	LeaveCriticalSection(&g_lcs);
	hist_save();
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

static void msgbox_u8(const char *s, UINT flags)
{
	WCHAR *w = utf8_to_w(s, -1, NULL);
	MessageBoxW(g_hwnd, w ? w : L"?", APPNAME, flags);
	free(w);
}

/* ---------------- geometry ---------------- */
static int content_top(void) { return g_topbar + g_tabbar; }
static int client_w(void) { RECT r; GetClientRect(g_hwnd, &r); return r.right; }
static int list_height(void)
{
	RECT r;
	GetClientRect(g_hwnd, &r);
	return r.bottom - content_top() - g_status_h;
}

/* grid cell geometry for width w */
static void grid_geom(int w, int *cw, int *tw, int *th, int *ch)
{
	*cw = (w - 4) / 2;
	*tw = *cw - 6;
	*th = *tw * 9 / 16;
	*ch = *th + 6 + 30 + 15 + 6;
}

static int has_more_row(View *v) { return v->kind != V_MGR && (v->list.cont || v->busy_more); }

/* ---------------- sorting ---------------- */
/* "1.2만", "3,456", "1.5M" ... -> number; *pp is advanced past it */
static double parse_num(const char **pp)
{
	const char *p = *pp;
	double v = 0, frac = 0, scale = 1;
	int seen = 0, dot = 0;
	while (*p) {
		if (*p >= '0' && *p <= '9') { if (dot) { scale /= 10; frac += (*p - '0') * scale; } else v = v * 10 + (*p - '0'); seen = 1; }
		else if (*p == '.' && !dot) dot = 1;
		else if (*p != ',') break;
		p++;
	}
	if (!seen) return -1;
	v += frac;
	while (*p == ' ') p++;
	if (!strncmp(p, "\xEC\xB2\x9C", 3)) { v *= 1e3; p += 3; }        /* 천 */
	else if (!strncmp(p, "\xEB\xA7\x8C", 3)) { v *= 1e4; p += 3; }   /* 만 */
	else if (!strncmp(p, "\xEC\x96\xB5", 3)) { v *= 1e8; p += 3; }   /* 억 */
	else if (*p == 'K' || *p == 'k') { v *= 1e3; p++; }
	else if (*p == 'M') { v *= 1e6; p++; }
	else if (*p == 'B') { v *= 1e9; p++; }
	*pp = p;
	return v;
}

static double views_of(const YtItem *it)
{
	const char *m = it->meta, *p;
	if (!m) return -1;
	p = strstr(m, "\xEC\xA1\xB0\xED\x9A\x8C\xEC\x88\x98");          /* 조회수 */
	if (p) { p += 9; while (*p == ' ') p++; return parse_num(&p); }
	p = strstr(m, "view");
	if (p) {
		/* number just before "views" */
		const char *q = p;
		while (q > m && q[-1] == ' ') q--;
		while (q > m && (strchr("0123456789.,KMBk", q[-1]))) q--;
		return parse_num(&q);
	}
	return -1;
}

static long length_of(const YtItem *it)
{
	const char *p = it->length;
	long t = 0;
	int parts = 0;
	if (!p || !(*p >= '0' && *p <= '9')) return -1;
	while (*p) {
		if (*p >= '0' && *p <= '9') { long n = 0; while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0'); t = t * 60 + n; parts++; }
		else if (*p == ':') p++;
		else break;
	}
	return parts ? t : -1;
}

/* bigger = newer. exact publish time when known, else "3일 전" / "2 weeks ago" */
static double newness_of(const YtItem *it)
{
	static const struct { const char *u; long s; } units[] = {
		{ "\xEC\xB4\x88", 1 }, { "\xEB\xB6\x84", 60 }, { "\xEC\x8B\x9C\xEA\xB0\x84", 3600 },
		{ "\xEC\x9D\xBC", 86400 }, { "\xEC\xA3\xBC", 604800 }, { "\xEA\xB0\x9C\xEC\x9B\x94", 2592000 },
		{ "\xEB\x85\x84", 31536000 }, { "second", 1 }, { "minute", 60 }, { "hour", 3600 }, { "day", 86400 },
		{ "week", 604800 }, { "month", 2592000 }, { "year", 31536000 }
	};
	const char *p;
	if (it->pub > 0) return (double)it->pub;
	for (p = it->meta; p && *p; p++) {
		if (*p >= '0' && *p <= '9' && (p == it->meta || !(p[-1] >= '0' && p[-1] <= '9') )) {
			const char *q = p;
			double n = parse_num(&q);
			unsigned i;
			while (*q == ' ') q++;
			for (i = 0; i < sizeof units / sizeof units[0]; i++)
				if (!strncmp(q, units[i].u, strlen(units[i].u)) && n >= 0)
					return 4e9 - n * units[i].s;   /* relative: newer = larger */
		}
	}
	return -1;
}

static View *g_sortv;
static int cmp_sort(const void *a, const void *b)
{
	const YtItem *x = (const YtItem *)a, *y = (const YtItem *)b;
	double kx, ky;
	switch (g_sortv->sort) {
	case 1: case 2: kx = newness_of(x); ky = newness_of(y); break;
	case 3: case 4: kx = (double)length_of(x); ky = (double)length_of(y); break;
	case 5: kx = views_of(x); ky = views_of(y); break;
	default:
		if (g_sortv->kind == V_SUBS) { kx = (double)x->pub; ky = (double)y->pub; break; }
		return x->ord - y->ord;
	}
	/* unknown values always go last */
	if (kx < 0 && ky < 0) return x->ord - y->ord;
	if (kx < 0) return 1;
	if (ky < 0) return -1;
	if (kx == ky) return x->ord - y->ord;
	if (g_sortv->sort == 2 || g_sortv->sort == 4) return kx < ky ? -1 : 1;
	return kx > ky ? -1 : 1;
}

/* call with g_lcs held */
static void apply_sort(View *v)
{
	if (v->kind == V_MGR || v->list.n < 2) return;
	g_sortv = v;
	qsort(v->list.items, v->list.n, sizeof(YtItem), cmp_sort);
}

/* numbers new items in arrival order (call with g_lcs held) */
static void number_items(View *v, int from)
{
	int i;
	for (i = from; i < v->list.n; i++) v->list.items[i].ord = v->ord_next++;
}

static const char *sort_name(int s)
{
	static const char *n[] = { "기본순", "최신순", "오래된순", "길이 긴순", "길이 짧은순", "조회수 많은순" };
	return (s >= 0 && s < 6) ? n[s] : n[0];
}

#define MGR_H 40
static int content_height(View *v)
{
	int n = v->list.n, h;
	if (v->kind == V_MGR) return n * MGR_H;
	if (g_grid) {
		int cw, tw, th, ch;
		grid_geom(client_w(), &cw, &tw, &th, &ch);
		h = ((n + 1) / 2) * ch;
	} else h = n * ROW_H;
	if (has_more_row(v)) h += g_grid ? MORE_H : ROW_H;
	return h;
}

static void clamp_scroll(void)
{
	View *v = cur();
	int maxs = content_height(v) - list_height();
	if (maxs < 0) maxs = 0;
	if (v->scroll > maxs) v->scroll = maxs;
	if (v->scroll < 0) v->scroll = 0;
}

static void list_invalidate(void)
{
	RECT r;
	GetClientRect(g_hwnd, &r);
	r.top = content_top();
	r.bottom -= g_status_h;
	InvalidateRect(g_hwnd, &r, FALSE);
}

/* item under a point (client coords). returns index, n for the "more" row, -1 none.
   *on_channel = 1 if the tap was on the uploader's name */
static int item_at(int x, int y, int *on_channel)
{
	View *v = cur();
	int ly = y - content_top() + v->scroll, n = v->list.n, idx;
	*on_channel = 0;
	if (y < content_top() || y >= content_top() + list_height() || ly < 0) return -1;
	if (v->kind == V_MGR) { idx = ly / MGR_H; return idx < n ? idx : -1; }
	if (g_grid) {
		int cw, tw, th, ch, row, col, iy;
		grid_geom(client_w(), &cw, &tw, &th, &ch);
		row = ly / ch;
		if (row * 2 >= n) return has_more_row(v) && ly < ((n + 1) / 2) * ch + MORE_H ? n : -1;
		col = x >= 2 + cw ? 1 : 0;
		idx = row * 2 + col;
		if (idx >= n) return -1;
		iy = ly - row * ch;
		if (iy >= th + 6 + 30 && iy < th + 6 + 30 + 16) *on_channel = 1;
		return idx;
	}
	idx = ly / ROW_H;
	if (idx < n) {
		int iy = ly - idx * ROW_H;
		if (x > LTW + 6 && iy >= 33 && iy < 50) *on_channel = 1;
		return idx;
	}
	if (idx == n && has_more_row(v)) return n;
	return -1;
}

/* ---------------- thumbnail cache ---------------- */
static unsigned short *tc_get(const char *id)
{
	int i, victim = -1;
	DWORD now = GetTickCount(), oldest = 0;
	for (i = 0; i < TMAX; i++) {
		if (g_tc[i].st && !strcmp(g_tc[i].id, id)) {
			g_tc[i].used = now;
			return (g_tc[i].st == 2 && g_tc[i].w == g_tw && g_tc[i].h == g_th) ? g_tc[i].px : NULL;
		}
	}
	for (i = 0; i < TMAX; i++) {
		if (!g_tc[i].st) { victim = i; break; }
		if (g_tc[i].st != 1 && (victim < 0 || now - g_tc[i].used > oldest)) { victim = i; oldest = now - g_tc[i].used; }
	}
	if (victim < 0) return NULL;
	free(g_tc[victim].px);
	memset(&g_tc[victim], 0, sizeof g_tc[victim]);
	strcpy(g_tc[victim].id, id);
	g_tc[victim].st = 3;          /* 3 = wanted */
	g_tc[victim].used = now;
	SetEvent(g_thumb_ev);
	return NULL;
}

static void tc_clear(void)
{
	int i;
	EnterCriticalSection(&g_lcs);
	for (i = 0; i < TMAX; i++) {
		if (g_tc[i].st == 1) { g_tc[i].st = 5; continue; } /* loading: drop when done */
		free(g_tc[i].px);
		memset(&g_tc[i], 0, sizeof g_tc[i]);
	}
	LeaveCriticalSection(&g_lcs);
}

static void set_thumb_size(void)
{
	if (g_grid) {
		int cw, tw, th, ch;
		grid_geom(client_w() > 0 ? client_w() : 240, &cw, &tw, &th, &ch);
		g_tw = tw; g_th = th;
	} else { g_tw = LTW; g_th = LTH; }
	tc_clear();
}

static DWORD WINAPI thumb_thread(LPVOID arg)
{
	(void)arg;
	while (!g_quit) {
		char id[16];
		int i, k = -1, tw, th;
		DWORD newest = 0;
		EnterCriticalSection(&g_lcs);
		/* most recently wanted first (= what is on screen now) */
		for (i = 0; i < TMAX; i++)
			if (g_tc[i].st == 3 && (k < 0 || g_tc[i].used > newest)) { k = i; newest = g_tc[i].used; }
		if (k >= 0) {
			strcpy(id, g_tc[k].id);
			g_tc[k].st = 1;
			/* more waiting: wake the other loader too */
			for (i = 0; i < TMAX; i++) if (g_tc[i].st == 3) { SetEvent(g_thumb_ev); break; }
		}
		tw = g_tw; th = g_th;
		LeaveCriticalSection(&g_lcs);
		if (k < 0) { WaitForSingleObject(g_thumb_ev, 2000); continue; }
		{
			char url[120];
			HttpResp r;
			NetCtl ctl;
			unsigned short *px = NULL;
			memset(&ctl, 0, sizeof ctl);
			/* list rows only need 96x54: the 120x90 'default' image is ~4x smaller to fetch and decode */
			_snprintf(url, sizeof url, "https://i.ytimg.com/vi/%s/%s", id, tw <= 120 ? "default.jpg" : "mqdefault.jpg");
			if (http_request(url, "GET", NULL, 0, NULL, NULL, &r, &ctl, 300000) && r.status == 200 && r.body.len > 0)
				px = thumb_decode((const unsigned char *)r.body.p, r.body.len, tw, th);
			http_resp_free(&r);
			EnterCriticalSection(&g_lcs);
			if (g_tc[k].st == 1 && !strcmp(g_tc[k].id, id) && tw == g_tw && th == g_th) {
				g_tc[k].px = px;
				g_tc[k].w = tw;
				g_tc[k].h = th;
				g_tc[k].st = px ? 2 : 4;
				px = NULL;
			} else if (!strcmp(g_tc[k].id, id) && (g_tc[k].st == 5 || g_tc[k].st == 1)) {
				memset(&g_tc[k], 0, sizeof g_tc[k]);
			}
			LeaveCriticalSection(&g_lcs);
			free(px);
			PostMessageW(g_hwnd, WM_APP_THUMB, 0, 0);
		}
	}
	return 0;
}

/* ---------------- background jobs ---------------- */
enum { J_SEARCH, J_CHANNEL, J_SUBS };
typedef struct {
	int type, uid, more;
	char *query, *cont, *chid;
	char *name;
	int ok;
	YtList res;
	NetCtl ctl;
	Sub *subs;
	int nsubs;
	char err[300];
} Job;

static int cmp_pub(const void *a, const void *b)
{
	const YtItem *x = (const YtItem *)a, *y = (const YtItem *)b;
	return x->pub < y->pub ? 1 : x->pub > y->pub ? -1 : 0;
}

/* one channel's newest uploads, handed to the UI thread as soon as it arrives */
typedef struct { int uid; char chid[32]; YtList items; } FeedPart;

typedef struct { Job *j; volatile LONG next, done, fails, fallbacks; } FeedRun;

#define FEED_WORKERS 2
#define FEED_FALLBACK_MAX 8   /* channel-page fallbacks per refresh (YouTube flags bursts as bots) */
static DWORD WINAPI feed_worker(LPVOID arg)
{
	FeedRun *fr = (FeedRun *)arg;
	Job *j = fr->j;
	NetCtl ctl;
	memset(&ctl, 0, sizeof ctl);
	for (;;) {
		int i = (int)InterlockedIncrement((LONG *)&fr->next) - 1, k;
		FeedPart *part;
		char *nm = NULL;
		if (i >= j->nsubs || g_quit) break;
		part = (FeedPart *)calloc(1, sizeof(FeedPart));
		part->uid = j->uid;
		strcpy(part->chid, j->subs[i].chid);
		if (i >= FEED_WORKERS) Sleep(250);   /* spread requests: bursts trigger YouTube's bot check */
		if (!yt_rss(j->subs[i].chid, &part->items, &nm, &ctl)) {
			/* feed unavailable: fall back to the channel page (a few times only) */
			yt_list_free(&part->items);
			memset(&part->items, 0, sizeof part->items);
			if (InterlockedIncrement((LONG *)&fr->fallbacks) > FEED_FALLBACK_MAX ||
			    !yt_channel(j->subs[i].chid, NULL, &part->items, &nm, &ctl)) InterlockedIncrement((LONG *)&fr->fails);
		}
		for (k = 0; k < part->items.n; k++) {
			YtItem *it = &part->items.items[k];
			if (!it->chid[0]) strncpy(it->chid, j->subs[i].chid, sizeof it->chid - 1);
			if (!it->channel) it->channel = xstrdup(nm ? nm : (j->subs[i].name ? j->subs[i].name : ""));
		}
		free(nm);
		PostMessageW(g_hwnd, WM_APP_PART, 0, (LPARAM)part);
		PostMessageW(g_hwnd, WM_APP_PROGRESS, (WPARAM)InterlockedIncrement((LONG *)&fr->done), j->nsubs);
	}
	return 0;
}

static DWORD WINAPI job_thread(LPVOID arg)
{
	Job *j = (Job *)arg;
	if (j->type == J_SEARCH) j->ok = yt_search(j->query, j->cont, &j->res, &j->ctl);
	else if (j->type == J_CHANNEL) j->ok = yt_channel(j->chid, j->cont, &j->res, &j->name, &j->ctl);
	else {
		/* subscriptions: several channels at once (each keeps its own connection) */
		FeedRun fr;
		HANDLE th[FEED_WORKERS];
		int i, n = 0;
		memset(&fr, 0, sizeof fr);
		fr.j = j;
		for (i = 0; i < FEED_WORKERS && i < j->nsubs; i++) {
			th[n] = CreateThread(NULL, 0x20000, feed_worker, &fr, THREAD_FLAGS, NULL);
			if (th[n]) n++;
		}
		if (!n) feed_worker(&fr);
		for (i = 0; i < n; i++) { WaitForSingleObject(th[i], INFINITE); CloseHandle(th[i]); }
		j->ok = 1;
		if (fr.fails) _snprintf(j->err, sizeof j->err, "채널 %d개를 불러오지 못했습니다", (int)fr.fails);
	}
	PostMessageW(g_hwnd, WM_APP_JOB, 0, (LPARAM)j);
	return 0;
}

static int job_start(Job *j)
{
	HANDLE th = CreateThread(NULL, 0x20000, job_thread, j, THREAD_FLAGS, NULL);
	if (!th) return 0;
	CloseHandle(th);
	return 1;
}

static void job_free(Job *j)
{
	int i;
	yt_list_free(&j->res);
	for (i = 0; i < j->nsubs; i++) free(j->subs[i].name);
	free(j->subs);
	free(j->query); free(j->cont); free(j->chid); free(j->name);
	free(j);
}

static void start_search(const char *q, int more)
{
	View *v = &g_vsearch;
	Job *j;
	if (v->busy || v->busy_more) return;
	if (!more) {
		EnterCriticalSection(&g_lcs);
		view_clear(v);
		v->uid = g_next_uid++;
		v->query = xstrdup(q);
		LeaveCriticalSection(&g_lcs);
	} else if (!v->list.cont) return;
	j = (Job *)calloc(1, sizeof(Job));
	j->type = J_SEARCH;
	j->uid = v->uid;
	j->more = more;
	j->query = xstrdup(v->query);
	j->cont = more ? xstrdup(v->list.cont) : NULL;
	if (more) v->busy_more = 1; else v->busy = 1;
	status_set(more ? "더 불러오는 중..." : "검색 중...");
	if (!job_start(j)) { v->busy = v->busy_more = 0; job_free(j); status_set("스레드 생성 실패"); }
	InvalidateRect(g_hwnd, NULL, FALSE);
}

static void load_channel(View *v, int more)
{
	Job *j;
	if (v->busy || v->busy_more) return;
	if (more && !v->list.cont) return;
	j = (Job *)calloc(1, sizeof(Job));
	j->type = J_CHANNEL;
	j->uid = v->uid;
	j->more = more;
	j->chid = xstrdup(v->chid);
	j->cont = more ? xstrdup(v->list.cont) : NULL;
	if (more) v->busy_more = 1; else v->busy = 1;
	status_set(more ? "더 불러오는 중..." : "채널 영상을 불러오는 중...");
	if (!job_start(j)) { v->busy = v->busy_more = 0; job_free(j); }
	InvalidateRect(g_hwnd, NULL, FALSE);
}

static void refresh_subs(void)
{
	View *v = &g_vsubs;
	Job *j;
	int i;
	if (v->busy) return;
	if (!g_nsubs) {
		EnterCriticalSection(&g_lcs);
		view_clear(v);
		LeaveCriticalSection(&g_lcs);
		strcpy(v->msg, "구독한 채널이 없습니다.\n\n영상의 채널 이름을 누르거나 영상을 길게 눌러 채널로 간 뒤 [구독]을 누르세요.\n\n유튜브 계정의 구독 목록은 메뉴 > '구독 목록 가져오기'로 옮겨 올 수 있습니다.");
		InvalidateRect(g_hwnd, NULL, FALSE);
		return;
	}
	j = (Job *)calloc(1, sizeof(Job));
	j->type = J_SUBS;
	j->uid = v->uid;
	j->subs = (Sub *)calloc(g_nsubs, sizeof(Sub));
	{
		/* YouTube refreshes these feeds every 15 minutes: channels fetched more
		   recently than that would only return what we already have */
		DWORD now = GetTickCount();
		for (i = 0; i < g_nsubs; i++) {
			if (!g_subs[i].feed) continue;
			if (g_subs[i].fetched && now - g_subs[i].fetched < 15 * 60 * 1000) continue;
			j->subs[j->nsubs] = g_subs[i];
			j->subs[j->nsubs].name = xstrdup(g_subs[i].name);
			j->nsubs++;
		}
	}
	g_subs_loaded_once = 1;
	if (!j->nsubs) {
		int nf = 0;
		for (i = 0; i < g_nsubs; i++) nf += g_subs[i].feed;
		job_free(j);
		if (!nf) {
			strcpy(v->msg, "피드에 넣은 채널이 없습니다.\n\n오른쪽 위 [관리]를 눌러\n영상을 받아 올 채널을 체크하세요.");
			status_set("구독 관리에서 채널을 고르세요");
		} else status_set("이미 최신 상태입니다 (15분마다 새로 받을 수 있어요)");
		InvalidateRect(g_hwnd, NULL, FALSE);
		return;
	}
	v->busy = 1;
	status_set("구독 피드를 불러오는 중...");
	if (!job_start(j)) { v->busy = 0; job_free(j); }
	InvalidateRect(g_hwnd, NULL, FALSE);
}

/* ---- subscription feed cache (shown immediately at start-up) ---- */
#define FEED_MAX 400
static void feed_save(void)
{
	WCHAR p[MAX_PATH];
	FILE *f;
	int i;
	app_path(p, L"m480tube_feed.txt");
	f = _wfopen(p, L"wb");
	if (!f) return;
	EnterCriticalSection(&g_lcs);
	for (i = 0; i < g_vsubs.list.n; i++) {
		YtItem *it = &g_vsubs.list.items[i];
		fprintf(f, "%ld\t", it->pub);
		hist_field(f, it->id);
		hist_field(f, it->chid);
		hist_field(f, it->title);
		hist_field(f, it->channel);
		hist_field(f, it->length);
		hist_field(f, it->meta);
		fputs("\r\n", f);
	}
	LeaveCriticalSection(&g_lcs);
	fclose(f);
}

static void feed_load(void)
{
	WCHAR p[MAX_PATH];
	FILE *f;
	char line[1200];
	app_path(p, L"m480tube_feed.txt");
	f = _wfopen(p, L"rb");
	if (!f) return;
	while (fgets(line, sizeof line, f) && g_vsubs.list.n < FEED_MAX) {
		char *fld[7], *s = line;
		int n = 0;
		YtItem *it;
		while (n < 7) {
			char *t = strchr(s, '\t');
			if (!t) break;
			*t = 0;
			fld[n++] = s;
			s = t + 1;
		}
		if (n < 4 || !fld[1][0] || strlen(fld[1]) > 15) continue;
		{
			int si = sub_index(fld[2]);
			if (si < 0 || !g_subs[si].feed) continue;   /* channel no longer subscribed / not in the feed */
		}
		g_vsubs.list.items = (YtItem *)realloc(g_vsubs.list.items, (g_vsubs.list.n + 1) * sizeof(YtItem));
		it = &g_vsubs.list.items[g_vsubs.list.n++];
		memset(it, 0, sizeof *it);
		it->pub = atol(fld[0]);
		strcpy(it->id, fld[1]);
		strncpy(it->chid, fld[2], sizeof it->chid - 1);
		it->title = xstrdup(fld[3]);
		if (n > 4 && fld[4][0]) it->channel = xstrdup(fld[4]);
		if (n > 5 && fld[5][0]) it->length = xstrdup(fld[5]);
		if (n > 6 && fld[6][0]) it->meta = xstrdup(fld[6]);
	}
	fclose(f);
	number_items(&g_vsubs, 0);
}

static void free_item(YtItem *it)
{
	free(it->title); free(it->channel); free(it->length); free(it->meta);
}

/* merge one channel's uploads into the subscription feed: new videos are added,
   ones already in the list are skipped, newest first */
static void on_feed_part(FeedPart *part)
{
	View *v = view_by_uid(part->uid);
	int i, k, si = sub_index(part->chid);
	if (si >= 0) g_subs[si].fetched = GetTickCount();
	if (v && si >= 0 && g_subs[si].feed) {
		EnterCriticalSection(&g_lcs);
		v->list.items = (YtItem *)realloc(v->list.items, (v->list.n + part->items.n + 1) * sizeof(YtItem));
		for (i = 0; i < part->items.n; i++) {
			YtItem *it = &part->items.items[i];
			for (k = 0; k < v->list.n; k++) if (!strcmp(v->list.items[k].id, it->id)) break;
			if (k < v->list.n) {
				/* already listed: refresh the view count / date text only */
				if (it->meta) { free(v->list.items[k].meta); v->list.items[k].meta = it->meta; it->meta = NULL; }
				if (it->pub) v->list.items[k].pub = it->pub;
				free_item(it);
				continue;
			}
			v->list.items[v->list.n++] = *it;
		}
		part->items.n = 0;
		if (v->list.n > 1) qsort(v->list.items, v->list.n, sizeof(YtItem), cmp_pub);
		if (v->sort) apply_sort(v);
		while (v->list.n > FEED_MAX) free_item(&v->list.items[--v->list.n]);
		v->msg[0] = 0;
		LeaveCriticalSection(&g_lcs);
		if (v == cur()) { clamp_scroll(); list_invalidate(); }
	}
	yt_list_free(&part->items);
	free(part);
}

static void merge_results(View *v, YtList *res, int replace)
{
	int i, n0;
	EnterCriticalSection(&g_lcs);
	if (replace) yt_list_free(&v->list);
	n0 = v->list.n;
	v->list.items = (YtItem *)realloc(v->list.items, (v->list.n + res->n + 1) * sizeof(YtItem));
	for (i = 0; i < res->n; i++) {
		int k, dup = 0;
		for (k = 0; k < v->list.n; k++) if (!strcmp(v->list.items[k].id, res->items[i].id)) { dup = 1; break; }
		if (dup) {
			free(res->items[i].title); free(res->items[i].channel);
			free(res->items[i].length); free(res->items[i].meta);
			continue;
		}
		v->list.items[v->list.n++] = res->items[i];
	}
	free(res->items);
	res->items = NULL;
	res->n = 0;
	free(v->list.cont);
	v->list.cont = res->cont;
	res->cont = NULL;
	number_items(v, n0);
	if (v->sort) apply_sort(v);
	LeaveCriticalSection(&g_lcs);
}

static void on_job(Job *j)
{
	View *v = view_by_uid(j->uid);
	if (v) {
		v->busy = v->busy_more = 0;
		if (j->type == J_CHANNEL && j->name && !v->title) v->title = xstrdup(j->name);
		if (j->type == J_SUBS) {
			char s[160];
			feed_save();
			if (!v->list.n) strcpy(v->msg, j->err[0] ? j->err : "구독한 채널에 영상이 없습니다");
			_snprintf(s, sizeof s, "구독 채널 %d개 · 영상 %d개%s%s", g_nsubs, v->list.n, j->err[0] ? " · " : "", j->err);
			if (v == cur()) { status_set(s); InvalidateRect(g_hwnd, NULL, FALSE); }
		} else if (!j->ok && !j->res.n) {
			const char *e = j->err[0] ? j->err : j->res.err[0] ? j->res.err : "불러오기 실패";
			if (!v->list.n) strncpy(v->msg, e, sizeof v->msg - 1);
			if (v == cur()) status_set(e);
		} else {
			char s[120];
			merge_results(v, &j->res, j->type == J_SUBS);
			v->msg[0] = 0;
			if (j->type == J_SUBS) _snprintf(s, sizeof s, "구독 채널 %d개 · 영상 %d개%s%s", g_nsubs, v->list.n, j->err[0] ? " · " : "", j->err);
			else _snprintf(s, sizeof s, "영상 %d개", v->list.n);
			if (v == cur()) status_set(g_tune.notice[0] ? g_tune.notice : s);
		}
		if (v == cur()) { clamp_scroll(); InvalidateRect(g_hwnd, NULL, FALSE); }
	}
	job_free(j);
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
		/* searching always shows the search tab */
		while (g_nchan) { View *c = g_chan[--g_nchan]; view_clear(c); free(c->title); free(c); }
		g_tab = 0;
		start_search(q, 0);
	}
	free(q);
}

/* ---------------- navigation ---------------- */
static void open_channel(const char *chid, const char *name)
{
	View *v;
	if (!chid || !chid[0]) { status_set("채널 정보가 없는 영상입니다"); return; }
	if (g_nchan == 8) { View *c = g_chan[0]; view_clear(c); free(c->title); free(c); memmove(g_chan, g_chan + 1, 7 * sizeof(View *)); g_nchan--; }
	v = (View *)calloc(1, sizeof(View));
	v->kind = V_CHANNEL;
	v->uid = g_next_uid++;
	strncpy(v->chid, chid, sizeof v->chid - 1);
	v->title = name ? xstrdup(name) : NULL;
	EnterCriticalSection(&g_lcs);
	g_chan[g_nchan++] = v;
	LeaveCriticalSection(&g_lcs);
	load_channel(v, 0);
	InvalidateRect(g_hwnd, NULL, FALSE);
}

static void go_back(void)
{
	View *v;
	if (!g_nchan) { if (g_mgr) mgr_close(); return; }
	EnterCriticalSection(&g_lcs);
	v = g_chan[--g_nchan];
	view_clear(v);
	LeaveCriticalSection(&g_lcs);
	free(v->title);
	free(v);
	status_set("");
	if (!g_nchan && g_mgr) mgr_build();   /* (un)subscribed in the channel? */
	InvalidateRect(g_hwnd, NULL, FALSE);
}

static void scroll_top(void)
{
	KillTimer(g_hwnd, TM_KINETIC);
	g_vel = 0;
	cur()->scroll = 0;
	list_invalidate();
}

static void set_tab(int t)
{
	while (g_nchan) go_back();
	if (g_mgr) mgr_close();
	g_tab = t;
	if (t == 1 && !g_subs_loaded_once) refresh_subs();
	clamp_scroll();
	InvalidateRect(g_hwnd, NULL, FALSE);
}

static void toggle_sub(const char *chid, const char *name)
{
	char s[200];
	if (sub_index(chid) >= 0) {
		sub_remove(chid);
		_snprintf(s, sizeof s, "구독 취소: %s", name ? name : chid);
	} else {
		sub_add(chid, name);
		_snprintf(s, sizeof s, "구독함: %s", name ? name : chid);
	}
	subs_save();
	{
		/* drop an unsubscribed channel's videos from the feed */
		int i, k = 0;
		EnterCriticalSection(&g_lcs);
		for (i = 0; i < g_vsubs.list.n; i++) {
			if (sub_index(g_vsubs.list.items[i].chid) < 0) free_item(&g_vsubs.list.items[i]);
			else g_vsubs.list.items[k++] = g_vsubs.list.items[i];
		}
		g_vsubs.list.n = k;
		LeaveCriticalSection(&g_lcs);
	}
	g_subs_loaded_once = 0;   /* refresh the feed next time (only new channels are fetched) */
	status_set(s);
	InvalidateRect(g_hwnd, NULL, FALSE);
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
	View *v = cur();
	if (g_opening || g_player || idx < 0 || idx >= v->list.n) return;
	j = (StreamJob *)calloc(1, sizeof(StreamJob));
	strcpy(j->id, v->list.items[idx].id);
	j->quality = g_quality;
	{
		/* copy first: adding may reorder the list we are reading from */
		YtItem copy = v->list.items[idx];
		hist_add(&copy);
	}
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
		status_set(j->s.err);
		msgbox_u8(j->s.err, MB_OK | MB_ICONEXCLAMATION);
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
		/* soft keys (F1/F2) switch the picture fit; Back/Esc returns to the list */
		if (w == VK_ESCAPE || w == VK_BACK) { PostMessageW(h, WM_CLOSE, 0, 0); return 0; }
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

/* ---------------- painting ---------------- */
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

/* copy a cached thumbnail (w x h) to the back buffer at x,y, clipped */
static void blit_thumb(const unsigned short *px, int x, int y, int w, int h)
{
	int ty;
	for (ty = 0; ty < h; ty++) {
		int yy = y + ty;
		if (yy < 0 || yy >= g_bbh) continue;
		memcpy(g_bbits + yy * g_bbstride + x, px + ty * w, w * 2);
	}
}

static void draw_length_badge(HDC dc, const char *len, int right, int bottom)
{
	SIZE sz;
	RECT r;
	WCHAR *wl = utf8_to_w(len, -1, NULL);
	if (!wl) return;
	SelectObject(dc, g_fonts);
	GetTextExtentPoint32W(dc, wl, (int)wcslen(wl), &sz);
	fill(dc, right - sz.cx - 4, bottom - 14, right, bottom, RGB(0, 0, 0));
	SetTextColor(dc, RGB(255, 255, 255));
	SetRect(&r, right - sz.cx - 4, bottom - 14, right - 2, bottom);
	DrawTextW(dc, wl, -1, &r, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	free(wl);
}

/* second text line: uploader (search / subscriptions) or meta (channel page) */
static const char *line2(View *v, YtItem *it)
{
	if (v->kind == V_CHANNEL) return it->meta ? it->meta : "";
	return it->channel ? it->channel : "";
}

static void paint_more_row(HDC dc, View *v, int y, int w, int h, int pressed)
{
	RECT r;
	fill(dc, 0, y, w, y + h, pressed ? T->press : T->morebg);
	SetRect(&r, 0, y, w, y + h);
	SelectObject(dc, g_fontb);
	SetTextColor(dc, T->morefg);
	text_u8(dc, v->busy_more ? "불러오는 중..." : "결과 더 보기", &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void paint_list(HDC dc, int w, int h)
{
	View *v = cur();
	int i, y = 0;
	make_backbuffer(w, h);
	if (!g_bbdc) return;
	fill(g_bbdc, 0, 0, w, h, T->bg);
	SetBkMode(g_bbdc, TRANSPARENT);
	EnterCriticalSection(&g_lcs);
	if (v->list.n == 0) {
		RECT r;
		const char *m;
		SetRect(&r, 10, 20, w - 10, h - 10);
		SelectObject(g_bbdc, g_font);
		SetTextColor(g_bbdc, T->sub);
		if (v->busy) m = v->kind == V_SUBS ? "구독 피드를 불러오는 중..." : "불러오는 중...";
		else if (v->msg[0]) m = v->msg;
		else if (v->kind == V_SUBS) m = "구독 탭입니다. 잠시 후 구독 채널의 새 영상이 나옵니다.";
		else if (v->kind == V_HIST) m = "아직 본 영상이 없습니다.\n\n[검색] 탭에서 영상을 찾아 보면\n여기에 최근 본 순서대로 쌓입니다.";
		else m = "위 칸에 검색어를 입력하고\n'검색'을 누르세요.\n\n영상을 누르면 재생되고,\n채널 이름을 누르면 채널로 갑니다.\n\n목록/격자, 다크 모드는\n오른쪽 위 메뉴에 있습니다.";
		text_u8(g_bbdc, m, &r, DT_CENTER | DT_WORDBREAK);
	} else if (v->kind == V_MGR) {
		int first = v->scroll / MGR_H;
		for (i = first, y = first * MGR_H - v->scroll; i < v->list.n && y < h; i++, y += MGR_H) {
			YtItem *it = &v->list.items[i];
			int si = sub_index(it->chid), on = si >= 0 && g_subs[si].feed;
			int bx = 10, by = y + (MGR_H - 18) / 2;
			RECT r;
			if (i == g_pressed) fill(g_bbdc, 0, y, w, y + MGR_H, T->press);
			/* check box */
			fill(g_bbdc, bx, by, bx + 18, by + 18, on ? T->barbtn : T->sub);
			if (!on) fill(g_bbdc, bx + 2, by + 2, bx + 16, by + 16, T->bg);
			else {
				int k;
				for (k = 0; k < 4; k++) fill(g_bbdc, bx + 3 + k, by + 8 + k, bx + 5 + k, by + 11 + k, RGB(255, 255, 255));
				for (k = 0; k < 8; k++) fill(g_bbdc, bx + 7 + k, by + 11 - k, bx + 9 + k, by + 14 - k, RGB(255, 255, 255));
			}
			SelectObject(g_bbdc, on ? g_fontb : g_font);
			SetTextColor(g_bbdc, on ? T->fg : T->sub);
			SetRect(&r, bx + 28, y, w - 6, y + MGR_H);
			text_u8(g_bbdc, it->title ? it->title : it->chid, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | TXT_ELLIPSIS);
			fill(g_bbdc, 0, y + MGR_H - 1, w, y + MGR_H, T->sep);
		}
	} else if (g_grid) {
		int cw, tw, th, ch, first;
		grid_geom(w, &cw, &tw, &th, &ch);
		first = v->scroll / ch;
		for (i = first * 2; i < v->list.n; i++) {
			YtItem *it = &v->list.items[i];
			int row = i / 2, col = i % 2;
			int cx = 2 + col * cw, cy = row * ch - v->scroll;
			int tx = cx + 3, ty = cy + 4;
			RECT r;
			unsigned short *px;
			if (cy >= h) break;
			if (i == g_pressed) fill(g_bbdc, cx, cy, cx + cw, cy + ch, T->press);
			fill(g_bbdc, tx, ty, tx + tw, ty + th, T->ph);
			GDIFLUSH();
			px = tc_get(it->id);
			if (px) blit_thumb(px, tx, ty, tw, th);
			if (it->length) draw_length_badge(g_bbdc, it->length, tx + tw, ty + th);
			SelectObject(g_bbdc, g_fontb);
			SetTextColor(g_bbdc, T->fg);
			SetRect(&r, tx, ty + th + 3, tx + tw, ty + th + 3 + 30);
			text_u8(g_bbdc, it->title, &r, DT_LEFT | DT_WORDBREAK);
			SelectObject(g_bbdc, g_fonts);
			SetTextColor(g_bbdc, (i == g_pressed && g_pressed_ch) ? T->morefg : T->sub);
			SetRect(&r, tx, ty + th + 3 + 30, tx + tw, ty + th + 3 + 45);
			text_u8(g_bbdc, line2(v, it), &r, DT_LEFT | DT_SINGLELINE | TXT_ELLIPSIS);
		}
		y = ((v->list.n + 1) / 2) * ch - v->scroll;
		if (has_more_row(v) && y < h) paint_more_row(g_bbdc, v, y, w, MORE_H, g_pressed == v->list.n);
	} else {
		int first = v->scroll / ROW_H;
		for (i = first, y = first * ROW_H - v->scroll; i < v->list.n && y < h; i++, y += ROW_H) {
			YtItem *it = &v->list.items[i];
			RECT r;
			unsigned short *px;
			if (i == g_pressed) fill(g_bbdc, 0, y, w, y + ROW_H, T->press);
			fill(g_bbdc, 4, y + 5, 4 + LTW, y + 5 + LTH, T->ph);
			GDIFLUSH();
			px = tc_get(it->id);
			if (px) blit_thumb(px, 4, y + 5, LTW, LTH);
			if (it->length) draw_length_badge(g_bbdc, it->length, 4 + LTW, y + 5 + LTH);
			SelectObject(g_bbdc, g_fontb);
			SetTextColor(g_bbdc, T->fg);
			SetRect(&r, LTW + 10, y + 3, w - 3, y + 3 + 32);
			text_u8(g_bbdc, it->title, &r, DT_LEFT | DT_WORDBREAK);
			SelectObject(g_bbdc, g_fonts);
			SetTextColor(g_bbdc, (i == g_pressed && g_pressed_ch) ? T->morefg : T->sub);
			SetRect(&r, LTW + 10, y + 35, w - 3, y + 49);
			text_u8(g_bbdc, line2(v, it), &r, DT_LEFT | DT_SINGLELINE | TXT_ELLIPSIS);
			SetTextColor(g_bbdc, T->sub);
			SetRect(&r, LTW + 10, y + 48, w - 3, y + 62);
			text_u8(g_bbdc, v->kind == V_CHANNEL ? "" : (it->meta ? it->meta : ""), &r, DT_LEFT | DT_SINGLELINE | TXT_ELLIPSIS);
			fill(g_bbdc, 0, y + ROW_H - 1, w, y + ROW_H, T->sep);
		}
		if (has_more_row(v) && y < h && i >= v->list.n) paint_more_row(g_bbdc, v, y, w, ROW_H, g_pressed == v->list.n);
	}
	LeaveCriticalSection(&g_lcs);
	{
		int chh = content_height(v);
		if (chh > h) {
			int bh = h * h / chh, by;
			if (bh < 16) bh = 16;
			by = (int)((long)v->scroll * (h - bh) / (chh - h));
			fill(g_bbdc, w - 4, by, w - 1, by + bh, T->sub);
		}
	}
	BitBlt(dc, 0, content_top(), w, h, g_bbdc, 0, 0, SRCCOPY);
}

/* tab bar hit areas: channel view = back / name / (un)subscribe,
   otherwise = history / search / subscriptions tabs + action button (d) */
static void tab_rects(int w, RECT *a, RECT *b, RECT *c, RECT *d)
{
	View *v = cur();
	if (v->kind == V_CHANNEL) {
		SetRect(a, 0, g_topbar, 44, g_topbar + g_tabbar);
		SetRect(b, 44, g_topbar, w - 70, g_topbar + g_tabbar);
		SetRect(c, w - 70, g_topbar, w, g_topbar + g_tabbar);
		SetRect(d, 0, 0, 0, 0);
	} else if (v->kind == V_MGR) {
		SetRect(a, 0, g_topbar, 50, g_topbar + g_tabbar);
		SetRect(b, 50, g_topbar, w - 120, g_topbar + g_tabbar);
		SetRect(c, w - 120, g_topbar, w - 60, g_topbar + g_tabbar);
		SetRect(d, w - 60, g_topbar, w, g_topbar + g_tabbar);
	} else {
		int tw = w >= 300 ? 52 : 44;
		SetRect(a, 0, g_topbar, tw, g_topbar + g_tabbar);
		SetRect(b, tw, g_topbar, 2 * tw, g_topbar + g_tabbar);
		SetRect(c, 2 * tw, g_topbar, 3 * tw, g_topbar + g_tabbar);
		SetRect(d, w - 70, g_topbar, w, g_topbar + g_tabbar);
	}
}

/* buttons on the right of the tab bar: sort (all tabs), manage + refresh (subscriptions) */
static void tab_buttons(int w, RECT *sortr, RECT *mgrr, RECT *refr)
{
	int tw = w >= 300 ? 52 : 44, left = 3 * tw + 2, bw;
	SetRectEmpty(sortr); SetRectEmpty(mgrr); SetRectEmpty(refr);
	if (cur()->kind == V_CHANNEL || cur()->kind == V_MGR) return;
	if (g_tab == 1) {
		bw = (w - left) / 3;
		if (bw > 56) bw = 56;
		SetRect(refr, w - bw, g_topbar, w, g_topbar + g_tabbar);
		SetRect(mgrr, w - 2 * bw, g_topbar, w - bw, g_topbar + g_tabbar);
		SetRect(sortr, w - 3 * bw, g_topbar, w - 2 * bw, g_topbar + g_tabbar);
	} else {
		bw = w - left;
		if (bw > 56) bw = 56;
		SetRect(sortr, w - bw, g_topbar, w, g_topbar + g_tabbar);
	}
}

static void draw_btn(HDC dc, RECT *r, const char *label, int on)
{
	fill(dc, r->left + 2, r->top + 3, r->right - 2, r->bottom - 4, on ? T->barbtn : T->ph);
	SetTextColor(dc, on ? RGB(255, 255, 255) : T->fg);
	text_u8(dc, label, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static int feed_count(void)
{
	int i, n = 0;
	for (i = 0; i < g_nsubs; i++) n += g_subs[i].feed;
	return n;
}

static void draw_tab(HDC dc, RECT *r, const char *label, int sel)
{
	SetTextColor(dc, sel ? T->tabsel : T->tabfg);
	text_u8(dc, label, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	if (sel) fill(dc, r->left + 8, r->bottom - 4, r->right - 8, r->bottom - 1, T->tabsel);
}

static void paint_tabbar(HDC dc, int w)
{
	View *v = cur();
	RECT a, b, c, d;
	tab_rects(w, &a, &b, &c, &d);
	fill(dc, 0, g_topbar, w, g_topbar + g_tabbar, T->tabbg);
	fill(dc, 0, g_topbar + g_tabbar - 1, w, g_topbar + g_tabbar, T->sep);
	SetBkMode(dc, TRANSPARENT);
	if (v->kind == V_MGR) {
		char s[80];
		SelectObject(dc, g_fontb);
		draw_btn(dc, &a, "완료", 1);
		SelectObject(dc, g_fonts);
		SetTextColor(dc, T->fg);
		_snprintf(s, sizeof s, "피드 %d / 구독 %d", feed_count(), g_nsubs);
		s[sizeof s - 1] = 0;
		b.left += 4;
		text_u8(dc, s, &b, DT_LEFT | DT_VCENTER | DT_SINGLELINE | TXT_ELLIPSIS);
		draw_btn(dc, &c, "전체 선택", 0);
		draw_btn(dc, &d, "전체 해제", 0);
	} else if (v->kind == V_CHANNEL) {
		int subbed = sub_index(v->chid) >= 0;
		SelectObject(dc, g_fontb);
		SetTextColor(dc, T->tabfg);
		text_u8(dc, "< 뒤로", &a, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		SetTextColor(dc, T->fg);
		b.left += 4;
		text_u8(dc, v->title ? v->title : "채널", &b, DT_LEFT | DT_VCENTER | DT_SINGLELINE | TXT_ELLIPSIS);
		fill(dc, c.left + 4, c.top + 3, c.right - 4, c.bottom - 4, subbed ? T->ph : T->barbtn);
		SetTextColor(dc, subbed ? T->fg : RGB(255, 255, 255));
		text_u8(dc, subbed ? "구독 중" : "구독", &c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	} else {
		SelectObject(dc, g_fontb);
		RECT sr, mr, rr;
		draw_tab(dc, &a, "기록", g_tab == 2);
		draw_tab(dc, &b, "검색", g_tab == 0);
		draw_tab(dc, &c, "구독", g_tab == 1);
		tab_buttons(w, &sr, &mr, &rr);
		SelectObject(dc, g_fonts);
		draw_btn(dc, &sr, v->sort ? "정렬*" : "정렬", v->sort != 0);
		if (g_tab == 1) {
			int narrow = rr.right - rr.left < 50;
			draw_btn(dc, &mr, "관리", 0);
			SetTextColor(dc, T->morefg);
			text_u8(dc, v->busy ? (narrow ? "..." : "불러오는 중") : (narrow ? "갱신" : "새로고침"), &rr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
	}
}

static void paint_main(HDC dc)
{
	RECT cr, r;
	GetClientRect(g_hwnd, &cr);
	/* top bar */
	fill(dc, 0, 0, cr.right, g_topbar, T->bar);
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, T->bartxt);
	SelectObject(dc, g_fontb);
	SetRect(&r, cr.right - 78, 0, cr.right - 34, g_topbar);
	fill(dc, r.left + 2, 4, r.right - 2, g_topbar - 4, T->barbtn);
	text_u8(dc, "검색", &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	SetRect(&r, cr.right - 32, 0, cr.right, g_topbar);
	fill(dc, r.left + 2, 4, r.right - 2, g_topbar - 4, T->barbtn);
	{
		int cx = (r.left + r.right) / 2, cy = g_topbar / 2;
		fill(dc, cx - 7, cy - 6, cx + 7, cy - 4, RGB(255, 255, 255));
		fill(dc, cx - 7, cy - 1, cx + 7, cy + 1, RGB(255, 255, 255));
		fill(dc, cx - 7, cy + 4, cx + 7, cy + 6, RGB(255, 255, 255));
	}
	paint_tabbar(dc, cr.right);
	paint_list(dc, cr.right, list_height());
	/* status */
	fill(dc, 0, cr.bottom - g_status_h, cr.right, cr.bottom, T->stbg);
	SelectObject(dc, g_fonts);
	SetTextColor(dc, T->stfg);
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

static void apply_theme(void)
{
	T = g_dark ? &g_darkt : &g_light;
	if (g_editbr) DeleteObject(g_editbr);
	g_editbr = CreateSolidBrush(T->editbg);
	InvalidateRect(g_edit, NULL, TRUE);
	InvalidateRect(g_hwnd, NULL, FALSE);
}

/* ---------------- menus ---------------- */
static int popup_at(HMENU m, int x, int y, UINT align)
{
	POINT pt;
	pt.x = x;
	pt.y = y;
	ClientToScreen(g_hwnd, &pt);
	return TrackPopupMenuEx(m, TPM_RETURNCMD | align, pt.x, pt.y, g_hwnd, NULL);
}

static void show_menu(void)
{
	HMENU m = CreatePopupMenu(), q = CreatePopupMenu(), s = CreatePopupMenu();
	int cmd;
	AppendMenuW(q, MF_STRING | (g_quality == 0 ? MF_CHECKED : 0), M_Q240, L"240p (기본)");
	AppendMenuW(q, MF_STRING | (g_quality == 1 ? MF_CHECKED : 0), M_Q144, L"144p (빠름)");
	AppendMenuW(q, MF_STRING | (g_quality == 2 ? MF_CHECKED : 0), M_Q360, L"360p (느릴 수 있음)");
	AppendMenuW(s, MF_STRING | (!g_grid ? MF_CHECKED : 0), M_LIST, L"목록으로 보기");
	AppendMenuW(s, MF_STRING | (g_grid ? MF_CHECKED : 0), M_GRID, L"격자로 보기");
	AppendMenuW(s, MF_SEPARATOR, 0, NULL);
	AppendMenuW(s, MF_STRING | (g_dark ? MF_CHECKED : 0), M_DARK, L"다크 모드");
	AppendMenuW(s, MF_SEPARATOR, 0, NULL);
	AppendMenuW(s, MF_STRING | (g_start_ori == ORI_PORTRAIT ? MF_CHECKED : 0), M_ORI_P, L"재생 화면: 세로");
	AppendMenuW(s, MF_STRING | (g_start_ori != ORI_PORTRAIT ? MF_CHECKED : 0), M_ORI_L, L"재생 화면: 가로");
	AppendMenuW(s, MF_STRING | (g_stats ? MF_CHECKED : 0), M_STATS, L"재생 중 초당 프레임 표시");
	AppendMenuW(m, MF_POPUP, (UINT_PTR)s, L"화면");
	AppendMenuW(m, MF_POPUP, (UINT_PTR)q, L"화질");
	AppendMenuW(m, MF_SEPARATOR, 0, NULL);
	AppendMenuW(m, MF_STRING, M_SUBMGR, L"구독 관리 (피드에 넣을 채널)");
	AppendMenuW(m, MF_STRING, M_IMPORT, L"구독 목록 가져오기");
	AppendMenuW(m, MF_STRING, M_CLEARHIST, L"시청 기록 모두 지우기");
	AppendMenuW(m, MF_STRING, M_UPDATE, L"유튜브 연결 설정 다시 받기");
	AppendMenuW(m, MF_STRING, M_SELFTEST, L"재생 엔진 자가진단");
	AppendMenuW(m, MF_STRING, M_ABOUT, L"정보");
	AppendMenuW(m, MF_STRING, M_EXIT, L"종료");
	cmd = popup_at(m, client_w() - 4, g_topbar, TPM_RIGHTALIGN | TPM_TOPALIGN);
	DestroyMenu(m); /* also destroys the submenus */
	if (cmd) PostMessageW(g_hwnd, WM_COMMAND, cmd, 0);
}

static int g_ctx_idx = -1;

static void item_menu(int idx, int x, int y)
{
	View *v = cur();
	YtItem *it;
	HMENU m;
	int cmd;
	if (idx < 0 || idx >= v->list.n) return;
	it = &v->list.items[idx];
	m = CreatePopupMenu();
	AppendMenuW(m, MF_STRING, C_PLAY, L"재생");
	if (v->kind == V_HIST) AppendMenuW(m, MF_STRING, C_DELHIST, L"기록에서 삭제");
	if (it->chid[0]) {
		if (v->kind != V_CHANNEL) AppendMenuW(m, MF_STRING, C_CHANNEL, L"채널 보기");
		if (sub_index(it->chid) >= 0) AppendMenuW(m, MF_STRING, C_UNSUB, L"채널 구독 취소");
		else AppendMenuW(m, MF_STRING, C_SUB, L"채널 구독");
	}
	g_ctx_idx = idx;
	cmd = popup_at(m, x, y, TPM_LEFTALIGN | TPM_TOPALIGN);
	DestroyMenu(m);
	if (cmd) PostMessageW(g_hwnd, WM_COMMAND, cmd, 0);
}

static DWORD WINAPI config_thread(LPVOID arg)
{
	NetCtl ctl;
	int r;
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

/* imports every .csv in the usual folders that contains channel ids
   (the Takeout file may be named subscriptions.csv, 구독정보.csv, ...) */
typedef struct { int added, found, files, scanned; char checked[400]; } ImportScan;

static int name_has(const WCHAR *name, const WCHAR *sub)
{
	int n = (int)wcslen(sub), i;
	for (i = 0; name[i]; i++) if (!_wcsnicmp(name + i, sub, n)) return 1;
	return 0;
}

/* walks the file system (not \\Windows) for *.csv with channel ids, depth-limited */
static void import_scan_dir(const WCHAR *dir, int depth, ImportScan *st)
{
	WCHAR pat[MAX_PATH];
	WIN32_FIND_DATAW fd;
	HANDLE fh;
	if (depth > 4 || st->scanned > 3000 || wcslen(dir) + 3 >= MAX_PATH) return;
	wcscpy(pat, dir);
	wcscat(pat, L"*");
	fh = FindFirstFileW(pat, &fd);
	if (fh == INVALID_HANDLE_VALUE) return;
	do {
		WCHAR path[MAX_PATH];
		WCHAR *dot;
		st->scanned++;
		if (wcslen(dir) + wcslen(fd.cFileName) + 2 >= MAX_PATH) continue;
		wcscpy(path, dir);
		wcscat(path, fd.cFileName);
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (depth == 0 && !_wcsicmp(fd.cFileName, L"Windows")) continue;
			if (fd.cFileName[0] == L'.') continue;
			wcscat(path, L"\\");
			import_scan_dir(path, depth + 1, st);
			continue;
		}
		dot = wcsrchr(fd.cFileName, L'.');
		/* .csv files, or files named like an export even if the extension got lost
		   (e.g. "subscriptions." after a rename on the phone) */
		if ((!dot || _wcsicmp(dot, L".csv")) && !name_has(fd.cFileName, L"subscri") && !name_has(fd.cFileName, L"\uAD6C\uB3C5")) continue;
		if (!_wcsnicmp(fd.cFileName, L"m480tube", 8)) continue;
		if (fd.nFileSizeLow > 2 * 1024 * 1024) continue;
		{
			int f = 0, r = subs_import_file(path, &f);
			char *nm = w_to_utf8(path, -1);
			if (nm && strlen(st->checked) + strlen(nm) + 30 < sizeof st->checked) {
				char one[300];
				_snprintf(one, sizeof one, "\n%s (채널 %d)", nm, f);
				one[sizeof one - 1] = 0;
				strcat(st->checked, one);
			}
			tube_log("import: %s r=%d found=%d", nm ? nm : "?", r, f);
			free(nm);
			if (r >= 0 && f > 0) { st->added += r; st->found += f; st->files++; }
		}
	} while (FindNextFileW(fh, &fd));
	FindClose(fh);
}

static void import_subs(void)
{
	ImportScan st;
	char s[700];
	HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
	memset(&st, 0, sizeof st);
	status_set("구독 목록 파일을 찾는 중...");
	import_scan_dir(L"\\", 0, &st);
	SetCursor(old);
	if (!st.files) {
		_snprintf(s, sizeof s, "구독 목록에서 채널을 찾지 못했습니다.\n찾은 .csv 파일:%s", st.checked[0] ? st.checked : "\n(없음)");
		s[sizeof s - 1] = 0;
		msgbox_u8(s, MB_OK);
		status_set("");
		return;
	}
	subs_save();
	if (st.added) _snprintf(s, sizeof s, "채널 %d개를 새로 구독했습니다.\n(전체 구독 %d개)\n\n가져온 채널은 아직 피드에 없습니다.\n구독 관리에서 영상을 받아 볼\n채널만 체크하세요.", st.added, g_nsubs);
	else _snprintf(s, sizeof s, "이미 모두 구독 중입니다.\n(전체 구독 %d개)", g_nsubs);
	msgbox_u8(s, MB_OK);
	g_subs_loaded_once = 0;
	set_tab(1);
	if (st.added) mgr_open();
}

/* ---------------- input ---------------- */
static LRESULT CALLBACK edit_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WM_KEYDOWN && w == VK_RETURN) { do_search_from_edit(); return 0; }
	if (m == WM_CHAR && (w == '\r' || w == '\n')) return 0;
	if (m == WM_SETFOCUS) sip_show(1);
	return CallWindowProcW(g_edit_proc, h, m, w, l);
}

static void maybe_load_more(void)
{
	View *v = cur();
	if (!v->list.cont || v->busy || v->busy_more) return;
	if (v->scroll + list_height() < content_height(v) - ROW_H) return;
	if (v->kind == V_SEARCH) start_search(NULL, 1);
	else if (v->kind == V_CHANNEL) load_channel(v, 1);
}

/* ---------------- subscription manager ---------------- */
static int cmp_name(const void *a, const void *b)
{
	const YtItem *x = (const YtItem *)a, *y = (const YtItem *)b;
	return _stricmp(x->title ? x->title : "", y->title ? y->title : "");
}

static void mgr_build(void)
{
	View *v = &g_vmgr;
	int i;
	EnterCriticalSection(&g_lcs);
	yt_list_free(&v->list);
	v->kind = V_MGR;
	if (g_nsubs) {
		v->list.items = (YtItem *)calloc(g_nsubs, sizeof(YtItem));
		for (i = 0; i < g_nsubs; i++) {
			YtItem *it = &v->list.items[v->list.n++];
			strcpy(it->chid, g_subs[i].chid);
			it->title = xstrdup(g_subs[i].name ? g_subs[i].name : g_subs[i].chid);
		}
		qsort(v->list.items, v->list.n, sizeof(YtItem), cmp_name);
	}
	strcpy(v->msg, "구독한 채널이 없습니다.\n\n메뉴 > '구독 목록 가져오기'로\n유튜브 구독 목록을 옮겨 오거나\n채널에서 [구독]을 누르세요.");
	LeaveCriticalSection(&g_lcs);
	clamp_scroll();
}

static int g_mgr_changed;

static void mgr_open(void)
{
	g_mgr_changed = 0;
	g_vmgr.scroll = 0;
	g_mgr = 1;
	mgr_build();
	status_set("체크한 채널의 새 영상만 구독 탭에 나옵니다");
	InvalidateRect(g_hwnd, NULL, FALSE);
}

static void mgr_close(void)
{
	int i, k = 0, changed = g_mgr_changed;
	if (!g_mgr) return;
	g_mgr = 0;
	EnterCriticalSection(&g_lcs);
	yt_list_free(&g_vmgr.list);
	/* drop videos of channels taken out of the feed */
	for (i = 0; i < g_vsubs.list.n; i++) {
		int si = sub_index(g_vsubs.list.items[i].chid);
		if (si < 0 || !g_subs[si].feed) { free_item(&g_vsubs.list.items[i]); changed = 1; }
		else g_vsubs.list.items[k++] = g_vsubs.list.items[i];
	}
	g_vsubs.list.n = k;
	LeaveCriticalSection(&g_lcs);
	subs_save();
	if (changed) {
		feed_save();
		g_vsubs.scroll = 0;
		g_subs_loaded_once = 0;
		g_tab = 1;
		clamp_scroll();
		refresh_subs();
	} else status_set("");
	InvalidateRect(g_hwnd, NULL, FALSE);
}

static void mgr_set_all(int on)
{
	int i;
	for (i = 0; i < g_nsubs; i++) {
		/* channels newly put in the feed are fetched right away */
		if (on && !g_subs[i].feed) g_subs[i].fetched = 0;
		if (g_subs[i].feed != on) g_mgr_changed = 1;
		g_subs[i].feed = on;
	}
	status_set(on ? "모든 채널을 피드에 넣었습니다" : "모든 채널을 피드에서 뺐습니다");
	InvalidateRect(g_hwnd, NULL, FALSE);
}

static void mgr_toggle(int idx)
{
	int si;
	if (idx < 0 || idx >= g_vmgr.list.n) return;
	si = sub_index(g_vmgr.list.items[idx].chid);
	if (si < 0) return;
	g_subs[si].feed = !g_subs[si].feed;
	if (g_subs[si].feed) g_subs[si].fetched = 0;
	g_mgr_changed = 1;
	InvalidateRect(g_hwnd, NULL, FALSE);
}

static void mgr_item_menu(int idx, int x, int y)
{
	HMENU m;
	int cmd, si;
	if (idx < 0 || idx >= g_vmgr.list.n) return;
	si = sub_index(g_vmgr.list.items[idx].chid);
	if (si < 0) return;
	m = CreatePopupMenu();
	AppendMenuW(m, MF_STRING, C_PLAY, g_subs[si].feed ? L"피드에서 빼기" : L"피드에 넣기");
	AppendMenuW(m, MF_STRING, C_CHANNEL, L"채널 보기");
	AppendMenuW(m, MF_STRING, C_MGR_UNSUB, L"구독 취소");
	g_ctx_idx = idx;
	cmd = popup_at(m, x, y, TPM_LEFTALIGN | TPM_TOPALIGN);
	DestroyMenu(m);
	if (cmd) PostMessageW(g_hwnd, WM_COMMAND, cmd, 0);
}

/* ---------------- sorting menu ---------------- */
static void sort_menu(int x, int y)
{
	View *v = cur();
	HMENU m = CreatePopupMenu();
	int cmd, i;
	for (i = 0; i < 6; i++) {
		WCHAR wl[40];
		const char *nm = sort_name(i);
		if (i == 0) nm = v->kind == V_SUBS ? "기본순 (새 영상 먼저)" : v->kind == V_HIST ? "기본순 (최근 본 순)" : "기본순 (유튜브 순서)";
		MultiByteToWideChar(CP_UTF8, 0, nm, -1, wl, 40);
		AppendMenuW(m, MF_STRING | (v->sort == i ? MF_CHECKED : 0), M_SORT0 + i, wl);
	}
	cmd = popup_at(m, x, y, TPM_RIGHTALIGN | TPM_TOPALIGN);
	DestroyMenu(m);
	if (cmd) PostMessageW(g_hwnd, WM_COMMAND, cmd, 0);
}

static void set_sort(int s)
{
	View *v = cur();
	char msg[80];
	if (v->kind == V_MGR) return;
	v->sort = s;
	EnterCriticalSection(&g_lcs);
	g_sortv = v;
	if (v->list.n > 1) qsort(v->list.items, v->list.n, sizeof(YtItem), cmp_sort);
	LeaveCriticalSection(&g_lcs);
	_snprintf(msg, sizeof msg, "정렬: %s%s", sort_name(s),
		(s && v->list.cont) ? " (불러온 영상 안에서)" : "");
	msg[sizeof msg - 1] = 0;
	status_set(msg);
	scroll_top();
	InvalidateRect(g_hwnd, NULL, FALSE);
}

static void activate_item(int idx, int on_channel)
{
	View *v = cur();
	if (v->kind == V_MGR) { mgr_toggle(idx); return; }
	if (idx == v->list.n) {
		if (v->kind == V_SEARCH) start_search(NULL, 1);
		else if (v->kind == V_CHANNEL) load_channel(v, 1);
		return;
	}
	if (idx < 0 || idx >= v->list.n) return;
	if (on_channel && v->kind != V_CHANNEL && v->list.items[idx].chid[0]) {
		YtItem *it = &v->list.items[idx];
		open_channel(it->chid, it->channel);
		return;
	}
	open_video(idx);
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
	case WM_CTLCOLOREDIT:
		SetTextColor((HDC)w, T->editfg);
		SetBkColor((HDC)w, T->editbg);
		return (LRESULT)g_editbr;
	case WM_SIZE:
		layout_children();
		if (g_grid) set_thumb_size();
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
		RECT cr, a, b, c, d;
		POINT pt;
		GetClientRect(h, &cr);
		pt.x = x; pt.y = y;
		if (y < g_topbar) {
			if (x >= cr.right - 32) show_menu();
			else if (x >= cr.right - 78) do_search_from_edit();
			return 0;
		}
		if (y < content_top()) {
			View *v = cur();
			tab_rects(cr.right, &a, &b, &c, &d);
			if (v->kind == V_MGR) {
				if (PtInRect(&a, pt)) mgr_close();
				else if (PtInRect(&c, pt)) mgr_set_all(1);
				else if (PtInRect(&d, pt)) mgr_set_all(0);
				else if (PtInRect(&b, pt)) scroll_top();
			} else if (v->kind == V_CHANNEL) {
				if (PtInRect(&a, pt)) go_back();
				else if (PtInRect(&c, pt)) toggle_sub(v->chid, v->title);
				else if (PtInRect(&b, pt)) scroll_top();   /* tap the channel name: back to the top */
			} else {
				/* tapping the tab you are already on jumps back to the top */
				RECT sr, mr, rr;
				int t = PtInRect(&a, pt) ? 2 : PtInRect(&b, pt) ? 0 : PtInRect(&c, pt) ? 1 : -1;
				tab_buttons(cr.right, &sr, &mr, &rr);
				if (t >= 0) { if (t == g_tab) scroll_top(); else set_tab(t); }
				else if (PtInRect(&sr, pt)) sort_menu(sr.right, sr.bottom);
				else if (PtInRect(&mr, pt)) mgr_open();
				else if (PtInRect(&rr, pt)) refresh_subs();
			}
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
		g_down_scroll = cur()->scroll;
		g_pressed = item_at(x, y, &g_pressed_ch);
		SetCapture(h);
		SetTimer(h, TM_HOLD, 650, NULL);
		list_invalidate();
		return 0;
	}
	case WM_MOUSEMOVE:
		if (g_down) {
			int y = (short)HIWORD(l);
			DWORD t = GetTickCount();
			if (!g_dragging && abs(y - g_down_y) > 8) { g_dragging = 1; g_pressed = -1; KillTimer(h, TM_HOLD); }
			if (g_dragging) {
				int dt = (int)(t - g_last_t);
				if (dt > 0) g_vel = (g_last_y - y) * 16 * 25 / dt;
				cur()->scroll = g_down_scroll + (g_down_y - y);
				clamp_scroll();
				g_last_y = y;
				g_last_t = t;
				list_invalidate();
			}
		}
		return 0;
	case WM_LBUTTONUP:
		KillTimer(h, TM_HOLD);
		if (g_down) {
			int x = (short)LOWORD(l), y = (short)HIWORD(l);
			g_down = 0;
			ReleaseCapture();
			if (!g_dragging) {
				int ch, idx = item_at(x, y, &ch);
				if (idx >= 0 && idx == g_pressed) { g_pressed = -1; activate_item(idx, ch); }
			} else if (abs(g_vel) > 24 && GetTickCount() - g_last_t < 100) {
				SetTimer(h, TM_KINETIC, 25, NULL);
			} else maybe_load_more();
			g_pressed = -1;
			list_invalidate();
		}
		return 0;
	case WM_TIMER:
		if (w == TM_KINETIC) {
			View *v = cur();
			int before = v->scroll;
			v->scroll += g_vel / 16;
			clamp_scroll();
			g_vel = g_vel * 93 / 100;
			list_invalidate();
			if (v->scroll == before || abs(g_vel) < 16) { KillTimer(h, TM_KINETIC); g_vel = 0; maybe_load_more(); }
		} else if (w == TM_HOLD) {
			KillTimer(h, TM_HOLD);
			if (g_down && !g_dragging && g_pressed >= 0 && g_pressed < cur()->list.n) {
				int idx = g_pressed;
				g_down = 0;
				ReleaseCapture();
				list_invalidate();
				if (cur()->kind == V_MGR) mgr_item_menu(idx, g_down_x, g_down_y);
				else item_menu(idx, g_down_x, g_down_y);
				g_pressed = -1;
				list_invalidate();
			}
		}
		return 0;
	case WM_KEYDOWN: {
		View *v = cur();
		int step = v->kind == V_MGR ? MGR_H : g_grid ? 140 : ROW_H;
		if (w == VK_DOWN) { v->scroll += step; clamp_scroll(); list_invalidate(); maybe_load_more(); }
		else if (w == VK_UP) { v->scroll -= step; clamp_scroll(); list_invalidate(); }
		else if (w == VK_ESCAPE || w == VK_BACK) go_back();
		else if (w == VK_RETURN && v->kind == V_MGR) mgr_toggle(v->scroll / MGR_H);
		else if (w == VK_RETURN) {
			int idx = g_grid ? 0 : v->scroll / ROW_H;
			if (g_grid) { int cw, tw, th, ch; grid_geom(client_w(), &cw, &tw, &th, &ch); idx = (v->scroll / ch) * 2; }
			if (idx < v->list.n) open_video(idx);
		}
		return 0;
	}
	case WM_COMMAND:
		switch (LOWORD(w)) {
		case M_Q240: g_quality = 0; settings_save(); return 0;
		case M_Q144: g_quality = 1; settings_save(); return 0;
		case M_Q360: g_quality = 2; settings_save(); return 0;
		case M_ORI_P: g_start_ori = ORI_PORTRAIT; settings_save(); return 0;
		case M_ORI_L: g_start_ori = ORI_LAND_LEFT; settings_save(); return 0;
		case M_STATS: g_stats = !g_stats; settings_save(); return 0;
		case M_LIST: case M_GRID: {
			View *v = cur();
			int ng = LOWORD(w) == M_GRID;
			if (ng != g_grid) {
				/* keep roughly the same item at the top */
				int cw, tw, th, ch, idx;
				grid_geom(client_w(), &cw, &tw, &th, &ch);
				idx = g_grid ? (v->scroll / ch) * 2 : v->scroll / ROW_H;
				g_grid = ng;
				set_thumb_size();
				v->scroll = g_grid ? (idx / 2) * ch : idx * ROW_H;
				clamp_scroll();
				settings_save();
				InvalidateRect(h, NULL, FALSE);
			}
			return 0;
		}
		case M_DARK: g_dark = !g_dark; settings_save(); apply_theme(); return 0;
		case M_IMPORT: import_subs(); return 0;
		case M_UPDATE: status_set("설정을 받는 중..."); start_config_update(1); return 0;
		case M_SELFTEST: {
			char r[300];
			player_selftest(r, sizeof r);
			msgbox_u8(r, MB_OK);
			return 0;
		}
		case M_ABOUT:
			msgbox_u8("M480Tube " VERSION "\nSCH-M480용 유튜브 뷰어\n\n채널 이름을 누르면 채널로,\n영상을 길게 누르면 메뉴가 나옵니다.", MB_OK);
			return 0;
		case M_EXIT: DestroyWindow(h); return 0;
		case M_SORT0: case M_SORT1: case M_SORT2: case M_SORT3: case M_SORT4: case M_SORT5:
			set_sort(LOWORD(w) - M_SORT0);
			return 0;
		case M_SUBMGR:
			set_tab(1);
			mgr_open();
			return 0;
		case C_MGR_UNSUB:
			if (cur() == &g_vmgr && g_ctx_idx >= 0 && g_ctx_idx < g_vmgr.list.n) {
				char chid[32], *name = xstrdup(g_vmgr.list.items[g_ctx_idx].title);
				strcpy(chid, g_vmgr.list.items[g_ctx_idx].chid);
				toggle_sub(chid, name);
				free(name);
				g_mgr_changed = 1;
				mgr_build();
				InvalidateRect(h, NULL, FALSE);
			}
			return 0;
		case C_PLAY:
			if (cur() == &g_vmgr) { mgr_toggle(g_ctx_idx); return 0; }
			open_video(g_ctx_idx);
			return 0;
		case C_DELHIST:
			if (cur() == &g_vhist) { hist_remove(g_ctx_idx); clamp_scroll(); InvalidateRect(h, NULL, FALSE); }
			return 0;
		case M_CLEARHIST:
			if (MessageBoxW(h, L"시청 기록을 모두 지울까요?", APPNAME, MB_YESNO | MB_ICONQUESTION) == IDYES) {
				EnterCriticalSection(&g_lcs);
				yt_list_free(&g_vhist.list);
				g_vhist.scroll = 0;
				LeaveCriticalSection(&g_lcs);
				hist_save();
				InvalidateRect(h, NULL, FALSE);
			}
			return 0;
		case C_CHANNEL: case C_SUB: case C_UNSUB: {
			View *v = cur();
			if (g_ctx_idx >= 0 && g_ctx_idx < v->list.n) {
				YtItem *it = &v->list.items[g_ctx_idx];
				char chid[32], *name = xstrdup(it->channel ? it->channel : "");
				strcpy(chid, it->chid);
				if (v->kind == V_MGR) { free(name); name = xstrdup(it->title ? it->title : ""); }
				if (LOWORD(w) == C_CHANNEL) open_channel(chid, name[0] ? name : NULL);
				else toggle_sub(chid, (v->kind == V_CHANNEL && v->title) ? v->title : name);
				free(name);
			}
			return 0;
		}
		}
		break;
	case WM_APP_JOB:
		on_job((Job *)l);
		return 0;
	case WM_APP_PART:
		on_feed_part((FeedPart *)l);
		return 0;
	case WM_APP_PROGRESS: {
		char s[80];
		if (cur() == &g_vsubs) {
			_snprintf(s, sizeof s, "구독 피드를 불러오는 중... %d/%d", (int)w, (int)l);
			status_set(s);
		}
		return 0;
	}
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
/* desktop test mode, see README in the repo history */
static char *g_script;
static DWORD g_script_t0;

static void dump_bmp(const char *fn, const unsigned short *bits, int w, int h, int stride)
{
	FILE *f = fopen(fn, "wb");
	BITMAPFILEHEADER fh;
	struct { BITMAPINFOHEADER h; DWORD m[3]; } bi;
	int yy;
	if (!f) return;
	memset(&fh, 0, sizeof fh); memset(&bi, 0, sizeof bi);
	fh.bfType = 0x4D42; fh.bfOffBits = sizeof fh + sizeof bi;
	fh.bfSize = fh.bfOffBits + stride * 2 * h;
	bi.h.biSize = sizeof(BITMAPINFOHEADER); bi.h.biWidth = w; bi.h.biHeight = -h;
	bi.h.biPlanes = 1; bi.h.biBitCount = 16; bi.h.biCompression = BI_BITFIELDS;
	bi.m[0] = 0xF800; bi.m[1] = 0x07E0; bi.m[2] = 0x001F;
	fwrite(&fh, sizeof fh, 1, f); fwrite(&bi, sizeof bi, 1, f);
	for (yy = 0; yy < h; yy++) fwrite(bits + yy * stride, 2, stride, f);
	fclose(f);
}

static void shot_window(const char *fn)
{
	/* render the whole main window into a DIB */
	RECT cr;
	HDC dc, mdc;
	HBITMAP bm, old;
	unsigned short *bits;
	struct { BITMAPINFOHEADER h; DWORD m[3]; } bi;
	GetClientRect(g_hwnd, &cr);
	memset(&bi, 0, sizeof bi);
	bi.h.biSize = sizeof(BITMAPINFOHEADER); bi.h.biWidth = cr.right; bi.h.biHeight = -cr.bottom;
	bi.h.biPlanes = 1; bi.h.biBitCount = 16; bi.h.biCompression = BI_BITFIELDS;
	bi.m[0] = 0xF800; bi.m[1] = 0x07E0; bi.m[2] = 0x001F;
	dc = GetDC(g_hwnd);
	bm = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
	mdc = CreateCompatibleDC(dc);
	old = (HBITMAP)SelectObject(mdc, bm);
	paint_main(mdc);
	{
		/* the edit control */
		RECT er;
		GetWindowRect(g_edit, &er);
		MapWindowPoints(NULL, g_hwnd, (POINT *)&er, 2);
		fill(mdc, er.left, er.top, er.right, er.bottom, T->editbg);
		er.left += 3;
		{
			WCHAR t[200];
			int n = GetWindowTextW(g_edit, t, 199);
			SetTextColor(mdc, T->editfg);
			SelectObject(mdc, g_font);
			DrawTextW(mdc, t, n, &er, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		}
	}
	GdiFlush();
	dump_bmp(fn, bits, cr.right, cr.bottom, ((cr.right * 2 + 3) & ~3) / 2);
	SelectObject(mdc, old);
	DeleteDC(mdc);
	DeleteObject(bm);
	ReleaseDC(g_hwnd, dc);
}

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
			else if (!strncmp(act, "tap=", 4)) {
				int x = atoi(act + 4), y = atoi(strchr(act, '/') + 1);
				if (g_player) player_tap(g_player, x, y);
				else { SendMessageW(g_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y)); SendMessageW(g_hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x, y)); }
			}
			else if (!strcmp(act, "stat") && g_player) player_debug(g_player, "stat", 0);
			else if (!strcmp(act, "quit")) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
			else if (!strncmp(act, "winshot=", 8)) shot_window(act + 8);
			else if (!strncmp(act, "scroll=", 7)) { cur()->scroll = atoi(act + 7); clamp_scroll(); list_invalidate(); }
			else if (!strcmp(act, "grid")) SendMessageW(g_hwnd, WM_COMMAND, M_GRID, 0);
			else if (!strcmp(act, "list")) SendMessageW(g_hwnd, WM_COMMAND, M_LIST, 0);
			else if (!strcmp(act, "dark")) SendMessageW(g_hwnd, WM_COMMAND, M_DARK, 0);
			else if (!strncmp(act, "menu=", 5)) { g_ctx_idx = atoi(act + 5); }
			else if (!strncmp(act, "cmd=", 4)) SendMessageW(g_hwnd, WM_COMMAND, atoi(act + 4), 0);
			else if (!strncmp(act, "tab=", 4)) set_tab(atoi(act + 4));
		}
		s = next ? next + 1 : NULL;
	}
}

static char *slurp(const char *fn, long *len)
{
	FILE *fp = fopen(fn, "rb");
	char *d;
	if (!fp) return NULL;
	fseek(fp, 0, SEEK_END); *len = ftell(fp); fseek(fp, 0, SEEK_SET);
	d = (char *)malloc(*len + 1); fread(d, 1, *len, fp); d[*len] = 0; fclose(fp);
	return d;
}

static void start_script(void)
{
	if (getenv("M480TUBE_SCRIPT")) {
		g_script = xstrdup(getenv("M480TUBE_SCRIPT"));
		g_script_t0 = GetTickCount();
		SetTimer(NULL, 0, 100, script_timer);
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
		long len;
		char *d = slurp(getenv("M480TUBE_LISTMOCK"), &len);
		Job *jb = (Job *)calloc(1, sizeof(Job));
		if (!d) return;
		g_vsearch.uid = g_next_uid++;
		g_vsearch.query = xstrdup("test");
		jb->type = J_SEARCH;
		jb->uid = g_vsearch.uid;
		jb->ok = yt_parse_search(d, (int)len, &jb->res);
		free(d);
		if (getenv("M480TUBE_CHANMOCK")) {
			/* channel page from a saved browse response, pushed on top */
			View *v;
			Job *jc = (Job *)calloc(1, sizeof(Job));
			d = slurp(getenv("M480TUBE_CHANMOCK"), &len);
			open_channel("UCTwECeGqMZee77BjdoYtI2Q", NULL);
			v = cur();
			v->busy = 0;
			jc->type = J_CHANNEL;
			jc->uid = v->uid;
			jc->ok = d ? yt_parse_channel(d, (int)len, &jc->res, &jc->name) : 0;
			free(d);
			on_job(jc);
		}
		on_job(jb);
		start_script();
		return;
	}
	if (getenv("M480TUBE_PARSE") || getenv("M480TUBE_PARSEP")) {
		const char *fn = getenv("M480TUBE_PARSE") ? getenv("M480TUBE_PARSE") : getenv("M480TUBE_PARSEP");
		long len;
		char *d = slurp(fn, &len);
		int i, q;
		if (!d) { tube_log("cannot open %s", fn); PostMessageW(g_hwnd, WM_CLOSE, 0, 0); return; }
		if (getenv("M480TUBE_PARSE")) {
			YtList l;
			memset(&l, 0, sizeof l);
			yt_parse_search(d, (int)len, &l);
			tube_log("parsed %d items, cont=%s", l.n, l.cont ? "yes" : "no");
			for (i = 0; i < l.n; i++) tube_log("%s | %s | %s | %s | %s | %s", l.items[i].id, l.items[i].chid, l.items[i].title, l.items[i].channel ? l.items[i].channel : "-", l.items[i].length ? l.items[i].length : "-", l.items[i].meta ? l.items[i].meta : "-");
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
	if (!t) { start_script(); return; }
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
	if (getenv("M480TUBE_ALT")) {
		j->s.alt[0].url = xstrdup(j->s.vurl); j->s.alt[0].itag = 133; j->s.alt[0].size = -1;
		j->s.alt[1].url = xstrdup(getenv("M480TUBE_ALT")); j->s.alt[1].itag = 160; j->s.alt[1].size = -1;
		j->s.alt[1].w = 426; j->s.alt[1].h = 240;
		j->s.nalt = 2;
	}
	j->s.media_post = getenv("M480TUBE_POST") ? 1 : 0;
	j->s.media_rn = j->s.media_post;
	j->s.title = xstrdup("테스트 영상 Test video title that is fairly long");
	strcpy(j->s.client, "TEST");
	free(t);
	on_streams(j);
	start_script();
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
	subs_load();
	yt_config_init();
	net_init();
	entropy_stir(&hi, sizeof hi);
	g_vsearch.kind = V_SEARCH;
	g_vsearch.uid = g_next_uid++;
	g_vsubs.kind = V_SUBS;
	g_vsubs.uid = g_next_uid++;
	g_vhist.kind = V_HIST;
	g_vhist.uid = g_next_uid++;
	hist_load();
	feed_load();
	g_tab = g_vhist.list.n ? 2 : 0;   /* open on the watch history when there is one */

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
	T = g_dark ? &g_darkt : &g_light;
	g_editbr = CreateSolidBrush(T->editbg);

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
		int sw = getenv("M480TUBE_W") ? atoi(getenv("M480TUBE_W")) : 320;
		int sh = getenv("M480TUBE_H") ? atoi(getenv("M480TUBE_H")) : 320;
		RECT r = { 0, 0, 0, 0 };
		r.right = sw; r.bottom = sh;
		AdjustWindowRect(&r, WS_POPUP | WS_BORDER, FALSE);
		g_hwnd = CreateWindowW(APPNAME, APPNAME, WS_POPUP | WS_BORDER | WS_VISIBLE, 0, 0, r.right - r.left, r.bottom - r.top, NULL, NULL, hi, NULL);
	}
#endif
	if (!g_hwnd) return 1;
	layout_children();
	set_thumb_size();
	ShowWindow(g_hwnd, show);
	UpdateWindow(g_hwnd);
	/* two loaders: hides network latency */
	th = CreateThread(NULL, 0x20000, thumb_thread, NULL, THREAD_FLAGS, NULL);
	if (th) CloseHandle(th);
	th = CreateThread(NULL, 0x20000, thumb_thread, NULL, THREAD_FLAGS, NULL);
	if (th) CloseHandle(th);
	start_config_update(0);
	status_set(g_tab == 2 ? "M480Tube " VERSION " - 최근 본 영상" : "M480Tube " VERSION " - 검색어를 입력하세요");
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
