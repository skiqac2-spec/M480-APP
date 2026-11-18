/* M480Tube - tiny JSON parser */
#include <stdlib.h>
#include <string.h>
#include "json.h"

typedef struct Blk { struct Blk *next; int used, cap; char data[1]; } Blk;
struct JDoc { Blk *blk; JNode *root; const char *p, *e; int depth; };

static void *arena(JDoc *d, int n)
{
	Blk *b = d->blk;
	n = (n + 7) & ~7;
	if (!b || b->used + n > b->cap) {
		int cap = n > 60000 ? n : 60000;
		b = (Blk *)malloc(sizeof(Blk) + cap);
		if (!b) return NULL;
		b->cap = cap;
		b->used = 0;
		b->next = d->blk;
		d->blk = b;
	}
	{
		void *r = b->data + b->used;
		b->used += n;
		return r;
	}
}

static void ws(JDoc *d) { while (d->p < d->e && (*d->p == ' ' || *d->p == '\t' || *d->p == '\n' || *d->p == '\r')) d->p++; }

static int hexv(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int put_utf8(char *o, unsigned long cp)
{
	if (cp < 0x80) { o[0] = (char)cp; return 1; }
	if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 63)); return 2; }
	if (cp < 0x10000) { o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 63)); o[2] = (char)(0x80 | (cp & 63)); return 3; }
	o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 63)); o[2] = (char)(0x80 | ((cp >> 6) & 63)); o[3] = (char)(0x80 | (cp & 63));
	return 4;
}

static int rd4(const char *p, const char *e)
{
	int i, v = 0;
	if (e - p < 4) return -1;
	for (i = 0; i < 4; i++) { int h = hexv(p[i]); if (h < 0) return -1; v = v * 16 + h; }
	return v;
}

static const char *pstring(JDoc *d)
{
	const char *s, *q;
	char *out, *o;
	int esc = 0;
	if (d->p >= d->e || *d->p != '"') return NULL;
	s = ++d->p;
	for (q = s; q < d->e && *q != '"'; q++) if (*q == '\\') { esc = 1; q++; }
	if (q >= d->e) return NULL;
	out = (char *)arena(d, (int)(q - s) + 1);
	if (!out) return NULL;
	if (!esc) { memcpy(out, s, q - s); out[q - s] = 0; d->p = q + 1; return out; }
	o = out;
	for (q = s; *q != '"'; q++) {
		if (*q != '\\') { *o++ = *q; continue; }
		q++;
		switch (*q) {
		case 'n': *o++ = '\n'; break;
		case 't': *o++ = '\t'; break;
		case 'r': *o++ = '\r'; break;
		case 'b': *o++ = '\b'; break;
		case 'f': *o++ = '\f'; break;
		case 'u': {
			long cp = rd4(q + 1, d->e);
			if (cp < 0) return NULL;
			q += 4;
			if (cp >= 0xD800 && cp < 0xDC00 && q[1] == '\\' && q[2] == 'u') {
				long lo = rd4(q + 3, d->e);
				if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); q += 6; }
			}
			/* a \uXXXX escape never produces more bytes than its 6 source chars */
			o += put_utf8(o, (unsigned long)cp);
			break;
		}
		default: *o++ = *q; break;
		}
	}
	*o = 0;
	d->p = q + 1;
	return out;
}

static JNode *pvalue(JDoc *d)
{
	JNode *n;
	ws(d);
	if (d->p >= d->e) return NULL;
	if (++d->depth > 200) return NULL;
	n = (JNode *)arena(d, sizeof(JNode));
	if (!n) return NULL;
	memset(n, 0, sizeof *n);
	switch (*d->p) {
	case '{': case '[': {
		char close = *d->p == '{' ? '}' : ']';
		JNode **tail = &n->child;
		n->type = close == '}' ? J_OBJ : J_ARR;
		d->p++;
		ws(d);
		if (d->p < d->e && *d->p == close) { d->p++; break; }
		for (;;) {
			const char *key = NULL;
			JNode *c;
			ws(d);
			if (n->type == J_OBJ) {
				key = pstring(d);
				if (!key) return NULL;
				ws(d);
				if (d->p >= d->e || *d->p != ':') return NULL;
				d->p++;
			}
			c = pvalue(d);
			if (!c) return NULL;
			c->key = key;
			*tail = c;
			tail = &c->next;
			ws(d);
			if (d->p >= d->e) return NULL;
			if (*d->p == ',') { d->p++; continue; }
			if (*d->p == close) { d->p++; break; }
			return NULL;
		}
		break;
	}
	case '"':
		n->type = J_STR;
		n->str = pstring(d);
		if (!n->str) return NULL;
		break;
	case 't': n->type = J_BOOL; n->bval = 1; d->p += 4; break;
	case 'f': n->type = J_BOOL; d->p += 5; break;
	case 'n': n->type = J_NULL; d->p += 4; break;
	default: {
		const char *s = d->p;
		char *t;
		while (d->p < d->e && (strchr("+-.eE", *d->p) || (*d->p >= '0' && *d->p <= '9'))) d->p++;
		if (d->p == s) return NULL;
		t = (char *)arena(d, (int)(d->p - s) + 1);
		if (!t) return NULL;
		memcpy(t, s, d->p - s);
		t[d->p - s] = 0;
		n->type = J_NUM;
		n->str = t;
		break;
	}
	}
	d->depth--;
	return n;
}

JDoc *json_parse(const char *text, int len)
{
	JDoc *d = (JDoc *)calloc(1, sizeof(JDoc));
	if (!d) return NULL;
	d->p = text;
	d->e = text + len;
	d->root = pvalue(d);
	if (!d->root) { json_free(d); return NULL; }
	return d;
}

JNode *json_root(JDoc *d) { return d ? d->root : NULL; }

void json_free(JDoc *d)
{
	Blk *b, *n;
	if (!d) return;
	for (b = d->blk; b; b = n) { n = b->next; free(b); }
	free(d);
}

JNode *jget(JNode *o, const char *key)
{
	JNode *c;
	if (!o || o->type != J_OBJ) return NULL;
	for (c = o->child; c; c = c->next) if (c->key && !strcmp(c->key, key)) return c;
	return NULL;
}

JNode *jidx(JNode *a, int i)
{
	JNode *c;
	if (!a || (a->type != J_ARR && a->type != J_OBJ)) return NULL;
	for (c = a->child; c && i > 0; c = c->next) i--;
	return c;
}

JNode *jpath(JNode *n, const char *path)
{
	char seg[64];
	while (n && *path) {
		int l = 0;
		while (*path && *path != '.' && l < 63) seg[l++] = *path++;
		seg[l] = 0;
		if (*path == '.') path++;
		if (n->type == J_ARR && seg[0] >= '0' && seg[0] <= '9') n = jidx(n, atoi(seg));
		else n = jget(n, seg);
	}
	return n;
}

const char *jstr(JNode *n) { return (n && n->type == J_STR) ? n->str : NULL; }
const char *jpstr(JNode *n, const char *path) { return jstr(jpath(n, path)); }

long jnum(JNode *n, long def)
{
	if (!n) return def;
	if (n->type == J_NUM || n->type == J_STR) return atol(n->str);
	return def;
}

void jfind(JNode *n, const char *key, void (*fn)(JNode *, void *), void *ctx)
{
	JNode *c;
	if (!n || (n->type != J_OBJ && n->type != J_ARR)) return;
	for (c = n->child; c; c = c->next) {
		if (c->key && !strcmp(c->key, key)) fn(c, ctx);
		else jfind(c, key, fn, ctx);
	}
}
