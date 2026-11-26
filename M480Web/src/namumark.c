/* M480Web - minimal NamuMark (namu.wiki markup) -> HTML converter.
 * Used when namu.wiki only sends its JavaScript page shell: the document's raw
 * markup (/raw/ page) is turned into simple HTML that the normal parser shows. */
#include "util.h"
#include "url.h"
#include <stdio.h>

typedef struct { Buf notes; int n; } Foot;

static void esc_n(Buf *o, const char *s, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		char c = s[i];
		if (c == '<') buf_adds(o, "&lt;");
		else if (c == '>') buf_adds(o, "&gt;");
		else if (c == '&') buf_adds(o, "&amp;");
		else if (c == '"') buf_adds(o, "&quot;");
		else buf_addc(o, c);
	}
}

/* %-encode a document title for a /w/ link (UTF-8 bytes, keep a few safe chars) */
static void title_href(Buf *o, const char *t, int n)
{
	static const char hex[] = "0123456789ABCDEF";
	int i;
	buf_adds(o, "https://namu.wiki/w/");
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)t[i];
		if (c == '#') { /* section part */
			buf_adds(o, "#");
			esc_n(o, t + i + 1, n - i - 1);
			return;
		}
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':' || c == '(' || c == ')')
			buf_addc(o, (char)c);
		else { buf_addc(o, '%'); buf_addc(o, hex[c >> 4]); buf_addc(o, hex[c & 15]); }
	}
}

static int find_close_br(const char *s, int n, int i)
{
	/* s[i] is just after "[*"; find matching ']' allowing nested [] */
	int depth = 1;
	for (; i < n; i++) {
		if (s[i] == '\\') { i++; continue; }
		if (s[i] == '[') depth++;
		else if (s[i] == ']' && --depth == 0) return i;
	}
	return -1;
}

static void inl(Buf *o, const char *s, int n, Foot *fn);

static void flush_notes(Buf *o, Foot *fn)
{
	if (!fn->notes.len) return;
	buf_adds(o, "<hr><small>");
	buf_add(o, fn->notes.p, fn->notes.len);
	buf_adds(o, "</small>");
	fn->notes.len = 0;
	if (fn->notes.p) fn->notes.p[0] = 0;
}

static void inl(Buf *o, const char *s, int n, Foot *fn)
{
	int i = 0, b = 0, it = 0, u = 0, st = 0, sup = 0, sub = 0;
	while (i < n) {
		char c = s[i];
		if (c == '\\' && i + 1 < n) { esc_n(o, s + i + 1, 1); i += 2; continue; }
		if (c == '\'' && i + 2 < n && s[i + 1] == '\'' && s[i + 2] == '\'') { buf_adds(o, b ? "</b>" : "<b>"); b = !b; i += 3; continue; }
		if (c == '\'' && i + 1 < n && s[i + 1] == '\'') { buf_adds(o, it ? "</i>" : "<i>"); it = !it; i += 2; continue; }
		if (c == '_' && i + 1 < n && s[i + 1] == '_') { buf_adds(o, u ? "</u>" : "<u>"); u = !u; i += 2; continue; }
		if (c == '~' && i + 1 < n && s[i + 1] == '~') { buf_adds(o, st ? "</s>" : "<s>"); st = !st; i += 2; continue; }
		if (c == '^' && i + 1 < n && s[i + 1] == '^') { buf_adds(o, sup ? "</small>" : "<small>"); sup = !sup; i += 2; continue; }
		if (c == ',' && i + 1 < n && s[i + 1] == ',') { buf_adds(o, sub ? "</small>" : "<small>"); sub = !sub; i += 2; continue; }
		if (c == '[' && i + 1 < n && s[i + 1] == '[') {
			int j = i + 2, e = -1, bar = -1;
			for (; j + 1 < n; j++) { if (s[j] == '\\') { j++; continue; } if (s[j] == ']' && s[j + 1] == ']') { e = j; break; } if (s[j] == '|' && bar < 0) bar = j; }
			if (e > 0) {
				const char *t = s + i + 2;
				int tl = (bar > 0 ? bar : e) - (i + 2);
				if (tl >= 3 && (!strncmp(t, "파일:", 7) || !strncmp(t, "File:", 5) || !strncmp(t, "분류:", 7))) { i = e + 2; continue; }
				if (tl > 0 && t[0] == ':') { t++; tl--; }
				if (tl > 4 && (!strncmp(t, "http://", 7) || !strncmp(t, "https://", 8))) {
					buf_adds(o, "<a href=\""); esc_n(o, t, tl); buf_adds(o, "\">");
				} else if (tl > 0 && t[0] == '#') {
					buf_adds(o, "<a href=\""); esc_n(o, t, tl); buf_adds(o, "\">");
				} else if (tl > 0) {
					buf_adds(o, "<a href=\""); title_href(o, t, tl); buf_adds(o, "\">");
				}
				if (bar > 0) inl(o, s + bar + 1, e - bar - 1, fn);
				else esc_n(o, t, tl);
				if (tl > 0) buf_adds(o, "</a>");
				i = e + 2;
				continue;
			}
		}
		if (c == '[' && i + 1 < n && s[i + 1] == '*') {
			int e = find_close_br(s, n, i + 2);
			if (e > 0) {
				int k = i + 2, num;
				while (k < e && s[k] != ' ') k++;   /* optional footnote name */
				num = ++fn->n;
				buf_addf(o, "<a href=\"#fn-%d\" name=\"rfn-%d\">[%d]</a>", num, num, num);
				buf_addf(&fn->notes, "<a name=\"fn-%d\" href=\"#rfn-%d\">[%d]</a> ", num, num, num);
				if (k < e) inl(&fn->notes, s + k + 1, e - k - 1, fn);
				buf_adds(&fn->notes, "<br>");
				i = e + 1;
				continue;
			}
		}
		if (c == '[') {
			/* macros: [br] [각주] [목차] [include(...)] [youtube(...)] ... */
			int j = i + 1;
			while (j < n && s[j] != ']' && s[j] != '[' && j - i < 300) j++;
			if (j < n && s[j] == ']') {
				int ml = j - i - 1;
				const char *m = s + i + 1;
				int known = 0;
				if (ml == 2 && !strncmp(m, "br", 2)) { buf_adds(o, "<br>"); known = 1; }
				else if ((ml == 6 && !strncmp(m, "각주", 6)) || (ml == 8 && !strncmp(m, "footnote", 8))) { flush_notes(o, fn); known = 1; }
				else if ((ml == 6 && !strncmp(m, "목차", 6)) || !strncmp(m, "tableofcontents", 15) || !strncmp(m, "include(", 8) ||
				         !strncmp(m, "youtube(", 8) || !strncmp(m, "age(", 4) || !strncmp(m, "date", 4) || !strncmp(m, "dday(", 5) ||
				         !strncmp(m, "kakaotv(", 8) || !strncmp(m, "nicovideo(", 10) || !strncmp(m, "vimeo(", 6) || !strncmp(m, "navertv(", 8) ||
				         !strncmp(m, "pagecount", 9) || !strncmp(m, "ruby(", 5) || !strncmp(m, "math(", 5)) known = 1;
				if (known) {
					if (!strncmp(m, "ruby(", 5) || !strncmp(m, "math(", 5)) { /* show the base text */
						int k = 5, e2 = ml - 1;
						while (k < ml && m[k] != ',' && m[k] != ')') k++;
						esc_n(o, m + 5, k - 5);
						(void)e2;
					}
					i = j + 1;
					continue;
				}
			}
		}
		esc_n(o, s + i, 1);
		i++;
	}
	if (b) buf_adds(o, "</b>");
	if (it) buf_adds(o, "</i>");
	if (u) buf_adds(o, "</u>");
	if (st) buf_adds(o, "</s>");
	if (sup || sub) buf_adds(o, "</small>");
}

/* pre-pass: resolve {{{ }}} blocks (wiki style boxes, folding, size/colour, html) */
static char *strip_braces(const char *s, int n)
{
	Buf o;
	int i = 0, sp = 0;
	char kind[64];          /* per open {{{ : 'k' keep content, 'd' drop content, 'r' raw literal */
	int drop = 0;
	buf_init(&o);
	while (i < n) {
		if (s[i] == '{' && i + 2 < n && s[i + 1] == '{' && s[i + 2] == '{') {
			const char *p = s + i + 3;
			int rest = n - i - 3;
			char k = 'k';
			int skip = 3;
			if (rest >= 6 && !strncmp(p, "#!wiki", 6)) { while (skip < n - i && s[i + skip] != '\n') skip++; }
			else if (rest >= 9 && !strncmp(p, "#!folding", 9)) {
				int e = 3 + 9;
				while (e < n - i && s[i + e] != '\n') e++;
				if (!drop) { buf_adds(&o, "\n'''"); buf_add(&o, s + i + 12, e - 12); buf_adds(&o, "'''\n"); }
				skip = e;
			} else if (rest >= 6 && !strncmp(p, "#!html", 6)) { k = 'd'; }
			else if (rest >= 8 && !strncmp(p, "#!syntax", 8)) { k = 'r'; while (skip < n - i && s[i + skip] != '\n') skip++; }
			else if (rest >= 1 && (p[0] == '+' || p[0] == '-') && rest >= 2 && p[1] >= '1' && p[1] <= '5') { skip = 5; }
			else if (rest >= 1 && p[0] == '#') { while (skip < n - i && s[i + skip] != ' ' && s[i + skip] != '\n' && s[i + skip] != '}') skip++; }
			else k = 'r';
			if (sp < 64) kind[sp] = k;
			sp++;
			if (k == 'd') drop++;
			i += skip;
			continue;
		}
		if (sp > 0 && s[i] == '}' && i + 2 < n && s[i + 1] == '}' && s[i + 2] == '}') {
			sp--;
			if (sp < 64 && kind[sp] == 'd') drop--;
			i += 3;
			continue;
		}
		if (!drop) {
			/* inside a literal {{{...}}} markup must not be interpreted: escape it */
			if (sp > 0 && sp <= 64 && kind[sp - 1] == 'r' && strchr("[]'_~^,=|*>", s[i])) buf_addc(&o, '\\');
			buf_addc(&o, s[i]);
		}
		i++;
	}
	return buf_detach(&o);
}

static int heading_level(const char *l, int n, int *ts, int *te)
{
	int a = 0, z = 0;
	while (a < n && l[a] == '=') a++;
	while (z < n - a && l[n - 1 - z] == '=') z++;
	if (a < 1 || a > 6 || a != z || n < 2 * a + 1) return 0;
	*ts = a; *te = n - z;
	if (*ts < *te && l[*ts] == '#') (*ts)++;          /* folded heading ==# x #== */
	if (*te > *ts && l[*te - 1] == '#') (*te)--;
	while (*ts < *te && l[*ts] == ' ') (*ts)++;
	while (*te > *ts && l[*te - 1] == ' ') (*te)--;
	return a;
}

static const char *cell_strip(const char *c, int *n)
{
	/* drop leading cell attributes like <-2><bgcolor=#fff><:> */
	while (*n > 0 && (c[0] == ' ')) { c++; (*n)--; }
	while (*n > 1 && c[0] == '<') {
		int k = 1;
		while (k < *n && c[k] != '>') k++;
		if (k >= *n) break;
		c += k + 1; *n -= k + 1;
		while (*n > 0 && c[0] == ' ') { c++; (*n)--; }
	}
	while (*n > 0 && c[*n - 1] == ' ') (*n)--;
	return c;
}

char *namumark_to_html(const char *src0, int len, const char *title)
{
	Buf o;
	Foot fn;
	char *src = strip_braces(src0, len);
	char *p = src;
	int list_depth = 0, list_ord[16], in_table = 0;
	int sec[7] = { 0 };
	buf_init(&o);
	buf_init(&fn.notes);
	fn.n = 0;
	buf_adds(&o, "<title>");
	esc_n(&o, title, (int)strlen(title));
	buf_adds(&o, " - 나무위키</title><h1>");
	esc_n(&o, title, (int)strlen(title));
	buf_adds(&o, "</h1>");
	/* redirect document */
	if (!strncmp(p, "#redirect ", 10) || !strncmp(p, "#넘겨주기 ", 14)) {
		const char *t = strchr(p, ' ') + 1;
		int tl = 0;
		while (t[tl] && t[tl] != '\n' && t[tl] != '\r') tl++;
		buf_adds(&o, "<meta http-equiv=\"refresh\" content=\"0; url=");
		title_href(&o, t, tl);
		buf_adds(&o, "\"><p>→ <a href=\"");
		title_href(&o, t, tl);
		buf_adds(&o, "\">");
		esc_n(&o, t, tl);
		buf_adds(&o, "</a></p>");
		free(src);
		buf_free(&fn.notes);
		return buf_detach(&o);
	}
	while (*p) {
		char *nl = strchr(p, '\n');
		int n = nl ? (int)(nl - p) : (int)strlen(p);
		char *l = p;
		int lv, ts, te, sp = 0;
		if (n > 0 && l[n - 1] == '\r') n--;
		p = nl ? nl + 1 : p + strlen(p);
		/* close a table when the table rows end */
		if (in_table && !(n >= 2 && l[0] == '|' && l[1] == '|')) { buf_adds(&o, "</table>"); in_table = 0; }
		while (sp < n && l[sp] == ' ') sp++;
		/* lists */
		{
			int is_item = 0, ord = 0, k = sp;
			if (sp > 0 && k < n) {
				if (l[k] == '*') { is_item = 1; k++; }
				else if (k + 1 < n && ((l[k] >= '0' && l[k] <= '9') || l[k] == 'a' || l[k] == 'A' || l[k] == 'i' || l[k] == 'I') && l[k + 1] == '.') { is_item = 1; ord = 1; k += 2; }
				if (k < n && l[k] == '#') { while (k < n && l[k] != ' ') k++; }
			}
			if (is_item) {
				int d = sp > 15 ? 15 : sp;
				while (list_depth < d) { list_ord[list_depth] = ord; buf_adds(&o, ord ? "<ol>" : "<ul>"); list_depth++; }
				while (list_depth > d) { list_depth--; buf_adds(&o, list_ord[list_depth] ? "</ol>" : "</ul>"); }
				buf_adds(&o, "<li>");
				while (k < n && l[k] == ' ') k++;
				inl(&o, l + k, n - k, &fn);
				continue;
			}
			if (list_depth && n > 0 && sp > 0) {   /* continuation line of a list item */
				buf_adds(&o, "<br>");
				inl(&o, l + sp, n - sp, &fn);
				continue;
			}
			while (list_depth > 0) { list_depth--; buf_adds(&o, list_ord[list_depth] ? "</ol>" : "</ul>"); }
		}
		if (n >= 2 && l[0] == '#' && l[1] == '#') continue;                     /* comment */
		if (n == 0) { buf_adds(&o, "<p>"); continue; }
		if ((lv = heading_level(l, n, &ts, &te)) > 0) {
			int k, hl = lv < 2 ? 2 : lv > 5 ? 5 : lv;
			Buf num;
			sec[lv]++;
			for (k = lv + 1; k < 7; k++) sec[k] = 0;
			buf_init(&num);
			for (k = 2; k <= lv; k++) if (sec[k] || k == lv) buf_addf(&num, "%d.", sec[k]);
			if (lv == 1) buf_addf(&num, "%d.", sec[1]);
			if (num.len) num.p[num.len - 1] = 0;   /* "2.1." -> anchor s-2.1 */
			buf_addf(&o, "<h%d><a name=\"s-%s\"></a>%s. ", hl, num.p ? num.p : "", num.p ? num.p : "");
			inl(&o, l + ts, te - ts, &fn);
			buf_addf(&o, "</h%d>", hl);
			buf_free(&num);
			continue;
		}
		if (n >= 4 && !strncmp(l, "----", 4)) { int k = 4; while (k < n && l[k] == '-') k++; if (k == n) { buf_adds(&o, "<hr>"); continue; } }
		if (n >= 2 && l[0] == '|' && l[1] == '|') {
			/* a row may continue on the next lines until it ends with || */
			Buf row;
			int c = 2;
			buf_init(&row);
			buf_add(&row, l, n);
			while ((row.len < 2 || row.p[row.len - 1] != '|' || row.p[row.len - 2] != '|') && *p) {
				char *nl2 = strchr(p, '\n');
				int n2 = nl2 ? (int)(nl2 - p) : (int)strlen(p);
				if (n2 > 0 && p[n2 - 1] == '\r') n2--;
				buf_adds(&row, "[br]");
				buf_add(&row, p, n2);
				p = nl2 ? nl2 + 1 : p + strlen(p);
			}
			if (!in_table) { buf_adds(&o, "<table>"); in_table = 1; }
			buf_adds(&o, "<tr>");
			while (c < row.len) {
				int e = c, cn;
				const char *cell;
				while (e + 1 < row.len && !(row.p[e] == '|' && row.p[e + 1] == '|')) e++;
				if (e + 1 >= row.len) break;
				cn = e - c;
				cell = cell_strip(row.p + c, &cn);
				if (cn > 0) { buf_adds(&o, "<td>"); inl(&o, cell, cn, &fn); buf_adds(&o, "</td>"); }
				c = e + 2;
			}
			buf_adds(&o, "</tr>");
			buf_free(&row);
			continue;
		}
		if (l[0] == '>') {
			int k = 0;
			while (k < n && l[k] == '>') k++;
			buf_adds(&o, "<blockquote>");
			inl(&o, l + k, n - k, &fn);
			buf_adds(&o, "</blockquote>");
			continue;
		}
		{
			/* lines that only held macros/images/categories leave nothing: no empty line */
			Buf t;
			buf_init(&t);
			inl(&t, l + sp, n - sp, &fn);
			if (t.len) { buf_add(&o, t.p, t.len); buf_adds(&o, "<br>"); }
			buf_free(&t);
		}
	}
	if (in_table) buf_adds(&o, "</table>");
	while (list_depth > 0) { list_depth--; buf_adds(&o, list_ord[list_depth] ? "</ol>" : "</ul>"); }
	flush_notes(&o, &fn);
	buf_free(&fn.notes);
	free(src);
	return buf_detach(&o);
}
