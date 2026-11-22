#include "util.h"
#include "url.h"
#include <stdio.h>

static int is_scheme_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
}

/* returns length of scheme (excluding ':') or 0 */
static int scheme_len(const char *s)
{
	int i = 0;
	if (!((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z'))) return 0;
	while (is_scheme_char(s[i])) i++;
	return s[i] == ':' ? i : 0;
}

/* clean: trim spaces, drop tabs/newlines, %-encode space and bytes >= 0x80 */
static char *clean_url(const char *s)
{
	Buf b;
	const char *e;
	static const char hex[] = "0123456789ABCDEF";
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
	buf_init(&b);
	for (; s < e; s++) {
		unsigned char c = (unsigned char)*s;
		if (c == '\t' || c == '\r' || c == '\n') continue;
		if (c == ' ' || c >= 0x80 || c == '"' || c == '<' || c == '>' || c == '`' || c == '{' || c == '}' || c == '|' || c == '\\' || c == '^') {
			if (c == '\\') { buf_addc(&b, '/'); continue; }
			buf_addc(&b, '%'); buf_addc(&b, hex[c >> 4]); buf_addc(&b, hex[c & 15]);
		} else buf_addc(&b, (char)c);
	}
	return buf_detach(&b);
}

int url_parse(const char *s, Url *u)
{
	int sl = scheme_len(s);
	const char *p, *hs, *he, *ps;
	memset(u, 0, sizeof *u);
	if (!sl || sl >= (int)sizeof u->scheme) return 0;
	memcpy(u->scheme, s, sl);
	u->scheme[sl] = 0;
	{ char *c; for (c = u->scheme; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32; }
	p = s + sl + 1;
	if (p[0] != '/' || p[1] != '/') return 0;
	p += 2;
	/* skip userinfo */
	{
		const char *at = NULL, *q;
		for (q = p; *q && *q != '/' && *q != '?' && *q != '#'; q++) if (*q == '@') at = q;
		if (at) p = at + 1;
	}
	hs = p;
	if (*p == '[') { while (*p && *p != ']') p++; if (*p) p++; }
	else while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#') p++;
	he = p;
	if (he - hs <= 0 || he - hs >= (int)sizeof u->host) return 0;
	memcpy(u->host, hs, he - hs);
	u->host[he - hs] = 0;
	{ char *c; for (c = u->host; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32; }
	u->port = strcmp(u->scheme, "https") == 0 ? 443 : 80;
	if (*p == ':') {
		p++;
		if (*p >= '0' && *p <= '9') u->port = atoi(p);
		while (*p >= '0' && *p <= '9') p++;
	}
	ps = p;
	{
		const char *fe = strchr(ps, '#');
		int n = fe ? (int)(fe - ps) : (int)strlen(ps);
		Buf b;
		buf_init(&b);
		if (n == 0 || ps[0] != '/') buf_addc(&b, '/');
		buf_add(&b, ps, n);
		u->path = buf_detach(&b);
	}
	return 1;
}

void url_free(Url *u) { free(u->path); u->path = NULL; }

/* remove dot segments from a path (no query) */
static void remove_dots(Buf *out, const char *path, int n)
{
	/* split into segments */
	char *tmp = xstrndup(path, n);
	char **segs;
	int nseg = 0, i, cap = 16;
	char *p, *start;
	int trailing = 0;
	segs = (char **)malloc(cap * sizeof(char *));
	p = tmp;
	if (*p == '/') p++;
	start = p;
	for (;;) {
		if (*p == '/' || *p == 0) {
			char c = *p;
			*p = 0;
			if (strcmp(start, ".") == 0) { trailing = 1; }
			else if (strcmp(start, "..") == 0) { if (nseg > 0) nseg--; trailing = 1; }
			else {
				if (nseg == cap) { cap *= 2; segs = (char **)realloc(segs, cap * sizeof(char *)); }
				segs[nseg++] = start;
				trailing = 0;
			}
			if (!c) break;
			p++;
			start = p;
		} else p++;
	}
	for (i = 0; i < nseg; i++) { buf_addc(out, '/'); buf_adds(out, segs[i]); }
	if (nseg == 0 || trailing) buf_addc(out, '/');
	free(segs);
	free(tmp);
}

char *url_resolve(const char *base, const char *ref0)
{
	char *ref = clean_url(ref0 ? ref0 : "");
	Buf out;
	int sl;
	const char *bq, *rpath_end;
	buf_init(&out);
	sl = scheme_len(ref);
	if (sl) {
		/* absolute: normalize path dots */
		char sch[16];
		if (sl < 16) { memcpy(sch, ref, sl); sch[sl] = 0; } else sch[0] = 0;
		if ((strieq(sch, "http") || strieq(sch, "https")) && ref[sl + 1] == '/' && ref[sl + 2] == '/') {
			const char *a = ref + sl + 3;
			const char *pp = a;
			while (*pp && *pp != '/' && *pp != '?' && *pp != '#') pp++;
			{ int k; for (k = 0; k < sl; k++) buf_addc(&out, (char)((ref[k] >= 'A' && ref[k] <= 'Z') ? ref[k] + 32 : ref[k])); }
			buf_adds(&out, "://");
			buf_add(&out, a, (int)(pp - a));
			if (*pp == '/') {
				const char *qe = pp; while (*qe && *qe != '?' && *qe != '#') qe++;
				remove_dots(&out, pp, (int)(qe - pp));
				buf_adds(&out, qe);
			} else {
				buf_addc(&out, '/');
				buf_adds(&out, pp);
			}
			free(ref);
			return buf_detach(&out);
		}
		return ref;
	}
	if (!base || !scheme_len(base)) { free(ref); return xstrdup(ref0 ? ref0 : ""); }
	{
		int bsl = scheme_len(base);
		const char *auth = base + bsl + 1;
		const char *auth_end, *bpath;
		if (auth[0] == '/' && auth[1] == '/') {
			auth_end = auth + 2;
			while (*auth_end && *auth_end != '/' && *auth_end != '?' && *auth_end != '#') auth_end++;
		} else auth_end = auth;
		bpath = auth_end;
		if (ref[0] == '/' && ref[1] == '/') {
			buf_add(&out, base, bsl + 1);
			{
				const char *a = ref + 2, *pp = a;
				while (*pp && *pp != '/' && *pp != '?' && *pp != '#') pp++;
				buf_adds(&out, "//");
				buf_add(&out, a, (int)(pp - a));
				if (*pp == '/') {
					const char *qe = pp; while (*qe && *qe != '?' && *qe != '#') qe++;
					remove_dots(&out, pp, (int)(qe - pp));
					buf_adds(&out, qe);
				} else { buf_addc(&out, '/'); buf_adds(&out, pp); }
			}
			free(ref);
			return buf_detach(&out);
		}
		buf_add(&out, base, (int)(auth_end - base));
		/* base path part (without query/fragment) */
		bq = bpath;
		while (*bq && *bq != '?' && *bq != '#') bq++;
		if (ref[0] == 0) {
			/* same document */
			const char *bf = strchr(bpath, '#');
			buf_add(&out, bpath, bf ? (int)(bf - bpath) : (int)strlen(bpath));
		} else if (ref[0] == '#') {
			const char *bf = strchr(bpath, '#');
			buf_add(&out, bpath, bf ? (int)(bf - bpath) : (int)strlen(bpath));
			buf_adds(&out, ref);
		} else if (ref[0] == '?') {
			if (bq == bpath) buf_addc(&out, '/'); else buf_add(&out, bpath, (int)(bq - bpath));
			buf_adds(&out, ref);
		} else {
			Buf merged;
			rpath_end = ref;
			while (*rpath_end && *rpath_end != '?' && *rpath_end != '#') rpath_end++;
			buf_init(&merged);
			if (ref[0] == '/') {
				buf_add(&merged, ref, (int)(rpath_end - ref));
			} else {
				const char *ls = bq;
				while (ls > bpath && ls[-1] != '/') ls--;
				if (ls == bpath) buf_addc(&merged, '/');
				else buf_add(&merged, bpath, (int)(ls - bpath));
				buf_add(&merged, ref, (int)(rpath_end - ref));
			}
			remove_dots(&out, merged.p ? merged.p : "/", merged.len);
			buf_free(&merged);
			buf_adds(&out, rpath_end);
		}
	}
	free(ref);
	return buf_detach(&out);
}

char *url_strip_fragment(const char *s)
{
	const char *h = strchr(s, '#');
	return h ? xstrndup(s, (int)(h - s)) : xstrdup(s);
}

char *url_encode(const char *s)
{
	Buf b;
	static const char hex[] = "0123456789ABCDEF";
	buf_init(&b);
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '*')
			buf_addc(&b, (char)c);
		else if (c == ' ') buf_addc(&b, '+');
		else if (c == '\n') buf_adds(&b, "%0D%0A");
		else if (c == '\r') continue;
		else { buf_addc(&b, '%'); buf_addc(&b, hex[c >> 4]); buf_addc(&b, hex[c & 15]); }
	}
	return buf_detach(&b);
}

char *url_fixup(const char *t)
{
	Buf b;
	const char *p;
	int has_space = 0, has_dot = 0;
	while (*t == ' ') t++;
	if (!*t) return xstrdup("about:home");
	if (strnieq(t, "about:", 6)) return xstrdup(t);
	if (scheme_len(t) && (strnieq(t, "http:", 5) || strnieq(t, "https:", 6)))
		return url_resolve(NULL, t);
	for (p = t; *p; p++) { if (*p == ' ') has_space = 1; if (*p == '.') has_dot = 1; }
	if (!has_space && (has_dot || strnieq(t, "localhost", 9))) {
		buf_init(&b);
		buf_adds(&b, "https://");
		buf_adds(&b, t);
		{ char *r = url_resolve(NULL, b.p); buf_free(&b); return r; }
	}
	{
		/* typed words go to namu.wiki (opens the document directly, or its search) */
		char *q = url_encode(t), *c;
		for (c = q; *c; c++) if (*c == '+') *c = ' ';
		buf_init(&b);
		buf_adds(&b, "https://namu.wiki/Go?q=");
		for (c = q; *c; c++) { if (*c == ' ') buf_adds(&b, "%20"); else buf_addc(&b, *c); }
		free(q);
		return buf_detach(&b);
	}
}
