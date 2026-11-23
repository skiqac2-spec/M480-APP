/* M480Comic - find comic images and chapter links in an HTML page.
 * No JavaScript: looks at <img>/<source> (incl. lazy-load attributes),
 * links to image files, and as a fallback image URLs written inside scripts. */
#include "util.h"
#include "url.h"
#include "extract.h"
#include <stdio.h>
#include <ctype.h>

#define MAX_IMGS  1500
#define MAX_LINKS 600

typedef struct { const char *n; int nl; const char *v; int vl; } Attr;
#define MAXATTR 40

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static const char *find_ci(const char *p, const char *end, const char *needle)
{
	int n = (int)strlen(needle);
	for (; p + n <= end; p++) if (strnieq(p, needle, n)) return p;
	return NULL;
}

/* decode HTML entities and drop whitespace (for URLs) or collapse it (for text) */
static char *decode_text(const char *s, int n, int is_url)
{
	Buf b;
	int i, sp = 0;
	buf_init(&b);
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c == '&') {
			int j = i + 1;
			while (j < n && j - i < 10 && s[j] != ';' && s[j] != '&' && s[j] != ' ') j++;
			if (j < n && s[j] == ';') {
				const char *e = s + i + 1;
				int el = j - i - 1;
				unsigned long cp = 0;
				if (el > 1 && e[0] == '#') {
					if (e[1] == 'x' || e[1] == 'X') cp = strtoul(e + 2, NULL, 16);
					else cp = strtoul(e + 1, NULL, 10);
				} else if (el == 3 && !strncmp(e, "amp", 3)) cp = '&';
				else if (el == 4 && !strncmp(e, "quot", 4)) cp = '"';
				else if (el == 4 && !strncmp(e, "apos", 4)) cp = '\'';
				else if (el == 2 && !strncmp(e, "lt", 2)) cp = '<';
				else if (el == 2 && !strncmp(e, "gt", 2)) cp = '>';
				else if (el == 4 && !strncmp(e, "nbsp", 4)) cp = ' ';
				else if (el == 5 && !strncmp(e, "raquo", 5)) cp = 0xBB;
				else if (el == 5 && !strncmp(e, "laquo", 5)) cp = 0xAB;
				else if (el == 4 && !strncmp(e, "rarr", 4)) cp = 0x2192;
				else if (el == 4 && !strncmp(e, "larr", 4)) cp = 0x2190;
				if (cp) {
					if (sp && !is_url) { buf_addc(&b, ' '); sp = 0; }
					if (cp < 0x80) buf_addc(&b, (char)cp);
					else if (cp < 0x800) { buf_addc(&b, (char)(0xC0 | (cp >> 6))); buf_addc(&b, (char)(0x80 | (cp & 63))); }
					else if (cp < 0x10000) { buf_addc(&b, (char)(0xE0 | (cp >> 12))); buf_addc(&b, (char)(0x80 | ((cp >> 6) & 63))); buf_addc(&b, (char)(0x80 | (cp & 63))); }
					i = j;
					continue;
				}
			}
		}
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			if (!is_url && b.len) sp = 1;
			continue;
		}
		if (sp) { buf_addc(&b, ' '); sp = 0; }
		buf_addc(&b, (char)c);
	}
	return buf_detach(&b);
}

static int parse_tag(const char *p, const char *end, char *name, int namecap, Attr *at, int *nat, const char **tag_end)
{
	int n = 0;
	*nat = 0;
	p++; /* '<' */
	if (p < end && *p == '/') { name[n++] = '/'; p++; }
	while (p < end && (isalnum((unsigned char)*p) || *p == '-' || *p == ':') && n < namecap - 1) name[n++] = (char)lc(*p++);
	name[n] = 0;
	for (;;) {
		const char *an, *av = NULL;
		int anl, avl = 0;
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '/')) p++;
		if (p >= end) { *tag_end = end; return n > 0; }
		if (*p == '>') { *tag_end = p + 1; return n > 0; }
		an = p;
		while (p < end && *p != '=' && *p != '>' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
		anl = (int)(p - an);
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
		if (p < end && *p == '=') {
			p++;
			while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
			if (p < end && (*p == '"' || *p == '\'')) {
				char q = *p++;
				av = p;
				while (p < end && *p != q) p++;
				avl = (int)(p - av);
				if (p < end) p++;
			} else {
				av = p;
				while (p < end && *p != ' ' && *p != '>' && *p != '\t' && *p != '\r' && *p != '\n') p++;
				avl = (int)(p - av);
			}
		}
		if (anl == 0) { p++; continue; }
		if (*nat < MAXATTR) { at[*nat].n = an; at[*nat].nl = anl; at[*nat].v = av; at[*nat].vl = avl; (*nat)++; }
	}
}

static const Attr *get_attr(const Attr *at, int nat, const char *name)
{
	int i, l = (int)strlen(name);
	for (i = 0; i < nat; i++) if (at[i].nl == l && strnieq(at[i].n, name, l)) return &at[i];
	return NULL;
}

static int has_img_ext(const char *u)
{
	const char *q = u, *e;
	int n;
	while (*q && *q != '?' && *q != '#') q++;
	for (e = q; e > u && e[-1] != '.' && e[-1] != '/'; e--) ;
	if (e == u || e[-1] != '.') return 0;
	n = (int)(q - e);
	return (n == 3 && (strnieq(e, "jpg", 3) || strnieq(e, "png", 3) || strnieq(e, "gif", 3) || strnieq(e, "bmp", 3))) ||
	       (n == 4 && (strnieq(e, "jpeg", 4) || strnieq(e, "webp", 4) || strnieq(e, "jfif", 4)));
}

static int bad_img_url(const char *u)
{
	const char *q = u;
	while (*q && *q != '?' && *q != '#') q++;
	if (q - u > 4 && strnieq(q - 4, ".svg", 4)) return 1;
	if (strnieq(u, "data:", 5) || strnieq(u, "javascript:", 11) || strnieq(u, "about:", 6)) return 1;
	return 0;
}

static void add_img(PageInfo *pi, const char *base, const char *raw, int rawlen, int is_decoded)
{
	char *v, *abs;
	int i;
	if (pi->nimgs >= MAX_IMGS) return;
	v = is_decoded ? xstrndup(raw, rawlen) : decode_text(raw, rawlen, 1);
	if (!v || !v[0] || bad_img_url(v)) { free(v); return; }
	abs = url_resolve(base, v);
	free(v);
	if (!abs) return;
	if (strncmp(abs, "http://", 7) && strncmp(abs, "https://", 8)) { free(abs); return; }
	for (i = 0; i < pi->nimgs; i++) if (!strcmp(pi->imgs[i], abs)) { free(abs); return; }
	if (pi->nimgs >= pi->capimgs) {
		int nc = pi->capimgs ? pi->capimgs * 2 : 32;
		char **ni = (char **)realloc(pi->imgs, sizeof(char *) * nc);
		if (!ni) { free(abs); return; }
		pi->imgs = ni; pi->capimgs = nc;
	}
	pi->imgs[pi->nimgs++] = abs;
}

/* srcset: "a.jpg 1x, b.jpg 2x" -> take the last (largest) candidate */
static void add_srcset(PageInfo *pi, const char *base, const char *v, int vl)
{
	char *s = decode_text(v, vl, 0), *best = NULL;
	char *p = s;
	while (p && *p) {
		char *u, *ue;
		while (*p == ' ' || *p == ',') p++;
		if (!*p) break;
		u = p;
		while (*p && *p != ' ') p++;
		ue = p;
		/* URL may end with ',' when there is no descriptor */
		if (ue > u && ue[-1] == ',') ue--;
		free(best);
		best = xstrndup(u, (int)(ue - u));
		while (*p && *p != ',') p++;
	}
	if (best) add_img(pi, base, best, (int)strlen(best), 1);
	free(best);
	free(s);
}

static const char *g_lazy_attrs[] = {
	"data-src", "data-original", "data-lazy-src", "data-lazy", "data-url", "data-echo", "data-lazyload",
	"data-original-src", "data-full", "data-file", "data-img", "data-image", "data-cfsrc", "file", "lazy-src", NULL
};

static void handle_img(PageInfo *pi, const char *base, const Attr *at, int nat)
{
	int i;
	const Attr *a;
	for (i = 0; g_lazy_attrs[i]; i++) {
		a = get_attr(at, nat, g_lazy_attrs[i]);
		if (a && a->v && a->vl > 0) {
			char *t = decode_text(a->v, a->vl, 1);
			int ok = t && t[0] && !bad_img_url(t);
			free(t);
			if (ok) { add_img(pi, base, a->v, a->vl, 0); return; }
		}
	}
	a = get_attr(at, nat, "data-srcset");
	if (a && a->v && a->vl > 0) { add_srcset(pi, base, a->v, a->vl); return; }
	a = get_attr(at, nat, "src");
	if (a && a->v && a->vl > 0) {
		char *t = decode_text(a->v, a->vl, 1);
		int ok = t && t[0] && !bad_img_url(t);
		free(t);
		if (ok) { add_img(pi, base, a->v, a->vl, 0); return; }
	}
	a = get_attr(at, nat, "srcset");
	if (a && a->v && a->vl > 0) add_srcset(pi, base, a->v, a->vl);
}

/* ---------------- links / chapter navigation ---------------- */
static const char *g_next_strong[] = { "다음화", "다음 화", "다음편", "다음 편", "다음회", "다음 회", "다음 에피소드", "다음글", "next chapter", "next episode", NULL };
static const char *g_prev_strong[] = { "이전화", "이전 화", "이전편", "이전 편", "이전회", "이전 회", "이전 에피소드", "이전글", "prev chapter", "previous chapter", "previous episode", NULL };
static const char *g_next_weak[] = { "다음", "next", "▶", "→", "›", "»", NULL };
static const char *g_prev_weak[] = { "이전", "prev", "◀", "←", "‹", "«", NULL };

static int contains_any(const char *hay, const char **list)
{
	int i;
	if (!hay) return 0;
	for (i = 0; list[i]; i++) if (stristr(hay, list[i])) return 1;
	return 0;
}

static int nav_score(const char *text, const char *meta, int rel, const char **strong, const char **weak)
{
	int s = 0;
	if (rel) s = 6;
	if (contains_any(text, strong)) s += 5;
	else if (contains_any(meta, strong)) s += 4;
	else if (text && text[0] && strlen(text) <= 16 && contains_any(text, weak)) s += 3;
	else if (contains_any(meta, weak) && (!text || strlen(text) <= 16)) s += 1;
	/* "다음 페이지" of comment/list paging is not a chapter */
	if (text && (stristr(text, "페이지") || stristr(text, "댓글") || stristr(text, "page"))) s -= 2;
	return s;
}

static void add_link(PageInfo *pi, char *href, char *text)
{
	int i;
	for (i = 0; i < pi->nlinks; i++) if (!strcmp(pi->links[i].href, href)) {
		/* keep the first non-empty text */
		if (!pi->links[i].text[0] && text[0]) { free(pi->links[i].text); pi->links[i].text = text; free(href); return; }
		free(href); free(text); return;
	}
	if (pi->nlinks >= MAX_LINKS) { free(href); free(text); return; }
	if (pi->nlinks >= pi->caplinks) {
		int nc = pi->caplinks ? pi->caplinks * 2 : 64;
		Link *nl = (Link *)realloc(pi->links, sizeof(Link) * nc);
		if (!nl) { free(href); free(text); return; }
		pi->links = nl; pi->caplinks = nc;
	}
	pi->links[pi->nlinks].href = href;
	pi->links[pi->nlinks].text = text;
	pi->nlinks++;
}

/* ---------------- fallback: image URLs inside scripts ---------------- */
static void scan_raw(PageInfo *pi, const char *base, const char *html, int len)
{
	Buf b;
	const char *p, *end;
	int i;
	/* unescape \/ and / */
	buf_init(&b);
	for (i = 0; i < len; i++) {
		if (html[i] == '\\' && i + 1 < len && html[i + 1] == '/') { buf_addc(&b, '/'); i++; }
		else if (html[i] == '\\' && i + 5 < len && strnieq(html + i + 1, "u002f", 5)) { buf_addc(&b, '/'); i += 5; }
		else buf_addc(&b, html[i]);
	}
	if (!b.p) return;
	p = b.p; end = b.p + b.len;
	while (p < end && pi->nimgs < MAX_IMGS) {
		const char *s = NULL, *e;
		if ((*p == 'h' || *p == 'H') && (strnieq(p, "http://", 7) || strnieq(p, "https://", 8))) s = p;
		else if (p + 2 < end && (*p == '"' || *p == '\'') && p[1] == '/' && p[2] == '/') s = p + 1;
		if (!s) { p++; continue; }
		e = s;
		while (e < end && *e != '"' && *e != '\'' && *e != ' ' && *e != '<' && *e != '>' && *e != '\\' &&
		       *e != ')' && *e != '\r' && *e != '\n' && *e != '\t' && *e != ',' && *e != ';') e++;
		if (e - s > 10 && e - s < 1500) {
			char *u = xstrndup(s, (int)(e - s));
			if (has_img_ext(u)) add_img(pi, base, u, (int)strlen(u), 0);
			free(u);
		}
		p = e > p ? e : p + 1;
	}
	buf_free(&b);
}

void extract_page(const char *html, int len, const char *base_url, PageInfo *pi)
{
	const char *p = html, *end = html + len;
	char *base = xstrdup(base_url);
	char name[24];
	Attr at[MAXATTR];
	int nat;
	/* current <a> */
	char *a_href = NULL;
	Buf a_text;
	char *a_meta = NULL;
	int a_rel_next = 0, a_rel_prev = 0;
	int best_next = 0, best_prev = 0;
	char *img_alt_in_a = NULL;
	int nimgs_tags;

	memset(pi, 0, sizeof *pi);
	buf_init(&a_text);
	while (p < end) {
		const char *lt = (const char *)memchr(p, '<', end - p);
		const char *te;
		if (!lt) { if (a_href) buf_add(&a_text, p, (int)(end - p)); break; }
		if (a_href && lt > p) buf_add(&a_text, p, (int)(lt - p));
		p = lt;
		if (end - p >= 4 && !strncmp(p, "<!--", 4)) {
			const char *c = find_ci(p + 4, end, "-->");
			p = c ? c + 3 : end;
			continue;
		}
		if (p + 1 >= end || !(isalpha((unsigned char)p[1]) || p[1] == '/')) {
			if (a_href) buf_addc(&a_text, '<');
			p++;
			continue;
		}
		parse_tag(p, end, name, sizeof name, at, &nat, &te);
		p = te;
		if (!strcmp(name, "script") || !strcmp(name, "style") || !strcmp(name, "noscript")) {
			char closer[16];
			const char *c;
			_snprintf(closer, sizeof closer, "</%s", name);
			if (!strcmp(name, "noscript")) continue;  /* lazy-load pages put the real <img> in <noscript>: parse it */
			c = find_ci(p, end, closer);
			p = c ? c : end;
			continue;
		}
		if (!strcmp(name, "base")) {
			const Attr *a = get_attr(at, nat, "href");
			if (a && a->v) {
				char *v = decode_text(a->v, a->vl, 1), *r = url_resolve(base, v);
				free(v);
				if (r) { free(base); base = r; }
			}
		} else if (!strcmp(name, "title") && !pi->title) {
			const char *c = find_ci(p, end, "</title");
			if (c) { pi->title = decode_text(p, (int)(c - p), 0); p = c; }
		} else if (!strcmp(name, "meta")) {
			const Attr *a = get_attr(at, nat, "charset");
			if (a && a->v && !pi->charset) pi->charset = xstrndup(a->v, a->vl);
			a = get_attr(at, nat, "content");
			if (a && a->v && !pi->charset) {
				char *v = xstrndup(a->v, a->vl);
				const char *cs = stristr(v, "charset=");
				if (cs) {
					int n = 0;
					cs += 8;
					while (cs[n] && cs[n] != ';' && cs[n] != ' ' && cs[n] != '"') n++;
					pi->charset = xstrndup(cs, n);
				}
				free(v);
			}
		} else if (!strcmp(name, "img") || !strcmp(name, "source") || !strcmp(name, "amp-img")) {
			handle_img(pi, base, at, nat);
			if (a_href && !img_alt_in_a) {
				const Attr *a = get_attr(at, nat, "alt");
				if (a && a->v && a->vl) img_alt_in_a = decode_text(a->v, a->vl, 0);
			}
		} else if (!strcmp(name, "a") || !strcmp(name, "/a")) {
			if (a_href) {
				/* finish previous link */
				char *text;
				{
					/* strip inner tags, then decode entities */
					char *r = a_text.p, *w = a_text.p, *e = a_text.p + a_text.len;
					int intag = 0;
					if (r) { while (r < e) { if (*r == '<') intag = 1; else if (*r == '>') intag = 0; else if (!intag) *w++ = *r; r++; } }
					text = decode_text(a_text.p ? a_text.p : "", a_text.p ? (int)(w - a_text.p) : 0, 0);
				}
				if (!text[0] && img_alt_in_a) { free(text); text = xstrdup(img_alt_in_a); }
				{
					int sn = nav_score(text, a_meta, a_rel_next, g_next_strong, g_next_weak);
					int sp = nav_score(text, a_meta, a_rel_prev, g_prev_strong, g_prev_weak);
					/* "다음" and "이전" both match → ambiguous, prefer the strong one */
					if (sn >= 3 && sn > sp && sn > best_next) { free(pi->next); pi->next = xstrdup(a_href); best_next = sn; }
					else if (sp >= 3 && sp > sn && sp > best_prev) { free(pi->prev); pi->prev = xstrdup(a_href); best_prev = sp; }
				}
				if (has_img_ext(a_href)) add_img(pi, base, a_href, (int)strlen(a_href), 1);
				add_link(pi, a_href, text);
				a_href = NULL;
				free(a_meta); a_meta = NULL;
				free(img_alt_in_a); img_alt_in_a = NULL;
				buf_free(&a_text);
				buf_init(&a_text);
			}
			if (name[0] == 'a') {
				const Attr *a = get_attr(at, nat, "href");
				if (a && a->v && a->vl) {
					char *v = decode_text(a->v, a->vl, 1);
					if (v[0] && v[0] != '#' && !strnieq(v, "javascript:", 11) && !strnieq(v, "mailto:", 7) && !strnieq(v, "tel:", 4)) {
						char *r = url_resolve(base, v);
						if (r && (!strncmp(r, "http://", 7) || !strncmp(r, "https://", 8))) {
							char *nofrag = url_strip_fragment(r);
							char *pagenofrag = url_strip_fragment(base_url);
							if (nofrag && pagenofrag && strcmp(nofrag, pagenofrag)) { a_href = r; r = NULL; }
							free(nofrag); free(pagenofrag);
						}
						free(r);
					}
					free(v);
					if (a_href) {
						const Attr *x;
						Buf m;
						buf_init(&m);
						if ((x = get_attr(at, nat, "rel")) && x->v) {
							char *rv = xstrndup(x->v, x->vl);
							a_rel_next = stristr(rv, "next") != NULL;
							a_rel_prev = stristr(rv, "prev") != NULL;
							free(rv);
						} else a_rel_next = a_rel_prev = 0;
						if ((x = get_attr(at, nat, "title")) && x->v) { buf_add(&m, x->v, x->vl); buf_addc(&m, ' '); }
						if ((x = get_attr(at, nat, "aria-label")) && x->v) { buf_add(&m, x->v, x->vl); buf_addc(&m, ' '); }
						if ((x = get_attr(at, nat, "class")) && x->v) { buf_add(&m, x->v, x->vl); buf_addc(&m, ' '); }
						if ((x = get_attr(at, nat, "id")) && x->v) { buf_add(&m, x->v, x->vl); }
						a_meta = buf_detach(&m);
					}
				}
			}
		} else if (a_href && (!strcmp(name, "br") || !strcmp(name, "div") || !strcmp(name, "span") || !strcmp(name, "p"))) {
			buf_addc(&a_text, ' ');
		}
	}
	if (a_href) { free(a_href); }
	free(a_meta);
	free(img_alt_in_a);
	buf_free(&a_text);

	nimgs_tags = pi->nimgs;
	if (nimgs_tags < 3) scan_raw(pi, base, html, len);
	free(base);
}

void pageinfo_free(PageInfo *pi)
{
	int i;
	for (i = 0; i < pi->nimgs; i++) free(pi->imgs[i]);
	free(pi->imgs);
	for (i = 0; i < pi->nlinks; i++) { free(pi->links[i].href); free(pi->links[i].text); }
	free(pi->links);
	free(pi->title); free(pi->charset); free(pi->next); free(pi->prev);
	memset(pi, 0, sizeof *pi);
}
