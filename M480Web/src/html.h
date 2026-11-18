#ifndef M480_HTML_H
#define M480_HTML_H

#include "util.h"

/* style bits */
#define S_B      0x0001
#define S_I      0x0002
#define S_U      0x0004
#define S_MONO   0x0008
#define S_SZ_SH  4          /* 2 bits: 0 small,1 normal,2 large,3 xlarge */
#define S_SZ_MASK 0x0030
#define S_LINK   0x0040
#define S_STRIKE 0x0080
#define S_GRAY   0x0100
#define S_PRE    0x0200
#define S_SZ(s)  (((s) & S_SZ_MASK) >> S_SZ_SH)

enum { IT_TEXT, IT_BR, IT_BLOCK, IT_HR, IT_IMG, IT_CTL, IT_INDENT, IT_OUTDENT, IT_BULLET, IT_ANCHOR };

#define BLK_CENTER 0x100   /* in aux of IT_BLOCK: align center; low byte = margin (half lines) */

typedef struct {
	unsigned char type;
	unsigned short style;
	int link;       /* -1 none */
	int text, len;  /* text pool offset/length */
	int aux;
} Item;

enum { CT_TEXT, CT_PASSWORD, CT_HIDDEN, CT_CHECK, CT_RADIO, CT_SUBMIT, CT_RESET, CT_BUTTON, CT_SELECT, CT_TEXTAREA, CT_IMAGE, CT_FILE };

typedef struct {
	int type;
	int form;
	WCHAR *name;
	WCHAR *value;      /* current value (text) / value attr */
	WCHAR *label;      /* for buttons */
	int checked;
	int size, rows;
	/* select */
	int nopt, sel;
	WCHAR **opt_label, **opt_value;
} Ctl;

typedef struct {
	char *action;  /* absolute URL */
	int post;
	int multipart;
} Form;

typedef struct {
	char *url;
	int w, h;          /* attr size in css px (0 = unknown) */
	int alt;           /* text pool offset */
	int altlen;
	/* loaded state (filled by image loader) */
	volatile int state; /* 0 pending,1 loading,2 ok,3 failed */
	int iw, ih;         /* decoded/scaled pixel size */
	unsigned short *pix;/* RGB565, iw*ih */
	void *hbm;          /* HBITMAP created in UI thread */
} Img;

typedef struct {
	WCHAR *name; int item;
} Anchor;

typedef struct Doc {
	char *url;      /* document URL */
	char *base;     /* base for links */
	UINT cp;        /* code page of the document */
	WCHAR *title;
	WBuf text;
	Item *items; int nitems, capitems;
	char **links; int nlinks, caplinks;
	Img *imgs; int nimgs, capimgs;
	Ctl *ctls; int nctls, capctls;
	Form *forms; int nforms, capforms;
	Anchor *anchors; int nanchors, capanchors;
	char *refresh_url; int refresh_delay;
	volatile LONG refs;
} Doc;

Doc *html_parse(const char *bytes, int len, const char *url, const char *charset_hint, int is_text);
void doc_addref(Doc *d);
void doc_release(Doc *d);
UINT charset_to_cp(const char *cs);

#endif
