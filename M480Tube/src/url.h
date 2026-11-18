#ifndef M480_URL_H
#define M480_URL_H

typedef struct {
	char scheme[16];
	char host[256];
	int  port;
	char *path;   /* path + query, malloc'd, starts with '/' */
} Url;

int   url_parse(const char *s, Url *u);   /* returns 1 ok */
void  url_free(Url *u);
char *url_resolve(const char *base, const char *ref); /* malloc'd absolute URL */
char *url_fixup(const char *typed);     /* user-typed text -> URL or search URL */
char *url_encode(const char *s);        /* application/x-www-form-urlencoded component */
char *url_strip_fragment(const char *s);

#endif
