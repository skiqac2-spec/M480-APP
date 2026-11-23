/* M480Comic - find comic images and chapter links in an HTML page */
#ifndef M480C_EXTRACT_H
#define M480C_EXTRACT_H

typedef struct { char *href; char *text; } Link;

typedef struct {
	char **imgs; int nimgs, capimgs;
	Link *links; int nlinks, caplinks;
	char *title;     /* raw bytes in page charset */
	char *charset;   /* from <meta>, may be NULL */
	char *next;      /* next chapter URL, may be NULL */
	char *prev;
} PageInfo;

void extract_page(const char *html, int len, const char *base_url, PageInfo *pi);
void pageinfo_free(PageInfo *pi);

#endif
