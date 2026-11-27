/* M480Nico - niconico search / stream lookup (keeps the M480Tube yt_* API) */
#ifndef M480_YT_H
#define M480_YT_H

#include "net.h"

typedef struct {
	char id[16];     /* sm12345678 / so.. / nm.. */
	char chid[32];   /* uploader user id (digits) if known */
	long pub;        /* registered time (unix), 0 unknown */
	int ord;         /* position as loaded (for 'default' sort) */
	char *title;     /* UTF-8 */
	char *channel;   /* uploader name */
	char *length;    /* "12:34" or NULL */
	char *meta;      /* views / date */
	char *thumb;     /* thumbnail URL or NULL */
} YtItem;

typedef struct {
	YtItem *items;
	int n;
	char *cont;      /* next page number ("2") or NULL */
	char err[300];
} YtList;

/* one HLS media playlist (fMP4 segments, AES-128-CBC) */
typedef struct {
	char *init;            /* EXT-X-MAP URL */
	int n;                 /* segments */
	char **seg;            /* segment URLs */
	long *t0;              /* start time of each segment (ms) */
	unsigned char key[16];
	unsigned char (*iv)[16];
	int enc;               /* 1 = AES-128 */
} HlsTrack;

typedef struct {
	char *title;
	long dur_ms;
	char *vurl, *aurl;     /* unused for HLS (kept for the test hook) */
	long vsize, asize;
	int vitag, aitag, w, h;
	char ua[320];
	char client[40];
	int media_post;
	int media_rn;
	int nalt;
	struct { char *url; long size; int itag, w, h; } alt[8];
	int client_idx;
	char id[16];
	int quality;
	int hls;               /* 1 = hv / ha are used */
	HlsTrack hv, ha;
	char err[400];
} YtStreams;

typedef struct {
	int asm_on;
	int chunk_kb;
	int loopfilter;
	char notice[400];
} YtTune;

extern YtTune g_tune;

void yt_config_init(void);
int  yt_config_update(NetCtl *ctl);   /* niconico: nothing to update, returns 0 */

/* empty query = 24h ranking */
int  yt_search(const char *query, const char *cont, YtList *out, NetCtl *ctl);
void yt_list_free(YtList *l);

/* uploader videos. tab: 0 newest, 1 most viewed */
int  yt_channel(const char *chid, const char *cont, int tab, YtList *out, char **name, NetCtl *ctl);
/* newest uploads of a user (for the follow feed, with registered times) */
int  yt_rss(const char *chid, YtList *out, char **name, NetCtl *ctl);
int  yt_parse_channel(const char *body, int len, YtList *out, char **name);

/* quality: 0 = ~240p, 1 = lowest, 2 = ~360p */
int  yt_streams(const char *id, int quality, YtStreams *out, NetCtl *ctl);
int  yt_streams_from(const char *id, int quality, int first_client, YtStreams *out, NetCtl *ctl);
void yt_streams_free(YtStreams *s);
void hls_track_copy(HlsTrack *dst, const HlsTrack *src);
void hls_track_free(HlsTrack *t);
int  hls_seg_at(const HlsTrack *t, long ms);   /* segment containing ms */

/* thumbnail URL for a video id (remembered from lists, else looked up). malloc'd or NULL */
char *nico_thumb_url(const char *id, NetCtl *ctl);

/* parsing (exposed for tests) */
int  yt_parse_search(const char *body, int len, YtList *out);
int  yt_parse_player(const char *body, int len, int quality, YtStreams *out);

#endif
