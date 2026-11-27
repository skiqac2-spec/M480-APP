/* M480Nico - niconico search / user pages / stream lookup.
 * Lists come from the public nvapi JSON API. A stream is looked up from the
 * watch page (server-response meta tag): access-rights -> HLS master playlist
 * -> media playlists (fMP4 + AES-128) -> keys. The player fetches and
 * decrypts the segments itself (see player.c). */
#include "util.h"
#include "net.h"
#include "json.h"
#include "yt.h"
#include "tubelog.h"
#include "url.h"
#include <stdio.h>

#define NV_HDRS "X-Frontend-Id: 6\r\nX-Frontend-Version: 0\r\nX-Niconico-Language: ja-jp\r\n"
#define PC_UA "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36"

YtTune g_tune;
static CRITICAL_SECTION g_ycs;
static int g_ycs_init;

void yt_config_init(void)
{
	if (!g_ycs_init) { InitializeCriticalSection(&g_ycs); g_ycs_init = 1; }
	memset(&g_tune, 0, sizeof g_tune);
	g_tune.chunk_kb = 1024;
	g_tune.asm_on = 1;
	g_tune.loopfilter = 0;
}

int yt_config_update(NetCtl *ctl) { (void)ctl; return 0; }

/* ------------------------------------------------------------------ */
/* thumbnail URL memory (id -> url)                                    */
/* ------------------------------------------------------------------ */
#define TU_MAX 600
static struct { char id[16]; char *url; } g_tu[TU_MAX];
static int g_tu_next;

static void thumb_remember(const char *id, const char *url)
{
	int i;
	if (!id || !url || !url[0]) return;
	if (!g_ycs_init) { InitializeCriticalSection(&g_ycs); g_ycs_init = 1; }
	EnterCriticalSection(&g_ycs);
	for (i = 0; i < TU_MAX; i++) if (g_tu[i].url && !strcmp(g_tu[i].id, id)) { LeaveCriticalSection(&g_ycs); return; }
	i = g_tu_next;
	g_tu_next = (g_tu_next + 1) % TU_MAX;
	free(g_tu[i].url);
	strncpy(g_tu[i].id, id, 15);
	g_tu[i].id[15] = 0;
	g_tu[i].url = xstrdup(url);
	LeaveCriticalSection(&g_ycs);
}

static char *xml_tag(const char *s, const char *tag)
{
	char open[40], close[40];
	const char *a, *b;
	_snprintf(open, sizeof open, "<%s>", tag);
	_snprintf(close, sizeof close, "</%s>", tag);
	a = strstr(s, open);
	if (!a) return NULL;
	a += strlen(open);
	b = strstr(a, close);
	if (!b) return NULL;
	return xstrndup(a, (int)(b - a));
}

char *nico_thumb_url(const char *id, NetCtl *ctl)
{
	int i;
	char url[120], *t = NULL;
	HttpResp r;
	if (!g_ycs_init) { InitializeCriticalSection(&g_ycs); g_ycs_init = 1; }
	EnterCriticalSection(&g_ycs);
	for (i = 0; i < TU_MAX; i++) if (g_tu[i].url && !strcmp(g_tu[i].id, id)) { t = xstrdup(g_tu[i].url); break; }
	LeaveCriticalSection(&g_ycs);
	if (t) return t;
	/* not seen in a list (history / saved feed): ask the thumbnail info API */
	_snprintf(url, sizeof url, "https://ext.nicovideo.jp/api/getthumbinfo/%s", id);
	if (http_request(url, "GET", NULL, 0, NULL, NULL, &r, ctl, 100000) && r.status == 200) {
		buf_addc(&r.body, 0);
		t = xml_tag(r.body.p, "thumbnail_url");
	}
	http_resp_free(&r);
	if (t) thumb_remember(id, t);
	return t;
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */
static int nv_get(const char *url, HttpResp *r, NetCtl *ctl, const char *extra, int max)
{
	const char *oh = ctl->hdrs;
	char hdrs[600];
	int ok;
	_snprintf(hdrs, sizeof hdrs, "%s%s", NV_HDRS, extra ? extra : "");
	hdrs[sizeof hdrs - 1] = 0;
	ctl->hdrs = hdrs;
	ok = http_request(url, "GET", NULL, 0, NULL, "https://www.nicovideo.jp/", r, ctl, max);
	ctl->hdrs = oh;
	return ok;
}

static int has_id(YtList *l, const char *id)
{
	int i;
	for (i = 0; i < l->n; i++) if (!strcmp(l->items[i].id, id)) return 1;
	return 0;
}

static YtItem *add_item(YtList *l, const char *id)
{
	YtItem *it;
	if (!id || strlen(id) > 15 || has_id(l, id)) return NULL;
	if (l->n >= 400) return NULL;
	l->items = (YtItem *)realloc(l->items, (l->n + 1) * sizeof(YtItem));
	it = &l->items[l->n++];
	memset(it, 0, sizeof *it);
	strcpy(it->id, id);
	it->ord = l->n - 1;
	return it;
}

/* "2026-11-17T21:58:44+09:00" -> unix time */
static long iso_time(const char *s)
{
	int y, mo, d, h = 0, mi = 0, se = 0;
	long days, t;
	const char *z;
	static const int cum[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
	if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 3 || mo < 1 || mo > 12) return 0;
	days = (y - 1970) * 365L + (y - 1969) / 4 + cum[mo - 1] + d - 1;
	if (mo > 2 && (y % 4) == 0) days++;
	t = days * 86400 + h * 3600 + mi * 60 + se;
	z = strlen(s) > 19 ? s + 19 : NULL;
	if (z && (*z == '+' || *z == '-')) {
		int oh = 0, om = 0;
		if (sscanf(z + 1, "%d:%d", &oh, &om) >= 1) {
			long off = oh * 3600L + om * 60L;
			t += (*z == '+') ? -off : off;
		}
	}
	return t;
}

static char *fmt_count(const char *label, long n)
{
	char *o = (char *)malloc(64);
	if (n >= 100000000) sprintf(o, "%s %ld.%ld\xEC\x96\xB5", label, n / 100000000, (n / 10000000) % 10);      /* 억 */
	else if (n >= 10000) sprintf(o, "%s %ld.%ld\xEB\xA7\x8C", label, n / 10000, (n / 1000) % 10);            /* 만 */
	else sprintf(o, "%s %ld", label, n);
	return o;
}

static char *fmt_ago(long t)
{
	char *o = (char *)malloc(48);
	long d = (long)now_unix() - t;
	if (t <= 0) { free(o); return NULL; }
	if (d < 0) d = 0;
	if (d < 3600) sprintf(o, "%ld\xEB\xB6\x84 \xEC\xA0\x84", d / 60 > 0 ? d / 60 : 1);         /* 분 전 */
	else if (d < 86400) sprintf(o, "%ld\xEC\x8B\x9C\xEA\xB0\x84 \xEC\xA0\x84", d / 3600);     /* 시간 전 */
	else if (d < 86400L * 30) sprintf(o, "%ld\xEC\x9D\xBC \xEC\xA0\x84", d / 86400);            /* 일 전 */
	else if (d < 86400L * 365) sprintf(o, "%ld\xEA\xB0\x9C\xEC\x9B\x94 \xEC\xA0\x84", d / (86400L * 30)); /* 개월 전 */
	else sprintf(o, "%ld\xEB\x85\x84 \xEC\xA0\x84", d / (86400L * 365));                       /* 년 전 */
	return o;
}

static char *join_meta(char *a, char *b)
{
	char *r;
	if (!a) return b;
	if (!b) return a;
	r = (char *)malloc(strlen(a) + strlen(b) + 8);
	sprintf(r, "%s \xC2\xB7 %s", a, b);
	free(a); free(b);
	return r;
}

/* one "essential" video object */
static void add_essential(YtList *l, JNode *v)
{
	const char *id = jstr(jget(v, "id")), *t;
	YtItem *it;
	long dur;
	if (!id) return;
	it = add_item(l, id);
	if (!it) return;
	t = jstr(jget(v, "title"));
	it->title = xstrdup(t ? t : "(\xEC\xA0\x9C\xEB\xAA\xA9 \xEC\x97\x86\xEC\x9D\x8C)");
	t = jpstr(v, "owner.name");
	if (t) it->channel = xstrdup(t);
	t = jpstr(v, "owner.id");
	if (t && jpstr(v, "owner.ownerType") && !strcmp(jpstr(v, "owner.ownerType"), "user")) {
		strncpy(it->chid, t, sizeof it->chid - 1);
	}
	dur = jnum(jget(v, "duration"), -1);
	if (dur >= 0) {
		char b[24];
		if (dur >= 3600) sprintf(b, "%ld:%02ld:%02ld", dur / 3600, (dur / 60) % 60, dur % 60);
		else sprintf(b, "%ld:%02ld", dur / 60, dur % 60);
		it->length = xstrdup(b);
	}
	it->pub = iso_time(jstr(jget(v, "registeredAt")));
	it->meta = join_meta(fmt_count("\xEC\x9E\xAC\xEC\x83\x9D", jnum(jpath(v, "count.view"), 0)), fmt_ago(it->pub)); /* 재생 */
	t = jpstr(v, "thumbnail.listingUrl");
	if (!t) t = jpstr(v, "thumbnail.middleUrl");
	if (!t) t = jpstr(v, "thumbnail.url");
	if (t) thumb_remember(id, t);   /* items are copied field by field elsewhere: keep URLs here */
}

static int api_error(JNode *root, char *err, int errlen, const char *what)
{
	long st = jnum(jpath(root, "meta.status"), 0);
	const char *code = jpstr(root, "meta.errorCode");
	if (st == 200) return 0;
	_snprintf(err, errlen, "%s (%ld %s)", what, st, code ? code : "");
	err[errlen - 1] = 0;
	return 1;
}

void yt_list_free(YtList *l)
{
	int i;
	for (i = 0; i < l->n; i++) {
		free(l->items[i].title); free(l->items[i].channel);
		free(l->items[i].length); free(l->items[i].meta);
	}
	free(l->items);
	free(l->cont);
	memset(l, 0, sizeof *l);
}

/* ------------------------------------------------------------------ */
/* search / ranking                                                    */
/* ------------------------------------------------------------------ */
int yt_parse_search(const char *body, int len, YtList *out)
{
	JDoc *d = json_parse(body, len);
	JNode *root, *items, *v;
	if (!d) { strcpy(out->err, "\xEA\xB2\x80\xEC\x83\x89 \xEA\xB2\xB0\xEA\xB3\xBC\xEB\xA5\xBC \xED\x95\xB4\xEC\x84\x9D\xED\x95\xA0 \xEC\x88\x98 \xEC\x97\x86\xEC\x9D\x8C"); return 0; }
	root = json_root(d);
	if (api_error(root, out->err, sizeof out->err, "\xEA\xB2\x80\xEC\x83\x89 \xEC\x98\xA4\xEB\xA5\x98")) { json_free(d); return 0; }
	items = jpath(root, "data.items");
	for (v = items ? items->child : NULL; v; v = v->next) {
		JNode *e = jget(v, "essential");
		add_essential(out, e ? e : v);
	}
	json_free(d);
	return 1;
}

int yt_search(const char *query, const char *cont, YtList *out, NetCtl *ctl)
{
	HttpResp r;
	char *url, *q;
	int page = cont ? atoi(cont) : 1, ok, n0 = out->n;
	if (page < 1) page = 1;
	out->err[0] = 0;
	free(out->cont);
	out->cont = NULL;
	if (!query || !query[0]) {
		url = xstrdup("https://nvapi.nicovideo.jp/v1/ranking/genre/all?term=24h");
	} else {
		q = url_encode(query);
		url = (char *)malloc(strlen(q) + 200);
		sprintf(url, "https://nvapi.nicovideo.jp/v2/search/video?keyword=%s&sortKey=hot&sortOrder=none&pageSize=25&page=%d", q, page);
		free(q);
	}
	if (!nv_get(url, &r, ctl, NULL, 2 * 1024 * 1024) || !r.body.len) {
		_snprintf(out->err, sizeof out->err, "\xEB\x84\xA4\xED\x8A\xB8\xEC\x9B\x8C\xED\x81\xAC \xEC\x98\xA4\xEB\xA5\x98: %s", r.err);
		http_resp_free(&r);
		free(url);
		return 0;
	}
	free(url);
	ok = yt_parse_search(r.body.p, r.body.len, out);
	if (ok && query && query[0] && strstr(r.body.p, "\"hasNext\":true") && out->n > n0) {
		char b[16];
		sprintf(b, "%d", page + 1);
		out->cont = xstrdup(b);
	}
	http_resp_free(&r);
	return ok;
}

/* ------------------------------------------------------------------ */
/* user pages                                                          */
/* ------------------------------------------------------------------ */
int yt_parse_channel(const char *body, int len, YtList *out, char **name)
{
	JDoc *d = json_parse(body, len);
	JNode *root, *items, *v;
	if (!d) { strcpy(out->err, "\xEC\x9C\xA0\xEC\xA0\x80 \xEC\xA0\x95\xEB\xB3\xB4\xEB\xA5\xBC \xED\x95\xB4\xEC\x84\x9D\xED\x95\xA0 \xEC\x88\x98 \xEC\x97\x86\xEC\x9D\x8C"); return 0; }
	root = json_root(d);
	if (api_error(root, out->err, sizeof out->err, "\xEC\x9C\xA0\xEC\xA0\x80 \xED\x8E\x98\xEC\x9D\xB4\xEC\xA7\x80 \xEC\x98\xA4\xEB\xA5\x98")) { json_free(d); return 0; }
	items = jpath(root, "data.items");
	for (v = items ? items->child : NULL; v; v = v->next) {
		JNode *e = jget(v, "essential");
		add_essential(out, e ? e : v);
		if (name && !*name && out->n && out->items[out->n - 1].channel) *name = xstrdup(out->items[out->n - 1].channel);
	}
	json_free(d);
	return 1;
}

static void user_name(const char *chid, char **name, NetCtl *ctl)
{
	char url[120];
	HttpResp r;
	if (!name || *name) return;
	_snprintf(url, sizeof url, "https://nvapi.nicovideo.jp/v1/users/%s", chid);
	if (nv_get(url, &r, ctl, NULL, 300000) && r.status == 200) {
		JDoc *d = json_parse(r.body.p, r.body.len);
		if (d) {
			const char *n = jpstr(json_root(d), "data.user.nickname");
			if (n) *name = xstrdup(n);
			json_free(d);
		}
	}
	http_resp_free(&r);
}

static int user_videos(const char *chid, int page, int size, const char *sort, YtList *out, char **name, NetCtl *ctl)
{
	char url[260];
	HttpResp r;
	int ok, n0 = out->n, i;
	out->err[0] = 0;
	_snprintf(url, sizeof url, "https://nvapi.nicovideo.jp/v3/users/%s/videos?sortKey=%s&sortOrder=desc&pageSize=%d&page=%d", chid, sort, size, page);
	if (!nv_get(url, &r, ctl, NULL, 2 * 1024 * 1024) || !r.body.len) {
		_snprintf(out->err, sizeof out->err, "\xEB\x84\xA4\xED\x8A\xB8\xEC\x9B\x8C\xED\x81\xAC \xEC\x98\xA4\xEB\xA5\x98: %s", r.err);
		http_resp_free(&r);
		return 0;
	}
	ok = yt_parse_channel(r.body.p, r.body.len, out, name);
	if (ok) {
		long total = 0;
		const char *tc = strstr(r.body.p, "\"totalCount\":");
		if (tc) total = atol(tc + 13);
		if ((long)page * size < total && out->n > n0) {
			char b[16];
			sprintf(b, "%d", page + 1);
			free(out->cont);
			out->cont = xstrdup(b);
		}
		for (i = n0; i < out->n; i++) if (!out->items[i].chid[0]) strncpy(out->items[i].chid, chid, sizeof out->items[i].chid - 1);
	}
	http_resp_free(&r);
	user_name(chid, name, ctl);
	return ok;
}

int yt_channel(const char *chid, const char *cont, int tab, YtList *out, char **name, NetCtl *ctl)
{
	int page = cont ? atoi(cont) : 1;
	if (page < 1) page = 1;
	free(out->cont);
	out->cont = NULL;
	return user_videos(chid, page, 25, tab == 1 ? "viewCount" : "registeredAt", out, name, ctl);
}

int yt_rss(const char *chid, YtList *out, char **name, NetCtl *ctl)
{
	int ok, i;
	YtList tmp;
	memset(&tmp, 0, sizeof tmp);
	ok = user_videos(chid, 1, 15, "registeredAt", &tmp, name, ctl);
	strcpy(out->err, tmp.err);
	for (i = 0; i < tmp.n; i++) {
		YtItem *it = add_item(out, tmp.items[i].id);
		if (!it) continue;
		*it = tmp.items[i];
		it->ord = out->n - 1;
		memset(&tmp.items[i], 0, sizeof tmp.items[i]);
	}
	yt_list_free(&tmp);
	if (ok && !out->n) { strcpy(out->err, "\xED\x88\xAC\xEA\xB3\xA0 \xEC\x98\x81\xEC\x83\x81\xEC\x9D\xB4 \xEC\x97\x86\xEC\x9D\x8C"); return 0; }
	return ok;
}

/* ------------------------------------------------------------------ */
/* streams                                                             */
/* ------------------------------------------------------------------ */
void hls_track_free(HlsTrack *t)
{
	int i;
	for (i = 0; i < t->n; i++) free(t->seg[i]);
	free(t->seg); free(t->t0); free(t->iv); free(t->init);
	memset(t, 0, sizeof *t);
}

void hls_track_copy(HlsTrack *d, const HlsTrack *s)
{
	int i;
	memset(d, 0, sizeof *d);
	d->init = xstrdup(s->init);
	d->n = s->n;
	d->enc = s->enc;
	memcpy(d->key, s->key, 16);
	if (s->n <= 0) return;
	d->seg = (char **)calloc(s->n, sizeof(char *));
	d->t0 = (long *)malloc(s->n * sizeof(long));
	d->iv = (unsigned char (*)[16])malloc(s->n * 16);
	for (i = 0; i < s->n; i++) d->seg[i] = xstrdup(s->seg[i]);
	memcpy(d->t0, s->t0, s->n * sizeof(long));
	memcpy(d->iv, s->iv, s->n * 16);
}

int hls_seg_at(const HlsTrack *t, long ms)
{
	int i;
	for (i = t->n - 1; i > 0; i--) if (t->t0[i] <= ms) return i;
	return 0;
}

void yt_streams_free(YtStreams *s)
{
	int i;
	for (i = 0; i < s->nalt; i++) free(s->alt[i].url);
	free(s->title); free(s->vurl); free(s->aurl);
	hls_track_free(&s->hv);
	hls_track_free(&s->ha);
	memset(s, 0, sizeof *s);
}

/* html attribute value -> text (&quot; &amp; &lt; &gt; &#39; &#NN; &#xNN;) */
static char *html_unescape(const char *s, int n)
{
	char *o = (char *)malloc(n + 1), *w = o;
	const char *e = s + n;
	if (!o) return NULL;
	while (s < e) {
		if (*s == '&') {
			const char *semi = s + 1;
			while (semi < e && semi - s < 10 && *semi != ';') semi++;
			if (semi < e && *semi == ';') {
				int L = (int)(semi - s - 1);
				const char *nm = s + 1;
				if (L == 4 && !strncmp(nm, "quot", 4)) { *w++ = '"'; s = semi + 1; continue; }
				if (L == 3 && !strncmp(nm, "amp", 3)) { *w++ = '&'; s = semi + 1; continue; }
				if (L == 2 && !strncmp(nm, "lt", 2)) { *w++ = '<'; s = semi + 1; continue; }
				if (L == 2 && !strncmp(nm, "gt", 2)) { *w++ = '>'; s = semi + 1; continue; }
				if (L == 4 && !strncmp(nm, "apos", 4)) { *w++ = '\''; s = semi + 1; continue; }
				if (L >= 2 && nm[0] == '#') {
					unsigned long c = (nm[1] == 'x' || nm[1] == 'X') ? strtoul(nm + 2, NULL, 16) : strtoul(nm + 1, NULL, 10);
					if (c < 0x80) *w++ = (char)c;
					else if (c < 0x800) { *w++ = (char)(0xC0 | (c >> 6)); *w++ = (char)(0x80 | (c & 63)); }
					else { *w++ = (char)(0xE0 | (c >> 12)); *w++ = (char)(0x80 | ((c >> 6) & 63)); *w++ = (char)(0x80 | (c & 63)); }
					s = semi + 1;
					continue;
				}
			}
		}
		*w++ = *s++;
	}
	*w = 0;
	return o;
}

/* pick a variant id from media.contents.videos/audios. quality: 0 ~240p, 1 lowest, 2 ~360p */
static const char *pick_variant(JNode *arr, int video, int quality, int *ow, int *oh)
{
	JNode *v;
	const char *best = NULL;
	long bestscore = 0x7fffffff;
	for (v = arr ? arr->child : NULL; v; v = v->next) {
		const char *id = jstr(jget(v, "id"));
		long w, h, br, score;
		if (!id || !jget(v, "isAvailable") || !jget(v, "isAvailable")->bval) continue;
		br = jnum(jget(v, "bitRate"), 0);
		if (!video) {
			score = br;     /* lowest audio bitrate: fewer bytes, 22 kHz output anyway */
		} else {
			w = jnum(jget(v, "width"), 0);
			h = jnum(jget(v, "height"), 0);
			if (h > w) { long t = w; w = h; h = t; }  /* portrait: use the short side */
			if (quality == 1) score = w * h;
			else {
				long want = quality == 2 ? 360 : 240;
				score = h > want ? 100000 + (h - want) * 10 : (want - h);
			}
			if (score < bestscore) { if (ow) *ow = (int)jnum(jget(v, "width"), 0); if (oh) *oh = (int)jnum(jget(v, "height"), 0); }
		}
		if (score < bestscore) { bestscore = score; best = id; }
	}
	return best;
}

static int hexval(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static void parse_iv(const char *hex, unsigned char iv[16])
{
	int i;
	memset(iv, 0, 16);
	if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex += 2;
	for (i = 0; i < 16 && hexval(hex[0]) >= 0 && hexval(hex[1]) >= 0; i++, hex += 2) iv[i] = (unsigned char)(hexval(hex[0]) * 16 + hexval(hex[1]));
}

static char *attr(const char *line, const char *name)
{
	char pat[24];
	const char *a, *b;
	_snprintf(pat, sizeof pat, "%s=", name);
	a = strstr(line, pat);
	if (!a) return NULL;
	a += strlen(pat);
	if (*a == '"') { a++; b = strchr(a, '"'); }
	else { b = a; while (*b && *b != ',' && *b != '\r' && *b != '\n') b++; }
	if (!b) return NULL;
	return xstrndup(a, (int)(b - a));
}

static int http_text(const char *url, NetCtl *ctl, char **out, char *err, int errlen)
{
	HttpResp r;
	*out = NULL;
	if (!http_request(url, "GET", NULL, 0, NULL, "https://www.nicovideo.jp/", &r, ctl, 1024 * 1024) || r.status != 200) {
		_snprintf(err, errlen, "\xEC\x9E\xAC\xEC\x83\x9D \xEB\xAA\xA9\xEB\xA1\x9D \xEC\x98\xA4\xEB\xA5\x98 (HTTP %d %s)", r.status, r.err);
		err[errlen - 1] = 0;
		http_resp_free(&r);
		return 0;
	}
	*out = xstrndup(r.body.p, r.body.len);
	http_resp_free(&r);
	return 1;
}

static char *g_hls_keyurl;   /* key URL seen by parse_media (under g_ycs) */

/* media playlist text -> track (relative URLs resolved against base) */
static int parse_media(const char *text, const char *base, HlsTrack *t)
{
	const char *p = text;
	long tms = 0, dur = 0;
	unsigned char iv[16];
	int cap = 0, have_iv = 0;
	memset(iv, 0, sizeof iv);
	while (*p) {
		const char *e = p;
		char *line;
		while (*e && *e != '\n') e++;
		line = xstrndup(p, (int)(e - p));
		{ int L = (int)strlen(line); while (L && (line[L - 1] == '\r' || line[L - 1] == ' ')) line[--L] = 0; }
		if (!strncmp(line, "#EXT-X-MAP:", 11)) {
			char *u = attr(line, "URI");
			if (u) { free(t->init); t->init = url_resolve(base, u); free(u); }
		} else if (!strncmp(line, "#EXT-X-KEY:", 11)) {
			char *m = attr(line, "METHOD"), *u = attr(line, "URI"), *ivs = attr(line, "IV");
			if (m && !strcmp(m, "AES-128")) {
				t->enc = 1;
				if (u) { free(g_hls_keyurl); g_hls_keyurl = url_resolve(base, u); }
				if (ivs) { parse_iv(ivs, iv); have_iv = 1; }
			} else if (m && !strcmp(m, "NONE")) t->enc = 0;
			free(m); free(u); free(ivs);
		} else if (!strncmp(line, "#EXTINF:", 8)) {
			dur = (long)(atof(line + 8) * 1000.0 + 0.5);
		} else if (line[0] && line[0] != '#') {
			if (t->n >= cap) {
				cap = cap ? cap * 2 : 64;
				t->seg = (char **)realloc(t->seg, cap * sizeof(char *));
				t->t0 = (long *)realloc(t->t0, cap * sizeof(long));
				t->iv = (unsigned char (*)[16])realloc(t->iv, cap * 16);
			}
			t->seg[t->n] = url_resolve(base, line);
			t->t0[t->n] = tms;
			if (have_iv) memcpy(t->iv[t->n], iv, 16);
			else {
				/* no IV attribute: the media sequence number (1-based here) */
				unsigned long sq = (unsigned long)t->n + 1;
				memset(t->iv[t->n], 0, 16);
				t->iv[t->n][12] = (unsigned char)(sq >> 24); t->iv[t->n][13] = (unsigned char)(sq >> 16);
				t->iv[t->n][14] = (unsigned char)(sq >> 8); t->iv[t->n][15] = (unsigned char)sq;
			}
			t->n++;
			tms += dur;
		}
		free(line);
		p = *e ? e + 1 : e;
	}
	return t->n > 0 && t->init;
}

static int load_track(const char *url, HlsTrack *t, NetCtl *ctl, char *err, int errlen)
{
	char *text;
	HttpResp r;
	int ok;
	if (!http_text(url, ctl, &text, err, errlen)) return 0;
	EnterCriticalSection(&g_ycs);
	free(g_hls_keyurl);
	g_hls_keyurl = NULL;
	ok = parse_media(text, url, t);
	{
		char *ku = g_hls_keyurl;
		g_hls_keyurl = NULL;
		LeaveCriticalSection(&g_ycs);
		free(text);
		if (!ok) { _snprintf(err, errlen, "\xEC\x9E\xAC\xEC\x83\x9D \xEB\xAA\xA9\xEB\xA1\x9D\xEC\x9D\x84 \xEC\x9D\xBD\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x9D\x8C"); free(ku); return 0; }
		if (t->enc) {
			if (!ku) { _snprintf(err, errlen, "\xEC\x95\x94\xED\x98\xB8 \xED\x82\xA4 \xEC\xA3\xBC\xEC\x86\x8C\xEA\xB0\x80 \xEC\x97\x86\xEC\x9D\x8C"); return 0; }
			ok = http_request(ku, "GET", NULL, 0, NULL, "https://www.nicovideo.jp/", &r, ctl, 4096) && r.status == 200 && r.body.len == 16;
			if (ok) memcpy(t->key, r.body.p, 16);
			else { _snprintf(err, errlen, "\xED\x82\xA4\xEB\xA5\xBC \xEB\xB0\x9B\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x9D\x8C (HTTP %d %s)", r.status, r.err); err[errlen - 1] = 0; }
			http_resp_free(&r);
			free(ku);
			if (!ok) return 0;
		} else free(ku);
	}
	return 1;
}

static void json_esc(Buf *b, const char *s)
{
	buf_addc(b, '"');
	for (; *s; s++) {
		if (*s == '"' || *s == '\\') { buf_addc(b, '\\'); buf_addc(b, *s); }
		else if ((unsigned char)*s < 32) buf_addf(b, "\\u%04x", (unsigned char)*s);
		else buf_addc(b, *s);
	}
	buf_addc(b, '"');
}

/* watch page -> metadata, variant ids, access right key. 1 ok, 0 error (out->err) */
int yt_parse_player(const char *body, int len, int quality, YtStreams *out)
{
	const char *a = NULL, *e;
	char *js;
	JDoc *d;
	JNode *root, *w;
	(void)len;
	static const char pat[] = "name=\"server-response\" content=\"";
	a = strstr(body, pat);
	if (!a) { strcpy(out->err, "\xEC\x8B\x9C\xEC\xB2\xAD \xED\x8E\x98\xEC\x9D\xB4\xEC\xA7\x80\xEB\xA5\xBC \xED\x95\xB4\xEC\x84\x9D\xED\x95\xA0 \xEC\x88\x98 \xEC\x97\x86\xEC\x9D\x8C"); return 0; }
	a += sizeof pat - 1;
	e = strchr(a, '"');
	if (!e) { strcpy(out->err, "\xEC\x8B\x9C\xEC\xB2\xAD \xED\x8E\x98\xEC\x9D\xB4\xEC\xA7\x80\xEA\xB0\x80 \xEC\x9E\x98\xEB\xA0\xA4 \xEC\x98\xB4"); return 0; }
	js = html_unescape(a, (int)(e - a));
	d = js ? json_parse(js, (int)strlen(js)) : NULL;
	free(js);
	if (!d) { strcpy(out->err, "\xEC\x8B\x9C\xEC\xB2\xAD \xEC\xA0\x95\xEB\xB3\xB4\xEB\xA5\xBC \xED\x95\xB4\xEC\x84\x9D\xED\x95\xA0 \xEC\x88\x98 \xEC\x97\x86\xEC\x9D\x8C"); return 0; }
	root = json_root(d);
	{
		long st = jnum(jpath(root, "meta.status"), 0);
		if (st != 200) {
			const char *code = jpstr(root, "meta.code");
			if (st == 403) _snprintf(out->err, sizeof out->err, "\xEC\x9D\xB4 \xEC\x98\x81\xEC\x83\x81\xEC\x9D\x80 \xEB\xB3\xBC \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (\xEB\xB9\x84\xEA\xB3\xB5\xEA\xB0\x9C / \xEC\xA7\x80\xEC\x97\xAD \xEC\xA0\x9C\xED\x95\x9C / \xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8 \xED\x95\x84\xEC\x9A\x94 \xEB\x93\xB1, %s)", code ? code : "403");
			else if (st == 404) strcpy(out->err, "\xEC\x82\xAD\xEC\xA0\x9C\xEB\x90\x98\xEC\x97\x88\xEA\xB1\xB0\xEB\x82\x98 \xEC\x97\x86\xEB\x8A\x94 \xEC\x98\x81\xEC\x83\x81\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4");
			else _snprintf(out->err, sizeof out->err, "\xEC\x8B\x9C\xEC\xB2\xAD \xED\x8E\x98\xEC\x9D\xB4\xEC\xA7\x80 \xEC\x98\xA4\xEB\xA5\x98 (%ld %s)", st, code ? code : "");
			json_free(d);
			return 0;
		}
	}
	w = jpath(root, "data.response.$watchV4.data");
	if (!w) w = jpath(root, "data.response");
	{
		const char *t = jpstr(w, "video.title");
		long dur = jnum(jpath(w, "video.duration"), 0);
		const char *key = jpstr(w, "media.accessRightKey");
		const char *trk = jpstr(w, "client.watchTrackId");
		const char *vid, *aid;
		out->title = xstrdup(t ? t : "");
		out->dur_ms = dur * 1000;
		if (!key) {
			strcpy(out->err, "\xEC\x9D\xB4 \xEC\x98\x81\xEC\x83\x81\xEC\x9D\x80 \xEC\x9E\xAC\xEC\x83\x9D\xED\x95\xA0 \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (\xEC\x9C\xA0\xEB\xA3\x8C / \xED\x94\x84\xEB\xA6\xAC\xEB\xAF\xB8\xEC\x97\x84 \xEC\xA0\x84\xEC\x9A\xA9 / \xEC\xA7\x80\xEC\x97\xAD \xEC\xA0\x9C\xED\x95\x9C)");
			json_free(d);
			return 0;
		}
		vid = pick_variant(jpath(w, "media.contents.videos"), 1, quality, &out->w, &out->h);
		aid = pick_variant(jpath(w, "media.contents.audios"), 0, quality, NULL, NULL);
		if (!vid) { strcpy(out->err, "\xEC\x9E\xAC\xEC\x83\x9D \xEA\xB0\x80\xEB\x8A\xA5\xED\x95\x9C \xED\x99\x94\xEC\xA7\x88\xEC\x9D\xB4 \xEC\x97\x86\xEC\x9D\x8C"); json_free(d); return 0; }
		/* stash for yt_streams: vurl = variant ids, aurl = access key, ua = track id */
		{
			Buf b;
			buf_init(&b);
			buf_adds(&b, "{\"outputs\":[[");
			json_esc(&b, vid);
			if (aid) { buf_addc(&b, ','); json_esc(&b, aid); }
			buf_adds(&b, "]]}");
			out->vurl = buf_detach(&b);
		}
		out->aurl = xstrdup(key);
		strncpy(out->ua, trk ? trk : "", sizeof out->ua - 1);
		strncpy(out->client, vid, sizeof out->client - 1);
	}
	json_free(d);
	return 1;
}

int yt_streams_from(const char *id, int quality, int first_client, YtStreams *out, NetCtl *ctl)
{
	(void)first_client;
	return yt_streams(id, quality, out, ctl);
}

int yt_streams(const char *id, int quality, YtStreams *out, NetCtl *ctl)
{
	char url[300], *master = NULL, *vpl = NULL, *apl = NULL, *body;
	HttpResp r;
	const char *oh = ctl->hdrs, *oua = ctl->ua;
	char hdrs[2600];
	int ok = 0;
	memset(out, 0, sizeof *out);
	strncpy(out->id, id, sizeof out->id - 1);
	out->quality = quality;
	out->vsize = out->asize = -1;
	/* 1. watch page */
	_snprintf(url, sizeof url, "https://www.nicovideo.jp/watch/%s", id);
	ctl->ua = PC_UA;
	if (!http_request(url, "GET", NULL, 0, NULL, NULL, &r, ctl, 3 * 1024 * 1024) || !r.body.len) {
		_snprintf(out->err, sizeof out->err, "\xEB\x84\xA4\xED\x8A\xB8\xEC\x9B\x8C\xED\x81\xAC \xEC\x98\xA4\xEB\xA5\x98: %s", r.err);
		http_resp_free(&r);
		ctl->ua = oua;
		return 0;
	}
	buf_addc(&r.body, 0);
	if (!yt_parse_player(r.body.p, r.body.len, quality, out)) { http_resp_free(&r); ctl->ua = oua; return 0; }
	http_resp_free(&r);
	/* 2. access rights */
	_snprintf(url, sizeof url, "https://nvapi.nicovideo.jp/v1/watch/%s/access-rights/hls?actionTrackId=%s", id, out->ua);
	_snprintf(hdrs, sizeof hdrs, "%sX-Request-With: https://www.nicovideo.jp\r\nX-Access-Right-Key: %s\r\nOrigin: https://www.nicovideo.jp\r\n", NV_HDRS, out->aurl);
	hdrs[sizeof hdrs - 1] = 0;
	ctl->hdrs = hdrs;
	body = out->vurl;
	if (!http_request(url, "POST", body, (int)strlen(body), "application/json", "https://www.nicovideo.jp/", &r, ctl, 200000) || (r.status != 200 && r.status != 201)) {
		_snprintf(out->err, sizeof out->err, "\xEC\x9E\xAC\xEC\x83\x9D \xEA\xB6\x8C\xED\x95\x9C\xEC\x9D\x84 \xEB\xB0\x9B\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x9D\x8C (HTTP %d %s)", r.status, r.err);
		http_resp_free(&r);
		goto out;
	}
	ctl->hdrs = oh;
	{
		JDoc *d = json_parse(r.body.p, r.body.len);
		const char *cu = d ? jpstr(json_root(d), "data.contentUrl") : NULL;
		master = cu ? xstrdup(cu) : NULL;
		if (d) json_free(d);
	}
	http_resp_free(&r);
	if (!master) { strcpy(out->err, "\xEC\x9E\xAC\xEC\x83\x9D \xEC\xA3\xBC\xEC\x86\x8C\xEA\xB0\x80 \xEC\x97\x86\xEC\x9D\x8C"); goto out; }
	/* 3. master playlist */
	{
		char *text, *p;
		if (!http_text(master, ctl, &text, out->err, sizeof out->err)) goto out;
		for (p = text; *p; ) {
			char *e = p, *line;
			while (*e && *e != '\n') e++;
			line = xstrndup(p, (int)(e - p));
			{ int L = (int)strlen(line); while (L && (line[L - 1] == '\r' || line[L - 1] == ' ')) line[--L] = 0; }
			if (!strncmp(line, "#EXT-X-MEDIA:", 13) && strstr(line, "TYPE=AUDIO") && !apl) {
				char *u = attr(line, "URI");
				if (u) { apl = url_resolve(master, u); free(u); }
			} else if (line[0] && line[0] != '#' && !vpl) vpl = url_resolve(master, line);
			free(line);
			p = *e ? e + 1 : e;
		}
		free(text);
	}
	if (!vpl) { strcpy(out->err, "\xEC\x98\x81\xEC\x83\x81 \xEB\xAA\xA9\xEB\xA1\x9D\xEC\x9D\xB4 \xEC\x97\x86\xEC\x9D\x8C"); goto out; }
	/* 4. media playlists + keys */
	if (!load_track(vpl, &out->hv, ctl, out->err, sizeof out->err)) goto out;
	if (apl && !load_track(apl, &out->ha, ctl, out->err, sizeof out->err)) goto out;
	out->hls = 1;
	free(out->vurl); out->vurl = xstrdup(out->hv.init);
	free(out->aurl); out->aurl = apl ? xstrdup(out->ha.init) : NULL;
	strncpy(out->ua, PC_UA, sizeof out->ua - 1);
	tube_log("nico %s: %s %dx%d, %d+%d segments", id, out->client, out->w, out->h, out->hv.n, out->ha.n);
	ok = 1;
out:
	ctl->hdrs = oh;
	ctl->ua = oua;
	free(master); free(vpl); free(apl);
	return ok;
}
