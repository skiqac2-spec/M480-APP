/* M480Web - forgiving HTML parser producing a flat flow of items */
#include "util.h"
#include "html.h"
#include "url.h"
#include <stdio.h>

/* ------------------------------------------------------------------ */
enum {
	T_UNKNOWN, T_A, T_ABBR, T_ADDRESS, T_ARTICLE, T_ASIDE, T_AUDIO, T_B, T_BASE, T_BIG, T_BLOCKQUOTE, T_BODY,
	T_BR, T_BUTTON, T_CANVAS, T_CAPTION, T_CENTER, T_CITE, T_CODE, T_DD, T_DEL, T_DETAILS, T_DFN, T_DIALOG, T_DIR, T_DIV, T_DL, T_DT,
	T_EM, T_EMBED, T_FIELDSET, T_FIGCAPTION, T_FIGURE, T_FONT, T_FOOTER, T_FORM, T_H1, T_H2, T_H3, T_H4, T_H5, T_H6,
	T_HEAD, T_HEADER, T_HR, T_HTML, T_I, T_IFRAME, T_IMG, T_INPUT, T_INS, T_KBD, T_LABEL, T_LEGEND, T_LI, T_LINK,
	T_MAIN, T_MAP, T_MARK, T_MATH, T_MENU, T_META, T_NAV, T_NOEMBED, T_NOFRAMES, T_NOSCRIPT, T_OBJECT, T_OL, T_OPTGROUP, T_OPTION,
	T_P, T_PARAM, T_PICTURE, T_PLAINTEXT, T_PRE, T_Q, T_S, T_SAMP, T_SCRIPT, T_SECTION, T_SELECT, T_SMALL, T_SOURCE, T_SPAN,
	T_STRIKE, T_STRONG, T_STYLE, T_SUB, T_SUMMARY, T_SUP, T_SVG, T_TABLE, T_TBODY, T_TD, T_TEMPLATE, T_TEXTAREA, T_TFOOT,
	T_TH, T_THEAD, T_TITLE, T_TR, T_TT, T_U, T_UL, T_VAR, T_VIDEO, T_WBR, T_XMP, T_AREA, T_COL, T_TRACK, T_HGROUP, T_FRAME, T_FRAMESET,
	T_COUNT
};
static const char *tag_names[] = {
	"", "a", "abbr", "address", "article", "aside", "audio", "b", "base", "big", "blockquote", "body",
	"br", "button", "canvas", "caption", "center", "cite", "code", "dd", "del", "details", "dfn", "dialog", "dir", "div", "dl", "dt",
	"em", "embed", "fieldset", "figcaption", "figure", "font", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6",
	"head", "header", "hr", "html", "i", "iframe", "img", "input", "ins", "kbd", "label", "legend", "li", "link",
	"main", "map", "mark", "math", "menu", "meta", "nav", "noembed", "noframes", "noscript", "object", "ol", "optgroup", "option",
	"p", "param", "picture", "plaintext", "pre", "q", "s", "samp", "script", "section", "select", "small", "source", "span",
	"strike", "strong", "style", "sub", "summary", "sup", "svg", "table", "tbody", "td", "template", "textarea", "tfoot",
	"th", "thead", "title", "tr", "tt", "u", "ul", "var", "video", "wbr", "xmp", "area", "col", "track", "hgroup", "frame", "frameset"
};

static int tag_lookup(const char *n)
{
	int i;
	for (i = 1; i < T_COUNT; i++) if (strcmp(tag_names[i], n) == 0) return i;
	return T_UNKNOWN;
}

static int is_void(int t)
{
	switch (t) {
	case T_AREA: case T_BASE: case T_BR: case T_COL: case T_EMBED: case T_HR: case T_IMG: case T_INPUT: case T_LINK:
	case T_META: case T_PARAM: case T_SOURCE: case T_TRACK: case T_WBR: case T_FRAME:
		return 1;
	}
	return 0;
}

static int is_block(int t)
{
	switch (t) {
	case T_ADDRESS: case T_ARTICLE: case T_ASIDE: case T_BLOCKQUOTE: case T_CENTER: case T_DD: case T_DETAILS: case T_DIALOG:
	case T_DIR: case T_DIV: case T_DL: case T_DT: case T_FIELDSET: case T_FIGCAPTION: case T_FIGURE: case T_FOOTER: case T_FORM:
	case T_H1: case T_H2: case T_H3: case T_H4: case T_H5: case T_H6: case T_HEADER: case T_HGROUP: case T_HR: case T_LI: case T_MAIN:
	case T_MENU: case T_NAV: case T_OL: case T_P: case T_PRE: case T_SECTION: case T_SUMMARY: case T_TABLE: case T_UL: case T_LEGEND:
	case T_CAPTION: case T_TR: case T_XMP: case T_PLAINTEXT:
		return 1;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* entities                                                            */
/* ------------------------------------------------------------------ */
static const struct { const char *n; unsigned short c; } ents[] = {
	{"amp",38},{"lt",60},{"gt",62},{"quot",34},{"apos",39},{"nbsp",160},{"copy",169},{"reg",174},{"trade",8482},
	{"middot",183},{"hellip",8230},{"mdash",8212},{"ndash",8211},{"laquo",171},{"raquo",187},{"lsquo",8216},
	{"rsquo",8217},{"ldquo",8220},{"rdquo",8221},{"sbquo",8218},{"bdquo",8222},{"bull",8226},{"times",215},{"divide",247},{"deg",176},
	{"euro",8364},{"yen",165},{"pound",163},{"cent",162},{"larr",8592},{"rarr",8594},{"uarr",8593},{"darr",8595},
	{"harr",8596},{"hearts",9829},{"spades",9824},{"clubs",9827},{"diams",9830},{"sect",167},{"para",182},{"plusmn",177},
	{"frac12",189},{"frac14",188},{"frac34",190},{"sup1",185},{"sup2",178},{"sup3",179},{"micro",181},{"iexcl",161},
	{"iquest",191},{"shy",173},{"ensp",8194},{"emsp",8195},{"thinsp",8201},{"zwnj",8204},{"zwj",8205},{"lrm",8206},{"rlm",8207},
	{"dagger",8224},{"Dagger",8225},{"permil",8240},{"prime",8242},{"Prime",8243},{"lsaquo",8249},{"rsaquo",8250},
	{"oline",8254},{"frasl",8260},{"le",8804},{"ge",8805},{"ne",8800},{"asymp",8776},{"infin",8734},{"minus",8722},
	{"star",9734},{"check",10003},{"acute",180},{"uml",168},{"macr",175},{"cedil",184},{"ordf",170},{"ordm",186},{"not",172},
	{"curren",164},{"brvbar",166},{"agrave",224},{"aacute",225},{"acirc",226},{"atilde",227},{"auml",228},{"aring",229},
	{"aelig",230},{"ccedil",231},{"egrave",232},{"eacute",233},{"ecirc",234},{"euml",235},{"igrave",236},{"iacute",237},
	{"icirc",238},{"iuml",239},{"ntilde",241},{"ograve",242},{"oacute",243},{"ocirc",244},{"otilde",245},{"ouml",246},
	{"oslash",248},{"ugrave",249},{"uacute",250},{"ucirc",251},{"uuml",252},{"yacute",253},{"yuml",255},{"szlig",223},
	{"Agrave",192},{"Aacute",193},{"Acirc",194},{"Atilde",195},{"Auml",196},{"Aring",197},{"AElig",198},{"Ccedil",199},
	{"Egrave",200},{"Eacute",201},{"Ecirc",202},{"Euml",203},{"Ntilde",209},{"Ouml",214},{"Uuml",220},{"Oslash",216},
	{"alpha",945},{"beta",946},{"gamma",947},{"delta",948},{"pi",960},{"sigma",963},{"omega",969},{"mu",956},{"lambda",955},
	{"Delta",916},{"Omega",937},{"Sigma",931},{"rArr",8658},{"lArr",8656},{"hArr",8660},{"sum",8721},{"radic",8730},
	{"loz",9674},{"circ",710},{"tilde",732},{"ensp",8194},{NULL,0}
};
static const unsigned short cp1252_hi[32] = {
	8364,129,8218,402,8222,8230,8224,8225,710,8240,352,8249,338,141,381,143,
	144,8216,8217,8220,8221,8226,8211,8212,732,8482,353,8250,339,157,382,376
};

/* decode entity at s (s[0]=='&'). returns chars consumed (0 if not an entity), *out = code */
static int decode_entity(const WCHAR *s, int n, unsigned long *out)
{
	int i = 1;
	if (n < 3) return 0;
	if (s[1] == '#') {
		unsigned long v = 0;
		int hex = 0, digits = 0;
		i = 2;
		if (i < n && (s[i] == 'x' || s[i] == 'X')) { hex = 1; i++; }
		for (; i < n && i < 12; i++) {
			WCHAR c = s[i];
			if (c >= '0' && c <= '9') v = v * (hex ? 16 : 10) + (c - '0');
			else if (hex && c >= 'a' && c <= 'f') v = v * 16 + (c - 'a' + 10);
			else if (hex && c >= 'A' && c <= 'F') v = v * 16 + (c - 'A' + 10);
			else break;
			digits++;
		}
		if (!digits) return 0;
		if (i < n && s[i] == ';') i++;
		if (v >= 128 && v < 160) v = cp1252_hi[v - 128];
		if (v == 0 || v > 0x10FFFF) v = 0xFFFD;
		*out = v;
		return i;
	} else {
		char name[12];
		int k = 0, j;
		for (i = 1; i < n && k < 10; i++) {
			WCHAR c = s[i];
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) name[k++] = (char)c;
			else break;
		}
		name[k] = 0;
		if (!k) return 0;
		if (k == 10) return 0;
		for (j = 0; ents[j].n; j++) if (strcmp(ents[j].n, name) == 0) {
			int used = 1 + k;
			if (used < n && s[used] == ';') used++;
			*out = ents[j].c;
			return used;
		}
		return 0;
	}
}

static void wbuf_addcp(WBuf *b, unsigned long c)
{
	if (c >= 0x10000) {
		c -= 0x10000;
		wbuf_addc(b, (WCHAR)(0xD800 + (c >> 10)));
		wbuf_addc(b, (WCHAR)(0xDC00 + (c & 0x3FF)));
	} else wbuf_addc(b, (WCHAR)c);
}

static void decode_into(WBuf *out, const WCHAR *s, int n)
{
	int i = 0;
	while (i < n) {
		if (s[i] == '&') {
			unsigned long c;
			int k = decode_entity(s + i, n - i, &c);
			if (k) { wbuf_addcp(out, c); i += k; continue; }
		}
		wbuf_addc(out, s[i]);
		i++;
	}
}

/* ------------------------------------------------------------------ */
/* builder                                                             */
/* ------------------------------------------------------------------ */
#define F_BLOCK   1
#define F_OUTDENT 2
#define F_LIST    4
#define F_SKIP    8

typedef struct {
	int tag;
	unsigned short prev_style;
	int prev_link;
	int prev_center, prev_pre;
	int flags;
	int margin;
} Frame;

typedef struct { char name[24]; WCHAR *val; } Attr;

typedef struct {
	Doc *d;
	Frame st[256];
	int sp;
	unsigned short style;
	int link;
	int center;
	int pre;
	int skip;            /* number of skip frames open */
	int at_line_start;
	int last_space;
	int form;
	int select_ctl;      /* -1 */
	int opt_open;        /* collecting option label */
	WBuf opt_label;
	WCHAR *opt_value;
	int opt_selected;
	int button_ctl;      /* -1 */
	int list_num[32], list_ord[32], list_depth;
	int cells;
	int pre_nl;
	int in_cell, cell_content, cell_start_item;
	int li_fresh;
	int flags;
	Attr at[40];
	int nat;
} B;

static void *grow(void *p, int *cap, int n, int sz)
{
	if (n < *cap) return p;
	*cap = *cap ? *cap * 2 : 64;
	return realloc(p, (*cap) * sz);
}

static Item *add_item(B *b, int type)
{
	Doc *d = b->d;
	Item *it;
	d->items = (Item *)grow(d->items, &d->capitems, d->nitems, sizeof(Item));
	it = &d->items[d->nitems++];
	memset(it, 0, sizeof *it);
	it->type = (unsigned char)type;
	it->style = b->style;
	it->link = b->link;
	return it;
}

static Item *last_item(B *b) { return b->d->nitems ? &b->d->items[b->d->nitems - 1] : NULL; }

static void trim_trailing_space(B *b)
{
	Item *it = last_item(b);
	if (it && it->type == IT_TEXT && it->len > 0 && b->d->text.p[it->text + it->len - 1] == ' ' && !(it->style & S_PRE)) {
		it->len--;
		if (it->len == 0) b->d->nitems--;
	}
}

static void emit_block(B *b, int margin)
{
	Item *it;
	if (b->in_cell && !b->cell_content) return;
	if (b->li_fresh) return;
	trim_trailing_space(b);
	it = last_item(b);
	if (it && it->type == IT_BLOCK) {
		if ((it->aux & 0xff) < margin) it->aux = (it->aux & ~0xff) | margin;
		it->aux = (it->aux & 0xff) | (b->center ? BLK_CENTER : 0);
	} else {
		it = add_item(b, IT_BLOCK);
		it->aux = margin | (b->center ? BLK_CENTER : 0);
		it->link = -1;
	}
	b->at_line_start = 1;
	b->last_space = 1;
}

static void emit_raw_text(B *b, const WCHAR *s, int n)
{
	Doc *d = b->d;
	Item *it = last_item(b);
	if (n <= 0) return;
	if (it && it->type == IT_TEXT && it->style == b->style && it->link == b->link && it->text + it->len == d->text.len) {
		wbuf_add(&d->text, s, n);
		it->len += n;
	} else {
		it = add_item(b, IT_TEXT);
		it->text = d->text.len;
		it->len = n;
		wbuf_add(&d->text, s, n);
	}
	b->at_line_start = 0;
	b->last_space = (s[n - 1] == ' ');
	b->cell_content = 1;
	b->li_fresh = 0;
}

static int is_ws(WCHAR c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

/* text already entity-decoded */
static void emit_text(B *b, const WCHAR *s, int n)
{
	WCHAR tmp[256];
	int k = 0, i;
	if (b->skip) return;
	if (b->button_ctl >= 0) {
		Ctl *c = &b->d->ctls[b->button_ctl];
		WBuf w; wbuf_init(&w);
		if (c->label) wbuf_add(&w, c->label, (int)wcslen(c->label));
		for (i = 0; i < n; i++) if (!is_ws(s[i]) || (w.len && w.p[w.len - 1] != ' ')) wbuf_addc(&w, is_ws(s[i]) ? ' ' : s[i]);
		free(c->label);
		c->label = w.p ? w.p : xwcsdup(L"");
		return;
	}
	if (b->select_ctl >= 0) {
		if (b->opt_open) {
			for (i = 0; i < n; i++) if (!is_ws(s[i]) || (b->opt_label.len && b->opt_label.p[b->opt_label.len - 1] != ' ')) wbuf_addc(&b->opt_label, is_ws(s[i]) ? ' ' : s[i]);
		}
		return;
	}
	if (b->pre) {
		i = 0;
		if (b->pre_nl) {
			b->pre_nl = 0;
			if (i < n && s[i] == '\r') i++;
			if (i < n && s[i] == '\n') i++;
		}
		for (; i < n; i++) {
			WCHAR c = s[i];
			if (c == '\r') continue;
			if (c == '\n') {
				if (k) { emit_raw_text(b, tmp, k); k = 0; }
				add_item(b, IT_BR);
				b->at_line_start = 1;
				continue;
			}
			if (c == '\t') c = ' ';
			tmp[k++] = c;
			if (k == 256) { emit_raw_text(b, tmp, k); k = 0; }
		}
		if (k) emit_raw_text(b, tmp, k);
		return;
	}
	for (i = 0; i < n; i++) {
		WCHAR c = s[i];
		if (is_ws(c)) {
			if (b->at_line_start || b->last_space || (k && tmp[k - 1] == ' ')) continue;
			c = ' ';
		}
		if (c == 0xAD || c == 0x200B || c == 0xFEFF) continue; /* soft hyphen, zero width */
		tmp[k++] = c;
		b->at_line_start = 0;
		b->last_space = (c == ' ');
		if (k == 256) { emit_raw_text(b, tmp, k); k = 0; }
	}
	if (k) emit_raw_text(b, tmp, k);
}

static void emit_forced(B *b, const WCHAR *s)
{
	if (b->skip) return;
	emit_raw_text(b, s, (int)wcslen(s));
}

static const WCHAR *attr(B *b, const char *name)
{
	int i;
	for (i = 0; i < b->nat; i++) if (strcmp(b->at[i].name, name) == 0) return b->at[i].val ? b->at[i].val : L"";
	return NULL;
}
static int attr_int(B *b, const char *name)
{
	const WCHAR *v = attr(b, name);
	int r = 0;
	if (!v) return 0;
	while (*v == ' ') v++;
	while (*v >= '0' && *v <= '9') { r = r * 10 + (*v - '0'); v++; if (r > 100000) break; }
	if (*v == '%') return 0;
	return r;
}
static char *attr_url(B *b, const char *name)
{
	const WCHAR *v = attr(b, name);
	char *u, *r;
	if (!v) return NULL;
	u = w_to_utf8(v, -1);
	r = url_resolve(b->d->base, u);
	free(u);
	return r;
}
static int wcontains_ci(const WCHAR *h, const char *needle)
{
	int nl = (int)strlen(needle), i;
	for (; *h; h++) {
		for (i = 0; i < nl; i++) {
			WCHAR c = h[i];
			if (!c) return 0;
			if (c >= 'A' && c <= 'Z') c += 32;
			if (c != (WCHAR)needle[i]) break;
		}
		if (i == nl) return 1;
	}
	return 0;
}
static int style_hidden(B *b)
{
	const WCHAR *s = attr(b, "style");
	if (attr(b, "hidden")) return 1;
	if (s) {
		/* normalise spaces */
		WCHAR tmp[256];
		int k = 0;
		for (; *s && k < 255; s++) if (*s != ' ') tmp[k++] = *s;
		tmp[k] = 0;
		if (wcontains_ci(tmp, "display:none") || wcontains_ci(tmp, "visibility:hidden")) return 1;
	}
	return 0;
}
static int wants_center(B *b)
{
	const WCHAR *a = attr(b, "align");
	const WCHAR *s = attr(b, "style");
	if (a && wcontains_ci(a, "center")) return 1;
	if (s) {
		WCHAR tmp[256]; int k = 0;
		for (; *s && k < 255; s++) if (*s != ' ') tmp[k++] = *s;
		tmp[k] = 0;
		if (wcontains_ci(tmp, "text-align:center")) return 1;
	}
	return 0;
}

static int add_link(Doc *d, char *url)
{
	if (d->nlinks > 0 && strcmp(d->links[d->nlinks - 1], url) == 0) { free(url); return d->nlinks - 1; }
	d->links = (char **)grow(d->links, &d->caplinks, d->nlinks, sizeof(char *));
	d->links[d->nlinks] = url;
	return d->nlinks++;
}

static void add_anchor(B *b, const WCHAR *name)
{
	Doc *d = b->d;
	Item *it;
	if (!name || !*name) return;
	d->anchors = (Anchor *)grow(d->anchors, &d->capanchors, d->nanchors, sizeof(Anchor));
	d->anchors[d->nanchors].name = xwcsdup(name);
	d->anchors[d->nanchors].item = d->nitems;
	d->nanchors++;
	it = add_item(b, IT_ANCHOR);
	it->link = -1;
}

static void push(B *b, int tag, int flags, int margin)
{
	Frame *f;
	if (b->sp >= 255) return;
	f = &b->st[b->sp++];
	f->tag = tag;
	f->prev_style = b->style;
	f->prev_link = b->link;
	f->prev_center = b->center;
	f->prev_pre = b->pre;
	f->flags = flags;
	f->margin = margin;
	if (flags & F_SKIP) b->skip++;
}

static void pop1(B *b)
{
	Frame *f = &b->st[--b->sp];
	if (f->flags & F_SKIP) b->skip--;
	if (f->tag == T_TABLE || f->tag == T_TR) b->in_cell = 0;
	if (f->tag == T_LI) b->li_fresh = 0;
	if (!b->skip) {
		if (f->flags & F_BLOCK) emit_block(b, f->margin);
		if (f->flags & F_OUTDENT) { emit_block(b, 0); add_item(b, IT_OUTDENT); }
	}
	if (f->flags & F_LIST) { if (b->list_depth > 0) b->list_depth--; if (!b->skip) emit_block(b, b->list_depth ? 0 : 1); }
	b->style = f->prev_style;
	b->link = f->prev_link;
	b->center = f->prev_center;
	b->pre = f->prev_pre;
}

/* pop up to and including the nearest frame with tag t (search limited to stop tags) */
static int pop_to(B *b, int t, int stop1, int stop2)
{
	int i;
	for (i = b->sp - 1; i >= 0 && i >= b->sp - 80; i--) {
		if (b->st[i].tag == t) {
			while (b->sp > i) pop1(b);
			return 1;
		}
		if (b->st[i].tag == stop1 || b->st[i].tag == stop2) return 0;
	}
	return 0;
}

static void close_p(B *b)
{
	if (b->sp && b->st[b->sp - 1].tag == T_P) pop1(b);
	else pop_to(b, T_P, T_DIV, T_TABLE);
}

static void finish_option(B *b)
{
	Ctl *c;
	WCHAR *lab;
	if (!b->opt_open || b->select_ctl < 0) return;
	c = &b->d->ctls[b->select_ctl];
	b->opt_open = 0;
	while (b->opt_label.len && b->opt_label.p[b->opt_label.len - 1] == ' ') b->opt_label.len--;
	if (b->opt_label.p) b->opt_label.p[b->opt_label.len] = 0;
	lab = b->opt_label.p ? xwcsdup(b->opt_label.p) : xwcsdup(L"");
	c->opt_label = (WCHAR **)realloc(c->opt_label, (c->nopt + 1) * sizeof(WCHAR *));
	c->opt_value = (WCHAR **)realloc(c->opt_value, (c->nopt + 1) * sizeof(WCHAR *));
	c->opt_label[c->nopt] = lab;
	c->opt_value[c->nopt] = b->opt_value ? b->opt_value : xwcsdup(lab);
	if (b->opt_selected) c->sel = c->nopt;
	c->nopt++;
	b->opt_value = NULL;
	b->opt_label.len = 0;
}

static Ctl *add_ctl(B *b, int type)
{
	Doc *d = b->d;
	Ctl *c;
	Item *it;
	const WCHAR *v;
	d->ctls = (Ctl *)grow(d->ctls, &d->capctls, d->nctls, sizeof(Ctl));
	c = &d->ctls[d->nctls];
	memset(c, 0, sizeof *c);
	c->type = type;
	c->form = b->form;
	v = attr(b, "name");
	c->name = v ? xwcsdup(v) : NULL;
	v = attr(b, "value");
	c->value = xwcsdup(v ? v : L"");
	c->size = attr_int(b, "size");
	c->checked = attr(b, "checked") != NULL;
	if (type != CT_HIDDEN && !b->skip) {
		it = add_item(b, IT_CTL);
		b->cell_content = 1;
		b->li_fresh = 0;
		it->aux = d->nctls;
		it->link = -1;
		b->at_line_start = 0;
		b->last_space = 0;
	}
	d->nctls++;
	return c;
}

static void start_tag(B *b, int t, int selfclose)
{
	Doc *d = b->d;
	const WCHAR *idv;

	if (b->select_ctl >= 0) {
		if (t == T_OPTION) {
			const WCHAR *v;
			finish_option(b);
			b->opt_open = 1;
			b->opt_label.len = 0;
			v = attr(b, "value");
			b->opt_value = v ? xwcsdup(v) : NULL;
			b->opt_selected = attr(b, "selected") != NULL;
		}
		return; /* ignore everything else inside select */
	}
	if (b->button_ctl >= 0) {
		if (t == T_IMG && !d->ctls[b->button_ctl].label) {
			const WCHAR *alt = attr(b, "alt");
			if (alt && *alt) d->ctls[b->button_ctl].label = xwcsdup(alt);
		}
		return;
	}

	if (is_block(t) && t != T_LI && t != T_DD && t != T_DT && t != T_TR) {
		if (t != T_TABLE || 1) close_p(b);
	}

	idv = attr(b, "id");
	if (idv && !b->skip) add_anchor(b, idv);

	if (!is_void(t) && style_hidden(b) && t != T_HTML && t != T_BODY) {
		push(b, t, F_SKIP, 0);
		return;
	}
	if (b->flags & HP_NOIMG) {
		if (t == T_IMG || t == T_PICTURE || t == T_SOURCE) { if (t == T_PICTURE) push(b, t, F_SKIP, 0); return; }
	}
	if (b->flags & HP_NAMU) {
		switch (t) {
		case T_HEADER: case T_NAV: case T_FOOTER: case T_ASIDE: case T_BUTTON: case T_SELECT: case T_TEXTAREA: case T_DIALOG: case T_NOSCRIPT:
			push(b, t, F_SKIP, 0);
			return;
		case T_FORM:
			if (!attr(b, "data-m480")) { push(b, t, F_SKIP, 0); return; }
			break;
		case T_INPUT:
			if (b->form < 0 || b->skip) return;
			break;
		case T_A: {
			static const char *admin[] = { "/edit/", "/history/", "/discuss/", "/backlink/", "/acl/", "/move/", "/delete/", "/member/",
				"/thread/", "/raw/", "/blame/", "/revert/", "/contribution/", "/RecentChanges", "/RecentDiscuss", "/Upload", "/License", NULL };
			const WCHAR *hv = attr(b, "href");
			int k;
			if (hv) {
				char *hu = w_to_utf8(hv, -1);
				const char *path = hu;
				if (!strncmp(path, "https://namu.wiki", 17)) path += 17;
				for (k = 0; admin[k]; k++) if (!strncmp(path, admin[k], strlen(admin[k]))) break;
				free(hu);
				if (admin[k]) { push(b, t, F_SKIP, 0); return; }
			}
			break;
		}
		}
	}

	switch (t) {
	case T_SCRIPT: case T_STYLE: case T_TEMPLATE: case T_SVG: case T_MATH: case T_IFRAME: case T_OBJECT: case T_CANVAS:
	case T_VIDEO: case T_AUDIO: case T_MAP: case T_HEAD: case T_NOEMBED: case T_NOFRAMES: case T_DIALOG: case T_FRAMESET:
		if (t == T_HEAD) { push(b, t, 0, 0); return; }
		if (t == T_DIALOG && attr(b, "open")) { push(b, t, F_BLOCK, 0); emit_block(b, 0); return; }
		if (t == T_IFRAME) {
			char *u = attr_url(b, "src");
			if (u && !b->skip) {
				int prev = b->link;
				emit_block(b, 0);
				b->link = add_link(d, u);
				b->style |= S_LINK | S_U;
				emit_forced(b, L"[\xD504\xB808\xC784 \xC5F4\xAE30]");
				b->style = (unsigned short)(b->style & ~(S_LINK | S_U));
				b->link = prev;
				emit_block(b, 0);
			} else free(u);
		}
		if (t == T_VIDEO || t == T_AUDIO) {
			char *u = attr_url(b, "src");
			if (u && !b->skip) {
				int prev = b->link;
				b->link = add_link(d, u);
				b->style |= S_LINK | S_U;
				emit_forced(b, t == T_VIDEO ? L"[\xB3D9\xC601\xC0C1]" : L"[\xC624\xB514\xC624]");
				b->style = (unsigned short)(b->style & ~(S_LINK | S_U));
				b->link = prev;
			} else free(u);
		}
		push(b, t, F_SKIP, 0);
		return;
	case T_FRAME: {
		char *u = attr_url(b, "src");
		if (u && !b->skip) {
			int prev = b->link;
			emit_block(b, 0);
			b->link = add_link(d, u);
			b->style |= S_LINK | S_U;
			emit_forced(b, L"[\xD504\xB808\xC784]");
			b->link = prev;
			b->style = (unsigned short)(b->style & ~(S_LINK | S_U));
			emit_block(b, 0);
		} else free(u);
		return;
	}
	case T_BASE: {
		char *u = attr_url(b, "href");
		if (u) { free(d->base); d->base = u; }
		return;
	}
	case T_META: {
		const WCHAR *he = attr(b, "http-equiv");
		const WCHAR *ct = attr(b, "content");
		if (he && ct && wcontains_ci(he, "refresh")) {
			int delay = 0;
			const WCHAR *p = ct;
			while (*p >= '0' && *p <= '9') { delay = delay * 10 + (*p - '0'); p++; }
			while (*p && *p != '=' ) p++;
			if (*p == '=') {
				char *u, *r;
				p++;
				while (*p == ' ' || *p == '\'' || *p == '"') p++;
				u = w_to_utf8(p, -1);
				{ int l = (int)strlen(u); while (l > 0 && (u[l - 1] == '\'' || u[l - 1] == '"' || u[l - 1] == ' ')) u[--l] = 0; }
				r = url_resolve(d->base, u);
				free(u);
				free(d->refresh_url);
				d->refresh_url = r;
				d->refresh_delay = delay;
			}
		}
		return;
	}
	case T_BR:
		if (b->skip) return;
		trim_trailing_space(b);
		add_item(b, IT_BR)->link = -1;
		b->at_line_start = 1;
		b->last_space = 1;
		return;
	case T_WBR: return;
	case T_HR:
		if (b->skip) return;
		b->li_fresh = 0;
		emit_block(b, 0);
		add_item(b, IT_HR)->link = -1;
		emit_block(b, 0);
		return;
	case T_IMG: {
		char *u = NULL;
		const WCHAR *src = attr(b, "src");
		const char *lazy[] = { "data-src", "data-original", "data-lazy-src", "data-url", NULL };
		int i, w, h;
		Img *im;
		Item *it;
		if (b->skip) return;
		for (i = 0; lazy[i]; i++) if (attr(b, lazy[i]) && *attr(b, lazy[i])) {
			if (!src || !*src || wcontains_ci(src, "data:image/gif") || wcontains_ci(src, "blank") || wcontains_ci(src, "spacer") || wcontains_ci(src, "data:image/svg")) {
				u = attr_url(b, lazy[i]);
				break;
			}
		}
		if (!u) u = attr_url(b, "src");
		w = attr_int(b, "width");
		h = attr_int(b, "height");
		if ((w && w <= 2) || (h && h <= 2)) { free(u); return; }
		if (!u || !*u) { free(u); u = xstrdup(""); }
		d->imgs = (Img *)grow(d->imgs, &d->capimgs, d->nimgs, sizeof(Img));
		im = &d->imgs[d->nimgs];
		memset(im, 0, sizeof *im);
		im->url = u;
		im->w = w; im->h = h;
		{
			const WCHAR *alt = attr(b, "alt");
			if (!alt || !*alt) alt = attr(b, "title");
			im->alt = d->text.len;
			if (alt) {
				int n = 0;
				while (alt[n] && n < 60) n++;
				wbuf_add(&d->text, alt, n);
				im->altlen = n;
			}
		}
		if (!*u || strnieq(u, "data:image/svg", 14) || stristr(u, ".svg")) im->state = 3;
		it = add_item(b, IT_IMG);
		b->cell_content = 1;
		b->li_fresh = 0;
		it->aux = d->nimgs;
		d->nimgs++;
		b->at_line_start = 0;
		b->last_space = 0;
		return;
	}
	case T_INPUT: {
		const WCHAR *ty = attr(b, "type");
		int type = CT_TEXT;
		Ctl *c;
		if (ty) {
			if (wcsieq_a(ty, (int)wcslen(ty), "password")) type = CT_PASSWORD;
			else if (wcsieq_a(ty, (int)wcslen(ty), "hidden")) type = CT_HIDDEN;
			else if (wcsieq_a(ty, (int)wcslen(ty), "checkbox")) type = CT_CHECK;
			else if (wcsieq_a(ty, (int)wcslen(ty), "radio")) type = CT_RADIO;
			else if (wcsieq_a(ty, (int)wcslen(ty), "submit")) type = CT_SUBMIT;
			else if (wcsieq_a(ty, (int)wcslen(ty), "image")) type = CT_IMAGE;
			else if (wcsieq_a(ty, (int)wcslen(ty), "reset")) type = CT_RESET;
			else if (wcsieq_a(ty, (int)wcslen(ty), "button")) type = CT_BUTTON;
			else if (wcsieq_a(ty, (int)wcslen(ty), "file")) type = CT_FILE;
		}
		if (b->skip && type != CT_HIDDEN) { if (type == CT_TEXT || type == CT_PASSWORD) type = CT_HIDDEN; else return; }
		c = add_ctl(b, type);
		if (type == CT_SUBMIT || type == CT_IMAGE || type == CT_RESET || type == CT_BUTTON) {
			const WCHAR *v = attr(b, "value");
			if (!v || !*v) v = attr(b, "alt");
			c->label = xwcsdup(v && *v ? v : (type == CT_RESET ? L"\xCD08\xAE30\xD654" : L"\xC804\xC1A1"));
		}
		if (type == CT_TEXT || type == CT_PASSWORD) {
			const WCHAR *ph = attr(b, "placeholder");
			if (ph && *ph) c->label = xwcsdup(ph);
		}
		if ((type == CT_CHECK || type == CT_RADIO) && !attr(b, "value")) { free(c->value); c->value = xwcsdup(L"on"); }
		return;
	}
	case T_SELECT: {
		Ctl *c;
		if (b->skip) { push(b, t, F_SKIP, 0); return; }
		c = add_ctl(b, CT_SELECT);
		c->sel = 0;
		b->select_ctl = b->d->nctls - 1;
		b->opt_open = 0;
		(void)c;
		return;
	}
	case T_BUTTON: {
		const WCHAR *ty = attr(b, "type");
		int type = CT_SUBMIT;
		if (ty && wcsieq_a(ty, (int)wcslen(ty), "button")) type = CT_BUTTON;
		if (ty && wcsieq_a(ty, (int)wcslen(ty), "reset")) type = CT_RESET;
		if (b->skip) { push(b, t, F_SKIP, 0); return; }
		add_ctl(b, type);
		b->button_ctl = b->d->nctls - 1;
		return;
	}
	case T_FORM: {
		Form *f;
		const WCHAR *m = attr(b, "method");
		const WCHAR *et = attr(b, "enctype");
		d->forms = (Form *)grow(d->forms, &d->capforms, d->nforms, sizeof(Form));
		f = &d->forms[d->nforms];
		f->action = attr_url(b, "action");
		if (!f->action || !attr(b, "action") || !*attr(b, "action")) { free(f->action); f->action = xstrdup(d->url); }
		f->post = m && wcsieq_a(m, (int)wcslen(m), "post");
		f->multipart = et && wcontains_ci(et, "multipart");
		b->form = d->nforms++;
		push(b, t, F_BLOCK, 0);
		emit_block(b, 0);
		return;
	}
	case T_A: {
		char *u;
		const WCHAR *nm = attr(b, "name");
		pop_to(b, T_A, T_TABLE, T_UNKNOWN - 1);
		if (nm && !b->skip) add_anchor(b, nm);
		push(b, t, 0, 0);
		u = attr_url(b, "href");
		if (u && *u && !strnieq(u, "javascript:", 11)) {
			b->link = add_link(d, u);
			b->style |= S_LINK | S_U;
		} else free(u);
		return;
	}
	case T_LI: {
		Item *it;
		int k;
		/* close previous li */
		for (k = b->sp - 1; k >= 0 && k >= b->sp - 40; k--) {
			if (b->st[k].tag == T_LI) { while (b->sp > k) pop1(b); break; }
			if (b->st[k].tag == T_UL || b->st[k].tag == T_OL || b->st[k].tag == T_MENU || b->st[k].tag == T_DIR) break;
		}
		close_p(b);
		push(b, t, F_BLOCK, 0);
		if (b->skip) return;
		b->li_fresh = 0;
		emit_block(b, 0);
		it = add_item(b, IT_BULLET);
		it->link = -1;
		if (b->list_depth > 0 && b->list_ord[b->list_depth - 1]) {
			const WCHAR *v = attr(b, "value");
			if (v) b->list_num[b->list_depth - 1] = attr_int(b, "value");
			it->aux = b->list_num[b->list_depth - 1]++;
		} else it->aux = -(b->list_depth > 0 ? b->list_depth : 1);
		b->at_line_start = 1;
		b->li_fresh = 1;
		return;
	}
	case T_UL: case T_OL: case T_MENU: case T_DIR:
		b->li_fresh = 0;
		if (!b->skip) { emit_block(b, b->list_depth ? 0 : 1); add_item(b, IT_INDENT)->link = -1; }
		push(b, t, F_LIST | F_OUTDENT, 0);
		if (b->list_depth < 32) {
			b->list_ord[b->list_depth] = (t == T_OL);
			b->list_num[b->list_depth] = attr(b, "start") ? attr_int(b, "start") : 1;
		}
		b->list_depth++;
		return;
	case T_DT: case T_DD: {
		int k;
		for (k = b->sp - 1; k >= 0 && k >= b->sp - 40; k--) {
			if (b->st[k].tag == T_DT || b->st[k].tag == T_DD) { while (b->sp > k) pop1(b); break; }
			if (b->st[k].tag == T_DL) break;
		}
		close_p(b);
		if (t == T_DD) {
			if (!b->skip) { emit_block(b, 0); add_item(b, IT_INDENT)->link = -1; }
			push(b, t, F_OUTDENT, 0);
		} else {
			push(b, t, F_BLOCK, 0);
			if (!b->skip) emit_block(b, 0);
			b->style |= S_B;
		}
		return;
	}
	case T_BLOCKQUOTE: case T_FIGURE:
		if (!b->skip) { emit_block(b, 1); add_item(b, IT_INDENT)->link = -1; }
		push(b, t, F_OUTDENT, 0);
		return;
	case T_TABLE:
		b->li_fresh = 0;
		push(b, t, F_BLOCK, 0);
		if (!b->skip) emit_block(b, 0);
		b->cells = 0;
		b->in_cell = 0;
		b->center = 0;
		return;
	case T_TR: {
		int k;
		for (k = b->sp - 1; k >= 0 && k >= b->sp - 60; k--) {
			if (b->st[k].tag == T_TR) { while (b->sp > k) pop1(b); break; }
			if (b->st[k].tag == T_TABLE) break;
		}
		b->in_cell = 0;
		push(b, t, F_BLOCK, 0);
		if (!b->skip) emit_block(b, 0);
		b->cells = 0;
		return;
	}
	case T_TD: case T_TH: {
		int k;
		for (k = b->sp - 1; k >= 0 && k >= b->sp - 60; k--) {
			if (b->st[k].tag == T_TD || b->st[k].tag == T_TH) { while (b->sp > k) pop1(b); break; }
			if (b->st[k].tag == T_TR || b->st[k].tag == T_TABLE) break;
		}
		push(b, t, 0, 0);
		if (b->cells > 0 && !b->skip) {
			/* drop a block break left at the end of the previous cell */
			Item *li = last_item(b);
			if (li && li->type == IT_BLOCK && b->in_cell && b->cell_content && b->d->nitems - 1 >= b->cell_start_item) { b->d->nitems--; b->at_line_start = 0; b->last_space = 0; }
		}
		b->in_cell = 1;
		b->cell_start_item = b->d->nitems;
		b->cell_content = 0;
		if (b->cells++ > 0 && !b->skip && !b->at_line_start && !b->last_space) emit_forced(b, L" ");
		b->cell_content = 0;
		if (t == T_TH) b->style |= S_B;
		if (wants_center(b)) b->center = 1;
		return;
	}
	case T_PRE: case T_XMP: case T_PLAINTEXT:
		push(b, t, F_BLOCK, 1);
		if (!b->skip) emit_block(b, 1);
		b->pre = 1;
		b->pre_nl = 1;
		b->style |= S_MONO | S_PRE;
		return;
	case T_H1: case T_H2: case T_H3: case T_H4: case T_H5: case T_H6: {
		int sz = t == T_H1 ? 3 : t == T_H2 ? 2 : t == T_H3 ? 2 : 1;
		push(b, t, F_BLOCK, 1);
		if (wants_center(b)) b->center = 1;
		if (!b->skip) emit_block(b, 1);
		b->style = (unsigned short)((b->style & ~S_SZ_MASK) | (sz << S_SZ_SH) | S_B);
		return;
	}
	case T_P: case T_ADDRESS:
		push(b, t, F_BLOCK, 1);
		if (wants_center(b)) b->center = 1;
		if (!b->skip) emit_block(b, 1);
		if (t == T_ADDRESS) b->style |= S_I;
		return;
	case T_CENTER:
		push(b, t, F_BLOCK, 0);
		b->center = 1;
		if (!b->skip) emit_block(b, 0);
		return;
	case T_DIV: case T_SECTION: case T_ARTICLE: case T_ASIDE: case T_HEADER: case T_FOOTER: case T_NAV: case T_MAIN:
	case T_FIGCAPTION: case T_FIELDSET: case T_DETAILS: case T_SUMMARY: case T_LEGEND: case T_CAPTION: case T_DL: case T_HGROUP:
		push(b, t, F_BLOCK, (t == T_DL || t == T_FIELDSET) ? 1 : 0);
		b->center = wants_center(b) ? 1 : (t == T_CAPTION ? 1 : b->center);
		if (!b->skip) emit_block(b, (t == T_DL || t == T_FIELDSET) ? 1 : 0);
		if (t == T_SUMMARY || t == T_LEGEND) b->style |= S_B;
		return;
	case T_B: case T_STRONG: push(b, t, 0, 0); b->style |= S_B; return;
	case T_I: case T_EM: case T_CITE: case T_DFN: case T_VAR: push(b, t, 0, 0); b->style |= S_I; return;
	case T_U: case T_INS: push(b, t, 0, 0); b->style |= S_U; return;
	case T_S: case T_STRIKE: case T_DEL: push(b, t, 0, 0); b->style |= S_STRIKE; return;
	case T_CODE: case T_TT: case T_KBD: case T_SAMP: push(b, t, 0, 0); b->style |= S_MONO; return;
	case T_SMALL: case T_SUB: case T_SUP:
		push(b, t, 0, 0);
		b->style = (unsigned short)((b->style & ~S_SZ_MASK) | (0 << S_SZ_SH));
		return;
	case T_BIG: push(b, t, 0, 0); b->style = (unsigned short)((b->style & ~S_SZ_MASK) | (2 << S_SZ_SH)); return;
	case T_Q: push(b, t, 0, 0); emit_forced(b, L"\x201C"); return;
	case T_FONT: {
		const WCHAR *sz = attr(b, "size");
		push(b, t, 0, 0);
		if (sz) {
			int v = 0, rel = 0;
			if (*sz == '+') { rel = 1; sz++; } else if (*sz == '-') { rel = -1; sz++; }
			while (*sz >= '0' && *sz <= '9') v = v * 10 + (*sz++ - '0');
			if (rel) v = 3 + rel * v;
			b->style = (unsigned short)((b->style & ~S_SZ_MASK) | ((v <= 2 ? 0 : v == 3 ? 1 : v <= 5 ? 2 : 3) << S_SZ_SH));
		}
		return;
	}
	default:
		if (!is_void(t)) push(b, t, 0, 0);
		return;
	}
}

static void end_tag(B *b, int t)
{
	if (b->select_ctl >= 0) {
		if (t == T_OPTION) finish_option(b);
		else if (t == T_SELECT) {
			Ctl *c;
			finish_option(b);
			c = &b->d->ctls[b->select_ctl];
			if (c->sel >= c->nopt) c->sel = 0;
			b->select_ctl = -1;
		}
		return;
	}
	if (b->button_ctl >= 0) {
		if (t == T_BUTTON) {
			Ctl *c = &b->d->ctls[b->button_ctl];
			if (!c->label || !*c->label) { free(c->label); c->label = xwcsdup(c->type == CT_SUBMIT ? L"\xC804\xC1A1" : L"\xBC84\xD2BC"); }
			b->button_ctl = -1;
		}
		return;
	}
	switch (t) {
	case T_BR:
		start_tag(b, T_BR, 0);
		return;
	case T_P:
		if (!pop_to(b, T_P, T_DIV, T_TABLE) && !b->skip) emit_block(b, 1);
		return;
	case T_Q:
		if (pop_to(b, T_Q, T_P, T_DIV)) emit_forced(b, L"\x201D");
		return;
	case T_FORM:
		pop_to(b, T_FORM, T_UNKNOWN - 1, T_UNKNOWN - 1);
		b->form = -1;
		return;
	case T_LI: pop_to(b, T_LI, T_UL, T_OL); return;
	case T_TD: case T_TH: pop_to(b, t, T_TR, T_TABLE); return;
	case T_TR: pop_to(b, T_TR, T_TABLE, T_TABLE); return;
	case T_HTML: case T_BODY: return;
	default:
		if (t == T_UNKNOWN) { if (b->sp && b->st[b->sp - 1].tag == T_UNKNOWN) pop1(b); return; }
		pop_to(b, t, T_UNKNOWN - 1, T_UNKNOWN - 1);
		return;
	}
}

/* ------------------------------------------------------------------ */
/* tokenizer                                                           */
/* ------------------------------------------------------------------ */
static int wncase_eq(const WCHAR *s, int n, const char *a)
{
	int i;
	for (i = 0; a[i]; i++) {
		WCHAR c;
		if (i >= n) return 0;
		c = s[i];
		if (c >= 'A' && c <= 'Z') c += 32;
		if (c != (WCHAR)a[i]) return 0;
	}
	return 1;
}

/* find "</name" from position i; returns index of '<' or n */
static int find_close(const WCHAR *s, int n, int i, const char *name)
{
	int nl = (int)strlen(name);
	for (; i < n - 1; i++) {
		if (s[i] == '<' && s[i + 1] == '/' && wncase_eq(s + i + 2, n - i - 2, name)) {
			WCHAR c = (i + 2 + nl < n) ? s[i + 2 + nl] : '>';
			if (c == '>' || is_ws(c) || c == '/') return i;
		}
	}
	return n;
}

static void free_attrs(B *b)
{
	int i;
	for (i = 0; i < b->nat; i++) free(b->at[i].val);
	b->nat = 0;
}

static void parse(B *b, const WCHAR *s, int n)
{
	int i = 0;
	WBuf tmp;
	wbuf_init(&tmp);
	while (i < n) {
		if (s[i] == '<' && i + 1 < n) {
			WCHAR c = s[i + 1];
			if (c == '!') {
				if (i + 3 < n && s[i + 2] == '-' && s[i + 3] == '-') {
					int j = i + 4;
					while (j + 2 < n && !(s[j] == '-' && s[j + 1] == '-' && s[j + 2] == '>')) j++;
					i = j + 3;
				} else {
					while (i < n && s[i] != '>') i++;
					i++;
				}
				continue;
			}
			if (c == '?') { while (i < n && s[i] != '>') i++; i++; continue; }
			if (c == '/' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
				int endtag = (c == '/');
				int j = i + 1 + endtag;
				char name[24];
				int k = 0, t, selfclose = 0;
				if (endtag && !(j < n && ((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z')))) {
					while (i < n && s[i] != '>') i++;
					i++;
					continue;
				}
				while (j < n && !is_ws(s[j]) && s[j] != '>' && s[j] != '/') {
					WCHAR ch = s[j];
					if (ch >= 'A' && ch <= 'Z') ch += 32;
					if (k < 23) name[k++] = (ch < 128) ? (char)ch : '?';
					j++;
				}
				name[k] = 0;
				t = tag_lookup(name);
				/* attributes */
				free_attrs(b);
				while (j < n && s[j] != '>') {
					int ns, ne, vs = -1, ve = -1;
					if (is_ws(s[j])) { j++; continue; }
					if (s[j] == '/') { if (j + 1 < n && s[j + 1] == '>') selfclose = 1; j++; continue; }
					ns = j;
					while (j < n && !is_ws(s[j]) && s[j] != '=' && s[j] != '>' && !(s[j] == '/' && j + 1 < n && s[j + 1] == '>')) j++;
					ne = j;
					while (j < n && is_ws(s[j])) j++;
					if (j < n && s[j] == '=') {
						j++;
						while (j < n && is_ws(s[j])) j++;
						if (j < n && (s[j] == '"' || s[j] == '\'')) {
							WCHAR q = s[j++];
							vs = j;
							while (j < n && s[j] != q) j++;
							ve = j;
							if (j < n) j++;
						} else {
							vs = j;
							while (j < n && !is_ws(s[j]) && s[j] != '>') j++;
							ve = j;
						}
					}
					if (!endtag && b->nat < 40 && ne > ns && ne - ns < 24) {
						Attr *a = &b->at[b->nat];
						int q;
						for (q = 0; q < ne - ns; q++) {
							WCHAR ch = s[ns + q];
							if (ch >= 'A' && ch <= 'Z') ch += 32;
							a->name[q] = ch < 128 ? (char)ch : '?';
						}
						a->name[ne - ns] = 0;
						if (vs >= 0) {
							tmp.len = 0;
							decode_into(&tmp, s + vs, ve - vs);
							a->val = (WCHAR *)malloc((tmp.len + 1) * sizeof(WCHAR));
							if (tmp.len) memcpy(a->val, tmp.p, tmp.len * sizeof(WCHAR));
							a->val[tmp.len] = 0;
						} else a->val = NULL;
						b->nat++;
					}
				}
				i = j + 1;
				if (endtag) { end_tag(b, t); continue; }
				/* raw text elements */
				if (t == T_SCRIPT || t == T_STYLE || t == T_TEXTAREA || t == T_TITLE || t == T_XMP || t == T_NOEMBED || t == T_NOFRAMES || (t == T_IFRAME) || t == T_TEMPLATE) {
					int ce;
					if (selfclose && t != T_TEXTAREA && t != T_TITLE) { if (t == T_IFRAME) { start_tag(b, t, 1); end_tag(b, t); } continue; }
					ce = find_close(s, n, i, name);
					if (t == T_TITLE) {
						if (!b->d->title && b->skip == 0) {
							tmp.len = 0;
							decode_into(&tmp, s + i, ce - i);
							{
								WBuf w; int q; wbuf_init(&w);
								for (q = 0; q < tmp.len; q++) {
									WCHAR ch = tmp.p[q];
									if (is_ws(ch)) { if (w.len && w.p[w.len - 1] != ' ') wbuf_addc(&w, ' '); }
									else wbuf_addc(&w, ch);
								}
								while (w.len && w.p[w.len - 1] == ' ') w.p[--w.len] = 0;
								b->d->title = w.p;
							}
						}
					} else if (t == T_TEXTAREA) {
						Ctl *cc;
						const WCHAR *rows = attr(b, "rows");
						if (b->skip) { i = ce; goto skip_close; }
						cc = add_ctl(b, CT_TEXTAREA);
						tmp.len = 0;
						{
							int st = i;
							if (st < ce && s[st] == '\r') st++;
							if (st < ce && s[st] == '\n') st++;
							decode_into(&tmp, s + st, ce - st);
						}
						free(cc->value);
						cc->value = (WCHAR *)malloc((tmp.len + 1) * sizeof(WCHAR));
						if (tmp.len) memcpy(cc->value, tmp.p, tmp.len * sizeof(WCHAR));
						cc->value[tmp.len] = 0;
						cc->rows = rows ? attr_int(b, "rows") : 3;
						{
							const WCHAR *ph = attr(b, "placeholder");
							if (ph && *ph) cc->label = xwcsdup(ph);
						}
					} else if (t == T_XMP) {
						start_tag(b, T_PRE, 0);
						emit_text(b, s + i, ce - i);
						end_tag(b, T_PRE);
					} else if (t == T_IFRAME) {
						start_tag(b, t, 0);
						end_tag(b, t);
					}
				skip_close:
					i = ce;
					while (i < n && s[i] != '>') i++;
					i++;
					continue;
				}
				start_tag(b, t, selfclose);
				if (selfclose && !is_void(t) && t != T_UNKNOWN && b->sp && b->st[b->sp - 1].tag == t) end_tag(b, t);
				else if (selfclose && t == T_UNKNOWN && b->sp && b->st[b->sp - 1].tag == T_UNKNOWN) pop1(b);
				continue;
			}
		}
		/* text run */
		{
			int j = i + 1;
			while (j < n && s[j] != '<') j++;
			tmp.len = 0;
			decode_into(&tmp, s + i, j - i);
			emit_text(b, tmp.p, tmp.len);
			i = j;
		}
	}
	free_attrs(b);
	wbuf_free(&tmp);
}

/* ------------------------------------------------------------------ */
UINT charset_to_cp(const char *cs)
{
	char c[40];
	int i;
	if (!cs) return 0;
	for (i = 0; cs[i] && i < 39; i++) c[i] = (char)((cs[i] >= 'A' && cs[i] <= 'Z') ? cs[i] + 32 : cs[i]);
	c[i] = 0;
	if (strstr(c, "utf-8") || strstr(c, "utf8")) return CP_UTF8;
	if (strstr(c, "euc-kr") || strstr(c, "euckr") || strstr(c, "949") || strstr(c, "ks_c_5601") || strstr(c, "ksc5601") || strstr(c, "korean")) return 949;
	if (strstr(c, "8859-1") || strstr(c, "1252") || strstr(c, "latin1") || strstr(c, "ascii")) return 1252;
	if (strstr(c, "shift_jis") || strstr(c, "sjis") || strstr(c, "932")) return 932;
	if (strstr(c, "gb2312") || strstr(c, "gbk") || strstr(c, "936") || strstr(c, "gb18030")) return 936;
	if (strstr(c, "big5") || strstr(c, "950")) return 950;
	if (strstr(c, "1251")) return 1251;
	if (strstr(c, "koi8")) return 20866;
	if (strstr(c, "euc-jp")) return 20932;
	if (strstr(c, "utf-16")) return 1200;
	return 0;
}

static int valid_utf8(const unsigned char *s, int n)
{
	int i = 0, nonascii = 0;
	if (n > 65536) n = 65536;
	while (i < n) {
		unsigned char c = s[i];
		int k;
		if (c < 0x80) { i++; continue; }
		nonascii = 1;
		if ((c & 0xE0) == 0xC0) k = 1;
		else if ((c & 0xF0) == 0xE0) k = 2;
		else if ((c & 0xF8) == 0xF0) k = 3;
		else return 0;
		if (i + k >= n) return 1; /* truncated at end of sample */
		while (k--) { i++; if ((s[i] & 0xC0) != 0x80) return 0; }
		i++;
	}
	(void)nonascii;
	return 1;
}

static UINT sniff_meta(const char *s, int n)
{
	int lim = n < 4096 ? n : 4096;
	int i;
	for (i = 0; i < lim - 8; i++) {
		if (strnieq(s + i, "charset", 7)) {
			int j = i + 7;
			char cs[40];
			int k = 0;
			while (j < lim && (s[j] == ' ' || s[j] == '=' || s[j] == '"' || s[j] == '\'')) j++;
			while (j < lim && k < 39 && s[j] != '"' && s[j] != '\'' && s[j] != ';' && s[j] != ' ' && s[j] != '>' && s[j] != '/') cs[k++] = s[j++];
			cs[k] = 0;
			if (k) {
				UINT cp = charset_to_cp(cs);
				if (cp) return cp;
			}
		}
	}
	return 0;
}

Doc *html_parse(const char *bytes, int len, const char *url, const char *charset_hint, int is_text)
{
	return html_parse_ex(bytes, len, url, charset_hint, is_text, 0);
}

Doc *html_parse_ex(const char *bytes, int len, const char *url, const char *charset_hint, int is_text, int flags)
{
	Doc *d = (Doc *)calloc(1, sizeof(Doc));
	B *b = (B *)calloc(1, sizeof(B));
	WCHAR *w = NULL;
	int wl = 0;
	UINT cp = charset_to_cp(charset_hint);
	const unsigned char *u = (const unsigned char *)bytes;
	d->refs = 1;
	d->url = xstrdup(url);
	d->base = xstrdup(url);
	if (len >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) { cp = CP_UTF8; bytes += 3; len -= 3; }
	else if (len >= 2 && u[0] == 0xFF && u[1] == 0xFE) { cp = 1200; bytes += 2; len -= 2; }
	if (!cp && !is_text) cp = sniff_meta(bytes, len);
	if (cp == CP_UTF8 && !valid_utf8((const unsigned char *)bytes, len)) cp = 949; /* mislabelled */
	if (!cp) cp = valid_utf8((const unsigned char *)bytes, len) ? CP_UTF8 : 949;
	if (cp == 1200) {
		wl = len / 2;
		w = (WCHAR *)malloc((wl + 1) * sizeof(WCHAR));
		memcpy(w, bytes, wl * 2);
		w[wl] = 0;
		cp = CP_UTF8;
	} else {
		w = cp_to_w(bytes, len, cp, &wl);
		if (!w) { w = (WCHAR *)calloc(1, sizeof(WCHAR)); wl = 0; }
	}
	d->cp = cp;
	b->d = d;
	b->flags = flags;
	b->link = -1;
	b->form = -1;
	b->select_ctl = -1;
	b->button_ctl = -1;
	b->at_line_start = 1;
	b->last_space = 1;
	b->style = 1 << S_SZ_SH;
	wbuf_init(&b->opt_label);
	if (is_text) {
		b->pre = 1;
		b->style |= S_MONO | S_PRE;
		emit_text(b, w, wl);
	} else {
		parse(b, w, wl);
	}
	finish_option(b);
	while (b->sp) pop1(b);
	trim_trailing_space(b);
	wbuf_free(&b->opt_label);
	free(b->opt_value);
	free(b);
	free(w);
	if (!d->text.p) wbuf_addc(&d->text, 0), d->text.len = 0;
	return d;
}

void doc_addref(Doc *d) { InterlockedIncrement((LONG *)&d->refs); }

void doc_release(Doc *d)
{
	int i, j;
	if (!d) return;
	if (InterlockedDecrement((LONG *)&d->refs) > 0) return;
	free(d->url); free(d->base); free(d->title); free(d->refresh_url);
	wbuf_free(&d->text);
	free(d->items);
	for (i = 0; i < d->nlinks; i++) free(d->links[i]);
	free(d->links);
	for (i = 0; i < d->nimgs; i++) {
		free(d->imgs[i].url);
		free(d->imgs[i].pix);
		if (d->imgs[i].hbm) DeleteObject((HGDIOBJ)d->imgs[i].hbm);
	}
	free(d->imgs);
	for (i = 0; i < d->nctls; i++) {
		Ctl *c = &d->ctls[i];
		free(c->name); free(c->value); free(c->label);
		for (j = 0; j < c->nopt; j++) { free(c->opt_label[j]); free(c->opt_value[j]); }
		free(c->opt_label); free(c->opt_value);
	}
	free(d->ctls);
	for (i = 0; i < d->nforms; i++) free(d->forms[i].action);
	free(d->forms);
	for (i = 0; i < d->nanchors; i++) free(d->anchors[i].name);
	free(d->anchors);
	free(d);
}
