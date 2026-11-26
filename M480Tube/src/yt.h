/* M480Tube - YouTube search / stream lookup */
#ifndef M480_YT_H
#define M480_YT_H

#include "net.h"

typedef struct {
	char id[16];
	char chid[32];   /* uploader channel id (UC...) if known */
	long pub;        /* publish time (unix), 0 unknown - subscription feed */
	int ord;         /* position as loaded (for 'default' sort) */
	char *title;     /* UTF-8 */
	char *channel;
	char *length;    /* "12:34" or NULL */
	char *meta;      /* views / date */
} YtItem;

typedef struct {
	YtItem *items;
	int n;
	char *cont;      /* continuation token for more results */
	char err[300];
} YtList;

typedef struct {
	char *title;
	long dur_ms;
	char *vurl, *aurl;
	long vsize, asize;
	int vitag, aitag, w, h;
	char ua[320];
	char client[40];
	int media_post;     /* fetch media with POST "x\0" */
	int media_rn;       /* append &rn=<request number> */
	/* alternative video formats (vurl is alt[0]); tried in order if the server refuses one */
	int nalt;
	struct { char *url; long size; int itag, w, h; } alt[8];
	int client_idx;     /* which configured client produced these URLs */
	char id[16];
	int quality;
	char err[400];
} YtStreams;

typedef struct {
	int asm_on;         /* allow ARM assembly in decoder */
	int chunk_kb;       /* HTTP range size */
	int loopfilter;     /* 0 = skip deblocking (faster) */
	char notice[400];   /* message from remote config */
} YtTune;

extern YtTune g_tune;

void yt_config_init(void);            /* built-in + cached config */
int  yt_config_update(NetCtl *ctl);   /* download latest config; 1 = updated */

int  yt_search(const char *query, const char *cont, YtList *out, NetCtl *ctl);
void yt_list_free(YtList *l);

/* channel "Videos" tab (cont = NULL for the first page); *name gets the channel title */
/* tab: 0 videos, 1 past live streams (live-now / upcoming left out) */
int  yt_channel(const char *chid, const char *cont, int tab, YtList *out, char **name, NetCtl *ctl);
/* latest ~15 uploads of a channel from its public RSS feed (with publish times) */
int  yt_rss(const char *chid, YtList *out, char **name, NetCtl *ctl);
int  yt_parse_channel(const char *body, int len, YtList *out, char **name);

/* quality: 0 = 240p, 1 = 144p, 2 = 360p */
int  yt_streams(const char *id, int quality, YtStreams *out, NetCtl *ctl);
/* same, but starting with the configured client number first_client */
int  yt_streams_from(const char *id, int quality, int first_client, YtStreams *out, NetCtl *ctl);
void yt_streams_free(YtStreams *s);

/* parsing (exposed for tests) */
int  yt_parse_search(const char *body, int len, YtList *out);
int  yt_parse_player(const char *body, int len, int quality, YtStreams *out);

#endif
