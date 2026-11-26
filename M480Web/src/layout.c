/* M480Web - layout and painting */
#include "util.h"
#include "layout.h"

int g_scale100 = 100;
int g_images_on = 1;
int g_dark = 0;

static HFONT g_fonts[32];
static int g_fh[32], g_fasc[32];
static int g_base = 15;

/* find a font that has Hangul glyphs */
static WCHAR g_face[LF_FACESIZE];
static int g_face_score = -1;
static int g_face_done;

static int face_score(const WCHAR *n)
{
	static const WCHAR *pref[] = { L"Gulim", L"\xAD74\xB9BC", L"Dotum", L"\xB3CB\xC6C0", L"Malgun", L"\xB9D1\xC740", L"Nanum", L"\xB098\xB214", L"Samsung", L"Batang", L"\xBC14\xD0D5", NULL };
	int i;
	for (i = 0; pref[i]; i++) if (wcsstr(n, pref[i])) return 100 - i;
	return 1;
}

static int CALLBACK enum_face2(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp)
{
	(void)tm; (void)type; (void)lp;
	if (lf->lfCharSet == HANGEUL_CHARSET && lf->lfFaceName[0] != '@') {
		int s = face_score(lf->lfFaceName);
		if (wcsstr(lf->lfFaceName, L"Coding") || wcsstr(lf->lfFaceName, L"Pen") || wcsstr(lf->lfFaceName, L"Brush")) s = 0;
		if (s > g_face_score) { g_face_score = s; wcsncpy(g_face, lf->lfFaceName, LF_FACESIZE - 1); }
	}
	return 1;
}

static int CALLBACK enum_face1(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp)
{
	(void)tm; (void)type;
	EnumFontFamiliesW((HDC)lp, lf->lfFaceName, (FONTENUMPROCW)enum_face2, 0);
	return 1;
}

static void find_face(void)
{
	HDC dc;
	if (g_face_done) return;
	g_face_done = 1;
	dc = GetDC(NULL);
	EnumFontFamiliesW(dc, NULL, (FONTENUMPROCW)enum_face1, (LPARAM)dc);
	ReleaseDC(NULL, dc);
}

static int font_index(int style)
{
	return ((style & S_B) ? 1 : 0) | ((style & S_I) ? 2 : 0) | ((style & S_MONO) ? 4 : 0) | (S_SZ(style) << 3);
}

static void cw_reset(void);
void fonts_set_base(int px)
{
	int i;
	cw_reset();
	for (i = 0; i < 32; i++) if (g_fonts[i]) { DeleteObject(g_fonts[i]); g_fonts[i] = NULL; }
	g_base = px;
}
int fonts_base(void) { return g_base; }

static void ensure_font(int idx)
{
	LOGFONTW lf;
	static const int pct[4] = { 84, 100, 118, 140 };
	HDC dc;
	TEXTMETRICW tm;
	HGDIOBJ old;
	if (g_fonts[idx]) return;
	memset(&lf, 0, sizeof lf);
	lf.lfHeight = -(g_base * pct[idx >> 3] / 100);
	lf.lfWeight = (idx & 1) ? FW_BOLD : FW_NORMAL;
	lf.lfItalic = (idx & 2) ? 1 : 0;
	find_face();
	lf.lfCharSet = DEFAULT_CHARSET;
	if (g_face[0]) { lf.lfCharSet = HANGEUL_CHARSET; wcscpy(lf.lfFaceName, g_face); }
#ifdef _WIN32_WCE
	lf.lfQuality = 5; /* CLEARTYPE_QUALITY */
#else
	lf.lfQuality = ANTIALIASED_QUALITY;
#endif
	if (idx & 4) { lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN; wcscpy(lf.lfFaceName, L"Courier New"); }
	g_fonts[idx] = CreateFontIndirectW(&lf);
	dc = GetDC(NULL);
	old = SelectObject(dc, g_fonts[idx]);
	GetTextMetricsW(dc, &tm);
	SelectObject(dc, old);
	ReleaseDC(NULL, dc);
	g_fh[idx] = tm.tmHeight;
	g_fasc[idx] = tm.tmAscent;
}

HFONT font_for(int style) { int i = font_index(style); ensure_font(i); return g_fonts[i]; }
int font_height(int style) { int i = font_index(style); ensure_font(i); return g_fh[i]; }
static int font_ascent(int style) { int i = font_index(style); ensure_font(i); return g_fasc[i]; }

/* ------------------------------------------------------------------ */
typedef struct {
	Layout *L;
	HDC hdc;
	Doc *d;
	int width, margin, unit;
	int indent;
	int x, y;
	int x0, right;
	int line_first;   /* index of first box in current line */
	int line_has;
	int pend;         /* pending block margin (half lines) */
	int center;
	int halfline;
} LS;

static Box *add_box(LS *s, int item, int x, int w, int h)
{
	Layout *L = s->L;
	Box *b;
	if (L->nboxes >= L->cap) {
		L->cap = L->cap ? L->cap * 2 : 256;
		L->boxes = (Box *)realloc(L->boxes, L->cap * sizeof(Box));
	}
	b = &L->boxes[L->nboxes++];
	b->x = x; b->w = w; b->h = h; b->y = 0; b->lb = 0;
	b->item = item; b->start = 0; b->len = 0;
	return b;
}

static void line_start(LS *s)
{
	if (s->line_has) return;
	if (s->pend && s->y > s->margin) s->y += s->pend * s->halfline;
	s->pend = 0;
	s->line_has = 1;
	s->line_first = s->L->nboxes;
	s->x = s->x0;
}

static void finish_line(LS *s)
{
	int i, lh = 0, maxx = 0;
	Layout *L = s->L;
	if (!s->line_has) return;
	for (i = s->line_first; i < L->nboxes; i++) {
		if (L->boxes[i].h > lh) lh = L->boxes[i].h;
		if (L->boxes[i].x + L->boxes[i].w > maxx) maxx = L->boxes[i].x + L->boxes[i].w;
	}
	if (lh == 0) lh = font_height(1 << S_SZ_SH);
	if (s->center && maxx < s->right) {
		int sh = (s->right - maxx) / 2;
		for (i = s->line_first; i < L->nboxes; i++) L->boxes[i].x += sh;
	}
	for (i = s->line_first; i < L->nboxes; i++) {
		L->boxes[i].y = s->y + lh - L->boxes[i].h;
		L->boxes[i].lb = s->y + lh;
	}
	s->y += lh + lh / 5 + SC(1);
	s->line_has = 0;
	s->x = s->x0;
}

static void set_indent(LS *s, int lvl)
{
	int maxi = (s->width / 3) / (s->unit ? s->unit : 1);
	s->indent = lvl < 0 ? 0 : lvl;
	s->x0 = s->margin + (s->indent > maxi ? maxi : s->indent) * s->unit;
	if (!s->line_has) s->x = s->x0;
}

static int is_cjk(WCHAR c)
{
	return (c >= 0x1100 && c <= 0x11FF) || (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF);
}
static int no_break_before(WCHAR c)
{
	return c == '.' || c == ',' || c == ')' || c == ']' || c == '}' || c == '!' || c == '?' || c == ':' || c == ';' ||
	       c == 0x3001 || c == 0x3002 || c == 0xFF0C || c == 0xFF0E || c == 0x300D || c == 0x300F || c == 0x201D || c == 0x2019 || c == '"' || c == '\'';
}
/* may we break between t[i-1] and t[i]? */
static int can_break(const WCHAR *t, int i)
{
	WCHAR a = t[i - 1], b = t[i];
	if (b == ' ' || a == ' ') return 1;
	if (no_break_before(b)) return 0;
	if (is_cjk(a) || is_cjk(b)) return 1;
	if (a == '-' || a == '/' || a == '&' || a == '?' || a == '_') return 1;
	return 0;
}

/* per-font character width cache: measuring every character with GDI is slow on
   a 400 MHz phone; long wiki pages lay out several times faster this way */
static unsigned short *g_cw[32];
static int g_cw_font = -1;

static void cw_reset(void)
{
	int i;
	for (i = 0; i < 32; i++) { free(g_cw[i]); g_cw[i] = NULL; }
}

static int measure_dx(HDC hdc, const WCHAR *t, int n, int *dx)
{
	if (g_cw_font >= 0) {
		unsigned short *cw = g_cw[g_cw_font];
		int i, acc = 0;
		if (!cw) cw = g_cw[g_cw_font] = (unsigned short *)calloc(65536, sizeof(unsigned short));
		if (cw) {
			for (i = 0; i < n; i++) {
				WCHAR c = t[i];
				int w = cw[c];
				if (!w) {
					SIZE s1;
					GetTextExtentPoint32W(hdc, &c, 1, &s1);
					w = s1.cx + 1;
					if (w > 65535) w = 65535;
					cw[c] = (unsigned short)w;
				}
				acc += w - 1;
				dx[i] = acc;
			}
			return acc;
		}
	}
	{
	SIZE sz;
	int fit = 0;
	if (!GetTextExtentExPointW(hdc, t, n, 0x7fffffff, &fit, dx, &sz)) {
		/* fallback: approximate */
		int i, acc = 0;
		for (i = 0; i < n; i++) {
			SIZE s1;
			GetTextExtentPoint32W(hdc, t + i, 1, &s1);
			acc += s1.cx;
			dx[i] = acc;
		}
		return acc;
	}
	return sz.cx;
	}
}

#define CHUNK 300

static void layout_text(LS *s, int idx)
{
	Item *it = &s->d->items[idx];
	const WCHAR *t = s->d->text.p + it->text;
	int n = it->len, pos = 0;
	int h = font_height(it->style);
	int pre = it->style & S_PRE;
	static int dx[CHUNK + 1];
	SelectObject(s->hdc, font_for(it->style));
	g_cw_font = font_index(it->style);
	while (pos < n) {
		int chunk, k, lo, hi, avail, bpos, e;
		if (!s->line_has || s->x == s->x0) {
			if (!pre) while (pos < n && t[pos] == ' ') pos++;
			if (pos >= n) break;
		}
		line_start(s);
		avail = s->right - s->x;
		chunk = n - pos;
		if (chunk > CHUNK) chunk = CHUNK;
		measure_dx(s->hdc, t + pos, chunk, dx);
		/* largest k with dx[k-1] <= avail */
		lo = 0; hi = chunk;
		while (lo < hi) {
			int mid = (lo + hi + 1) / 2;
			if (dx[mid - 1] <= avail) lo = mid; else hi = mid - 1;
		}
		k = lo;
		if (k == chunk && (chunk == n - pos || can_break(t + pos, chunk))) {
			Box *b = add_box(s, idx, s->x, dx[chunk - 1], h);
			b->start = it->text + pos; b->len = chunk;
			s->x += dx[chunk - 1];
			pos += chunk;
			continue;
		}
		if (k == chunk) k = chunk - 1;
		/* find break point <= k */
		bpos = k;
		while (bpos > 0 && !(bpos < chunk && can_break(t + pos, bpos))) bpos--;
		if (bpos == 0) {
			if (s->x > s->x0) { finish_line(s); continue; }
			bpos = k > 0 ? k : 1;
		}
		e = bpos;
		while (e > 0 && t[pos + e - 1] == ' ' && !pre) e--;
		if (e > 0) {
			Box *b = add_box(s, idx, s->x, dx[e - 1], h);
			b->start = it->text + pos; b->len = e;
		}
		finish_line(s);
		pos += bpos;
	}
}

static void img_size(LS *s, Img *im, int *w, int *h)
{
	int avail = s->right - s->x0;
	int aw = im->w ? SC(im->w) : 0, ah = im->h ? SC(im->h) : 0;
	if (avail < 8) avail = 8;
	if (g_images_on && im->state == 2 && im->iw > 0) {
		*w = im->iw; *h = im->ih;
	} else if (g_images_on && im->state != 3 && aw && ah) {
		*w = aw; *h = ah;
	} else if (g_images_on && im->state != 3 && (aw || ah)) {
		*w = aw ? aw : ah; *h = ah ? ah : aw;
	} else {
		/* alt text box */
		const WCHAR *alt = s->d->text.p + im->alt;
		int n = im->altlen;
		SIZE sz;
		int st = 0 | S_GRAY;
		SelectObject(s->hdc, font_for(st));
		if (n == 0) { alt = L"[\xC774\xBBF8\xC9C0]"; n = 5; }
		if (n > 40) n = 40;
		GetTextExtentPoint32W(s->hdc, alt, n, &sz);
		*w = sz.cx + SC(6);
		*h = font_height(st) + SC(2);
		if (!g_images_on || im->state == 3) return;
		/* unknown size while loading: small placeholder */
		*w = SC(16); *h = SC(16);
	}
	if (*w > avail) { *h = (int)((long)*h * avail / *w); *w = avail; }
	if (*h < 1) *h = 1;
	if (*w < 1) *w = 1;
}

static void ctl_size(LS *s, Ctl *c, int *w, int *h)
{
	int fh = font_height(1 << S_SZ_SH);
	int avail = s->right - s->x0;
	SIZE sz;
	SelectObject(s->hdc, font_for(1 << S_SZ_SH));
	switch (c->type) {
	case CT_CHECK: case CT_RADIO:
		*w = *h = fh * 3 / 4 + SC(2);
		break;
	case CT_SUBMIT: case CT_RESET: case CT_BUTTON: case CT_IMAGE: {
		const WCHAR *l = c->label ? c->label : L"?";
		int n = (int)wcslen(l);
		if (n > 60) n = 60;
		GetTextExtentPoint32W(s->hdc, l, n, &sz);
		*w = sz.cx + SC(16);
		*h = fh + SC(8);
		break;
	}
	case CT_SELECT: {
		int i, mw = SC(40);
		for (i = 0; i < c->nopt; i++) {
			int n = (int)wcslen(c->opt_label[i]);
			if (n > 50) n = 50;
			GetTextExtentPoint32W(s->hdc, c->opt_label[i], n, &sz);
			if (sz.cx > mw) mw = sz.cx;
		}
		*w = mw + SC(22);
		*h = fh + SC(6);
		break;
	}
	case CT_TEXTAREA: {
		int rows = c->rows < 2 ? 2 : c->rows > 6 ? 6 : c->rows;
		*w = avail;
		*h = rows * fh + SC(6);
		break;
	}
	case CT_FILE:
		GetTextExtentPoint32W(s->hdc, L"[\xD30C\xC77C \xCCA8\xBD80 \xBD88\xAC00]", 10, &sz);
		*w = sz.cx; *h = fh;
		break;
	default: {
		int cw = c->size > 0 ? c->size * fh / 2 + SC(8) : SC(150);
		if (cw < SC(60)) cw = SC(60);
		*w = cw;
		*h = fh + SC(6);
		break;
	}
	}
	if (*w > avail) *w = avail;
}

void layout_doc(Layout *L, HDC hdc, Doc *d, int width)
{
	LS s;
	int i;
	L->doc = d;
	L->nboxes = 0;
	L->width = width;
	memset(&s, 0, sizeof s);
	s.L = L; s.hdc = hdc; s.d = d;
	s.width = width;
	s.margin = SC(6);
	s.unit = font_height(1 << S_SZ_SH) + SC(4);
	s.right = width - s.margin;
	s.halfline = font_height(1 << S_SZ_SH) / 2;
	s.y = s.margin;
	set_indent(&s, 0);
	for (i = 0; i < d->nitems; i++) {
		Item *it = &d->items[i];
		switch (it->type) {
		case IT_TEXT:
			layout_text(&s, i);
			break;
		case IT_BR:
			if (s.line_has) finish_line(&s);
			else { line_start(&s); add_box(&s, i, s.x, 0, font_height(it->style)); finish_line(&s); }
			break;
		case IT_BLOCK:
			finish_line(&s);
			if ((it->aux & 0xff) > s.pend) s.pend = it->aux & 0xff;
			s.center = (it->aux & BLK_CENTER) != 0;
			break;
		case IT_HR:
			finish_line(&s);
			line_start(&s);
			{ Box *b = add_box(&s, i, s.x0, s.right - s.x0, SC(9)); (void)b; }
			finish_line(&s);
			break;
		case IT_INDENT:
			finish_line(&s);
			set_indent(&s, s.indent + 1);
			break;
		case IT_OUTDENT:
			finish_line(&s);
			set_indent(&s, s.indent - 1);
			break;
		case IT_BULLET: {
			int bw = s.unit - SC(2);
			finish_line(&s);
			line_start(&s);
			add_box(&s, i, s.x0 - bw - SC(2), bw, font_height(1 << S_SZ_SH));
			break;
		}
		case IT_IMG: {
			int w, h;
			img_size(&s, &d->imgs[it->aux], &w, &h);
			line_start(&s);
			if (s.x + w > s.right && s.x > s.x0) { finish_line(&s); line_start(&s); }
			add_box(&s, i, s.x, w, h);
			s.x += w + SC(2);
			break;
		}
		case IT_CTL: {
			int w, h;
			ctl_size(&s, &d->ctls[it->aux], &w, &h);
			line_start(&s);
			if (s.x + w > s.right && s.x > s.x0) { finish_line(&s); line_start(&s); }
			add_box(&s, i, s.x, w, h);
			s.x += w + SC(4);
			break;
		}
		case IT_ANCHOR:
			/* zero size marker */
			if (!s.line_has) { int y = s.y; line_start(&s); add_box(&s, i, s.x, 0, 0); s.line_has = 0; L->boxes[L->nboxes - 1].y = y; L->boxes[L->nboxes - 1].lb = y; s.x = s.x0; }
			else add_box(&s, i, s.x, 0, 0);
			break;
		}
	}
	finish_line(&s);
	L->height = s.y + s.margin * 2;
}

void layout_free(Layout *L)
{
	free(L->boxes);
	L->boxes = NULL;
	L->nboxes = L->cap = 0;
	L->doc = NULL;
}

/* ------------------------------------------------------------------ */
void img_make_bitmap(Img *im)
{
	struct { BITMAPINFOHEADER h; DWORD masks[3]; } bi;
	void *bits = NULL;
	HBITMAP hb;
	int stride, y;
	if (im->hbm || !im->pix) return;
	memset(&bi, 0, sizeof bi);
	bi.h.biSize = sizeof(BITMAPINFOHEADER);
	bi.h.biWidth = im->iw;
	bi.h.biHeight = -im->ih;
	bi.h.biPlanes = 1;
	bi.h.biBitCount = 16;
	bi.h.biCompression = BI_BITFIELDS;
	bi.masks[0] = 0xF800; bi.masks[1] = 0x07E0; bi.masks[2] = 0x001F;
	{
		HDC sdc = GetDC(NULL);
		hb = CreateDIBSection(sdc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
		ReleaseDC(NULL, sdc);
	}
	if (!hb || !bits) return;
	stride = ((im->iw * 2) + 3) & ~3;
	for (y = 0; y < im->ih; y++) memcpy((char *)bits + y * stride, im->pix + y * im->iw, im->iw * 2);
	im->hbm = hb;
	free(im->pix);
	im->pix = NULL;
}

static int first_visible(Layout *L, int y)
{
	int lo = 0, hi = L->nboxes;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (L->boxes[mid].lb < y) lo = mid + 1; else hi = mid;
	}
	return lo;
}

static void hline(HDC hdc, int x1, int x2, int y, COLORREF c)
{
	RECT r;
	HBRUSH br = CreateSolidBrush(c);
	r.left = x1; r.right = x2; r.top = y; r.bottom = y + (g_scale100 >= 175 ? 2 : 1);
	FillRect(hdc, &r, br);
	DeleteObject(br);
}

static void fill(HDC hdc, int l, int t, int r_, int b, COLORREF c)
{
	RECT r;
	HBRUSH br = CreateSolidBrush(c);
	r.left = l; r.top = t; r.right = r_; r.bottom = b;
	FillRect(hdc, &r, br);
	DeleteObject(br);
}

static void frame(HDC hdc, int l, int t, int r_, int b, COLORREF c)
{
	hline(hdc, l, r_, t, c);
	hline(hdc, l, r_, b - 1, c);
	fill(hdc, l, t, l + 1, b, c);
	fill(hdc, r_ - 1, t, r_, b, c);
}

static void draw_ctl(Layout *L, HDC hdc, Ctl *c, int x, int y, int w, int h, int pressed)
{
	int st = 1 << S_SZ_SH;
	HFONT f = font_for(st);
	int fh = font_height(st);
	RECT r;
	SelectObject(hdc, f);
	SetTextColor(hdc, DK(RGB(0, 0, 0), RGB(222, 222, 218)));
	r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
	switch (c->type) {
	case CT_TEXT: case CT_PASSWORD: case CT_TEXTAREA: {
		const WCHAR *v = c->value ? c->value : L"";
		int n = (int)wcslen(v);
		fill(hdc, x, y, x + w, y + h, DK(RGB(255, 255, 255), RGB(40, 42, 48)));
		frame(hdc, x, y, x + w, y + h, pressed ? DK(RGB(0, 90, 220), RGB(110, 160, 255)) : DK(RGB(130, 130, 130), RGB(110, 112, 120)));
		r.left += SC(3); r.right -= SC(3); r.top += SC(3);
		if (n == 0 && c->label) {
			SetTextColor(hdc, DK(RGB(160, 160, 160), RGB(120, 120, 125)));
			DrawTextW(hdc, c->label, -1, &r, DT_LEFT | DT_TOP | DT_NOPREFIX | (c->type == CT_TEXTAREA ? DT_WORDBREAK : DT_SINGLELINE));
		} else if (c->type == CT_PASSWORD) {
			WCHAR stars[64];
			int i;
			if (n > 63) n = 63;
			for (i = 0; i < n; i++) stars[i] = 0x25CF;
			DrawTextW(hdc, stars, n, &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
		} else {
			DrawTextW(hdc, v, n, &r, DT_LEFT | DT_TOP | DT_NOPREFIX | (c->type == CT_TEXTAREA ? DT_WORDBREAK : DT_SINGLELINE));
		}
		break;
	}
	case CT_CHECK:
		fill(hdc, x, y, x + w, y + h, DK(RGB(255, 255, 255), RGB(40, 42, 48)));
		frame(hdc, x, y, x + w, y + h, DK(RGB(90, 90, 90), RGB(150, 150, 155)));
		if (c->checked) {
			int m = w / 4 + 1;
			fill(hdc, x + m, y + m, x + w - m, y + h - m, DK(RGB(30, 30, 30), RGB(220, 220, 220)));
		}
		break;
	case CT_RADIO: {
		HPEN pen = CreatePen(PS_SOLID, 1, DK(RGB(90, 90, 90), RGB(150, 150, 155)));
		HGDIOBJ op = SelectObject(hdc, pen), ob = SelectObject(hdc, GetStockObject(WHITE_BRUSH));
		Ellipse(hdc, x, y, x + w, y + h);
		SelectObject(hdc, op);
		DeleteObject(pen);
		if (c->checked) {
			int m = w / 4 + 1;
			HBRUSH br = CreateSolidBrush(DK(RGB(0, 90, 220), RGB(110, 160, 255)));
			pen = CreatePen(PS_SOLID, 1, DK(RGB(0, 90, 220), RGB(110, 160, 255)));
			op = SelectObject(hdc, pen);
			SelectObject(hdc, br);
			Ellipse(hdc, x + m, y + m, x + w - m, y + h - m);
			SelectObject(hdc, op);
			DeleteObject(pen);
			SelectObject(hdc, ob);
			DeleteObject(br);
		} else SelectObject(hdc, ob);
		break;
	}
	case CT_SELECT: {
		const WCHAR *l = (c->nopt > 0 && c->sel < c->nopt) ? c->opt_label[c->sel] : L"";
		fill(hdc, x, y, x + w, y + h, DK(RGB(245, 245, 245), RGB(48, 50, 56)));
		frame(hdc, x, y, x + w, y + h, pressed ? DK(RGB(0, 90, 220), RGB(110, 160, 255)) : DK(RGB(130, 130, 130), RGB(110, 112, 120)));
		r.left += SC(3); r.right -= SC(18);
		DrawTextW(hdc, l, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		r.left = x + w - SC(16); r.right = x + w;
		DrawTextW(hdc, L"\x25BC", 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		break;
	}
	case CT_FILE:
		SetTextColor(hdc, DK(RGB(140, 140, 140), RGB(130, 130, 135)));
		DrawTextW(hdc, L"[\xD30C\xC77C \xCCA8\xBD80 \xBD88\xAC00]", -1, &r, DT_LEFT | DT_SINGLELINE);
		break;
	default:
		fill(hdc, x, y, x + w, y + h, pressed ? DK(RGB(170, 200, 240), RGB(70, 95, 140)) : DK(RGB(225, 225, 230), RGB(60, 62, 70)));
		frame(hdc, x, y, x + w, y + h, DK(RGB(120, 120, 130), RGB(100, 102, 112)));
		DrawTextW(hdc, c->label ? c->label : L"", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		break;
	}
	(void)fh; (void)L;
}

void layout_paint(Layout *L, HDC hdc, int scrolly, int viewh, int pressed_link, int pressed_ctl)
{
	Doc *d = L->doc;
	int i;
	HDC mdc = NULL;
	if (!d) return;
	SetBkMode(hdc, TRANSPARENT);
	for (i = first_visible(L, scrolly); i < L->nboxes; i++) {
		Box *b = &L->boxes[i];
		Item *it = &d->items[b->item];
		int y = b->y - scrolly;
		if (y > viewh) break;
		if (y + b->h < 0) continue;
		switch (it->type) {
		case IT_TEXT: {
			int st = it->style;
			COLORREF col = DK(RGB(0, 0, 0), RGB(222, 222, 218));
			if (st & S_LINK) col = DK(RGB(0, 0, 204), RGB(120, 170, 255));
			else if (st & S_GRAY) col = DK(RGB(110, 110, 110), RGB(150, 150, 150));
			if (it->link >= 0 && it->link == pressed_link) fill(hdc, b->x - 1, y - 1, b->x + b->w + 1, y + b->h + 1, DK(RGB(190, 215, 255), RGB(55, 75, 115)));
			SelectObject(hdc, font_for(st));
			SetTextColor(hdc, col);
			ExtTextOutW(hdc, b->x, y, 0, NULL, d->text.p + b->start, b->len, NULL);
			if (st & S_U) hline(hdc, b->x, b->x + b->w, y + font_ascent(st) + 1, col);
			if (st & S_STRIKE) hline(hdc, b->x, b->x + b->w, y + b->h / 2 + 1, col);
			break;
		}
		case IT_HR:
			hline(hdc, b->x, b->x + b->w, y + b->h / 2, DK(RGB(170, 170, 170), RGB(80, 82, 90)));
			break;
		case IT_BULLET: {
			WCHAR t[16];
			RECT r;
			int n;
			SelectObject(hdc, font_for(1 << S_SZ_SH));
			SetTextColor(hdc, DK(RGB(60, 60, 60), RGB(170, 170, 170)));
			r.left = b->x; r.right = b->x + b->w; r.top = y; r.bottom = y + b->h;
			if (it->aux > 0) {
				n = wsprintfW(t, L"%d.", it->aux);
				DrawTextW(hdc, t, n, &r, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOCLIP);
			} else {
				int d = b->h / 4 + 1, cx = b->x + b->w - d - SC(3), cy = y + b->h / 2;
				if (d < 3) d = 3;
				if (it->aux == -1) fill(hdc, cx, cy - d / 2, cx + d, cy - d / 2 + d, DK(RGB(70, 70, 70), RGB(170, 170, 170)));
				else frame(hdc, cx, cy - d / 2, cx + d, cy - d / 2 + d, DK(RGB(70, 70, 70), RGB(170, 170, 170)));
			}
			break;
		}
		case IT_IMG: {
			Img *im = &d->imgs[it->aux];
			if (g_images_on && im->state == 2 && (im->hbm || im->pix)) {
				HGDIOBJ old;
				if (!im->hbm) img_make_bitmap(im);
				if (!im->hbm) break;
				if (!mdc) mdc = CreateCompatibleDC(hdc);
				old = SelectObject(mdc, (HBITMAP)im->hbm);
				if (b->w == im->iw && b->h == im->ih) BitBlt(hdc, b->x, y, b->w, b->h, mdc, 0, 0, SRCCOPY);
				else StretchBlt(hdc, b->x, y, b->w, b->h, mdc, 0, 0, im->iw, im->ih, SRCCOPY);
				SelectObject(mdc, old);
				if (it->link >= 0 && it->link == pressed_link) frame(hdc, b->x, y, b->x + b->w, y + b->h, DK(RGB(0, 90, 255), RGB(110, 160, 255)));
			} else {
				RECT r;
				const WCHAR *alt = d->text.p + im->alt;
				int n = im->altlen;
				COLORREF col = it->link >= 0 ? DK(RGB(0, 0, 204), RGB(120, 170, 255)) : DK(RGB(110, 110, 110), RGB(150, 150, 150));
				if (it->link >= 0 && it->link == pressed_link) fill(hdc, b->x, y, b->x + b->w, y + b->h, DK(RGB(190, 215, 255), RGB(55, 75, 115)));
				else if (g_images_on && im->state != 3) fill(hdc, b->x, y, b->x + b->w, y + b->h, DK(RGB(235, 235, 235), RGB(45, 47, 52)));
				if (g_images_on && im->state != 3 && b->w <= SC(16)) break;
				r.left = b->x + SC(2); r.right = b->x + b->w - 1; r.top = y + 1; r.bottom = y + b->h;
				SelectObject(hdc, font_for(0 | S_GRAY));
				SetTextColor(hdc, col);
				if (n == 0) { alt = L"[\xC774\xBBF8\xC9C0]"; n = 5; }
				if (n > 40) n = 40;
				DrawTextW(hdc, alt, n, &r, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
				if (!(g_images_on && im->state != 3)) frame(hdc, b->x, y, b->x + b->w, y + b->h, DK(RGB(200, 200, 200), RGB(80, 82, 90)));
			}
			break;
		}
		case IT_CTL:
			draw_ctl(L, hdc, &d->ctls[it->aux], b->x, y, b->w, b->h, it->aux == pressed_ctl);
			break;
		}
	}
	if (mdc) DeleteDC(mdc);
}

int layout_hit(Layout *L, int x, int y, int *link, int *ctl)
{
	Doc *d = L->doc;
	int i, best_link = -1, best_ctl = -1, best_dist = 1 << 30;
	int tol = SC(6);
	*link = -1; *ctl = -1;
	if (!d) return 0;
	for (i = first_visible(L, y - SC(40)); i < L->nboxes; i++) {
		Box *b = &L->boxes[i];
		Item *it = &d->items[b->item];
		int dx, dy, dist;
		if (b->y > y + tol) break;
		if (b->w == 0) continue;
		dx = x < b->x ? b->x - x : x > b->x + b->w ? x - (b->x + b->w) : 0;
		dy = y < b->y ? b->y - y : y > b->y + b->h ? y - (b->y + b->h) : 0;
		if (dx > tol || dy > tol) continue;
		dist = dx + dy;
		if (it->type == IT_CTL) {
			Ctl *c = &d->ctls[it->aux];
			if (c->type == CT_FILE) continue;
			if (dist < best_dist || (dist == best_dist && best_ctl < 0)) { best_dist = dist; best_ctl = it->aux; best_link = -1; }
		} else if (it->link >= 0) {
			if (dist < best_dist) { best_dist = dist; best_link = it->link; best_ctl = -1; }
		}
	}
	*link = best_link; *ctl = best_ctl;
	return best_link >= 0 || best_ctl >= 0;
}

int layout_item_y(Layout *L, int item)
{
	int lo = 0, hi = L->nboxes;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (L->boxes[mid].item < item) lo = mid + 1; else hi = mid;
	}
	if (lo >= L->nboxes) return L->height;
	return L->boxes[lo].y;
}

int layout_ctl_box(Layout *L, int ctl, RECT *r)
{
	int i;
	Doc *d = L->doc;
	for (i = 0; i < L->nboxes; i++) {
		Item *it = &d->items[L->boxes[i].item];
		if (it->type == IT_CTL && it->aux == ctl) {
			r->left = L->boxes[i].x; r->top = L->boxes[i].y;
			r->right = r->left + L->boxes[i].w; r->bottom = r->top + L->boxes[i].h;
			return 1;
		}
	}
	return 0;
}
