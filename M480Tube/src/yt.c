/* M480Tube - YouTube search / stream lookup via the innertube JSON API.
 * Client identities, endpoints and format preferences come from a config
 * file that is refreshed from GitHub at start-up, so breakages on YouTube's
 * side can be fixed without a new build. */
#include "util.h"
#include "net.h"
#include "json.h"
#include "yt.h"
#include "tubelog.h"
#include <stdio.h>

#define CONFIG_URL "https://raw.githubusercontent.com/skiqac2-spec/M480-APP/main/M480Tube/m480tube_config.txt"
#define CONFIG_FILE L"m480tube_config.txt"

typedef struct {
	char name[40], ver[48], id[8];
	char ua[320];
	char url[300];          /* endpoint; {T} {ID} replaced */
	char visitor_url[200];  /* if set: fetch visitorData first */
	char client_extra[600]; /* extra members of context.client */
	char body_extra[400];   /* extra members of context (request/user objects) */
	char headers[400];      /* extra headers, separated by '|' */
	char params[80];        /* search filter params */
	int media_post, media_cpn, media_rn;
	char visitor[600];      /* cached visitorData */
} Client;

#define MAXCL 6
static Client g_search;
static Client g_player[MAXCL];
static int g_nplayer;
static int g_vpref[3][16], g_nvpref[3];
static int g_apref[16], g_napref;
static char g_hl[8], g_gl[8];
static char g_channel_params[80] = "EgZ2aWRlb3PyBgQKAjoA";
static char g_streams_params[80] = "EgdzdHJlYW1z8gYECgJ6AA%3D%3D";
static CRITICAL_SECTION g_ycs;
static int g_ycs_init;
YtTune g_tune;

static const char *g_builtin =
	"version=2\n"
	"hl=ko\n"
	"gl=KR\n"
	"[search]\n"
	"name=WEB\n"
	"version=2.20261019.01.00\n"
	"id=1\n"
	"ua=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36\n"
	"url=https://www.youtube.com/youtubei/v1/search?prettyPrint=false\n"
	"client_extra=\"originalUrl\":\"https://www.youtube.com\",\"platform\":\"DESKTOP\",\"utcOffsetMinutes\":540\n"
	"body_extra=\"request\":{\"internalExperimentFlags\":[],\"useSsl\":true},\"user\":{\"lockedSafetyMode\":false}\n"
	"headers=Origin: https://www.youtube.com|Referer: https://www.youtube.com|Cookie: SOCS=CAE=|X-YouTube-Client-Name: 1|X-YouTube-Client-Version: 2.20261019.01.00\n"
	"params=EgIQAQ%3D%3D\n"
	"[player]\n"
	"name=VISIONOS\n"
	"version=1.04\n"
	"id=101\n"
	"ua=com.google.visionos.youtube/1.04(RealityDevice17,1; U; CPU visionOS 26_6_0 like Mac OS X; KR)\n"
	"url=https://youtubei.googleapis.com/youtubei/v1/player?prettyPrint=false&t={T}&id={ID}\n"
	"visitor_url=https://www.youtube.com/youtubei/v1/visitor_id?prettyPrint=false\n"
	"client_extra=\"clientScreen\":\"WATCH\",\"platform\":\"MOBILE\",\"deviceMake\":\"Apple\",\"deviceModel\":\"RealityDevice17,1\",\"osName\":\"visionOS\",\"osVersion\":\"26.6.0.23O770\",\"utcOffsetMinutes\":0\n"
	"body_extra=\"request\":{\"internalExperimentFlags\":[],\"useSsl\":true},\"user\":{\"lockedSafetyMode\":false}\n"
	"headers=X-Goog-Api-Format-Version: 2\n"
	"media_post=1\n"
	"media_cpn=1\n"
	"media_rn=0\n"
	"[player]\n"
	"name=ANDROID_VR\n"
	"version=1.65.10\n"
	"id=28\n"
	"ua=com.google.android.apps.youtube.vr.oculus/1.65.10 (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip\n"
	"url=https://www.youtube.com/youtubei/v1/player?prettyPrint=false\n"
	"visitor_url=https://www.youtube.com/youtubei/v1/visitor_id?prettyPrint=false\n"
	"client_extra=\"deviceMake\":\"Oculus\",\"deviceModel\":\"Quest 3\",\"androidSdkVersion\":32,\"osName\":\"Android\",\"osVersion\":\"12L\"\n"
	"headers=X-YouTube-Client-Name: 28|X-YouTube-Client-Version: 1.65.10\n"
	"media_post=0\n"
	"media_cpn=0\n"
	"[formats]\n"
	"video240=133,160,134\n"
	"video144=160,133\n"
	"video360=134,133,160\n"
	"audio=140,139\n"
	"[tuning]\n"
	"chunk_kb=1024\n"
	"asm=1\n"
	"loopfilter=0\n"
	"notice=\n";

static void parse_ints(const char *s, int *out, int *n, int max)
{
	*n = 0;
	while (*s && *n < max) {
		while (*s == ' ' || *s == ',') s++;
		if (!*s) break;
		out[(*n)++] = atoi(s);
		while (*s && *s != ',') s++;
	}
}

#define CPY(dst, v) do { strncpy(dst, v, sizeof(dst) - 1); dst[sizeof(dst) - 1] = 0; } while (0)

static void client_key(Client *c, const char *k, const char *v)
{
	if (!strcmp(k, "name")) CPY(c->name, v);
	else if (!strcmp(k, "version")) CPY(c->ver, v);
	else if (!strcmp(k, "id")) CPY(c->id, v);
	else if (!strcmp(k, "ua")) CPY(c->ua, v);
	else if (!strcmp(k, "url")) CPY(c->url, v);
	else if (!strcmp(k, "visitor_url")) CPY(c->visitor_url, v);
	else if (!strcmp(k, "client_extra")) CPY(c->client_extra, v);
	else if (!strcmp(k, "body_extra")) CPY(c->body_extra, v);
	else if (!strcmp(k, "headers")) CPY(c->headers, v);
	else if (!strcmp(k, "params")) CPY(c->params, v);
	else if (!strcmp(k, "media_post")) c->media_post = atoi(v);
	else if (!strcmp(k, "media_cpn")) c->media_cpn = atoi(v);
	else if (!strcmp(k, "media_rn")) c->media_rn = atoi(v);
}

/* returns 1 if the text looked like a valid config */
static int apply_config(const char *text)
{
	const char *p = text;
	enum { S_TOP, S_SEARCH, S_PLAYER, S_OTHER } sec = S_TOP;
	Client *cur = NULL;
	int nplayer = 0;
	Client *pl;
	if (!strstr(text, "version=")) return 0;
	pl = (Client *)calloc(MAXCL, sizeof(Client));
	if (!pl) return 0;
	while (*p) {
		const char *e = p;
		char line[1200];
		int l;
		char *eq;
		while (*e && *e != '\n') e++;
		l = (int)(e - p);
		if (l > (int)sizeof line - 1) l = sizeof line - 1;
		memcpy(line, p, l);
		line[l] = 0;
		while (l > 0 && (line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
		p = *e ? e + 1 : e;
		if (!line[0] || line[0] == '#') continue;
		if (line[0] == '[') {
			if (!strncmp(line, "[search]", 8)) { sec = S_SEARCH; memset(&g_search, 0, sizeof g_search); cur = &g_search; }
			else if (!strncmp(line, "[player]", 8)) {
				sec = S_PLAYER;
				cur = nplayer < MAXCL ? &pl[nplayer++] : NULL;
			} else { sec = S_OTHER; cur = NULL; }
			continue;
		}
		eq = strchr(line, '=');
		if (!eq) continue;
		*eq = 0;
		{
			const char *k = line, *v = eq + 1;
			if ((sec == S_SEARCH || sec == S_PLAYER) && cur) client_key(cur, k, v);
			else if (!strcmp(k, "hl")) CPY(g_hl, v);
			else if (!strcmp(k, "channel_params")) CPY(g_channel_params, v);
			else if (!strcmp(k, "streams_params")) CPY(g_streams_params, v);
			else if (!strcmp(k, "gl")) CPY(g_gl, v);
			else if (!strcmp(k, "video240")) parse_ints(v, g_vpref[0], &g_nvpref[0], 16);
			else if (!strcmp(k, "video144")) parse_ints(v, g_vpref[1], &g_nvpref[1], 16);
			else if (!strcmp(k, "video360")) parse_ints(v, g_vpref[2], &g_nvpref[2], 16);
			else if (!strcmp(k, "audio")) parse_ints(v, g_apref, &g_napref, 16);
			else if (!strcmp(k, "chunk_kb")) { g_tune.chunk_kb = atoi(v); if (g_tune.chunk_kb < 64) g_tune.chunk_kb = 64; if (g_tune.chunk_kb > 8192) g_tune.chunk_kb = 8192; }
			else if (!strcmp(k, "asm")) g_tune.asm_on = atoi(v);
			else if (!strcmp(k, "loopfilter")) g_tune.loopfilter = atoi(v);
			else if (!strcmp(k, "notice")) CPY(g_tune.notice, v);
		}
	}
	if (nplayer > 0) {
		memcpy(g_player, pl, sizeof(Client) * nplayer);
		g_nplayer = nplayer;
	}
	free(pl);
	return 1;
}

static char *read_file_w(const WCHAR *name, int *len)
{
	WCHAR path[MAX_PATH];
	FILE *f;
	char *d;
	long n;
	app_path(path, name);
	f = _wfopen(path, L"rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n <= 0 || n > 200000) { fclose(f); return NULL; }
	d = (char *)malloc(n + 1);
	if (!d) { fclose(f); return NULL; }
	n = (long)fread(d, 1, n, f);
	d[n] = 0;
	fclose(f);
	if (len) *len = (int)n;
	return d;
}

void yt_config_init(void)
{
	char *t;
	if (!g_ycs_init) { InitializeCriticalSection(&g_ycs); g_ycs_init = 1; }
	memset(&g_tune, 0, sizeof g_tune);
	apply_config(g_builtin);
	t = read_file_w(CONFIG_FILE, NULL);
	if (t) {
		/* only accept a cached config of the current format */
		if (strstr(t, "version=2") || strstr(t, "version=3")) apply_config(t);
		free(t);
	}
}

int yt_config_update(NetCtl *ctl)
{
	HttpResp r;
	int ok = 0;
	if (http_request(CONFIG_URL, "GET", NULL, 0, NULL, NULL, &r, ctl, 200000) && r.status == 200 && r.body.len > 0) {
		char *t = xstrndup(r.body.p, r.body.len);
		char *old = read_file_w(CONFIG_FILE, NULL);
		if ((strstr(t, "version=2") || strstr(t, "version=3")) && (!old || strcmp(old, t))) {
			WCHAR path[MAX_PATH];
			FILE *f;
			EnterCriticalSection(&g_ycs);
			memset(&g_tune, 0, sizeof g_tune);
			apply_config(g_builtin);
			apply_config(t);
			LeaveCriticalSection(&g_ycs);
			app_path(path, CONFIG_FILE);
			f = _wfopen(path, L"wb");
			if (f) { fwrite(t, 1, strlen(t), f); fclose(f); }
			ok = 1;
			tube_log("config updated");
		}
		free(old);
		free(t);
	}
	http_resp_free(&r);
	return ok;
}

/* ------------------------------------------------------------------ */
static void json_esc(Buf *b, const char *s)
{
	buf_addc(b, '"');
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\') { buf_addc(b, '\\'); buf_addc(b, (char)c); }
		else if (c < 0x20) buf_addf(b, "\\u%04x", c);
		else buf_addc(b, (char)c);
	}
	buf_addc(b, '"');
}

static void rand_str(char *out, int n)
{
	static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	unsigned char r[32];
	int i;
	entropy_stir(out, n);
	entropy_get(r);
	for (i = 0; i < n; i++) out[i] = abc[r[i % 32] & 63];
	out[n] = 0;
}

/* body: {"context":{"client":{...},<body_extra>} */
static void client_ctx(Buf *b, const Client *c, int with_visitor)
{
	buf_adds(b, "{\"context\":{\"client\":{\"clientName\":");
	json_esc(b, c->name);
	buf_adds(b, ",\"clientVersion\":");
	json_esc(b, c->ver);
	if (with_visitor && c->visitor[0]) { buf_adds(b, ",\"visitorData\":"); json_esc(b, c->visitor); }
	buf_addf(b, ",\"hl\":\"%s\",\"gl\":\"%s\"", g_hl, g_gl);
	if (c->client_extra[0]) { buf_addc(b, ','); buf_adds(b, c->client_extra); }
	buf_adds(b, "}");
	if (c->body_extra[0]) { buf_addc(b, ','); buf_adds(b, c->body_extra); }
	buf_adds(b, "}");
}

static char *expand_url(const char *tmpl, const char *id)
{
	Buf b;
	const char *p;
	buf_init(&b);
	for (p = tmpl; *p; p++) {
		if (!strncmp(p, "{T}", 3)) { char t[16]; rand_str(t, 12); buf_adds(&b, t); p += 2; }
		else if (!strncmp(p, "{ID}", 4)) { buf_adds(&b, id ? id : ""); p += 3; }
		else buf_addc(&b, *p);
	}
	return buf_detach(&b);
}

static int api_post(const Client *c, const char *url, const char *body, int bodylen, HttpResp *r, NetCtl *ctl, char *err, int errlen)
{
	char hdrs[600];
	NetCtl local;
	const char *oua, *ohd;
	int i, j;
	if (!ctl) { memset(&local, 0, sizeof local); ctl = &local; }
	oua = ctl->ua; ohd = ctl->hdrs;
	/* '|' separated header list -> CRLF lines */
	for (i = j = 0; c->headers[i] && j < (int)sizeof hdrs - 4; i++) {
		if (c->headers[i] == '|') { hdrs[j++] = '\r'; hdrs[j++] = '\n'; }
		else hdrs[j++] = c->headers[i];
	}
	if (j > 0) { hdrs[j++] = '\r'; hdrs[j++] = '\n'; }
	hdrs[j] = 0;
	ctl->ua = c->ua[0] ? c->ua : NULL;
	ctl->hdrs = hdrs;
	if (!http_request(url, "POST", body, bodylen, "application/json", NULL, r, ctl, 6 * 1024 * 1024)) {
		ctl->ua = oua; ctl->hdrs = ohd;
		_snprintf(err, errlen, "%s", r->err[0] ? r->err : "네트워크 오류");
		tube_log("api %s: %s", c->name, err);
		return 0;
	}
	ctl->ua = oua; ctl->hdrs = ohd;
	if (r->status != 200) {
		if (r->ctype && strstr(r->ctype, "html"))
			_snprintf(err, errlen, "유튜브가 요청을 거부함 (HTTP %d, 자동 요청 차단일 수 있음)", r->status);
		else _snprintf(err, errlen, "유튜브 응답 오류 (HTTP %d)", r->status);
		tube_log("api %s: HTTP %d %.200s", c->name, r->status, r->body.p ? r->body.p : "");
		return 0;
	}
	return 1;
}

/* ------------------------------------------------------------------ */
/* search                                                              */
/* ------------------------------------------------------------------ */
static char *runs_text(JNode *n)
{
	JNode *runs, *r;
	Buf b;
	const char *s;
	if (!n) return NULL;
	if (n->type == J_STR) return xstrdup(n->str);
	if ((s = jpstr(n, "simpleText")) != NULL) return xstrdup(s);
	if ((s = jpstr(n, "content")) != NULL) return xstrdup(s);
	runs = jget(n, "runs");
	if (!runs) return NULL;
	buf_init(&b);
	for (r = runs->child; r; r = r->next) { s = jpstr(r, "text"); if (s) buf_adds(&b, s); }
	if (!b.len) { buf_free(&b); return NULL; }
	return buf_detach(&b);
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
	return it;
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

static void on_video_renderer(JNode *v, void *ctx)
{
	YtList *l = (YtList *)ctx;
	YtItem *it = add_item(l, jpstr(v, "videoId"));
	JNode *t;
	if (!it) return;
	it->title = runs_text(jget(v, "title"));
	if (!it->title) it->title = runs_text(jget(v, "headline"));
	t = jget(v, "ownerText");
	if (!t) t = jget(v, "longBylineText");
	if (!t) t = jget(v, "shortBylineText");
	it->channel = runs_text(t);
	{
		const char *b = jpstr(t, "runs.0.navigationEndpoint.browseEndpoint.browseId");
		if (b && strlen(b) < sizeof it->chid) strcpy(it->chid, b);
	}
	it->length = runs_text(jget(v, "lengthText"));
	if (!it->length) {
		const char *s = jpstr(v, "thumbnailOverlays.0.thumbnailOverlayTimeStatusRenderer.text.simpleText");
		if (!s) s = jpstr(v, "thumbnailOverlays.0.thumbnailOverlayTimeStatusRenderer.text.runs.0.text");
		if (s) it->length = xstrdup(s);
	}
	it->meta = join_meta(runs_text(jget(v, "shortViewCountText")), runs_text(jget(v, "publishedTimeText")));
	if (!it->meta) it->meta = runs_text(jget(v, "viewCountText"));
	if (!it->title) it->title = xstrdup("(제목 없음)");
}

typedef struct { Buf *b; } MetaCtx;

static void collect_content(JNode *n, void *ctx)
{
	MetaCtx *m = (MetaCtx *)ctx;
	const char *s = jpstr(n, "content");
	if (!s) s = jstr(n);
	if (s && *s) { if (m->b->len) buf_adds(m->b, " \xC2\xB7 "); buf_adds(m->b, s); }
}

static void first_text(JNode *n, void *ctx)
{
	char **out = (char **)ctx;
	const char *s = jstr(n);
	if (!*out && s && *s) *out = xstrdup(s);
}

static void on_lockup(JNode *v, void *ctx)
{
	YtList *l = (YtList *)ctx;
	const char *ct = jpstr(v, "contentType");
	YtItem *it;
	JNode *md;
	if (ct && !strstr(ct, "VIDEO")) return;
	it = add_item(l, jpstr(v, "contentId"));
	if (!it) return;
	md = jpath(v, "metadata.lockupMetadataViewModel");
	it->title = runs_text(jget(md, "title"));
	if (!it->title) it->title = xstrdup("(제목 없음)");
	{
		char *b = NULL;
		jfind(md, "browseId", first_text, &b);
		if (b && !strncmp(b, "UC", 2) && strlen(b) < sizeof it->chid) strcpy(it->chid, b);
		free(b);
	}
	{
		JNode *rows = jpath(md, "metadata.contentMetadataViewModel.metadataRows");
		JNode *r;
		int nrows = 0;
		for (r = rows ? rows->child : NULL; r; r = r->next) nrows++;
		if (nrows == 1) {
			/* channel pages: the only row is "views · date" */
			Buf b;
			MetaCtx m;
			buf_init(&b);
			m.b = &b;
			jfind(rows->child, "text", collect_content, &m);
			if (b.len) it->meta = buf_detach(&b); else buf_free(&b);
			rows = NULL;
		}
		for (r = rows ? rows->child : NULL; r; r = r->next) {
			Buf b;
			MetaCtx m;
			buf_init(&b);
			m.b = &b;
			jfind(r, "text", collect_content, &m);
			if (b.len) {
				char *s = buf_detach(&b);
				if (!it->channel) it->channel = s;
				else it->meta = join_meta(it->meta, s);
			} else buf_free(&b);
		}
		/* channel tabs: "views · date" row followed by a badges row -> it is meta, not the channel */
		if (it->channel && !it->meta && (strstr(it->channel, "\xEC\xA1\xB0\xED\x9A\x8C\xEC\x88\x98") || strstr(it->channel, " view") ||
		    strstr(it->channel, "Streamed") || strstr(it->channel, "\xEC\x8A\xA4\xED\x8A\xB8\xEB\xA6\xAC\xEB\xB0\x8D"))) {
			it->meta = it->channel;
			it->channel = NULL;
		}
	}
	{
		JNode *ov = jpath(v, "contentImage.thumbnailViewModel.overlays");
		char *txt = NULL;
		JNode *o;
		for (o = ov ? ov->child : NULL; o && !txt; o = o->next) {
			JNode *b = jpath(o, "thumbnailOverlayBadgeViewModel.thumbnailBadges.0.thumbnailBadgeViewModel.text");
			if (b && jstr(b)) txt = xstrdup(jstr(b));
			if (!txt) {
				JNode *bb = jget(o, "thumbnailBottomOverlayViewModel");
				if (bb) jfind(bb, "text", first_text, &txt);
			}
		}
		it->length = txt;
	}
}

static void on_shorts(JNode *v, void *ctx)
{
	YtList *l = (YtList *)ctx;
	YtItem *it = add_item(l, jpstr(v, "onTap.innertubeCommand.reelWatchEndpoint.videoId"));
	const char *s;
	if (!it) return;
	s = jpstr(v, "overlayMetadata.primaryText.content");
	it->title = xstrdup(s ? s : "(쇼츠)");
	it->length = xstrdup("쇼츠");
	s = jpstr(v, "overlayMetadata.secondaryText.content");
	if (s) it->meta = xstrdup(s);
}

static void on_cont(JNode *v, void *ctx)
{
	YtList *l = (YtList *)ctx;
	const char *t = jpstr(v, "token");
	if (t && !l->cont) l->cont = xstrdup(t);
}

int yt_parse_search(const char *body, int len, YtList *out)
{
	JDoc *d = json_parse(body, len);
	if (!d) { strcpy(out->err, "검색 결과를 해석할 수 없음 (메모리 부족 또는 형식 변경)"); return 0; }
	free(out->cont);
	out->cont = NULL;
	jfind(json_root(d), "videoRenderer", on_video_renderer, out);
	jfind(json_root(d), "videoWithContextRenderer", on_video_renderer, out);
	jfind(json_root(d), "compactVideoRenderer", on_video_renderer, out);
	jfind(json_root(d), "lockupViewModel", on_lockup, out);
	jfind(json_root(d), "shortsLockupViewModel", on_shorts, out);
	jfind(json_root(d), "continuationCommand", on_cont, out);
	json_free(d);
	return 1;
}

int yt_search(const char *query, const char *cont, YtList *out, NetCtl *ctl)
{
	Client c;
	int n0 = out->n, attempt;
	EnterCriticalSection(&g_ycs);
	c = g_search;
	LeaveCriticalSection(&g_ycs);
	out->err[0] = 0;
	for (attempt = 0; attempt < 2; attempt++) {
		Buf body;
		HttpResp r;
		int ok;
		buf_init(&body);
		client_ctx(&body, &c, 0);
		if (cont) { buf_adds(&body, ",\"continuation\":"); json_esc(&body, cont); }
		else {
			buf_adds(&body, ",\"query\":");
			json_esc(&body, query);
			if (c.params[0] && attempt == 0) {
				/* params are given url-encoded in the config */
				char p[80];
				int i, j;
				for (i = j = 0; c.params[i] && j < 79; i++) {
					if (c.params[i] == '%' && c.params[i + 1] && c.params[i + 2]) {
						char hx[3] = { c.params[i + 1], c.params[i + 2], 0 };
						p[j++] = (char)strtol(hx, NULL, 16);
						i += 2;
					} else p[j++] = c.params[i];
				}
				p[j] = 0;
				buf_adds(&body, ",\"params\":");
				json_esc(&body, p);
			}
		}
		buf_adds(&body, "}");
		ok = api_post(&c, c.url, body.p, body.len, &r, ctl, out->err, sizeof out->err);
		buf_free(&body);
		if (!ok) {
			int st = r.status;
			http_resp_free(&r);
			if (st == 400 && c.params[0] && !cont && attempt == 0) continue; /* retry without filter */
			return 0;
		}
		ok = yt_parse_search(r.body.p, r.body.len, out);
		http_resp_free(&r);
		if (!ok) return 0;
		break;
	}
	if (out->n == n0 && !cont) { strcpy(out->err, "검색 결과가 없습니다"); return 0; }
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
/* channels                                                            */
/* ------------------------------------------------------------------ */
int yt_parse_channel(const char *body, int len, YtList *out, char **name)
{
	JDoc *d = json_parse(body, len);
	const char *t;
	if (!d) { strcpy(out->err, "채널 정보를 해석할 수 없음"); return 0; }
	if (name && !*name) {
		t = jpstr(json_root(d), "metadata.channelMetadataRenderer.title");
		if (t) *name = xstrdup(t);
	}
	free(out->cont);
	out->cont = NULL;
	jfind(json_root(d), "videoRenderer", on_video_renderer, out);
	jfind(json_root(d), "gridVideoRenderer", on_video_renderer, out);
	jfind(json_root(d), "lockupViewModel", on_lockup, out);
	jfind(json_root(d), "continuationCommand", on_cont, out);
	json_free(d);
	return 1;
}

int yt_channel(const char *chid, const char *cont, int tab, YtList *out, char **name, NetCtl *ctl)
{
	Client c;
	Buf body;
	HttpResp r;
	char url[200];
	int ok, n0 = out->n;
	EnterCriticalSection(&g_ycs);
	c = g_search;
	LeaveCriticalSection(&g_ycs);
	out->err[0] = 0;
	{
		/* same host as the search endpoint */
		const char *h = strstr(c.url, "/youtubei/");
		int hl = h ? (int)(h - c.url) : 0;
		if (!hl) strcpy(url, "https://www.youtube.com");
		else { memcpy(url, c.url, hl); url[hl] = 0; }
		strcat(url, "/youtubei/v1/browse?prettyPrint=false");
	}
	buf_init(&body);
	client_ctx(&body, &c, 0);
	if (cont) { buf_adds(&body, ",\"continuation\":"); json_esc(&body, cont); }
	else {
		buf_adds(&body, ",\"browseId\":");
		json_esc(&body, chid);
		buf_addf(&body, ",\"params\":\"%s\"", tab == 1 ? g_streams_params : g_channel_params);
	}
	buf_adds(&body, "}");
	ok = api_post(&c, url, body.p, body.len, &r, ctl, out->err, sizeof out->err);
	buf_free(&body);
	if (!ok) { http_resp_free(&r); return 0; }
	ok = yt_parse_channel(r.body.p, r.body.len, out, name);
	http_resp_free(&r);
	if (ok && tab == 1) {
		/* keep finished streams only: live-now / upcoming have no "h:mm:ss" length */
		int i, k = n0;
		for (i = n0; i < out->n; i++) {
			YtItem *it = &out->items[i];
			if (it->length && strchr(it->length, ':')) { if (k != i) out->items[k] = *it; k++; }
			else { free(it->title); free(it->channel); free(it->length); free(it->meta); }
		}
		out->n = k;
	}
	if (ok && out->n == n0 && !cont) {
		strcpy(out->err, tab == 1 ? "지난 라이브 영상이 없습니다" : "이 채널에는 영상이 없습니다");
		return 0;
	}
	return ok;
}

/* -------- RSS -------- */
static char *xml_text(const char *s, const char *e, const char *tag, const char **after)
{
	char open[64], close[64];
	const char *a, *b;
	Buf o;
	_snprintf(open, sizeof open, "<%s>", tag);
	_snprintf(close, sizeof close, "</%s>", tag);
	a = strstr(s, open);
	if (!a || a >= e) return NULL;
	a += strlen(open);
	b = strstr(a, close);
	if (!b || b > e) return NULL;
	if (after) *after = b + strlen(close);
	buf_init(&o);
	while (a < b) {
		if (*a == '&') {
			if (!strncmp(a, "&amp;", 5)) { buf_addc(&o, '&'); a += 5; continue; }
			if (!strncmp(a, "&lt;", 4)) { buf_addc(&o, '<'); a += 4; continue; }
			if (!strncmp(a, "&gt;", 4)) { buf_addc(&o, '>'); a += 4; continue; }
			if (!strncmp(a, "&quot;", 6)) { buf_addc(&o, '"'); a += 6; continue; }
			if (!strncmp(a, "&#39;", 5)) { buf_addc(&o, '\''); a += 5; continue; }
		}
		buf_addc(&o, *a++);
	}
	return buf_detach(&o);
}

/* "2026-11-17T21:58:44+00:00" -> unix time */
static long iso_time(const char *s)
{
	int y, mo, d, h = 0, mi = 0, se = 0;
	long days;
	static const int cum[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
	if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 3 || mo < 1 || mo > 12) return 0;
	days = (y - 1970) * 365L + (y - 1969) / 4 + cum[mo - 1] + d - 1;
	if (mo > 2 && (y % 4) == 0) days++;
	return days * 86400 + h * 3600 + mi * 60 + se;
}

int yt_rss(const char *chid, YtList *out, char **name, NetCtl *ctl)
{
	char url[160];
	HttpResp r;
	const char *p, *end;
	int n0 = out->n;
	_snprintf(url, sizeof url, "https://www.youtube.com/feeds/videos.xml?channel_id=%s", chid);
	out->err[0] = 0;
	if (!http_request(url, "GET", NULL, 0, NULL, NULL, &r, ctl, 1024 * 1024) || r.status != 200) {
		_snprintf(out->err, sizeof out->err, "피드 오류 (%s)", r.err[0] ? r.err : "HTTP");
		http_resp_free(&r);
		return 0;
	}
	buf_addc(&r.body, 0);
	p = r.body.p;
	end = p + strlen(p);
	if (name && !*name) {
		const char *e1 = strstr(p, "<entry>");
		*name = xml_text(p, e1 ? e1 : end, "title", NULL);
	}
	while ((p = strstr(p, "<entry>")) != NULL) {
		const char *e = strstr(p, "</entry>");
		char *vid, *title, *pub, *views = NULL;
		YtItem *it;
		if (!e) break;
		vid = xml_text(p, e, "yt:videoId", NULL);
		title = xml_text(p, e, "title", NULL);
		pub = xml_text(p, e, "published", NULL);
		{
			const char *v = strstr(p, "views=\"");
			if (v && v < e) { long n = atol(v + 7); views = (char *)malloc(40); sprintf(views, "\xEC\xA1\xB0\xED\x9A\x8C\xEC\x88\x98 %ld\xED\x9A\x8C", n); }
		}
		it = vid ? add_item(out, vid) : NULL;
		if (it) {
			it->title = title ? title : xstrdup("(제목 없음)");
			title = NULL;
			strncpy(it->chid, chid, sizeof it->chid - 1);
			if (name && *name) it->channel = xstrdup(*name);
			it->pub = iso_time(pub);
			if (pub && strlen(pub) >= 10) {
				char d[40];
				_snprintf(d, sizeof d, "%.10s", pub);
				d[sizeof d - 1] = 0;
				it->meta = views ? join_meta(views, xstrdup(d)) : xstrdup(d);
				views = NULL;
			}
		}
		free(vid); free(title); free(pub); free(views);
		p = e;
	}
	http_resp_free(&r);
	if (out->n == n0) { strcpy(out->err, "피드에 영상이 없음"); return 0; }
	return 1;
}

/* ------------------------------------------------------------------ */
/* streams                                                             */
/* ------------------------------------------------------------------ */
typedef struct { int itag; const char *url, *mime; long size; int w, h, deflang; } Fmt;

static int collect_fmts(JNode *arr, Fmt *f, int n, int max)
{
	JNode *c;
	for (c = arr ? arr->child : NULL; c && n < max; c = c->next) {
		const char *u = jpstr(c, "url");
		if (!u) continue;   /* ciphered: unusable without player JS */
		f[n].itag = (int)jnum(jget(c, "itag"), 0);
		f[n].url = u;
		f[n].mime = jpstr(c, "mimeType");
		f[n].size = jnum(jget(c, "contentLength"), -1);
		f[n].w = (int)jnum(jget(c, "width"), 0);
		f[n].h = (int)jnum(jget(c, "height"), 0);
		/* multi-language videos list one audio format per dub: 2 = original/default */
		{
			JNode *at = jget(c, "audioTrack");
			JNode *def = jget(at, "audioIsDefault");
			const char *dn = jpstr(at, "displayName");
			const char *tid = jpstr(at, "id");
			f[n].deflang = !at ? 1 : (def && def->bval) ? 2 : 0;
			if (f[n].deflang == 0 && ((dn && (strstr(dn, "original") || strstr(dn, "\xEC\x9B\x90\xEB\xB3\xB8"))) || (tid && !strncmp(tid, "ko", 2))))
				f[n].deflang = 1;
		}
		/* drop formats whose URL needs the "n" challenge (would be throttled/403) */
		if (strstr(u, "&n=")) continue;
		n++;
	}
	return n;
}

static Fmt *pick(Fmt *f, int n, const int *pref, int npref, int want_audio)
{
	int i, j, lv;
	/* audio: prefer the original-language track */
	for (lv = want_audio ? 2 : 0; lv >= 0; lv--)
		for (i = 0; i < npref; i++)
			for (j = 0; j < n; j++)
				if (f[j].itag == pref[i] && f[j].deflang >= lv) return &f[j];
	for (j = 0; j < n; j++) {
		if (!f[j].mime) continue;
		if (want_audio ? (strstr(f[j].mime, "mp4a") && strstr(f[j].mime, "audio/mp4") && f[j].deflang) :
		                 (strstr(f[j].mime, "avc1") && f[j].h > 0 && f[j].h <= 360)) return &f[j];
	}
	return NULL;
}

void yt_streams_free(YtStreams *s)
{
	int i;
	for (i = 0; i < s->nalt; i++) free(s->alt[i].url);
	free(s->title); free(s->vurl); free(s->aurl);
	memset(s, 0, sizeof *s);
}

/* parse a player response; fills out (urls without cpn). returns 1 ok, 0 = try another client, -1 = final error */
int yt_parse_player(const char *body, int len, int quality, YtStreams *out)
{
	JDoc *d;
	JNode *root, *sd, *live;
	const char *st;
	Fmt *f;
	int nf = 0;
	Fmt *v, *a;
	if (quality < 0 || quality > 2) quality = 0;
	d = json_parse(body, len);
	f = (Fmt *)malloc(400 * sizeof(Fmt));
	if (!f) { json_free(d); strcpy(out->err, "메모리 부족"); return 0; }
	if (!d) { strcpy(out->err, "응답을 해석할 수 없음"); { free(f); return 0; } }
	root = json_root(d);
	st = jpstr(root, "playabilityStatus.status");
	if (!st || strcmp(st, "OK")) {
		const char *why = jpstr(root, "playabilityStatus.reason");
		if (!why) why = jpstr(root, "playabilityStatus.messages.0");
		if (why && (strstr(why, "\xEB\xB4\x87") /* 봇 */ || strstr(why, "bot")))
			_snprintf(out->err, sizeof out->err, "유튜브가 이 인터넷 연결을 봇으로 의심해 막았습니다. 잠시 뒤 다시 하거나 Wi-Fi/데이터를 바꿔 보세요.");
		else _snprintf(out->err, sizeof out->err, "재생 불가: %s", why ? why : (st ? st : "알 수 없음"));
		json_free(d);
		{ free(f); return 0; }
	}
	live = jpath(root, "videoDetails.isLive");
	if (live && live->bval) {
		strcpy(out->err, "실시간 방송은 지원하지 않습니다");
		json_free(d);
		{ free(f); return -1; }
	}
	sd = jget(root, "streamingData");
	nf = collect_fmts(jget(sd, "adaptiveFormats"), f, nf, 400);
	nf = collect_fmts(jget(sd, "formats"), f, nf, 400);
	v = pick(f, nf, g_vpref[quality], g_nvpref[quality], 0);
	a = pick(f, nf, g_apref, g_napref, 1);
	if (!v) {
		strcpy(out->err, nf ? "재생 가능한 H.264 영상 형식이 없음" : "영상 주소를 받지 못함 (암호화된 형식)");
		json_free(d);
		{ free(f); return 0; }
	}
	/* alternatives: preferred formats in order, then any other usable H.264 up to 360p */
	{
		int i, j, k;
		Fmt *cand[16];
		int nc = 0;
		cand[nc++] = v;
		for (i = 0; i < g_nvpref[quality] && nc < 16; i++)
			for (j = 0; j < nf && nc < 16; j++)
				if (f[j].itag == g_vpref[quality][i]) {
					for (k = 0; k < nc && cand[k] != &f[j]; k++) {}
					if (k == nc) cand[nc++] = &f[j];
				}
		for (j = 0; j < nf && nc < 16; j++)
			if (f[j].mime && strstr(f[j].mime, "avc1") && f[j].h > 0 && f[j].h <= 360) {
				for (k = 0; k < nc && cand[k] != &f[j]; k++) {}
				if (k == nc) cand[nc++] = &f[j];
			}
		for (i = 0; i < nc && out->nalt < 8; i++) {
			out->alt[out->nalt].url = xstrdup(cand[i]->url);
			out->alt[out->nalt].size = cand[i]->size;
			out->alt[out->nalt].itag = cand[i]->itag;
			out->alt[out->nalt].w = cand[i]->w;
			out->alt[out->nalt].h = cand[i]->h;
			out->nalt++;
		}
	}
	out->vurl = xstrdup(v->url);
	out->vsize = v->size;
	out->vitag = v->itag;
	out->w = v->w;
	out->h = v->h;
	if (a) { out->aurl = xstrdup(a->url); out->asize = a->size; out->aitag = a->itag; }
	else if (v->mime && strstr(v->mime, "mp4a")) { out->aurl = xstrdup(v->url); out->asize = v->size; out->aitag = v->itag; }
	st = jpstr(root, "videoDetails.title");
	out->title = xstrdup(st ? st : "");
	out->dur_ms = jnum(jpath(root, "videoDetails.lengthSeconds"), 0) * 1000;
	json_free(d);
	{ free(f); return 1; }
}

static int fetch_visitor(Client *c, NetCtl *ctl, char *err, int errlen)
{
	Buf body;
	HttpResp r;
	JDoc *d;
	const char *vd;
	int ok = 0;
	buf_init(&body);
	client_ctx(&body, c, 0);
	buf_adds(&body, "}");
	if (api_post(c, c->visitor_url, body.p, body.len, &r, ctl, err, errlen)) {
		d = json_parse(r.body.p, r.body.len);
		vd = d ? jpstr(json_root(d), "responseContext.visitorData") : NULL;
		if (vd && strlen(vd) < sizeof c->visitor) { strcpy(c->visitor, vd); ok = 1; }
		else _snprintf(err, errlen, "방문자 정보를 받지 못함");
		json_free(d);
	}
	buf_free(&body);
	http_resp_free(&r);
	return ok;
}

static char *add_param(char *url, const char *name, const char *val)
{
	char *r = (char *)malloc(strlen(url) + strlen(name) + strlen(val) + 4);
	sprintf(r, "%s%c%s=%s", url, strchr(url, '?') ? '&' : '?', name, val);
	free(url);
	return r;
}

int yt_streams(const char *id, int quality, YtStreams *out, NetCtl *ctl)
{
	return yt_streams_from(id, quality, 0, out, ctl);
}

static int streams_round(const char *id, int quality, int first_client, YtStreams *out, NetCtl *ctl, int fresh);

/* YouTube's "confirm you're not a bot" check is often random per request:
   when every client got it, wait a moment and go round once more with fresh visitorData */
int yt_streams_from(const char *id, int quality, int first_client, YtStreams *out, NetCtl *ctl)
{
	int r = streams_round(id, quality, first_client, out, ctl, 0);
	if (!r && !(ctl && ctl->abort) && strstr(out->err, "\xEB\xB4\x87")) {   /* 봇 */
		tube_log("bot check: retrying");
		Sleep(1500);
		yt_streams_free(out);
		r = streams_round(id, quality, first_client, out, ctl, 1);
	}
	return r;
}

static int streams_round(const char *id, int quality, int first_client, YtStreams *out, NetCtl *ctl, int fresh)
{
	int ci, n;
	char lasterr[400] = "";
	Client *cl;
	memset(out, 0, sizeof *out);
	EnterCriticalSection(&g_ycs);
	n = g_nplayer;
	cl = (Client *)malloc(sizeof(Client) * (n ? n : 1));
	memcpy(cl, g_player, sizeof(Client) * n);
	LeaveCriticalSection(&g_ycs);
	for (ci = first_client; ci < n; ci++) {
		Client *c = &cl[ci];
		Buf body;
		HttpResp r;
		char cpn[20], *url;
		int res, attempt;
		if (ctl && ctl->abort) break;
		for (attempt = 0; attempt < 2; attempt++) {
			if (c->visitor_url[0]) {
				/* cached visitorData lives in the global table */
				EnterCriticalSection(&g_ycs);
				if (ci < g_nplayer && !strcmp(g_player[ci].name, c->name)) strcpy(c->visitor, g_player[ci].visitor);
				LeaveCriticalSection(&g_ycs);
				if (!c->visitor[0] || attempt > 0 || (fresh && attempt == 0)) {
					c->visitor[0] = 0;
					if (!fetch_visitor(c, ctl, lasterr, sizeof lasterr)) break;
					EnterCriticalSection(&g_ycs);
					if (ci < g_nplayer && !strcmp(g_player[ci].name, c->name)) strcpy(g_player[ci].visitor, c->visitor);
					LeaveCriticalSection(&g_ycs);
				}
			}
			rand_str(cpn, 16);
			buf_init(&body);
			client_ctx(&body, c, 1);
			buf_adds(&body, ",\"videoId\":");
			json_esc(&body, id);
			buf_addf(&body, ",\"cpn\":\"%s\",\"contentCheckOk\":true,\"racyCheckOk\":true}", cpn);
			url = expand_url(c->url, id);
			res = api_post(c, url, body.p, body.len, &r, ctl, lasterr, sizeof lasterr);
			free(url);
			buf_free(&body);
			if (!res) { http_resp_free(&r); break; }
			memset(out, 0, sizeof *out);
			res = yt_parse_player(r.body.p, r.body.len, quality, out);
			http_resp_free(&r);
			if (res == 1) {
				if (c->media_cpn) {
					int k;
					out->vurl = add_param(out->vurl, "cpn", cpn);
					if (out->aurl) out->aurl = add_param(out->aurl, "cpn", cpn);
					for (k = 0; k < out->nalt; k++) out->alt[k].url = add_param(out->alt[k].url, "cpn", cpn);
				}
				out->client_idx = ci;
				strncpy(out->id, id, sizeof out->id - 1);
				out->quality = quality;
				strncpy(out->ua, c->ua, sizeof out->ua - 1);
				_snprintf(out->client, sizeof out->client, "%s %s", c->name, c->ver);
				out->media_post = c->media_post;
				out->media_rn = c->media_rn;
				tube_log("streams via %s: v%d a%d", c->name, out->vitag, out->aitag);
				free(cl);
				return 1;
			}
			strcpy(lasterr, out->err);
			tube_log("player %s: %s", c->name, out->err);
			yt_streams_free(out);
			if (res < 0) { free(cl); strcpy(out->err, lasterr); return 0; }
			/* stale visitorData can cause LOGIN_REQUIRED etc: retry once with a fresh one */
			if (!c->visitor_url[0]) break;
		}
	}
	free(cl);
	strncpy(out->err, lasterr[0] ? lasterr : "재생 정보를 가져오지 못함", sizeof out->err - 1);
	return 0;
}
