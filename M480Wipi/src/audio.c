/* sound engine: PCM (WAV, SMAF ADPCM) + a small software synth for MIDI and SMAF (MMF) scores */
#include "audio.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------------- event list ---------------- */
enum { E_ON, E_OFF, E_PROG, E_CC, E_WAVE, E_BEND, E_END };
typedef struct { u32 t; u8 type, ch, a, b; int wave; } Ev;

typedef struct { s16 *d; u32 n; } Wave;

typedef struct Sound {
    int used;
    s16 *pcm; u32 pcmlen;           /* PCM sound */
    Ev *ev; int nev;                /* sequence */
    Wave *waves; int nwaves;
    u32 dur_ms;
    /* playback */
    int playing, paused, loops, vol;
    u32 pos;                        /* samples */
    int evi;
    u8 prog[16], chvol[16], expr[16];
    s16 bend[16];
    int is_drum[16];
} Sound;

#define MAXSND 128
static Sound snds[MAXSND];
static int master = 100;

/* ---------------- voices ---------------- */
typedef struct {
    int on, snd, ch, note;
    u32 phase, inc;
    int amp;                        /* current amplitude 0..32767<<8 */
    int target, decay, release;     /* per-sample multipliers (16.16) */
    int wave;                       /* 0 square 1 saw 2 tri 3 sine 4 noise 5 pcm 6 kick */
    int vel;
    int releasing;
    u32 noise;
    const s16 *pcm; u32 pcmlen; u32 pcmpos;   /* fixed 16.16 */
    u32 age;
} Voice;
#define NVOICE 20
static Voice voices[NVOICE];
static s16 sine_tab[256];
static int tab_ready;

static void init_tabs(void)
{
    int i;
    for (i = 0; i < 256; i++) sine_tab[i] = (s16)(sin(i * 6.283185307 / 256) * 32767);
    tab_ready = 1;
}

static u32 note_inc(int note, int bend)
{
    double f = 440.0 * pow(2.0, (note - 69 + bend / 4096.0) / 12.0);
    return (u32)(f * 4294967296.0 / AUDIO_RATE);
}

static int wave_for_prog(int p)
{
    if (p < 8) return 2;          /* piano -> triangle-ish */
    if (p < 16) return 3;         /* chromatic perc -> sine */
    if (p < 24) return 0;         /* organ -> square */
    if (p < 32) return 1;         /* guitar -> saw */
    if (p < 40) return 2;         /* bass */
    if (p < 56) return 1;         /* strings / ensemble */
    if (p < 64) return 0;         /* brass */
    if (p < 80) return 3;         /* reed / pipe */
    if (p < 88) return 0;         /* synth lead */
    return 1;
}

static void voice_on(int sh, Sound *s, int ch, int note, int vel)
{
    int i, best = 0;
    u32 oldest = 0;
    Voice *v;
    for (i = 0; i < NVOICE; i++) {
        if (!voices[i].on) { best = i; oldest = 0xFFFFFFFF; break; }
        if (voices[i].age >= oldest) { oldest = voices[i].age; best = i; }
    }
    v = &voices[best];
    memset(v, 0, sizeof *v);
    v->on = 1; v->snd = sh; v->ch = ch; v->note = note; v->vel = vel;
    v->noise = 0x12345 + note;
    if (s->is_drum[ch]) {
        if (note == 35 || note == 36) { v->wave = 6; v->inc = note_inc(40, 0); v->decay = 65200; }
        else { v->wave = 4; v->decay = (note >= 42 && note <= 46) || note >= 49 ? 65000 : 64600; v->inc = note_inc(note + 24, 0); }
        v->amp = vel * 200 << 8;
    } else {
        v->wave = wave_for_prog(s->prog[ch]);
        v->inc = note_inc(note, s->bend[ch]);
        v->amp = vel * 190 << 8;
        v->decay = (v->wave == 2 || v->wave == 3) ? 65500 : 65525;
    }
    v->release = 64000;
}

static void voice_off(int sh, int ch, int note)
{
    int i;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].on && voices[i].snd == sh && voices[i].ch == ch && voices[i].note == note && !voices[i].releasing) {
            voices[i].releasing = 1;
            if (voices[i].wave != 4 && voices[i].wave != 6) break;
        }
}

static void voice_wave(int sh, const Wave *w)
{
    int i, best = 0;
    Voice *v;
    for (i = 0; i < NVOICE; i++) if (!voices[i].on) { best = i; break; }
    v = &voices[best];
    memset(v, 0, sizeof *v);
    v->on = 1; v->snd = sh; v->ch = 15; v->wave = 5;
    v->pcm = w->d; v->pcmlen = w->n; v->pcmpos = 0;
    v->amp = 127 * 200 << 8;
}

static void voices_kill(int sh)
{
    int i;
    for (i = 0; i < NVOICE; i++) if (voices[i].snd == sh) voices[i].on = 0;
}

/* ---------------- mixing ---------------- */
static int render_voice(Voice *v, const Sound *s)
{
    int smp, a = v->amp >> 8;
    u32 ph;
    if (v->wave == 5) {
        u32 idx = v->pcmpos >> 16;
        if (idx >= v->pcmlen) { v->on = 0; return 0; }
        v->pcmpos += 1 << 16;
        return v->pcm[idx] * (s->vol) / 100;
    }
    ph = v->phase;
    v->phase += v->inc;
    switch (v->wave) {
    case 0: smp = (ph & 0x80000000u) ? 9000 : -9000; break;
    case 1: smp = ((s32)(ph >> 16) - 32768) * 9000 / 32768; break;
    case 2: { s32 x = (s32)(ph >> 16); smp = (x < 32768 ? x : 65535 - x) * 2 - 32768; smp = smp * 12000 / 32768; break; }
    case 3: smp = sine_tab[ph >> 24] * 12000 / 32767; break;
    case 4: v->noise = v->noise * 1103515245u + 12345u; smp = ((s32)(v->noise >> 16) - 32768) * 7000 / 32768; break;
    case 6: { smp = sine_tab[ph >> 24] * 14000 / 32767; if (v->inc > 600000) v->inc = v->inc * 65500 >> 16; break; }
    default: smp = 0;
    }
    smp = smp * a / 32767;
    if (v->releasing) v->amp = (int)(((s64)v->amp * v->release) >> 16);
    else v->amp = (int)(((s64)v->amp * v->decay) >> 16);
    if (v->amp < (40 << 8)) v->on = 0;
    {
        int ch = v->ch;
        smp = smp * s->chvol[ch] / 127 * s->expr[ch] / 127;
    }
    return smp * s->vol / 100;
}

static void seq_tick(int sh, Sound *s, u32 now_ms)
{
    while (s->evi < s->nev && s->ev[s->evi].t <= now_ms) {
        Ev *e = &s->ev[s->evi++];
        switch (e->type) {
        case E_ON: if (e->b) voice_on(sh, s, e->ch, e->a, e->b); else voice_off(sh, e->ch, e->a); break;
        case E_OFF: voice_off(sh, e->ch, e->a); break;
        case E_PROG: s->prog[e->ch] = e->a; break;
        case E_CC:
            if (e->a == 7) s->chvol[e->ch] = e->b;
            else if (e->a == 11) s->expr[e->ch] = e->b;
            else if (e->a == 0 && e->b == 0x7d) s->is_drum[e->ch] = 1;
            else if (e->a == 120 || e->a == 123) { int i; for (i = 0; i < NVOICE; i++) if (voices[i].snd == sh && voices[i].ch == e->ch) voices[i].releasing = 1; }
            break;
        case E_BEND: s->bend[e->ch] = (s16)(((e->a | (e->b << 7)) - 8192) / 4); break;
        case E_WAVE: if (e->wave >= 0 && e->wave < s->nwaves) voice_wave(sh, &s->waves[e->wave]); break;
        default: break;
        }
    }
}

static void reset_channels(Sound *s)
{
    int c;
    for (c = 0; c < 16; c++) { s->prog[c] = 0; s->chvol[c] = 100; s->expr[c] = 127; s->bend[c] = 0; }
    s->evi = 0;
}

void audio_render(s16 *out, int n)
{
    int i, k;
    static s32 mix[4096];
    if (!tab_ready) init_tabs();
    while (n > 0) {
        int chunk = n > 4096 ? 4096 : n;
        memset(mix, 0, chunk * sizeof(s32));
        pal_audio_lock();
        for (k = 1; k < MAXSND; k++) {
            Sound *s = &snds[k];
            if (!s->used || !s->playing || s->paused) continue;
            if (s->pcm) {
                for (i = 0; i < chunk; i++) {
                    if (s->pos >= s->pcmlen) {
                        if (s->loops < 0 || --s->loops > 0) s->pos = 0;
                        else { s->playing = 0; break; }
                    }
                    mix[i] += s->pcm[s->pos++] * s->vol / 100;
                }
            }
        }
        /* sequences: advance in 1ms-ish steps (22 samples) for timing */
        for (i = 0; i < chunk; i++) {
            if ((i % 22) == 0) {
                for (k = 1; k < MAXSND; k++) {
                    Sound *s = &snds[k];
                    u32 ms;
                    if (!s->used || !s->playing || s->paused || !s->ev) continue;
                    ms = (u32)((u64)s->pos * 1000 / AUDIO_RATE);
                    seq_tick(k, s, ms);
                    if (s->evi >= s->nev && ms >= s->dur_ms) {
                        if (s->loops < 0 || --s->loops > 0) { s->pos = 0; reset_channels(s); voices_kill(k); }
                        else { s->playing = 0; }
                    }
                }
            }
            for (k = 1; k < MAXSND; k++) if (snds[k].used && snds[k].playing && !snds[k].paused && snds[k].ev) snds[k].pos++;
            {
                s32 acc = mix[i];
                for (k = 0; k < NVOICE; k++) {
                    Voice *v = &voices[k];
                    Sound *s;
                    if (!v->on) continue;
                    s = &snds[v->snd];
                    if (!s->playing || s->paused) { if (!s->playing) v->on = 0; continue; }
                    v->age++;
                    acc += render_voice(v, s);
                }
                acc = acc * master / 100;
                if (acc > 32767) acc = 32767;
                if (acc < -32768) acc = -32768;
                out[i] = (s16)acc;
            }
        }
        pal_audio_unlock();
        out += chunk;
        n -= chunk;
    }
}

/* ---------------- decoders ---------------- */
static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static u32 le32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
static u16 le16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }

static s16 *resample(const s16 *in, u32 n, u32 rate, u32 *outn)
{
    u32 on, i;
    s16 *o;
    if (!rate) rate = AUDIO_RATE;
    on = (u32)((u64)n * AUDIO_RATE / rate);
    o = (s16 *)malloc((on + 1) * 2);
    if (!o) return NULL;
    for (i = 0; i < on; i++) {
        u64 p = (u64)i * rate * 65536 / AUDIO_RATE;
        u32 a = (u32)(p >> 16), f = (u32)(p & 0xFFFF);
        s32 x0 = in[a < n ? a : n - 1], x1 = in[a + 1 < n ? a + 1 : n - 1];
        o[i] = (s16)(x0 + (((x1 - x0) * (s32)f) >> 16));
    }
    *outn = on;
    return o;
}

static int load_wav(Sound *s, const u8 *d, u32 len)
{
    const u8 *p = d + 12, *end = d + len;
    int ch = 1, bits = 16, fmt = 1;
    u32 rate = 8000;
    while (p + 8 <= end) {
        u32 cl = le32(p + 4);
        const u8 *cd = p + 8;
        if (cd + cl > end) cl = (u32)(end - cd);
        if (!memcmp(p, "fmt ", 4) && cl >= 16) { fmt = le16(cd); ch = le16(cd + 2); rate = le32(cd + 4); bits = le16(cd + 14); }
        else if (!memcmp(p, "data", 4)) {
            u32 n, i;
            s16 *tmp;
            if (fmt != 1) return 0;
            n = cl / (bits / 8) / ch;
            tmp = (s16 *)malloc((n + 1) * 2);
            if (!tmp) return 0;
            for (i = 0; i < n; i++) {
                s32 v = 0;
                int c;
                for (c = 0; c < ch; c++) {
                    const u8 *q = cd + ((size_t)i * ch + c) * (bits / 8);
                    v += bits == 8 ? ((s32)q[0] - 128) << 8 : (s16)le16(q);
                }
                tmp[i] = (s16)(v / ch);
            }
            s->pcm = resample(tmp, n, rate, &s->pcmlen);
            free(tmp);
            s->dur_ms = (u32)((u64)s->pcmlen * 1000 / AUDIO_RATE);
            return s->pcm != NULL;
        }
        p = cd + cl + (cl & 1);
    }
    return 0;
}

/* growing event buffer */
typedef struct { Ev *e; int n, cap; } EvBuf;
static void evadd(EvBuf *b, u32 t, int type, int ch, int a, int bb, int wave)
{
    if (b->n == b->cap) { b->cap = b->cap ? b->cap * 2 : 512; b->e = (Ev *)realloc(b->e, b->cap * sizeof(Ev)); }
    b->e[b->n].t = t; b->e[b->n].type = (u8)type; b->e[b->n].ch = (u8)(ch & 15); b->e[b->n].a = (u8)a; b->e[b->n].b = (u8)bb; b->e[b->n].wave = wave;
    b->n++;
}
static int evcmp(const void *x, const void *y)
{
    const Ev *a = (const Ev *)x, *b = (const Ev *)y;
    if (a->t != b->t) return a->t < b->t ? -1 : 1;
    /* offs before ons at the same time, setup first */
    {
        static const int pri[7] = { 3, 2, 0, 1, 4, 1, 5 };
        return pri[a->type] - pri[b->type];
    }
}
static void finish_events(Sound *s, EvBuf *b)
{
    u32 last = 0;
    int i;
    qsort(b->e, b->n, sizeof(Ev), evcmp);
    for (i = 0; i < b->n; i++) if (b->e[i].t > last) last = b->e[i].t;
    s->ev = b->e; s->nev = b->n;
    s->dur_ms = last + 300;
}

/* Standard MIDI file */
static u32 vlq(const u8 **pp, const u8 *end)
{
    u32 v = 0;
    const u8 *p = *pp;
    while (p < end) { u8 c = *p++; v = (v << 7) | (c & 0x7F); if (!(c & 0x80)) break; }
    *pp = p;
    return v;
}

static int load_midi(Sound *s, const u8 *d, u32 len)
{
    int ntrk, div, t;
    const u8 *p = d + 14, *end = d + len;
    EvBuf b = { 0, 0, 0 };
    /* tempo map: collect from all tracks, simple approach: tick->ms using tempo changes in track order */
    typedef struct { u32 tick; u32 us; } Tempo;
    Tempo tempos[256];
    int ntempo = 1;
    if (len < 14) return 0;
    ntrk = (d[10] << 8) | d[11];
    div = (d[12] << 8) | d[13];
    if (div <= 0 || (div & 0x8000)) div = 96;
    tempos[0].tick = 0; tempos[0].us = 500000;
    /* first pass: tempo events */
    for (t = 0; t < ntrk && p + 8 <= end; t++) {
        u32 tl = be32(p + 4);
        const u8 *q = p + 8, *qe = q + tl;
        u32 tick = 0;
        u8 run = 0;
        if (qe > end) qe = end;
        while (q < qe) {
            u8 st;
            tick += vlq(&q, qe);
            if (q >= qe) break;
            st = *q;
            if (st & 0x80) q++; else st = run;
            if (st == 0xFF) {
                u8 type = *q++;
                u32 l = vlq(&q, qe);
                if (type == 0x51 && l >= 3 && ntempo < 256) { tempos[ntempo].tick = tick; tempos[ntempo].us = (q[0] << 16) | (q[1] << 8) | q[2]; ntempo++; }
                q += l;
            } else if (st == 0xF0 || st == 0xF7) { u32 l = vlq(&q, qe); q += l; }
            else { run = st; q += ((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0) ? 1 : 2; }
        }
        p = qe;
    }
    /* sort tempos by tick (insertion) */
    {
        int i, j;
        for (i = 1; i < ntempo; i++) { Tempo x = tempos[i]; j = i - 1; while (j >= 0 && tempos[j].tick > x.tick) { tempos[j + 1] = tempos[j]; j--; } tempos[j + 1] = x; }
    }
    p = d + 14;
    for (t = 0; t < ntrk && p + 8 <= end; t++) {
        u32 tl = be32(p + 4);
        const u8 *q = p + 8, *qe = q + tl;
        u32 tick = 0;
        u8 run = 0;
        if (memcmp(p, "MTrk", 4)) break;
        if (qe > end) qe = end;
        while (q < qe) {
            u8 st;
            u32 ms;
            tick += vlq(&q, qe);
            if (q >= qe) break;
            {
                /* tick -> ms */
                int k = 0;
                double acc = 0;
                while (k + 1 < ntempo && tempos[k + 1].tick <= tick) {
                    acc += (double)(tempos[k + 1].tick - tempos[k].tick) * tempos[k].us / div / 1000.0;
                    k++;
                }
                acc += (double)(tick - tempos[k].tick) * tempos[k].us / div / 1000.0;
                ms = (u32)acc;
            }
            st = *q;
            if (st & 0x80) q++; else st = run;
            if (st == 0xFF) { q++; { u32 l = vlq(&q, qe); q += l; } continue; }
            if (st == 0xF0 || st == 0xF7) { u32 l = vlq(&q, qe); q += l; continue; }
            run = st;
            switch (st & 0xF0) {
            case 0x90: evadd(&b, ms, E_ON, st & 15, q[0], q[1], -1); q += 2; break;
            case 0x80: evadd(&b, ms, E_OFF, st & 15, q[0], 0, -1); q += 2; break;
            case 0xB0: evadd(&b, ms, E_CC, st & 15, q[0], q[1], -1); q += 2; break;
            case 0xC0: evadd(&b, ms, E_PROG, st & 15, q[0], 0, -1); q += 1; break;
            case 0xE0: evadd(&b, ms, E_BEND, st & 15, q[0], q[1], -1); q += 2; break;
            case 0xD0: q += 1; break;
            default: q += 2; break;
            }
        }
        p = qe;
    }
    if (!b.n) { free(b.e); return 0; }
    finish_events(s, &b);
    s->is_drum[9] = 1;
    return 1;
}

/* ---------------- SMAF (MMF) ---------------- */
static int timebase(u8 r)
{
    switch (r) { case 0: return 1; case 1: return 2; case 2: return 4; case 3: return 5; case 0x10: return 10; case 0x11: return 20; case 0x12: return 40; case 0x13: return 50; }
    return 1;
}
static u32 smaf_vlq(const u8 **pp, const u8 *e) { return vlq(pp, e); }
static u32 handy_vlq(const u8 **pp, const u8 *e)
{
    const u8 *p = *pp;
    u32 v;
    if (p >= e) return 0;
    if (!(p[0] & 0x80)) { *pp = p + 1; return p[0]; }
    if (p + 1 >= e) { *pp = e; return 0; }
    v = ((((u32)(p[0] & 0x7F)) + 1) << 7) | p[1];
    *pp = p + 2;
    return v;
}

static s16 *adpcm_decode(const u8 *d, u32 n, u32 *outn)
{
    static const u16 st[8] = { 230, 230, 230, 230, 307, 409, 512, 614 };
    s16 *o = (s16 *)malloc((n * 2 + 1) * 2);
    s32 hist = 0, step = 127;
    u32 i, k = 0;
    if (!o) return NULL;
    for (i = 0; i < n * 2; i++) {
        int nib = (i & 1) ? d[i >> 1] >> 4 : d[i >> 1] & 15;
        int dl = nib & 7;
        s32 diff = ((1 + (dl << 1)) * step) >> 3;
        hist += (nib & 8) ? -diff : diff;
        if (hist > 32767) hist = 32767;
        if (hist < -32768) hist = -32768;
        step = (st[dl] * step) >> 8;
        if (step < 127) step = 127;
        if (step > 24576) step = 24576;
        o[k++] = (s16)hist;
    }
    *outn = k;
    return o;
}

static int add_wave(Sound *s, const u8 *d, u32 n, int fmt, int bits, u32 rate)
{
    s16 *raw = NULL;
    u32 rn = 0, i;
    Wave *w;
    if (fmt == 2 || fmt == 1 + 16) raw = adpcm_decode(d, n, &rn);           /* Yamaha ADPCM */
    else if (bits == 1 || bits == 8) { raw = (s16 *)malloc(n * 2 + 2); rn = n; for (i = 0; i < n; i++) raw[i] = (s16)(fmt == 1 ? ((s32)d[i] - 128) << 8 : (s8)d[i] << 8); }
    else return -1;
    if (!raw) return -1;
    s->waves = (Wave *)realloc(s->waves, (s->nwaves + 1) * sizeof(Wave));
    w = &s->waves[s->nwaves];
    w->d = resample(raw, rn, rate, &w->n);
    free(raw);
    if (!w->d) return -1;
    return s->nwaves++;
}

static u8 *huff_decode(const u8 *src, u32 slen, u32 outlen)
{
    /* SMAF mobile-standard compressed sequence */
    enum { N = 256 };
    static u16 left[2 * N], right[2 * N];
    int avail = N, root;
    u8 *out;
    u32 i;
    {
        struct { const u8 *s; u32 n; u32 byte; int bit; int err; } r;
        r.s = src; r.n = slen; r.byte = 0; r.bit = 8; r.err = 0;
        {
            int node_stack[2 * N], side_stack[2 * N], depth = 0, cur;
            int b0;
#define GETBIT(dst) do { if (r.bit == 8) { if (r.byte >= r.n) { r.err = 1; dst = 0; break; } r.bit = 0; } dst = (r.s[r.byte] >> (7 - r.bit)) & 1; r.bit++; if (r.bit == 8) r.byte++; } while (0)
            GETBIT(b0);
            if (!b0) {
                int v = 0, k, bb;
                for (k = 0; k < 8; k++) { GETBIT(bb); v = (v << 1) | bb; }
                root = v;
            } else {
                root = avail++;
                node_stack[0] = root; side_stack[0] = 0; depth = 1;
                while (depth > 0 && !r.err) {
                    int bb, child;
                    cur = node_stack[depth - 1];
                    if (side_stack[depth - 1] == 2) { depth--; continue; }
                    GETBIT(bb);
                    if (bb) {
                        if (avail >= 2 * N - 1) { r.err = 1; break; }
                        child = avail++;
                        if (side_stack[depth - 1] == 0) left[cur] = (u16)child; else right[cur] = (u16)child;
                        side_stack[depth - 1]++;
                        node_stack[depth] = child; side_stack[depth] = 0; depth++;
                    } else {
                        int v = 0, k, b2;
                        for (k = 0; k < 8; k++) { GETBIT(b2); v = (v << 1) | b2; }
                        if (side_stack[depth - 1] == 0) left[cur] = (u16)v; else right[cur] = (u16)v;
                        side_stack[depth - 1]++;
                    }
                }
            }
            if (r.err) return NULL;
            out = (u8 *)malloc(outlen + 1);
            if (!out) return NULL;
            for (i = 0; i < outlen; i++) {
                int node = root, bb;
                while (node >= N) { GETBIT(bb); if (r.err) { free(out); return NULL; } node = bb ? right[node] : left[node]; }
                out[i] = (u8)node;
            }
#undef GETBIT
        }
    }
    return out;
}

static void smaf_score(Sound *s, EvBuf *b, const u8 *d, u32 n, int track_no)
{
    int fmt, tbd, tbg, chofs;
    const u8 *p, *end = d + n;
    const u8 *seq = NULL;
    u32 seqlen = 0;
    int wave_ids[64];
    u8 *decomp = NULL;
    s8 oct[16];
    int i;
    if (n < 4) return;
    fmt = d[0];
    tbd = timebase(d[2]); tbg = timebase(d[3]);
    p = d + 4 + (fmt == 0 ? 2 : 16);
    for (i = 0; i < 64; i++) wave_ids[i] = -1;
    memset(oct, 0, sizeof oct);
    /* handyphone tracks carry 4 channels each */
    chofs = fmt == 0 ? ((track_no - 1) & 3) * 4 : 0;
    while (p + 8 <= end) {
        u32 cl = be32(p + 4);
        const u8 *cd = p + 8;
        if (cd + cl > end) cl = (u32)(end - cd);
        if (!memcmp(p, "Mtsq", 4)) { seq = cd; seqlen = cl; }
        else if (!memcmp(p, "Mtsp", 4)) {
            const u8 *q = cd, *qe = cd + cl;
            while (q + 8 <= qe) {
                u32 wl = be32(q + 4);
                if (!memcmp(q, "Mwa", 3) && wl >= 3 && q + 8 + wl <= qe) {
                    int id = q[3];
                    u8 wt = q[8];
                    u32 rate = (q[9] << 8) | q[10];
                    int wf = (wt >> 4) & 7, bb = wt & 15;
                    int wi = add_wave(s, q + 11, wl - 3, wf == 2 ? 2 : wf, bb == 1 ? 8 : bb == 0 ? 4 : 16, rate);
                    if (id < 64) wave_ids[id] = wi;
                }
                q += 8 + wl;
            }
        }
        p = cd + cl;
    }
    if (!seq) return;
    if (fmt == 1) {
        u32 dl = be32(seq);
        decomp = huff_decode(seq + 4, seqlen - 4, dl);
        if (!decomp) return;
        seq = decomp; seqlen = dl;
    }
    {
        const u8 *q = seq, *qe = seq + seqlen;
        u32 now = 0;
        u8 prog[16];
        memset(prog, 0, sizeof prog);
        while (q < qe) {
            u32 dur;
            u8 st;
            if (fmt == 0) {
                if (q + 4 <= qe && !q[0] && !q[1] && !q[2] && !q[3]) break;
                dur = handy_vlq(&q, qe);
            } else dur = smaf_vlq(&q, qe);
            if (q >= qe) break;
            now += dur * tbd;
            st = *q++;
            if (fmt == 0) {
                if (st != 0 && st != 0xFF) {
                    int ch = ((st >> 6) & 3) + chofs, note = ((st >> 4) & 3) * 12 + (st & 15) + oct[ch & 15] * 12 + 36;
                    u32 gate = handy_vlq(&q, qe) * tbg;
                    if (note > 127) note = 127;
                    if (s->is_drum[ch & 15]) note = prog[ch & 15] ? prog[ch & 15] : note;
                    evadd(b, now, E_ON, ch, note, 100, -1);
                    evadd(b, now + (gate ? gate : 1), E_OFF, ch, note, 0, -1);
                } else if (st == 0) {
                    u8 nb = *q++;
                    int ch = ((nb >> 6) & 3) + chofs, et = nb & 0x3F;
                    static const u8 ex[15] = { 0x00, 0x00, 0x1f, 0x27, 0x2f, 0x37, 0x3f, 0x47, 0x4f, 0x57, 0x5f, 0x67, 0x6f, 0x77, 0x7f };
                    if (et == 0) q++;
                    else if (et >= 1 && et <= 0x0e) evadd(b, now, E_CC, ch, 11, ex[et], -1);
                    else if (et >= 0x11 && et <= 0x1e) { int v = (et - 0x10) * 16384 / 16; if (v > 16383) v = 16383; evadd(b, now, E_BEND, ch, v & 127, v >> 7, -1); }
                    else if (et >= 0x21 && et <= 0x2e) {}
                    else if (et == 0x30) { u8 v = *q++; prog[ch & 15] = v; evadd(b, now, E_PROG, ch, v & 127, 0, -1); }
                    else if (et == 0x31) { u8 v = *q++; if (v == 0x7d || (v & 0x80)) s->is_drum[ch & 15] = 1; }
                    else if (et == 0x32) { u8 v = *q++; oct[ch & 15] = (s8)(v <= 4 ? v : (v >= 0x81 && v <= 0x84) ? -(v - 0x80) : 0); }
                    else if (et == 0x33) q++;
                    else if (et == 0x34) { u8 v = *q++; int pb = 8192 + (v - 128) * 64; if (pb < 0) pb = 0; if (pb > 16383) pb = 16383; evadd(b, now, E_BEND, ch, pb & 127, pb >> 7, -1); }
                    else if (et == 0x36 || et == 0x3b) { u8 v = *q++; evadd(b, now, E_CC, ch, 11, v & 127, -1); }
                    else if (et == 0x37) { u8 v = *q++; evadd(b, now, E_CC, ch, 7, v & 127, -1); }
                    else if (et == 0x3a) q++;
                } else {
                    u8 nb = *q++;
                    if (nb == 0xF0) { while (q < qe && *q != 0xF7) q++; if (q < qe) q++; }
                }
            } else {
                int ch = st & 15;
                switch (st & 0xF0) {
                case 0x80: case 0x90: {
                    int note = *q++, vel = 100;
                    u32 gate;
                    if ((st & 0xF0) == 0x90) vel = *q++;
                    gate = smaf_vlq(&q, qe) * tbg;
                    if (note == 0) {
                        int w = ch + 1 < 64 ? wave_ids[ch + 1] : -1;
                        if (w >= 0) evadd(b, now, E_WAVE, ch, 0, 0, w);
                    } else {
                        if (vel == 0) vel = 1;
                        evadd(b, now, E_ON, ch, note, vel, -1);
                        evadd(b, now + (gate ? gate : 1), E_OFF, ch, note, 0, -1);
                    }
                    break;
                }
                case 0xA0: q += 2; break;
                case 0xB0: { int c = *q++, v = *q++; evadd(b, now, E_CC, ch, c, v, -1); if (c == 0 && v == 0x7d) s->is_drum[ch] = 1; break; }
                case 0xC0: evadd(b, now, E_PROG, ch, *q++ & 127, 0, -1); break;
                case 0xD0: q++; break;
                case 0xE0: { int lsb = *q++, msb = *q++; evadd(b, now, E_BEND, ch, lsb & 127, msb & 127, -1); break; }
                case 0xF0:
                    if (st == 0xF0) { u32 l = smaf_vlq(&q, qe); q += l; }
                    else if (st == 0xFF) { u8 x = *q++; if (x == 0x2F) { q = qe; } }
                    break;
                }
            }
        }
        evadd(b, now, E_END, 0, 0, 0, -1);
    }
    free(decomp);
}

static void smaf_pcm_track(Sound *s, EvBuf *b, const u8 *d, u32 n)
{
    u16 wt;
    int rate_i, fmt, tbd;
    u32 rate;
    const u8 *p, *end = d + n, *seq = NULL;
    u32 seqlen = 0;
    int ids[64], i;
    if (n < 6) return;
    wt = (u16)((d[2] << 8) | d[3]);
    fmt = (wt >> 12) & 7;
    rate_i = (wt >> 8) & 15;
    rate = rate_i == 0 ? 4000 : rate_i == 1 ? 8000 : rate_i == 2 ? 11025 : rate_i == 3 ? 22050 : 44100;
    tbd = timebase(d[4]);
    for (i = 0; i < 64; i++) ids[i] = -1;
    p = d + 6;
    while (p + 8 <= end) {
        u32 cl = be32(p + 4);
        const u8 *cd = p + 8;
        if (cd + cl > end) cl = (u32)(end - cd);
        if (!memcmp(p, "Atsq", 4)) { seq = cd; seqlen = cl; }
        else if (!memcmp(p, "Awa", 3)) { int id = p[3]; int w = add_wave(s, cd, cl, fmt == 1 ? 2 : 0, fmt == 1 ? 4 : 8, rate); if (id < 64) ids[id] = w; }
        p = cd + cl;
    }
    if (!seq) return;
    {
        const u8 *q = seq, *qe = seq + seqlen;
        u32 now = 0;
        while (q < qe) {
            u8 st;
            if (qe - q == 4 && !q[0] && !q[1] && !q[2] && !q[3]) break;
            now += smaf_vlq(&q, qe) * tbd;
            if (q >= qe) break;
            st = *q++;
            if (st == 0xFF) { u8 x = *q++; if (x == 0xF0) { u8 l = *q++; q += l; } }
            else if (st != 0) {
                int wn = st & 0x3F;
                smaf_vlq(&q, qe);
                if (wn < 64 && ids[wn] >= 0) evadd(b, now, E_WAVE, 15, 0, 0, ids[wn]);
            } else { q++; }
        }
        evadd(b, now, E_END, 0, 0, 0, -1);
    }
}

static int load_smaf(Sound *s, const u8 *d, u32 len)
{
    const u8 *p = d + 8, *end = d + len;
    EvBuf b = { 0, 0, 0 };
    u32 total = be32(d + 4) + 8;
    if (total < len) end = d + total;
    while (p + 8 <= end) {
        u32 cl = be32(p + 4);
        const u8 *cd = p + 8;
        if (cd + cl > end) cl = (u32)(end - cd);
        if (!memcmp(p, "MTR", 3)) smaf_score(s, &b, cd, cl, p[3]);
        else if (!memcmp(p, "ATR", 3)) smaf_pcm_track(s, &b, cd, cl);
        p = cd + cl;
    }
    if (!b.n) { free(b.e); return 0; }
    finish_events(s, &b);
    return 1;
}

/* ---------------- public API ---------------- */
static Sound *get(int h) { return (h > 0 && h < MAXSND && snds[h].used) ? &snds[h] : NULL; }

int snd_load(const u8 *d, u32 len)
{
    int h, ok = 0;
    Sound *s;
    if (!d || len < 8) return 0;
    for (h = 1; h < MAXSND; h++) if (!snds[h].used) break;
    if (h >= MAXSND) return 0;
    s = &snds[h];
    pal_audio_lock();
    memset(s, 0, sizeof *s);
    pal_audio_unlock();
    s->vol = 100;
    reset_channels(s);
    if (!memcmp(d, "RIFF", 4) && len > 12 && !memcmp(d + 8, "WAVE", 4)) ok = load_wav(s, d, len);
    else if (!memcmp(d, "MThd", 4)) ok = load_midi(s, d, len);
    else if (!memcmp(d, "MMMD", 4)) ok = load_smaf(s, d, len);
    if (!ok) {
        free(s->pcm); free(s->ev);
        { int i; for (i = 0; i < s->nwaves; i++) free(s->waves[i].d); free(s->waves); }
        memset(s, 0, sizeof *s);
        return 0;
    }
    pal_audio_lock();
    s->used = 1;
    pal_audio_unlock();
    return h;
}

void snd_play(int h, int loops)
{
    Sound *s = get(h);
    if (!s) return;
    pal_audio_lock();
    voices_kill(h);
    s->pos = 0; reset_channels(s);
    s->loops = loops == 0 ? 1 : loops;
    s->paused = 0;
    s->playing = 1;
    pal_audio_unlock();
}
void snd_stop(int h) { Sound *s = get(h); if (!s) return; pal_audio_lock(); s->playing = 0; voices_kill(h); pal_audio_unlock(); }
void snd_pause(int h, int p) { Sound *s = get(h); if (!s) return; pal_audio_lock(); s->paused = p; pal_audio_unlock(); }
void snd_volume(int h, int v) { Sound *s = get(h); if (s) s->vol = v < 0 ? 0 : v > 100 ? 100 : v; }
int snd_playing(int h) { Sound *s = get(h); return s && s->playing; }
int snd_position_ms(int h) { Sound *s = get(h); return s ? (int)((u64)s->pos * 1000 / AUDIO_RATE) : 0; }
int snd_duration_ms(int h) { Sound *s = get(h); return s ? (int)s->dur_ms : 0; }
void snd_free(int h)
{
    Sound *s = get(h);
    int i;
    if (!s) return;
    pal_audio_lock();
    s->used = 0; s->playing = 0;
    voices_kill(h);
    pal_audio_unlock();
    free(s->pcm); free(s->ev);
    for (i = 0; i < s->nwaves; i++) free(s->waves[i].d);
    free(s->waves);
    memset(s, 0, sizeof *s);
}
void snd_stop_all(void)
{
    int i;
    pal_audio_lock();
    for (i = 1; i < MAXSND; i++) snds[i].playing = 0;
    for (i = 0; i < NVOICE; i++) voices[i].on = 0;
    pal_audio_unlock();
}
void snd_set_master(int v) { master = v < 0 ? 0 : v > 100 ? 100 : v; }

void snd_tone(int note, int ms, int vol)
{
    static int toneh;
    Sound *s;
    if (toneh) snd_free(toneh);
    for (toneh = 1; toneh < MAXSND; toneh++) if (!snds[toneh].used) break;
    if (toneh >= MAXSND) { toneh = 0; return; }
    s = &snds[toneh];
    memset(s, 0, sizeof *s);
    reset_channels(s);
    s->prog[0] = 80;
    s->ev = (Ev *)malloc(3 * sizeof(Ev));
    s->ev[0].t = 0; s->ev[0].type = E_ON; s->ev[0].ch = 0; s->ev[0].a = (u8)(note & 127); s->ev[0].b = 110;
    s->ev[1].t = (u32)ms; s->ev[1].type = E_OFF; s->ev[1].ch = 0; s->ev[1].a = (u8)(note & 127); s->ev[1].b = 0;
    s->ev[2].t = (u32)ms; s->ev[2].type = E_END;
    s->nev = 3;
    s->dur_ms = (u32)ms + 50;
    s->vol = vol < 0 ? 0 : vol > 100 ? 100 : vol;
    s->used = 1;
    snd_play(toneh, 1);
}
