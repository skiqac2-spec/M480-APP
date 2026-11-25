/* M480Tube - player: HTTP range ring buffers -> FFmpeg (mov demux, h264,
 * aac_fixed) -> waveOut (audio clock) + RGB565 GDI output with optional
 * 90 degree rotation for landscape viewing. */
#include <winsock2.h>
#include "util.h"
#include "net.h"
#include "player.h"
#include "tubelog.h"
#include <stdio.h>
#ifdef _WIN32_WCE
#define TXT_ELLIPSIS 0
#else
#define TXT_ELLIPSIS DT_END_ELLIPSIS
#endif
#include <mmsystem.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/cpu.h>

#ifdef _WIN32_WCE
#define GDIFLUSH()
#define THREAD_FLAGS 0x00010000 /* STACK_SIZE_PARAM_IS_A_RESERVATION */
#else
#define GDIFLUSH() GdiFlush()
#define THREAD_FLAGS 0
#endif

#define OUT_RATE 22050
#define NBUF 8
#define BUFSAMP 2048

/* ------------------------------------------------------------------ */
/* HTTP ring buffer source                                             */
/* ------------------------------------------------------------------ */
typedef struct {
	char *url;
	long size;            /* -1 unknown */
	long orig_size;
	unsigned char *ring;
	long cap, keep, chunk;
	long lo, hi, rp;      /* file offsets: buffered [lo,hi), read position rp */
	LONG gen;
	long restart_at;
	int eof, failed, starving;
	char err[200];
	char lasterr[160];
	volatile int quit;
	volatile int intr;    /* interrupt blocked reads (seek requested) */
	long downloaded;
	CRITICAL_SECTION cs;
	HANDLE ev_data, ev_space, th;
	NetCtl ctl;
	char ua[320];
	int post, rn, rangeparam;
	int mode;             /* request style being tried */
	int first_status;     /* status of the response starting at offset 0 */
	char first_ctype[64];
	unsigned char head[16];
	int headlen;
	char *oldurls[24];
	int noldurls;
	const char *name;
} Src;

/* serialises FFmpeg (de)muxer/codec setup: built without thread support */
static CRITICAL_SECTION g_ffcs;
static int g_ffcs_init;

static DWORD WINAPI src_thread(LPVOID arg)
{
	Src *s = (Src *)arg;
	HStream *hs = hs_new(&s->ctl);
	long pos = 0, req_len = 0, resp_got = 0;

	int active = 0, fails = 0;
	LONG mygen;
	if (!hs) { s->failed = 1; strcpy(s->err, "메모리 부족"); SetEvent(s->ev_data); return 0; }
	EnterCriticalSection(&s->cs);
	mygen = s->gen;
	LeaveCriticalSection(&s->cs);
	while (!s->quit) {
		long space, idx, n;
		int r;
		EnterCriticalSection(&s->cs);
		if (s->restart_at >= 0) {
			pos = s->restart_at;
			s->restart_at = -1;
			mygen = s->gen;
			active = 0;
			fails = 0;
			s->eof = s->failed = 0;
		}
		if (s->failed || (s->size >= 0 && pos >= s->size)) {
			if (!s->failed) s->eof = 1;
			LeaveCriticalSection(&s->cs);
			SetEvent(s->ev_data);
			WaitForSingleObject(s->ev_space, 500);
			continue;
		}
		/* make room: drop data older than rp - keep */
		{
			long newlo = s->rp - s->keep;
			if (newlo > s->hi) newlo = s->hi;
			if (newlo > s->lo) s->lo = newlo;
		}
		space = s->cap - (s->hi - s->lo);
		LeaveCriticalSection(&s->cs);
		if (space <= 0) { WaitForSingleObject(s->ev_space, 300); continue; }
		if (!active) {
			long end = pos + s->chunk - 1;
			int st;
			if (s->size >= 0 && end >= s->size) end = s->size - 1;
			hs_options(hs, s->post, s->rn, s->rangeparam);
			st = hs_get(hs, s->url, pos, end);
			if (s->quit) break;
			if (pos == 0) {
				EnterCriticalSection(&s->cs);
				if (s->gen == mygen) {
					s->first_status = st;
					strncpy(s->first_ctype, hs_ctype(hs), sizeof s->first_ctype - 1);
				}
				LeaveCriticalSection(&s->cs);
				tube_log("%s: mode %d first response HTTP %d %s", s->name, s->mode, st, hs_ctype(hs));
			}
			if (st == 206 || (st == 200 && (pos == 0 || s->rangeparam))) {
				if (s->size < 0) {
					long t = hs_total(hs);
					if (t > 0) { EnterCriticalSection(&s->cs); s->size = t; LeaveCriticalSection(&s->cs); }
				}
				active = 1;
				fails = 0;
				req_len = end - pos + 1;
				resp_got = 0;
			} else {
				fails++;
				tube_log("%s: HTTP %d at %ld (%s)", s->name, st, pos, hs_err(hs));
				EnterCriticalSection(&s->cs);
				_snprintf(s->lasterr, sizeof s->lasterr, "%s", hs_err(hs));
				s->lasterr[sizeof s->lasterr - 1] = 0;
				LeaveCriticalSection(&s->cs);
				if (fails >= 5 || st == 403 || st == 404 || st == 410 || (st == 200 && pos > 0) || (st >= 400 && st < 500)) {
					EnterCriticalSection(&s->cs);
					if (s->gen == mygen) {
						s->failed = 1;
						if (st == 403) strcpy(s->err, "영상 서버가 접근을 거부함 (HTTP 403)");
						else if (st) _snprintf(s->err, sizeof s->err, "영상 서버 오류 (HTTP %d)", st);
						else _snprintf(s->err, sizeof s->err, "네트워크 오류: %s", hs_err(hs));
					}
					LeaveCriticalSection(&s->cs);
					SetEvent(s->ev_data);
				} else Sleep(400 * fails);
				continue;
			}
		}
		idx = pos % s->cap;
		n = s->cap - idx;
		if (n > space) n = space;
		if (n > 32768) n = 32768;
		r = hs_read(hs, s->ring + idx, (int)n);
		if (r > 0) {
			EnterCriticalSection(&s->cs);
			if (s->gen == mygen && s->restart_at < 0) {
				if (pos < 16 && s->headlen < 16) {
					int k;
					for (k = 0; k < r && pos + k < 16; k++) s->head[pos + k] = s->ring[(idx + k) % s->cap];
					if (pos + k > s->headlen) s->headlen = (int)(pos + k);
				}
				s->hi += r;
				s->downloaded += r;
			}
			LeaveCriticalSection(&s->cs);
			pos += r;
			resp_got += r;
			SetEvent(s->ev_data);
		} else if (r == 0) {
			active = 0;
			if (resp_got < req_len && s->size < 0) {
				/* server ended early with unknown size: treat as end of file */
				EnterCriticalSection(&s->cs);
				if (s->gen == mygen) s->size = pos;
				LeaveCriticalSection(&s->cs);
			}
		} else {
			active = 0;
			fails++;
			tube_log("%s: read error at %ld", s->name, pos);
			if (fails >= 6) {
				EnterCriticalSection(&s->cs);
				if (s->gen == mygen) { s->failed = 1; strcpy(s->err, "네트워크 연결이 끊어짐"); }
				LeaveCriticalSection(&s->cs);
				SetEvent(s->ev_data);
			} else Sleep(300 * fails);
		}
	}
	hs_free(hs);
	return 0;
}

static int src_start(Src *s, const YtStreams *ys, const char *url, long size, long cap, const char *name)
{
	const char *ua = ys->ua;
	memset(s, 0, sizeof *s);
	s->post = ys->media_post;
	s->rn = ys->media_rn;
	s->rangeparam = 0;
	s->mode = 0;
	s->url = xstrdup(url);
	s->size = size > 0 ? size : -1;
	s->orig_size = s->size;
	s->cap = cap;
	s->keep = 192 * 1024;
	s->chunk = (long)g_tune.chunk_kb * 1024;
	if (s->chunk < 65536) s->chunk = 65536;
	s->restart_at = -1;
	s->name = name;
	strncpy(s->ua, ua, sizeof s->ua - 1);
	s->ctl.ua = s->ua[0] ? s->ua : NULL;
	s->ring = (unsigned char *)malloc(cap);
	if (!s->ring) return 0;
	InitializeCriticalSection(&s->cs);
	s->ev_data = CreateEventW(NULL, FALSE, FALSE, NULL);
	s->ev_space = CreateEventW(NULL, FALSE, FALSE, NULL);
	s->th = CreateThread(NULL, 0x20000, src_thread, s, THREAD_FLAGS, NULL);
	return s->th != NULL;
}

static void src_stop(Src *s)
{
	if (!s->ring) return;
	if (s->th) {
		s->quit = 1;
		s->ctl.abort = 1;
		SetEvent(s->ev_space);
		if (WaitForSingleObject(s->th, 6000) != WAIT_OBJECT_0) {
			tube_log("%s: thread did not stop", s->name);
			return; /* leak rather than free memory a live thread uses */
		}
		CloseHandle(s->th);
	}
	CloseHandle(s->ev_data);
	CloseHandle(s->ev_space);
	DeleteCriticalSection(&s->cs);
	free(s->ring);
	free(s->url);
	while (s->noldurls > 0) free(s->oldurls[--s->noldurls]);
	s->ring = NULL;
}

static int src_read(void *opaque, uint8_t *buf, int len)
{
	Src *s = (Src *)opaque;
	long n, idx;
	EnterCriticalSection(&s->cs);
	for (;;) {
		if (s->quit || s->intr) { LeaveCriticalSection(&s->cs); return AVERROR_EXIT; }
		if (s->rp < s->hi) break;
		if (s->size >= 0 && s->rp >= s->size) { LeaveCriticalSection(&s->cs); return AVERROR_EOF; }
		if (s->failed) { LeaveCriticalSection(&s->cs); return AVERROR(EIO); }
		s->starving = 1;
		LeaveCriticalSection(&s->cs);
		WaitForSingleObject(s->ev_data, 200);
		EnterCriticalSection(&s->cs);
	}
	s->starving = 0;
	n = s->hi - s->rp;
	if (n > len) n = len;
	idx = s->rp % s->cap;
	if (n > s->cap - idx) n = s->cap - idx;
	memcpy(buf, s->ring + idx, n);
	s->rp += n;
	LeaveCriticalSection(&s->cs);
	SetEvent(s->ev_space);
	return (int)n;
}

static int64_t src_seek(void *opaque, int64_t off, int whence)
{
	Src *s = (Src *)opaque;
	long target;
	if (whence & AVSEEK_SIZE) return s->size >= 0 ? s->size : AVERROR(ENOSYS);
	whence &= ~AVSEEK_FORCE;
	EnterCriticalSection(&s->cs);
	if (whence == SEEK_SET) target = (long)off;
	else if (whence == SEEK_CUR) target = s->rp + (long)off;
	else if (whence == SEEK_END && s->size >= 0) target = s->size + (long)off;
	else { LeaveCriticalSection(&s->cs); return AVERROR(EINVAL); }
	if (target < 0) target = 0;
	if (target >= s->lo && target <= s->hi && s->restart_at < 0) s->rp = target;
	else if (target > s->hi && target - s->hi < 256 * 1024 && s->restart_at < 0 && !s->failed) s->rp = target; /* short skip: keep downloading */
	else {
		s->gen++;
		s->restart_at = target;
		s->lo = s->hi = s->rp = target;
		s->eof = s->failed = 0;
	}
	LeaveCriticalSection(&s->cs);
	SetEvent(s->ev_space);
	return target;
}

/* restart the download from the start using another request style.
   returns 0 when all styles were tried */
static int src_next_mode(Src *s, int config_post)
{
	EnterCriticalSection(&s->cs);
	s->mode++;
	switch (s->mode) {
	case 1: s->post = !config_post; s->rangeparam = 0; break;  /* other method, Range header */
	case 2: s->post = 1; s->rangeparam = 1; break;             /* POST + &range= */
	case 3: s->post = 0; s->rangeparam = 1; break;             /* GET + &range= */
	default: LeaveCriticalSection(&s->cs); return 0;
	}
	s->gen++;
	s->restart_at = 0;
	s->lo = s->hi = s->rp = 0;
	s->eof = s->failed = 0;
	s->first_status = 0;
	s->first_ctype[0] = 0;
	s->headlen = 0;
	s->lasterr[0] = 0;
	s->size = s->orig_size;
	s->downloaded = 0;
	s->err[0] = 0;
	LeaveCriticalSection(&s->cs);
	SetEvent(s->ev_space);
	tube_log("%s: retry with request mode %d", s->name, s->mode);
	return 1;
}

/* point the source at another URL (other format / client) and restart from 0 */
static void src_switch_url(Src *s, const char *url, long size, const char *ua, int post, int rn)
{
	EnterCriticalSection(&s->cs);
	if (s->noldurls < 24) s->oldurls[s->noldurls++] = s->url; /* the reader may still use it */
	s->url = xstrdup(url);
	s->orig_size = s->size = size > 0 ? size : -1;
	if (ua) { strncpy(s->ua, ua, sizeof s->ua - 1); s->ua[sizeof s->ua - 1] = 0; }
	s->post = post;
	s->rn = rn;
	s->rangeparam = 0;
	s->mode = 0;
	s->gen++;
	s->restart_at = 0;
	s->lo = s->hi = s->rp = 0;
	s->eof = s->failed = 0;
	s->first_status = 0;
	s->first_ctype[0] = 0;
	s->headlen = 0;
	s->lasterr[0] = 0;
	s->err[0] = 0;
	s->downloaded = 0;
	LeaveCriticalSection(&s->cs);
	SetEvent(s->ev_space);
}

static void src_diag(Src *s, const char *fferr, char *out, int len)
{
	char hx[40];
	int i, n = s->headlen > 12 ? 12 : s->headlen;
	for (i = 0; i < n; i++) {
		unsigned char ch = s->head[i];
		hx[i] = (ch >= 32 && ch < 127) ? (char)ch : '.';
	}
	hx[n] = 0;
	_snprintf(out, len, "[%d] HTTP %d %s, %ldB '%s' %s / %s", s->mode, s->first_status, s->first_ctype,
	          s->downloaded, hx, fferr, s->failed ? s->err : s->lasterr);
	out[len - 1] = 0;
	tube_log("%s diag: %s", s->name, out);
}

static AVFormatContext *open_fmt_once(Src *s, char *fferr, int errlen)
{
	AVFormatContext *fc = avformat_alloc_context();
	unsigned char *b;
	AVIOContext *io;
	int r;
	if (!fc) return NULL;
	b = (unsigned char *)av_malloc(32768);
	io = b ? avio_alloc_context(b, 32768, 0, s, src_read, NULL, src_seek) : NULL;
	if (!io) { av_free(b); avformat_free_context(fc); return NULL; }
	fc->pb = io;
	fc->flags |= AVFMT_FLAG_CUSTOM_IO;
	r = avformat_open_input(&fc, NULL, av_find_input_format("mov"), NULL);
	if (r < 0) {
		av_strerror(r, fferr, errlen);
		tube_log("%s: open failed: %s", s->name, fferr);
		av_freep(&io->buffer);
		avio_context_free(&io);
		return NULL;
	}
	return fc;
}

/* opens the demuxer, trying other request styles if the server's answer is not usable */
static AVFormatContext *open_fmt(Src *s, int config_post, volatile int *quit, char *diag, int diaglen)
{
	char all[600] = "";
	for (;;) {
		char fferr[64] = "", d[200];
		AVFormatContext *fc = open_fmt_once(s, fferr, sizeof fferr);
		if (fc) return fc;
		if (*quit) return NULL;
		src_diag(s, fferr, d, sizeof d);
		if (strlen(all) + strlen(d) + 2 < sizeof all) { if (all[0]) strcat(all, "\n"); strcat(all, d); }
		if (s->failed && s->first_status == 0) break; /* network down: other request styles won't help */
		if (!src_next_mode(s, config_post)) break;
	}
	strncpy(diag, all, diaglen - 1);
	diag[diaglen - 1] = 0;
	return NULL;
}

static void close_fmt(AVFormatContext **fc)
{
	AVIOContext *io;
	if (!*fc) return;
	io = (*fc)->pb;
	avformat_close_input(fc);
	if (io) { av_freep(&io->buffer); avio_context_free(&io); }
}

/* ------------------------------------------------------------------ */
/* Player                                                              */
/* ------------------------------------------------------------------ */
struct Player {
	HWND hwnd;
	char *title;
	char client[40];
	long dur_ms;
	int vitag, aitag, srcw, srch;
	Src vs, as;
	int has_audio;
	HANDLE vth, ath;
	volatile int quit;
	volatile int state;
	char msg[1100];
	volatile int paused;
	volatile int bg;              /* screen off: audio only, video decoding stopped */
	DWORD idle_tick;
	volatile LONG seek_gen;
	volatile long seek_ms;
	CRITICAL_SECTION cs;          /* drawing + shared state */

	/* clock */
	HWAVEOUT hwo;
	volatile int audio_dead, audio_started, aeof_done;
	volatile long abase_ms;
	long sbase_ms;
	DWORD sbase_tick;
	int sclock_on;
	volatile long vpos_ms;        /* pts of last shown frame */
	volatile int veof;

	/* display */
	int ori;
	int SW, SH, LW, LH;
	HBITMAP sdib, ldib, sold, lold;
	unsigned short *sbits, *lbits;
	HDC sdc, ldc;
	int sstride, lstride;
	HFONT font, fontb;
	RECT vrect;                   /* video rect in logical coords */
	unsigned short *vframe;       /* last frame at vrect size */
	int vfw, vfh;
	int have_frame;
	int ovl, need_clear;
	DWORD ovl_tick;
	int drag, drag_x;
	long drag_ms;
	int show_stats;
	int fps, fcount, dropped;
	DWORD ftick;
	int frames_since_seek;
	int media_post;
	char adiag[400];
	YtStreams ys;      /* own copy: alternatives / client for video fallback */
	int fill;          /* 0 = whole picture (letterbox), 1 = fill the area (crop) */
	char toast[80];
	DWORD toast_tick;
	int alt_next;
};

#define TB 30
#define BB 74

static long clock_ms(Player *p)
{
	if (p->has_audio && !p->audio_dead) {
		MMTIME mt;
		if (!p->audio_started) return p->abase_ms;
		mt.wType = TIME_BYTES;
		if (waveOutGetPosition(p->hwo, &mt, sizeof mt) != MMSYSERR_NOERROR) return p->abase_ms;
		return p->abase_ms + (long)((long long)(mt.u.cb / 4) * 1000 / OUT_RATE);
	}
	if (!p->sclock_on) return p->sbase_ms;
	return p->sbase_ms + (long)(GetTickCount() - p->sbase_tick);
}

static void sclock_set(Player *p, long ms, int run)
{
	p->sbase_ms = ms;
	p->sbase_tick = GetTickCount();
	p->sclock_on = run;
}

static void set_error(Player *p, const char *m)
{
	EnterCriticalSection(&p->cs);
	if (p->state != PS_ERROR) {
		strncpy(p->msg, m, sizeof p->msg - 1);
		p->state = PS_ERROR;
		tube_log("error: %s", m);
	}
	LeaveCriticalSection(&p->cs);
	InvalidateRect(p->hwnd, NULL, FALSE);
}

/* ---------------- colour conversion ---------------- */
static unsigned short g_rt[1024], g_gt[1024], g_bt[1024];
static int g_yt[256], g_rv[256], g_gu[256], g_gv[256], g_bu[256];
static int g_tabs_done;
#define CLIPOFS 384

static void init_tabs(void)
{
	int i;
	if (g_tabs_done) return;
	for (i = 0; i < 1024; i++) {
		int v = i - CLIPOFS;
		if (v < 0) v = 0;
		if (v > 255) v = 255;
		g_rt[i] = (unsigned short)((v >> 3) << 11);
		g_gt[i] = (unsigned short)((v >> 2) << 5);
		g_bt[i] = (unsigned short)(v >> 3);
	}
	for (i = 0; i < 256; i++) {
		g_yt[i] = 298 * (i - 16) + 128 + (CLIPOFS << 8);
		g_rv[i] = 409 * (i - 128);
		g_gu[i] = 100 * (i - 128);
		g_gv[i] = 208 * (i - 128);
		g_bu[i] = 516 * (i - 128);
	}
	g_tabs_done = 1;
}

/* scale YUV420P frame into dst (w x h) */
/* scale YUV420P frame into dst (w x h); crop = take only the centre part with
   the destination's aspect ratio (fill mode) */
static void yuv_to_565(const AVFrame *f, unsigned short *dst, int w, int h, int crop)
{
	static int xmap[1024];
	int x, y, sw = f->width, sh = f->height, x0 = 0, y0 = 0;
	if (w <= 0 || h <= 0 || w > 1024) return;
	if (crop) {
		if ((long)sw * h > (long)sh * w) { int cw = (int)((long)sh * w / h); x0 = (sw - cw) / 2; sw = cw; }
		else { int ch = (int)((long)sw * h / w); y0 = (sh - ch) / 2; sh = ch; }
	}
	for (x = 0; x < w; x++) xmap[x] = x0 + (int)((long)x * sw / w);
	for (y = 0; y < h; y++) {
		int sy = y0 + (int)((long)y * sh / h);
		const unsigned char *py = f->data[0] + sy * f->linesize[0];
		const unsigned char *pu = f->data[1] + (sy >> 1) * f->linesize[1];
		const unsigned char *pv = f->data[2] + (sy >> 1) * f->linesize[2];
		unsigned short *o = dst + y * w;
		for (x = 0; x < w; x++) {
			int sx = xmap[x];
			int cx = sx >> 1;
			int u = pu[cx], v = pv[cx];
			int yy = g_yt[py[sx]];
			o[x] = (unsigned short)(g_rt[(yy + g_rv[v]) >> 8] | g_gt[(yy - g_gu[u] - g_gv[v]) >> 8] | g_bt[(yy + g_bu[u]) >> 8]);
		}
	}
}

/* ---------------- layout ---------------- */
/* vertical (shorts) videos and landscape mode use the whole screen with
   controls floating on top; a normal video in portrait sits above the controls */
static int floating(Player *p)
{
	return p->ori != ORI_PORTRAIT || p->srch > p->srcw;
}

static void layout(Player *p)
{
	int aw = p->srcw > 0 ? p->srcw : 16, ah = p->srch > 0 ? p->srch : 9;
	int ax, ay, AW, AH, vw, vh;
	if (floating(p)) { ax = 0; ay = 0; AW = p->LW; AH = p->LH; }
	else { ax = 0; ay = TB; AW = p->LW; AH = p->LH - TB - BB; }
	if (AW < 16) AW = 16;
	if (AH < 16) AH = 16;
	if (p->fill && floating(p)) { vw = AW; vh = AH; }
	else {
		/* largest size with the video's own aspect ratio that fits */
		vw = AW;
		vh = (int)((long)AW * ah / aw);
		if (vh > AH) { vh = AH; vw = (int)((long)AH * aw / ah); }
		if (p->fill && !floating(p)) vw = AW; /* fill the width, crop top/bottom */
	}
	if (vw > 1024) vw = 1024;
	if (floating(p)) SetRect(&p->vrect, ax + (AW - vw) / 2, ay + (AH - vh) / 2, ax + (AW - vw) / 2 + vw, ay + (AH - vh) / 2 + vh);
	else SetRect(&p->vrect, ax + (AW - vw) / 2, ay, ax + (AW - vw) / 2 + vw, ay + vh);
	vw = p->vrect.right - p->vrect.left;
	vh = p->vrect.bottom - p->vrect.top;
	if (vw != p->vfw || vh != p->vfh) {
		free(p->vframe);
		p->vframe = (unsigned short *)calloc((size_t)vw * vh, 2);
		p->vfw = vw;
		p->vfh = vh;
		p->have_frame = 0;
	}
	p->need_clear = 1;
}

static int bottom_y(Player *p)
{
	return floating(p) ? p->LH - BB : p->vrect.bottom;
}

static HBITMAP make_dib(HDC ref, int w, int h, unsigned short **bits)
{
	struct { BITMAPINFOHEADER h; DWORD m[3]; } bi;
	memset(&bi, 0, sizeof bi);
	bi.h.biSize = sizeof(BITMAPINFOHEADER);
	bi.h.biWidth = w;
	bi.h.biHeight = -h;
	bi.h.biPlanes = 1;
	bi.h.biBitCount = 16;
	bi.h.biCompression = BI_BITFIELDS;
	bi.m[0] = 0xF800; bi.m[1] = 0x07E0; bi.m[2] = 0x001F;
	return CreateDIBSection(ref, (BITMAPINFO *)&bi, DIB_RGB_COLORS, (void **)bits, NULL, 0);
}

static void free_surfaces(Player *p)
{
	if (p->sdc) { SelectObject(p->sdc, p->sold); DeleteDC(p->sdc); p->sdc = NULL; }
	if (p->sdib) { DeleteObject(p->sdib); p->sdib = NULL; }
	if (p->ldc) { SelectObject(p->ldc, p->lold); DeleteDC(p->ldc); p->ldc = NULL; }
	if (p->ldib) { DeleteObject(p->ldib); p->ldib = NULL; }
}

static void make_surfaces(Player *p)
{
	RECT r;
	HDC dc;
	free_surfaces(p);
	GetClientRect(p->hwnd, &r);
	p->SW = r.right > 0 ? r.right : 1;
	p->SH = r.bottom > 0 ? r.bottom : 1;
	dc = GetDC(p->hwnd);
	p->sdib = make_dib(dc, p->SW, p->SH, &p->sbits);
	p->sdc = CreateCompatibleDC(dc);
	p->sold = (HBITMAP)SelectObject(p->sdc, p->sdib);
	p->sstride = ((p->SW * 2 + 3) & ~3) / 2;
	if (p->ori == ORI_PORTRAIT) {
		p->LW = p->SW; p->LH = p->SH;
	} else {
		p->LW = p->SH; p->LH = p->SW;
		p->ldib = make_dib(dc, p->LW, p->LH, &p->lbits);
		p->ldc = CreateCompatibleDC(dc);
		p->lold = (HBITMAP)SelectObject(p->ldc, p->ldib);
		p->lstride = ((p->LW * 2 + 3) & ~3) / 2;
	}
	ReleaseDC(p->hwnd, dc);
	layout(p);
}

/* ---------------- overlay drawing ---------------- */
static void fill(HDC dc, int l, int t, int r, int b, COLORREF c)
{
	RECT rc;
	HBRUSH br = CreateSolidBrush(c);
	SetRect(&rc, l, t, r, b);
	FillRect(dc, &rc, br);
	DeleteObject(br);
}

static void text_u8(HDC dc, const char *s, RECT *r, UINT fmt)
{
	WCHAR *w = utf8_to_w(s, -1, NULL);
	if (!w) return;
	DrawTextW(dc, w, -1, r, fmt);
	free(w);
}

static void fmt_time(char *o, long ms)
{
	long s = ms / 1000;
	if (s < 0) s = 0;
	if (s >= 3600) sprintf(o, "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
	else sprintf(o, "%ld:%02ld", s / 60, s % 60);
}

static void draw_button(HDC dc, int l, int t, int r, int b, const char *label)
{
	RECT rc;
	fill(dc, l, t, r, b, RGB(60, 60, 60));
	SetRect(&rc, l, t, r, b);
	text_u8(dc, label, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void play_rects(Player *p, RECT *rew, RECT *play, RECT *fwd, RECT *track)
{
	int y = bottom_y(p), cx = p->LW / 2;
	SetRect(track, 8, y + 22, p->LW - 8, y + 38);
	SetRect(rew, cx - 96, y + 42, cx - 36, y + BB - 4);
	SetRect(play, cx - 30, y + 42, cx + 30, y + BB - 4);
	SetRect(fwd, cx + 36, y + 42, cx + 96, y + BB - 4);
}

static long cur_pos(Player *p)
{
	long pos = p->drag ? p->drag_ms : clock_ms(p);
	if (p->state == PS_LOADING) pos = p->seek_ms;
	if (pos < 0) pos = 0;
	if (p->dur_ms > 0 && pos > p->dur_ms) pos = p->dur_ms;
	return pos;
}

static void draw_overlay(Player *p, HDC dc)
{
	RECT rc, rew, play, fwd, track;
	char t1[24], t2[24], line[900];
	int y = bottom_y(p);
	long pos = cur_pos(p);
	const char *st = "";
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(255, 255, 255));
	SelectObject(dc, p->font);
	/* top bar */
	fill(dc, 0, 0, p->LW, TB, RGB(28, 28, 28));
	draw_button(dc, 2, 2, 58, TB - 2, "< 목록");
	draw_button(dc, p->LW - 50, 2, p->LW - 2, TB - 2, "회전");
	draw_button(dc, p->LW - 100, 2, p->LW - 54, TB - 2, p->fill ? "채움" : "비율");
	SetRect(&rc, 62, 0, p->LW - 104, TB);
	text_u8(dc, p->title, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | TXT_ELLIPSIS | DT_NOPREFIX);
	/* bottom bar */
	fill(dc, 0, y, p->LW, y + BB, RGB(28, 28, 28));
	fmt_time(t1, pos);
	fmt_time(t2, p->dur_ms);
	_snprintf(line, sizeof line, "%s / %s", t1, t2);
	SetRect(&rc, 8, y + 2, p->LW - 8, y + 20);
	text_u8(dc, line, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	switch (p->state) {
	case PS_LOADING: st = "불러오는 중..."; break;
	case PS_BUFFERING: st = "버퍼링..."; break;
	case PS_PAUSED: st = "일시정지"; break;
	case PS_ENDED: st = "끝"; break;
	case PS_ERROR: st = "오류"; break;
	}
	if (p->state == PS_PLAYING && p->show_stats) {
		_snprintf(line, sizeof line, "%dp %dfps", p->srch, p->fps);
		st = line;
	}
	SetRect(&rc, 8, y + 2, p->LW - 8, y + 20);
	text_u8(dc, st, &rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	play_rects(p, &rew, &play, &fwd, &track);
	fill(dc, track.left, track.top + 5, track.right, track.bottom - 5, RGB(90, 90, 90));
	if (p->dur_ms > 0) {
		int w = track.right - track.left;
		int bx;
		/* buffered amount (video) */
		if (p->vs.size > 0) {
			long hi = p->vs.hi;
			int bw = (int)((long long)hi * w / p->vs.size);
			if (bw > w) bw = w;
			fill(dc, track.left, track.top + 5, track.left + bw, track.bottom - 5, RGB(130, 130, 130));
		}
		bx = (int)((long long)pos * w / p->dur_ms);
		if (bx > w) bx = w;
		fill(dc, track.left, track.top + 5, track.left + bx, track.bottom - 5, RGB(230, 30, 30));
		fill(dc, track.left + bx - 4, track.top, track.left + bx + 4, track.bottom, RGB(255, 255, 255));
	}
	draw_button(dc, rew.left, rew.top, rew.right, rew.bottom, "-10초");
	draw_button(dc, fwd.left, fwd.top, fwd.right, fwd.bottom, "+10초");
	draw_button(dc, play.left, play.top, play.right, play.bottom, "");
	if (p->paused || p->state == PS_ENDED) {
		POINT tri[3];
		HBRUSH br = CreateSolidBrush(RGB(255, 255, 255)), ob;
		HPEN pen = (HPEN)GetStockObject(NULL_PEN), op;
		int cx = (play.left + play.right) / 2, cy = (play.top + play.bottom) / 2;
		tri[0].x = cx - 7; tri[0].y = cy - 9;
		tri[1].x = cx - 7; tri[1].y = cy + 9;
		tri[2].x = cx + 9; tri[2].y = cy;
		ob = (HBRUSH)SelectObject(dc, br);
		op = (HPEN)SelectObject(dc, pen);
		Polygon(dc, tri, 3);
		SelectObject(dc, ob);
		SelectObject(dc, op);
		DeleteObject(br);
	} else {
		int cx = (play.left + play.right) / 2, cy = (play.top + play.bottom) / 2;
		fill(dc, cx - 8, cy - 9, cx - 3, cy + 9, RGB(255, 255, 255));
		fill(dc, cx + 3, cy - 9, cx + 8, cy + 9, RGB(255, 255, 255));
	}
	if (p->toast[0] && GetTickCount() - p->toast_tick < 2500) {
		RECT tr;
		int cy = (TB + bottom_y(p)) / 2;
		fill(dc, p->LW / 2 - 80, cy - 12, p->LW / 2 + 80, cy + 12, RGB(40, 40, 40));
		SetRect(&tr, p->LW / 2 - 80, cy - 12, p->LW / 2 + 80, cy + 12);
		SetTextColor(dc, RGB(255, 255, 255));
		text_u8(dc, p->toast, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	/* errors are shown over the (unused) picture area, in a small font */
	if (p->state == PS_ERROR) {
		HFONT of;
		LOGFONTW lf;
		HFONT smf;
		memset(&lf, 0, sizeof lf);
		lf.lfHeight = -11;
		smf = CreateFontIndirectW(&lf);
		fill(dc, 0, TB, p->LW, y, RGB(0, 0, 0));
		SetRect(&rc, 4, TB + 2, p->LW - 4, y - 2);
		SetTextColor(dc, RGB(255, 140, 140));
		of = (HFONT)SelectObject(dc, smf);
		text_u8(dc, p->msg, &rc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
		SelectObject(dc, of);
		DeleteObject(smf);
		if (!floating(p)) fill(dc, 0, y + BB, p->LW, p->LH, RGB(0, 0, 0));
		return;
	}
	/* portrait: info area */
	if (!floating(p)) {
		int iy = y + BB + 6;
		fill(dc, 0, y + BB, p->LW, p->LH, RGB(0, 0, 0));
		SetTextColor(dc, RGB(200, 200, 200));
		SetRect(&rc, 8, iy, p->LW - 8, p->LH - 4);
		if (p->state == PS_ERROR) {
			SetTextColor(dc, RGB(255, 120, 120));
			text_u8(dc, p->msg, &rc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
		} else {
			if (p->audio_dead && p->adiag[0])
				_snprintf(line, sizeof line, "%s\n\n화질 %dp (영상 %d / 음성 %d)\n받은 데이터 %ld KB\n\n소리 오류:\n%s",
				          p->title, p->srch, p->vitag, p->aitag, (p->vs.downloaded + p->as.downloaded) / 1024, p->adiag);
			else
			_snprintf(line, sizeof line, "%s\n\n화질 %dp (영상 %d / 음성 %d)\n받은 데이터 %ld KB\n\n좌우 버튼: 10초 이동\n가운데 버튼: 재생/일시정지\n'회전'을 누르면 가로 전체화면",
			          p->title, p->srch, p->vitag, p->aitag, (p->vs.downloaded + p->as.downloaded) / 1024);
			text_u8(dc, line, &rc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
		}
	} else if (p->state == PS_ERROR) {
		SetRect(&rc, 10, TB + 10, p->LW - 10, y - 10);
		fill(dc, rc.left, rc.top, rc.right, rc.bottom, RGB(0, 0, 0));
		SetTextColor(dc, RGB(255, 120, 120));
		text_u8(dc, p->msg, &rc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
	}
}

/* must be called with p->cs held */
static void compose(Player *p)
{
	unsigned short *cv = p->ori == ORI_PORTRAIT ? p->sbits : p->lbits;
	int cs = p->ori == ORI_PORTRAIT ? p->sstride : p->lstride;
	HDC cdc = p->ori == ORI_PORTRAIT ? p->sdc : p->ldc;
	int y;
	int ovl = p->ovl || !floating(p) || p->paused || p->state != PS_PLAYING;
	if (!cv || !cdc) return;
	if (p->need_clear) {
		memset(cv, 0, (size_t)cs * 2 * p->LH);
		p->need_clear = 0;
	}
	if (p->vframe) {
		if (p->have_frame) {
			for (y = 0; y < p->vfh; y++)
				memcpy(cv + (p->vrect.top + y) * cs + p->vrect.left, p->vframe + y * p->vfw, p->vfw * 2);
		} else {
			for (y = 0; y < p->vfh; y++)
				memset(cv + (p->vrect.top + y) * cs + p->vrect.left, 0, p->vfw * 2);
		}
	}
	if (ovl) {
		draw_overlay(p, cdc);
		GDIFLUSH();
	}
	if (p->ori != ORI_PORTRAIT) {
		int sx, sy;
		for (sy = 0; sy < p->SH; sy++) {
			unsigned short *o = p->sbits + sy * p->sstride;
			if (p->ori == ORI_LAND_LEFT) {
				const unsigned short *src = p->lbits + (p->SH - 1 - sy);
				for (sx = 0; sx < p->SW; sx++) o[sx] = src[sx * p->lstride];
			} else {
				const unsigned short *src = p->lbits + sy + (p->SW - 1) * p->lstride;
				for (sx = 0; sx < p->SW; sx++) o[sx] = src[-sx * p->lstride];
			}
		}
	}
}

static void present(Player *p)
{
	HDC dc;
	EnterCriticalSection(&p->cs);
	compose(p);
	dc = GetDC(p->hwnd);
	BitBlt(dc, 0, 0, p->SW, p->SH, p->sdc, 0, 0, SRCCOPY);
	ReleaseDC(p->hwnd, dc);
	LeaveCriticalSection(&p->cs);
}

/* ---------------- audio ---------------- */
typedef struct {
	int srate, pos;            /* pos: 16.16 phase */
	short pl, pr;              /* previous sample */
	int hxl, hxr, hyl, hyr;    /* high-pass filter state */
} Resamp;

/* The M480 earphone amp distorts with loud, bass-heavy material: cut below
   ~90 Hz and play at 60 % level, like M480Music does. */
#define HPA 31939   /* exp(-2*pi*90/22050) in Q15 */
#define GAIN 19661  /* 0.6 in Q15 */
static short shape(int x, int *hx, int *hy)
{
	int y = (int)(((long long)HPA * (*hy + x - *hx)) >> 15);
	*hx = x;
	*hy = y;
	y = (y * GAIN) >> 15;
	if (y > 32767) y = 32767;
	if (y < -32768) y = -32768;
	return (short)y;
}

static int find_stream(AVFormatContext *fc, enum AVMediaType t)
{
	unsigned i;
	int idx = -1;
	for (i = 0; i < fc->nb_streams; i++) {
		if (idx < 0 && fc->streams[i]->codecpar->codec_type == t) idx = (int)i;
		else fc->streams[i]->discard = AVDISCARD_ALL;
	}
	return idx;
}

static DWORD WINAPI audio_thread(LPVOID arg)
{
	Player *p = (Player *)arg;
	AVFormatContext *fc = NULL;
	AVCodecContext *cc = NULL;
	AVPacket *pkt = NULL;
	AVFrame *fr = NULL;
	const AVCodec *dec;
	WAVEHDR hdr[NBUF];
	short *mem = NULL;
	int busy[NBUF];
	int cur = 0, fill_n = 0, queued = 0, started = 0, eof = 0, paused_applied = 0;
	int aidx, i;
	LONG gen;
	long drop_before = 0;
	HANDLE ev = NULL;
	Resamp rs;
	AVRational tb;
	WAVEFORMATEX wf;

	memset(hdr, 0, sizeof hdr);
	memset(busy, 0, sizeof busy);
	memset(&rs, 0, sizeof rs);
	fc = open_fmt(&p->as, p->media_post, &p->quit, p->adiag, sizeof p->adiag);
	if (!fc) goto dead;
	aidx = find_stream(fc, AVMEDIA_TYPE_AUDIO);
	if (aidx < 0) goto dead;
	tb = fc->streams[aidx]->time_base;
	dec = avcodec_find_decoder_by_name("aac_fixed");
	if (!dec) dec = avcodec_find_decoder(fc->streams[aidx]->codecpar->codec_id);
	if (!dec) goto dead;
	cc = avcodec_alloc_context3(dec);
	if (!cc || avcodec_parameters_to_context(cc, fc->streams[aidx]->codecpar) < 0) goto dead;
	cc->thread_count = 1;
	EnterCriticalSection(&g_ffcs);
	i = avcodec_open2(cc, dec, NULL);
	LeaveCriticalSection(&g_ffcs);
	if (i < 0) { strcpy(p->adiag, "음성 디코더를 열 수 없음"); goto dead; }
	pkt = av_packet_alloc();
	fr = av_frame_alloc();
	mem = (short *)malloc(NBUF * BUFSAMP * 4);
	ev = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (!pkt || !fr || !mem || !ev) goto dead;
	memset(&wf, 0, sizeof wf);
	wf.wFormatTag = WAVE_FORMAT_PCM;
	wf.nChannels = 2;
	wf.nSamplesPerSec = OUT_RATE;
	wf.wBitsPerSample = 16;
	wf.nBlockAlign = 4;
	wf.nAvgBytesPerSec = OUT_RATE * 4;
	if (waveOutOpen(&p->hwo, WAVE_MAPPER, &wf, (DWORD)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
		tube_log("waveOutOpen failed");
		p->hwo = NULL;
		goto dead;
	}
	for (i = 0; i < NBUF; i++) {
		hdr[i].lpData = (LPSTR)(mem + i * BUFSAMP * 2);
		hdr[i].dwBufferLength = BUFSAMP * 4;
		waveOutPrepareHeader(p->hwo, &hdr[i], sizeof(WAVEHDR));
	}
	waveOutPause(p->hwo);
	gen = -1;
	while (!p->quit) {
		int r;
		if (p->seek_gen != gen) {
			long target;
			gen = p->seek_gen;
			target = p->seek_ms;
			p->as.intr = 0;
			waveOutReset(p->hwo);
			waveOutPause(p->hwo);
			for (i = 0; i < NBUF; i++) busy[i] = 0;
			cur = fill_n = queued = started = eof = 0;
			p->audio_started = 0;
			p->aeof_done = 0;
			p->abase_ms = target;
			drop_before = target;
			memset(&rs, 0, sizeof rs);
			if (target > 0 || p->seek_gen > 1) {
				av_seek_frame(fc, aidx, av_rescale_q(target, (AVRational){1, 1000}, tb), AVSEEK_FLAG_BACKWARD);
				fc->pb->error = 0;
				fc->pb->eof_reached = 0;
			}
			avcodec_flush_buffers(cc);
			paused_applied = 0;
			continue;
		}
		if (p->paused) {
			if (!paused_applied) { waveOutPause(p->hwo); paused_applied = 1; }
			Sleep(40);
			continue;
		} else if (paused_applied) {
			if (started) waveOutRestart(p->hwo);
			paused_applied = 0;
		}
		if (eof) {
			int pending = 0;
			if (fill_n > 0 && !busy[cur]) {
				hdr[cur].dwBufferLength = fill_n * 4;
				waveOutWrite(p->hwo, &hdr[cur], sizeof(WAVEHDR));
				busy[cur] = 1;
				cur = (cur + 1) % NBUF;
				fill_n = 0;
			}
			if (!started) { waveOutRestart(p->hwo); started = 1; p->audio_started = 1; }
			for (i = 0; i < NBUF; i++) if (busy[i] && !(hdr[i].dwFlags & WHDR_DONE)) pending = 1;
			if (!pending) p->aeof_done = 1;
			WaitForSingleObject(ev, 100);
			continue;
		}
		r = av_read_frame(fc, pkt);
		if (r < 0) {
			if (r == AVERROR_EXIT) { Sleep(10); continue; }
			if (r == AVERROR_EOF || avio_feof(fc->pb)) { eof = 1; continue; }
			if (p->as.failed) {
				tube_log("audio source failed: %s", p->as.err);
				p->audio_dead = 1;
				sclock_set(p, clock_ms(p), 1);
				break;
			}
			Sleep(50);
			continue;
		}
		if (pkt->stream_index != aidx) { av_packet_unref(pkt); continue; }
		if (avcodec_send_packet(cc, pkt) < 0) { av_packet_unref(pkt); continue; }
		av_packet_unref(pkt);
		while (!p->quit && p->seek_gen == gen && avcodec_receive_frame(cc, fr) == 0) {
			int64_t ts = fr->best_effort_timestamp;
			long fms = ts == AV_NOPTS_VALUE ? -1 : (long)av_rescale_q(ts, tb, (AVRational){1, 1000});
			int ns = fr->nb_samples, ch = fr->channels, k;
			long fdur = fr->sample_rate > 0 ? ns * 1000L / fr->sample_rate : 0;
			const int32_t *L, *R;
			int step, ph;
			if (fms >= 0 && fms + fdur <= drop_before) { av_frame_unref(fr); continue; }
			if (!started && queued == 0 && fill_n == 0 && fms >= 0) p->abase_ms = fms > drop_before ? fms : drop_before;
			if (fr->format != AV_SAMPLE_FMT_S32P && fr->format != AV_SAMPLE_FMT_FLTP && fr->format != AV_SAMPLE_FMT_S16P) { av_frame_unref(fr); continue; }
			L = (const int32_t *)fr->extended_data[0];
			R = (const int32_t *)fr->extended_data[ch > 1 ? 1 : 0];
			if (rs.srate != fr->sample_rate) { rs.srate = fr->sample_rate; rs.pos = 0; }
			if (rs.srate <= 0) { av_frame_unref(fr); continue; }
			step = (int)(((long long)rs.srate << 16) / OUT_RATE);
			ph = rs.pos;
			/* sample k: k=0 is previous frame's last sample, k=1..ns this frame */
			while ((ph >> 16) < ns) {
				int k0 = ph >> 16, f = ph & 0xFFFF;
				int l0, r0, l1, r1, lo, ro;
				short *o;
				if (fr->format == AV_SAMPLE_FMT_S32P) {
					l1 = L[k0] >> 16; r1 = R[k0] >> 16;
				} else if (fr->format == AV_SAMPLE_FMT_S16P) {
					l1 = ((const short *)L)[k0]; r1 = ((const short *)R)[k0];
				} else {
					float lf = ((const float *)L)[k0] * 32767.0f, rf = ((const float *)R)[k0] * 32767.0f;
					l1 = (int)(lf > 32767 ? 32767 : lf < -32768 ? -32768 : lf);
					r1 = (int)(rf > 32767 ? 32767 : rf < -32768 ? -32768 : rf);
				}
				if (k0 == 0) { l0 = rs.pl; r0 = rs.pr; }
				else if (fr->format == AV_SAMPLE_FMT_S32P) { l0 = L[k0 - 1] >> 16; r0 = R[k0 - 1] >> 16; }
				else if (fr->format == AV_SAMPLE_FMT_S16P) { l0 = ((const short *)L)[k0 - 1]; r0 = ((const short *)R)[k0 - 1]; }
				else { l0 = l1; r0 = r1; }
				if (step >= 0x18000) {
					/* downsampling by ~2: average for a little low-pass */
					lo = (l0 + l1) >> 1; ro = (r0 + r1) >> 1;
				} else {
					lo = l0 + (((l1 - l0) * f) >> 16);
					ro = r0 + (((r1 - r0) * f) >> 16);
				}
				/* wait for a free buffer */
				while (busy[cur] && !(hdr[cur].dwFlags & WHDR_DONE)) {
					if (p->quit || p->seek_gen != gen || p->paused) break;
					WaitForSingleObject(ev, 100);
				}
				if (p->quit || p->seek_gen != gen) break;
				if (p->paused && busy[cur] && !(hdr[cur].dwFlags & WHDR_DONE)) { Sleep(30); continue; }
				busy[cur] = 0;
				o = (short *)hdr[cur].lpData + fill_n * 2;
				o[0] = shape(lo, &rs.hxl, &rs.hyl);
				o[1] = shape(ro, &rs.hxr, &rs.hyr);
				if (++fill_n == BUFSAMP) {
					hdr[cur].dwBufferLength = BUFSAMP * 4;
					waveOutWrite(p->hwo, &hdr[cur], sizeof(WAVEHDR));
					busy[cur] = 1;
					cur = (cur + 1) % NBUF;
					fill_n = 0;
					queued++;
					if (!started && queued >= 3 && !p->paused) {
						waveOutRestart(p->hwo);
						started = 1;
						p->audio_started = 1;
					}
				}
				ph += step;
			}
			/* remember last sample, rebase phase */
			k = ns - 1;
			if (fr->format == AV_SAMPLE_FMT_S32P) { rs.pl = (short)(L[k] >> 16); rs.pr = (short)(R[k] >> 16); }
			else if (fr->format == AV_SAMPLE_FMT_S16P) { rs.pl = ((const short *)L)[k]; rs.pr = ((const short *)R)[k]; }
			rs.pos = ph - (ns << 16);
			if (rs.pos < 0) rs.pos = 0;
			av_frame_unref(fr);
		}
	}
	goto cleanup;
dead:
	tube_log("audio disabled");
	p->audio_dead = 1;
	sclock_set(p, p->abase_ms, !p->paused);
cleanup:
	if (p->hwo) {
		waveOutReset(p->hwo);
		for (i = 0; i < NBUF; i++) if (hdr[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(p->hwo, &hdr[i], sizeof(WAVEHDR));
		waveOutClose(p->hwo);
		p->hwo = NULL;
	}
	free(mem);
	if (ev) CloseHandle(ev);
	av_frame_free(&fr);
	av_packet_free(&pkt);
	avcodec_free_context(&cc);
	close_fmt(&fc);
	return 0;
}

/* ---------------- video ---------------- */
/* ---------------- screen off = keep playing the sound only ----------------
   'unattended mode' keeps the CPU and the audio chip running when the power button
   turns the screen off (same as M480Music); a power-notification queue tells us when
   the screen is off so the video decoder can rest */
static volatile int g_screen_off;
#ifdef _WIN32_WCE
typedef BOOL (WINAPI *PFN_PPN)(DWORD, DWORD);
typedef HANDLE (WINAPI *PFN_SPR)(PVOID, int, ULONG, PVOID, ULONG);
typedef DWORD (WINAPI *PFN_RPR)(HANDLE);
typedef HANDLE (WINAPI *PFN_CMQ)(LPCWSTR, void *);
typedef BOOL (WINAPI *PFN_RMQ)(HANDLE, LPVOID, DWORD, LPDWORD, DWORD, DWORD *);
typedef HANDLE (WINAPI *PFN_RPN)(HANDLE, DWORD);
static PFN_PPN pPPN;
static PFN_SPR pSPR;
static PFN_RPR pRPR;
static HANDLE g_pwreq;
static int g_unatt, g_pw_init;

typedef struct { DWORD size, flags, maxmsgs, maxmsg; BOOL read; } MQOpts;
typedef struct { DWORD Message, Flags, Length; WCHAR name[64]; } PwMsg;

static DWORD WINAPI power_watch(LPVOID arg)
{
	HMODULE h = (HMODULE)arg;
	PFN_CMQ cmq = (PFN_CMQ)GetProcAddressW(h, L"CreateMsgQueue");
	PFN_RMQ rmq = (PFN_RMQ)GetProcAddressW(h, L"ReadMsgQueue");
	PFN_RPN rpn = (PFN_RPN)GetProcAddressW(h, L"RequestPowerNotifications");
	MQOpts o;
	HANDLE q;
	if (!cmq || !rmq || !rpn) return 0;
	memset(&o, 0, sizeof o);
	o.size = sizeof o;
	o.flags = 1;                 /* MSGQUEUE_NOPRECOMMIT */
	o.maxmsg = sizeof(PwMsg);
	o.read = TRUE;
	q = cmq(NULL, &o);
	if (!q) return 0;
	if (!rpn(q, 0x1 /* PBT_TRANSITION */)) return 0;
	for (;;) {
		PwMsg m;
		DWORD got = 0, fl = 0;
		if (WaitForSingleObject(q, INFINITE) != WAIT_OBJECT_0) break;
		memset(&m, 0, sizeof m);
		if (!rmq(q, &m, sizeof m, &got, 0, &fl)) continue;
		if (m.Message != 0x1) continue;
		/* ON = screen on; UNATTENDED / IDLE / SUSPEND / OFF = screen off */
		g_screen_off = (m.Flags & (0x00400000 | 0x00100000 | 0x00200000 | 0x00020000)) && !(m.Flags & 0x00010000);
		tube_log("power: flags=%08lx -> screen %s", (unsigned long)m.Flags, g_screen_off ? "off" : "on");
	}
	return 0;
}

static void power_keep(int on)
{
	if (!g_pw_init) {
		HMODULE h = LoadLibraryW(L"coredll.dll");
		g_pw_init = 1;
		if (h) {
			HANDLE th;
			pPPN = (PFN_PPN)GetProcAddressW(h, L"PowerPolicyNotify");
			pSPR = (PFN_SPR)GetProcAddressW(h, L"SetPowerRequirement");
			pRPR = (PFN_RPR)GetProcAddressW(h, L"ReleasePowerRequirement");
			th = CreateThread(NULL, 0x4000, power_watch, h, 0, NULL);
			if (th) CloseHandle(th);
		}
	}
	if (on && !g_unatt) {
		if (pPPN) pPPN(0x0003 /* PPN_UNATTENDEDMODE */, TRUE);
		if (pSPR && !g_pwreq) g_pwreq = pSPR((PVOID)L"wav1:", 0 /* D0 */, 0x1 | 0x1000 /* POWER_NAME|POWER_FORCE */, NULL, 0);
		g_unatt = 1;
	} else if (!on && g_unatt) {
		if (pPPN) pPPN(0x0003, FALSE);
		if (pRPR && g_pwreq) pRPR(g_pwreq);
		g_pwreq = NULL;
		g_unatt = 0;
	}
}
#else
static void power_keep(int on) { (void)on; }
#endif

static DWORD WINAPI video_thread(LPVOID arg)
{
	Player *p = (Player *)arg;
	AVFormatContext *fc = NULL;
	AVCodecContext *cc = NULL;
	AVPacket *pkt = NULL;
	AVFrame *fr = NULL;
	const AVCodec *dec;
	int vidx;
	LONG gen = -1;
	long drop_before = 0;
	AVRational tb;
	int keyskip = 0;
	int r0;

	{
		char diag[600], msg[1100], all[900] = "";
		int vpost = p->media_post;
		for (;;) {
			char line[120];
			int refused;
			fc = open_fmt(&p->vs, vpost, &p->quit, diag, sizeof diag);
			if (fc || p->quit) break;
			refused = p->vs.first_status >= 400;
			_snprintf(line, sizeof line, "%d: %s", p->vitag, diag);
			line[sizeof line - 1] = 0;
			{ char *nl = strchr(line, '\n'); if (nl) *nl = 0; }
			if (strlen(all) + strlen(line) + 2 < sizeof all) { if (all[0]) strcat(all, "\n"); strcat(all, line); }
			if (!refused) break;
			/* the server refused this format: try the next one, then another client */
			if (p->alt_next < p->ys.nalt) {
				int k = p->alt_next++;
				tube_log("video: itag %d refused, trying itag %d", p->vitag, p->ys.alt[k].itag);
				p->vitag = p->ys.alt[k].itag;
				if (p->ys.alt[k].w > 0) { EnterCriticalSection(&p->cs); p->srcw = p->ys.alt[k].w; p->srch = p->ys.alt[k].h; layout(p); LeaveCriticalSection(&p->cs); }
				src_switch_url(&p->vs, p->ys.alt[k].url, p->ys.alt[k].size, NULL, p->ys.media_post, p->ys.media_rn);
				continue;
			} else {
				YtStreams ns;
				NetCtl ctl;
				memset(&ctl, 0, sizeof ctl);
				memset(&ns, 0, sizeof ns);
				if (!p->ys.id[0] || !yt_streams_from(p->ys.id, p->ys.quality, p->ys.client_idx + 1, &ns, &ctl)) {
					yt_streams_free(&ns);
					break;
				}
				tube_log("video: switching to client %s", ns.client);
				yt_streams_free(&p->ys);
				p->ys = ns;
				p->alt_next = 1;
				vpost = ns.media_post;
				p->vitag = ns.vitag;
				strncpy(p->client, ns.client, sizeof p->client - 1);
				src_switch_url(&p->vs, ns.vurl, ns.vsize, ns.ua, ns.media_post, ns.media_rn);
			}
		}
		if (!fc) {
			char st[260];
			if (p->quit) return 0;
			player_selftest(st, sizeof st);
			_snprintf(msg, sizeof msg, "영상을 열 수 없습니다. 이 화면을 사진으로 보내 주세요.\n%s\n%s", all, st);
			msg[sizeof msg - 1] = 0;
			set_error(p, msg);
			return 0;
		}
	}
	vidx = find_stream(fc, AVMEDIA_TYPE_VIDEO);
	if (vidx < 0) { set_error(p, "영상 트랙이 없습니다"); goto out; }
	tb = fc->streams[vidx]->time_base;
	dec = avcodec_find_decoder(fc->streams[vidx]->codecpar->codec_id);
	if (!dec) { set_error(p, "지원하지 않는 영상 코덱입니다"); goto out; }
	cc = avcodec_alloc_context3(dec);
	if (!cc || avcodec_parameters_to_context(cc, fc->streams[vidx]->codecpar) < 0) { set_error(p, "디코더 초기화 실패"); goto out; }
	cc->thread_count = 1;
	cc->flags2 |= AV_CODEC_FLAG2_FAST;
	if (!g_tune.loopfilter) cc->skip_loop_filter = AVDISCARD_ALL;
	EnterCriticalSection(&g_ffcs);
	r0 = avcodec_open2(cc, dec, NULL);
	LeaveCriticalSection(&g_ffcs);
	if (r0 < 0) { set_error(p, "디코더를 열 수 없습니다"); goto out; }
	if (fc->streams[vidx]->codecpar->width > 0) {
		EnterCriticalSection(&p->cs);
		AVCodecParameters *cp = fc->streams[vidx]->codecpar;
		int dw = cp->width, dh = cp->height;
		/* non-square pixels: use the display shape */
		if (cp->sample_aspect_ratio.num > 0 && cp->sample_aspect_ratio.den > 0 && cp->sample_aspect_ratio.num != cp->sample_aspect_ratio.den)
			dw = (int)((long long)dw * cp->sample_aspect_ratio.num / cp->sample_aspect_ratio.den);
		if (p->srcw != dw || p->srch != dh) {
			p->srcw = dw;
			p->srch = dh;
			layout(p);
		}
		LeaveCriticalSection(&p->cs);
	}
	pkt = av_packet_alloc();
	fr = av_frame_alloc();
	if (!pkt || !fr) { set_error(p, "메모리 부족"); goto out; }
	tube_log("video %dx%d opened", cc->width, cc->height);

	while (!p->quit) {
		int r;
		if (p->bg && p->seek_gen == gen && gen >= 0) {
			/* screen off: stop downloading/decoding video, catch up when it comes back */
			int was = 0;
			while (p->bg && !p->quit && p->seek_gen == gen) { was = 1; Sleep(150); }
			if (was && !p->quit && p->seek_gen == gen && !p->veof) {
				long target = clock_ms(p);
				tube_log("video: screen back on, resync to %ld", target);
				p->vs.intr = 0;
				drop_before = target;
				keyskip = 0;
				cc->skip_frame = AVDISCARD_DEFAULT;
				if (av_seek_frame(fc, vidx, av_rescale_q(target, (AVRational){1, 1000}, tb), AVSEEK_FLAG_BACKWARD) < 0)
					tube_log("video resync seek failed");
				fc->pb->error = 0;
				fc->pb->eof_reached = 0;
				avcodec_flush_buffers(cc);
				p->need_clear = 1;
			}
			continue;
		}
		if (p->seek_gen != gen) {
			long target;
			gen = p->seek_gen;
			target = p->seek_ms;
			p->vs.intr = 0;
			drop_before = target;
			p->veof = 0;
			p->frames_since_seek = 0;
			keyskip = 0;
			cc->skip_frame = AVDISCARD_DEFAULT;
			if (target > 0 || gen > 1) {
				if (av_seek_frame(fc, vidx, av_rescale_q(target, (AVRational){1, 1000}, tb), AVSEEK_FLAG_BACKWARD) < 0)
					tube_log("video seek failed");
				fc->pb->error = 0;
				fc->pb->eof_reached = 0;
			}
			avcodec_flush_buffers(cc);
			if (p->audio_dead || !p->has_audio) sclock_set(p, target, 0);
			continue;
		}
		if (p->veof) {
			if (p->state == PS_PLAYING || p->state == PS_BUFFERING) {
				int ended = (p->has_audio && !p->audio_dead) ? p->aeof_done : clock_ms(p) >= p->vpos_ms + 200;
				if (ended) { p->state = PS_ENDED; p->paused = 1; InvalidateRect(p->hwnd, NULL, FALSE); }
			}
			Sleep(60);
			continue;
		}
		r = av_read_frame(fc, pkt);
		if (r < 0) {
			if (r == AVERROR_EXIT) { Sleep(10); continue; }
			if (r == AVERROR_EOF || avio_feof(fc->pb)) {
				/* drain decoder */
				avcodec_send_packet(cc, NULL);
				while (avcodec_receive_frame(cc, fr) == 0) av_frame_unref(fr);
				p->veof = 1;
				continue;
			}
			if (p->vs.failed) { set_error(p, p->vs.err); break; }
			Sleep(50);
			continue;
		}
		if (pkt->stream_index != vidx) { av_packet_unref(pkt); continue; }
		if (keyskip && !(pkt->flags & AV_PKT_FLAG_KEY)) { av_packet_unref(pkt); continue; }
		keyskip = 0;
		r = avcodec_send_packet(cc, pkt);
		av_packet_unref(pkt);
		if (r < 0 && r != AVERROR(EAGAIN)) continue;
		while (!p->quit && p->seek_gen == gen && avcodec_receive_frame(cc, fr) == 0) {
			int64_t ts = fr->best_effort_timestamp;
			long pts = ts == AV_NOPTS_VALUE ? p->vpos_ms + 40 : (long)av_rescale_q(ts, tb, (AVRational){1, 1000});
			long late = 0;
			int show = 1;
			if (pts < drop_before - 30) { av_frame_unref(fr); continue; }
			if (p->frames_since_seek > 0) {
				/* wait until it is time for this frame */
				for (;;) {
					long clk, diff;
					if (p->quit || p->seek_gen != gen) break;
					if (p->paused) { Sleep(30); continue; }
					clk = clock_ms(p);
					diff = pts - clk;
					if (diff <= 12) { late = -diff; break; }
					/* no audio clock movement (buffering): just wait */
					Sleep(diff > 40 ? 30 : (DWORD)(diff - 8));
				}
				if (p->quit || p->seek_gen != gen) { av_frame_unref(fr); break; }
				if (late > 150) { show = 0; p->dropped++; }
				if (late > 3000) { keyskip = 1; cc->skip_frame = AVDISCARD_NONKEY; }
				else if (late > 500) cc->skip_frame = AVDISCARD_NONREF;
				else if (late < 120) cc->skip_frame = AVDISCARD_DEFAULT;
				if (late > 200) cc->skip_loop_filter = AVDISCARD_ALL;
				else if (late < 60 && g_tune.loopfilter) cc->skip_loop_filter = AVDISCARD_DEFAULT;
			} else {
				/* first frame after a seek: show at once and start clocks */
				if (p->audio_dead || !p->has_audio) sclock_set(p, pts, !p->paused);
				if (p->state == PS_LOADING) p->state = p->paused ? PS_PAUSED : PS_PLAYING;
			}
			p->frames_since_seek++;
			if (show && (fr->format == AV_PIX_FMT_YUV420P || fr->format == AV_PIX_FMT_YUVJ420P)) {
				EnterCriticalSection(&p->cs);
				if (p->vframe) {
					yuv_to_565(fr, p->vframe, p->vfw, p->vfh, p->fill);
					p->have_frame = 1;
				}
				LeaveCriticalSection(&p->cs);
				p->vpos_ms = pts;
				present(p);
				p->fcount++;
			}
			av_frame_unref(fr);
		}
	}
out:
	av_frame_free(&fr);
	av_packet_free(&pkt);
	avcodec_free_context(&cc);
	close_fmt(&fc);
	return 0;
}

/* ---------------- engine self-test (embedded tiny DASH files) ---------------- */
#include "selftest_data.h"
typedef struct { const unsigned char *d; int len, pos; } MemIO;
static int mem_read(void *o, uint8_t *buf, int n)
{
	MemIO *m = (MemIO *)o;
	if (m->pos >= m->len) return AVERROR_EOF;
	if (n > m->len - m->pos) n = m->len - m->pos;
	memcpy(buf, m->d + m->pos, n);
	m->pos += n;
	return n;
}
static int64_t mem_seek(void *o, int64_t off, int whence)
{
	MemIO *m = (MemIO *)o;
	if (whence & AVSEEK_SIZE) return m->len;
	whence &= ~AVSEEK_FORCE;
	if (whence == SEEK_CUR) off += m->pos;
	else if (whence == SEEK_END) off += m->len;
	if (off < 0 || off > m->len) return AVERROR(EINVAL);
	m->pos = (int)off;
	return off;
}

static void selftest_one(const unsigned char *d, int len, enum AVMediaType t, char *out, int outlen)
{
	MemIO m;
	AVFormatContext *fc = avformat_alloc_context();
	unsigned char *b = (unsigned char *)av_malloc(4096);
	AVIOContext *io;
	AVCodecContext *cc = NULL;
	const AVCodec *dec;
	AVPacket *pkt = NULL;
	AVFrame *fr = NULL;
	int r, idx = -1, frames = 0, pkts = 0;
	unsigned i;
	char e[64] = "";
	m.d = d; m.len = len; m.pos = 0;
	io = avio_alloc_context(b, 4096, 0, &m, mem_read, NULL, mem_seek);
	fc->pb = io;
	fc->flags |= AVFMT_FLAG_CUSTOM_IO;
	r = avformat_open_input(&fc, NULL, av_find_input_format("mov"), NULL);
	if (r < 0) { av_strerror(r, e, sizeof e); _snprintf(out, outlen, "열기 실패(%s)", e); fc = NULL; goto done; }
	for (i = 0; i < fc->nb_streams; i++) if (fc->streams[i]->codecpar->codec_type == t) { idx = (int)i; break; }
	if (idx < 0) { _snprintf(out, outlen, "트랙 없음"); goto done; }
	dec = t == AVMEDIA_TYPE_AUDIO ? avcodec_find_decoder_by_name("aac_fixed") : avcodec_find_decoder(fc->streams[idx]->codecpar->codec_id);
	if (!dec) { _snprintf(out, outlen, "디코더 없음"); goto done; }
	cc = avcodec_alloc_context3(dec);
	avcodec_parameters_to_context(cc, fc->streams[idx]->codecpar);
	cc->thread_count = 1;
	EnterCriticalSection(&g_ffcs);
	r = avcodec_open2(cc, dec, NULL);
	LeaveCriticalSection(&g_ffcs);
	if (r < 0) { av_strerror(r, e, sizeof e); _snprintf(out, outlen, "디코더 열기 실패(%s)", e); goto done; }
	pkt = av_packet_alloc();
	fr = av_frame_alloc();
	while (av_read_frame(fc, pkt) >= 0) {
		if (pkt->stream_index == idx) {
			pkts++;
			if (avcodec_send_packet(cc, pkt) >= 0)
				while (avcodec_receive_frame(cc, fr) == 0) { frames++; av_frame_unref(fr); }
		}
		av_packet_unref(pkt);
	}
	avcodec_send_packet(cc, NULL);
	while (avcodec_receive_frame(cc, fr) == 0) { frames++; av_frame_unref(fr); }
	_snprintf(out, outlen, "%s (패킷 %d, 프레임 %d)", frames > 0 ? "정상" : "디코딩 실패", pkts, frames);
done:
	out[outlen - 1] = 0;
	av_frame_free(&fr);
	av_packet_free(&pkt);
	avcodec_free_context(&cc);
	if (fc) avformat_close_input(&fc);
	av_freep(&io->buffer);
	avio_context_free(&io);
}

void player_selftest(char *out, int len)
{
	char v[120], a[120];
	if (!g_ffcs_init) { InitializeCriticalSection(&g_ffcs); g_ffcs_init = 1; }
	av_force_cpu_flags(g_tune.asm_on ? -1 : 0);
	selftest_one(g_test_v, sizeof g_test_v, AVMEDIA_TYPE_VIDEO, v, sizeof v);
	selftest_one(g_test_a, sizeof g_test_a, AVMEDIA_TYPE_AUDIO, a, sizeof a);
	_snprintf(out, len, "엔진 자가진단 - 영상: %s / 소리: %s", v, a);
	out[len - 1] = 0;
	tube_log("%s", out);
}

/* ---------------- public ---------------- */
static void log_cb(void *avcl, int level, const char *fmt, va_list vl)
{
	char line[300];
	(void)avcl;
	if (level > AV_LOG_ERROR) return;
	_vsnprintf(line, sizeof line - 1, fmt, vl);
	line[sizeof line - 1] = 0;
	tube_log("ff: %s", line);
}

Player *player_open(HWND hwnd, const YtStreams *s, int orientation, int show_stats)
{
	Player *p = (Player *)calloc(1, sizeof(Player));
	LOGFONTW lf;
	if (!p) return NULL;
	init_tabs();
	if (!g_ffcs_init) { InitializeCriticalSection(&g_ffcs); g_ffcs_init = 1; }
	av_log_set_callback(log_cb);
	av_force_cpu_flags(g_tune.asm_on ? -1 : 0);
	InitializeCriticalSection(&p->cs);
	p->hwnd = hwnd;
	p->title = xstrdup(s->title ? s->title : "");
	strncpy(p->client, s->client, sizeof p->client - 1);
	p->dur_ms = s->dur_ms;
	p->vitag = s->vitag;
	p->aitag = s->aitag;
	p->srcw = s->w > 0 ? s->w : 426;
	p->srch = s->h > 0 ? s->h : 240;
	p->ori = orientation;
	p->media_post = s->media_post;
	{
		int k;
		p->ys = *s;
		p->ys.title = NULL;
		p->ys.vurl = p->ys.aurl = NULL;
		for (k = 0; k < s->nalt; k++) p->ys.alt[k].url = xstrdup(s->alt[k].url);
		p->alt_next = 1;
	}
	p->show_stats = show_stats;
	p->state = PS_LOADING;
	p->ovl = 1;
	p->ovl_tick = GetTickCount();
	p->ftick = GetTickCount();
	p->seek_gen = 1;
	p->seek_ms = 0;
	memset(&lf, 0, sizeof lf);
	lf.lfHeight = -13;
	lf.lfQuality = 5; /* CLEARTYPE_QUALITY */
	p->font = CreateFontIndirectW(&lf);
	lf.lfHeight = -15;
	lf.lfWeight = FW_BOLD;
	p->fontb = CreateFontIndirectW(&lf);
	make_surfaces(p);
	tube_log("open: itag %d/%d via %s, %s", s->vitag, s->aitag, s->client, s->title ? s->title : "");
	if (!src_start(&p->vs, s, s->vurl, s->vsize, 3 * 1024 * 1024, "video")) {
		set_error(p, "메모리 부족 (영상 버퍼)");
		return p;
	}
	if (s->aurl) {
		p->has_audio = 1;
		if (!src_start(&p->as, s, s->aurl, s->asize, 1536 * 1024, "audio")) p->has_audio = 0;
	}
	if (!p->has_audio) p->audio_dead = 1;
	p->vth = CreateThread(NULL, 0x40000, video_thread, p, THREAD_FLAGS, NULL);
	if (p->has_audio) p->ath = CreateThread(NULL, 0x40000, audio_thread, p, THREAD_FLAGS, NULL);
	SetThreadPriority(p->ath, THREAD_PRIORITY_ABOVE_NORMAL);
	return p;
}

void player_close(Player *p)
{
	int ok = 1;
	if (!p) return;
	power_keep(0);
	p->quit = 1;
	p->vs.quit = p->as.quit = 1;
	if (p->vs.ring) SetEvent(p->vs.ev_data);
	if (p->as.ring) SetEvent(p->as.ev_data);
	if (p->vth) { if (WaitForSingleObject(p->vth, 5000) != WAIT_OBJECT_0) ok = 0; CloseHandle(p->vth); }
	if (p->ath) { if (WaitForSingleObject(p->ath, 5000) != WAIT_OBJECT_0) ok = 0; CloseHandle(p->ath); }
	src_stop(&p->vs);
	src_stop(&p->as);
	if (!ok) { tube_log("player threads did not stop; leaking"); return; }
	free_surfaces(p);
	if (p->font) DeleteObject(p->font);
	if (p->fontb) DeleteObject(p->fontb);
	free(p->vframe);
	free(p->title);
	yt_streams_free(&p->ys);
	DeleteCriticalSection(&p->cs);
	free(p);
}

void player_paint(Player *p, HDC dc)
{
	EnterCriticalSection(&p->cs);
	compose(p);
	BitBlt(dc, 0, 0, p->SW, p->SH, p->sdc, 0, 0, SRCCOPY);
	LeaveCriticalSection(&p->cs);
}

void player_resize(Player *p)
{
	EnterCriticalSection(&p->cs);
	make_surfaces(p);
	LeaveCriticalSection(&p->cs);
	InvalidateRect(p->hwnd, NULL, FALSE);
}

int player_orientation(Player *p) { return p->ori; }

static void do_seek(Player *p, long ms)
{
	if (ms < 0) ms = 0;
	if (p->dur_ms > 0 && ms > p->dur_ms - 1000) ms = p->dur_ms - 1000;
	if (ms < 0) ms = 0;
	if (p->state == PS_ERROR) return;
	p->seek_ms = ms;
	p->audio_started = 0;
	p->abase_ms = ms;
	if (p->audio_dead) sclock_set(p, ms, 0);
	if (p->state == PS_ENDED) { p->state = PS_PLAYING; p->paused = 0; }
	p->vs.intr = 1;
	p->as.intr = 1;
	InterlockedIncrement((LONG *)&p->seek_gen);
	SetEvent(p->vs.ev_data);
	if (p->as.ring) SetEvent(p->as.ev_data);
	p->ovl_tick = GetTickCount();
}

static void toggle_pause(Player *p)
{
	if (p->state == PS_ERROR) return;
	if (p->state == PS_ENDED) { do_seek(p, 0); return; }
	if (p->paused) {
		p->paused = 0;
		if (p->audio_dead) sclock_set(p, p->sbase_ms, 1);
		if (p->state == PS_PAUSED) p->state = PS_PLAYING;
	} else {
		if (p->audio_dead) sclock_set(p, clock_ms(p), 0);
		p->paused = 1;
		if (p->state == PS_PLAYING || p->state == PS_BUFFERING) p->state = PS_PAUSED;
	}
	p->ovl_tick = GetTickCount();
}

static void toggle_fill(Player *p)
{
	EnterCriticalSection(&p->cs);
	p->fill = !p->fill;
	layout(p);
	strcpy(p->toast, p->fill ? "화면 맞춤: 꽉 채우기" : "화면 맞춤: 원본 비율");
	p->toast_tick = GetTickCount();
	LeaveCriticalSection(&p->cs);
	p->ovl = 1;
	p->ovl_tick = GetTickCount();
	InvalidateRect(p->hwnd, NULL, FALSE);
}

static void to_logical(Player *p, int sx, int sy, int *lx, int *ly)
{
	if (p->ori == ORI_LAND_LEFT) { *lx = p->SH - 1 - sy; *ly = sx; }
	else if (p->ori == ORI_LAND_RIGHT) { *lx = sy; *ly = p->SW - 1 - sx; }
	else { *lx = sx; *ly = sy; }
}

static long track_ms(Player *p, int lx)
{
	RECT rew, play, fwd, track;
	int w;
	play_rects(p, &rew, &play, &fwd, &track);
	w = track.right - track.left;
	if (w <= 0 || p->dur_ms <= 0) return 0;
	if (lx < track.left) lx = track.left;
	if (lx > track.right) lx = track.right;
	return (long)((long long)(lx - track.left) * p->dur_ms / w);
}

int player_tap(Player *p, int x, int y)
{
	int lx, ly;
	RECT rew, play, fwd, track;
	POINT pt;
	int ovl_shown = p->ovl || !floating(p) || p->paused || p->state != PS_PLAYING;
	to_logical(p, x, y, &lx, &ly);
	pt.x = lx; pt.y = ly;
	if (!ovl_shown) {
		p->ovl = 1;
		p->ovl_tick = GetTickCount();
		InvalidateRect(p->hwnd, NULL, FALSE);
		return TAP_NONE;
	}
	p->ovl_tick = GetTickCount();
	if (ly < TB) {
		if (lx < 60) return TAP_BACK;
		if (lx > p->LW - 52) {
			EnterCriticalSection(&p->cs);
			p->ori = (p->ori + 1) % 3;
			make_surfaces(p);
			LeaveCriticalSection(&p->cs);
			InvalidateRect(p->hwnd, NULL, FALSE);
		} else if (lx > p->LW - 102) toggle_fill(p);
		return TAP_NONE;
	}
	play_rects(p, &rew, &play, &fwd, &track);
	if (PtInRect(&rew, pt)) do_seek(p, cur_pos(p) - 10000);
	else if (PtInRect(&fwd, pt)) do_seek(p, cur_pos(p) + 10000);
	else if (PtInRect(&play, pt)) toggle_pause(p);
	else if (ly >= track.top - 6 && ly <= track.bottom + 4) {
		p->drag = 1;
		p->drag_ms = track_ms(p, lx);
		SetCapture(p->hwnd);
	} else if (floating(p) && ly > TB && ly < bottom_y(p)) {
		/* tap on the picture hides the controls */
		p->ovl = 0;
		p->need_clear = 1;
	}
	InvalidateRect(p->hwnd, NULL, FALSE);
	return TAP_NONE;
}

void player_drag(Player *p, int x, int y, int up)
{
	int lx, ly;
	if (!p->drag) return;
	to_logical(p, x, y, &lx, &ly);
	p->drag_ms = track_ms(p, lx);
	p->ovl_tick = GetTickCount();
	if (up) {
		p->drag = 0;
		ReleaseCapture();
		do_seek(p, p->drag_ms);
	}
	InvalidateRect(p->hwnd, NULL, FALSE);
}

void player_key(Player *p, int vk)
{
	switch (vk) {
	case VK_LEFT: do_seek(p, cur_pos(p) - 10000); break;
	case VK_RIGHT: do_seek(p, cur_pos(p) + 10000); break;
	case VK_RETURN: case VK_SPACE: toggle_pause(p); break;
	case VK_UP: case VK_DOWN: p->ovl = 1; break;
	case VK_F1: case VK_F2: toggle_fill(p); return;   /* soft keys */
	default: return;
	}
	p->ovl = 1;
	p->ovl_tick = GetTickCount();
	InvalidateRect(p->hwnd, NULL, FALSE);
}

void player_tick(Player *p)
{
	DWORD now = GetTickCount();
	int starving, playing = !p->paused && (p->state == PS_PLAYING || p->state == PS_BUFFERING || p->state == PS_LOADING);
	/* keep the sound going with the screen off (power button) while playing */
	power_keep(playing && p->has_audio && !p->audio_dead);
	{
		int bg = g_screen_off && playing && p->has_audio && !p->audio_dead;
		if (bg != p->bg) { p->bg = bg; if (!bg) p->need_clear = 1; }
	}
	/* while watching, don't let the screen time out */
	if (playing && !g_screen_off && now - p->idle_tick > 10000) {
		p->idle_tick = now;
#ifdef _WIN32_WCE
		SystemIdleTimerReset();
#endif
	}
	if (p->bg) return;
	if (now - p->ftick >= 1000) {
		p->fps = p->fcount * 1000 / (int)(now - p->ftick);
		p->fcount = 0;
		p->ftick = now;
	}
	/* buffering state */
	starving = p->vs.starving || (p->has_audio && !p->audio_dead && p->as.starving && !p->aeof_done);
	if (p->state == PS_PLAYING && starving && !p->veof) p->state = PS_BUFFERING;
	else if (p->state == PS_BUFFERING && !starving) p->state = PS_PLAYING;
	if (p->state == PS_LOADING && p->vs.failed) set_error(p, p->vs.err);
	/* auto-hide controls in landscape */
	if (floating(p) && p->ovl && !p->drag && p->state == PS_PLAYING && now - p->ovl_tick > 4000) {
		p->ovl = 0;
		p->need_clear = 1;
	}
	/* refresh overlay when no frames are coming (paused, buffering, ...) or the clock changes */
	if (!floating(p) || p->ovl || p->state != PS_PLAYING || p->need_clear || (p->toast[0] && now - p->toast_tick < 2500)) present(p);
	if (p->toast[0] && now - p->toast_tick >= 2500) { p->toast[0] = 0; p->need_clear = 1; }
}

#ifndef _WIN32_WCE
void player_debug(Player *p, const char *what, long arg)
{
	if (!strcmp(what, "seek")) { do_seek(p, arg); return; }
	if (!strcmp(what, "stat")) {
		tube_log("stat: state=%d pos=%ld vpos=%ld fps=%d dropped=%d vbuf=%ld..%ld rp=%ld abuf=%ld..%ld adead=%d astarted=%d",
		         p->state, clock_ms(p), p->vpos_ms, p->fps, p->dropped, p->vs.lo, p->vs.hi, p->vs.rp, p->as.lo, p->as.hi, p->audio_dead, p->audio_started);
		return;
	}
	{
		/* dump screen as BMP */
		FILE *f = fopen(what, "wb");
		int y;
		BITMAPFILEHEADER fh;
		struct { BITMAPINFOHEADER h; DWORD m[3]; } bi;
		if (!f) return;
		EnterCriticalSection(&p->cs);
		memset(&fh, 0, sizeof fh);
		memset(&bi, 0, sizeof bi);
		fh.bfType = 0x4D42;
		fh.bfOffBits = sizeof fh + sizeof bi;
		fh.bfSize = fh.bfOffBits + p->sstride * 2 * p->SH;
		bi.h.biSize = sizeof(BITMAPINFOHEADER);
		bi.h.biWidth = p->SW;
		bi.h.biHeight = -p->SH;
		bi.h.biPlanes = 1;
		bi.h.biBitCount = 16;
		bi.h.biCompression = BI_BITFIELDS;
		bi.m[0] = 0xF800; bi.m[1] = 0x07E0; bi.m[2] = 0x001F;
		fwrite(&fh, sizeof fh, 1, f);
		fwrite(&bi, sizeof bi, 1, f);
		for (y = 0; y < p->SH; y++) fwrite(p->sbits + y * p->sstride, 2, p->sstride, f);
		LeaveCriticalSection(&p->cs);
		fclose(f);
	}
}
#endif
