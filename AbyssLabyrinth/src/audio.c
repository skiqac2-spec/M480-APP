/* 심연의 미궁 - sound engine: IMA-ADPCM music + PCM effects mixer (22050 Hz mono) */
#include <stdlib.h>
#include <string.h>
#include "game.h"

#define NVOICE 6
#define MAXT 16
#define MAXS 40

int snd_rate = 22050;
int snd_music_on = 1, snd_sfx_on = 1;

static void *sf;
static int ntr, nsf, ready;
static unsigned toff[MAXT], tlen[MAXT], tloop[MAXT], tbytes[MAXT];
static short *sfxd[MAXS];
static int sfxn[MAXS];

/* voices */
static struct { const short *d; int n, pos, vol; } V[NVOICE];

/* music state (touched under lock) */
static unsigned char *mdata;      /* adpcm bytes of the current track */
static int mtrack = -1, mtotal, mloop, mblock, mpos, mfade, mfadeout;
static unsigned char *mnext; static int mnext_track = -1;
static short mbuf[1024];

static const short STEP[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166,
    1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
    8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
static const signed char IDXT[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

static unsigned rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

static void decode_block(int b)
{
    const unsigned char *p = mdata + b * 516;
    int pred = (short)(p[0] | p[1] << 8), idx = p[2], i;
    if (idx > 88) idx = 88;
    mbuf[0] = (short)pred;
    for (i = 1; i < 1024; i++) {
        int byte = p[4 + ((i - 1) >> 1)], code = ((i - 1) & 1) ? byte >> 4 : byte & 15;
        int step = STEP[idx], dq = step >> 3;
        if (code & 4) dq += step;
        if (code & 2) dq += step >> 1;
        if (code & 1) dq += step >> 2;
        pred = (code & 8) ? pred - dq : pred + dq;
        if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
        idx += IDXT[code & 7];
        if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
        mbuf[i] = (short)pred;
    }
}

void audio_init(void)
{
    unsigned char h[12], *t;
    int i, n;
    sf = plat_fopen(L"abyss_snd.dat");
    if (!sf) return;
    if (plat_fread(sf, h, 12) != 12 || memcmp(h, "ASND", 4)) { plat_fclose(sf); sf = 0; return; }
    ntr = h[6] | h[7] << 8; nsf = h[8] | h[9] << 8; snd_rate = h[10] | h[11] << 8;
    if (ntr > MAXT) ntr = MAXT;
    if (nsf > MAXS) nsf = MAXS;
    n = ntr * 16 + nsf * 8;
    t = (unsigned char *)malloc(n);
    if (!t) return;
    plat_fread(sf, t, n);
    for (i = 0; i < ntr; i++) { toff[i] = rd32(t + i * 16); tlen[i] = rd32(t + i * 16 + 4); tloop[i] = rd32(t + i * 16 + 8); tbytes[i] = rd32(t + i * 16 + 12); }
    for (i = 0; i < nsf; i++) {
        unsigned off = rd32(t + ntr * 16 + i * 8), len = rd32(t + ntr * 16 + i * 8 + 4);
        sfxd[i] = (short *)malloc(len * 2);
        if (sfxd[i]) {
            plat_fseek(sf, (long)off);
            plat_fread(sf, sfxd[i], (int)len * 2);
            sfxn[i] = (int)len;
        }
    }
    free(t);
    ready = 1;
}

void snd_play_v(int id, int vol)
{
    int i, best = 0, bp = -1;
    if (!ready || !snd_sfx_on || id < 0 || id >= nsf || !sfxd[id]) return;
    plat_audio_lock();
    for (i = 0; i < NVOICE; i++) {
        if (!V[i].d) { best = i; bp = 1 << 30; break; }
        if (V[i].pos > bp) { bp = V[i].pos; best = i; }   /* steal the oldest */
    }
    V[best].d = sfxd[id]; V[best].n = sfxn[id]; V[best].pos = 0; V[best].vol = vol;
    plat_audio_unlock();
}
void snd_play(int id) { snd_play_v(id, 256); }

/* load a track (main thread); the mixer swaps it in after fading out the old one */
void snd_music(int tr)
{
    unsigned char *d;
    if (!ready) return;
    if (!snd_music_on) tr = -1;
    if (tr == mtrack && !mnext) return;
    if (tr == mnext_track && mnext) return;
    d = 0;
    if (tr >= 0 && tr < ntr) {
        d = (unsigned char *)malloc(tbytes[tr]);
        if (d) { plat_fseek(sf, (long)toff[tr]); plat_fread(sf, d, (int)tbytes[tr]); }
        else tr = -1;
    } else tr = -1;
    plat_audio_lock();
    if (mnext) free(mnext);
    mnext = d; mnext_track = tr;
    if (!mnext) {           /* stop */
        mnext_track = -1;
        mfadeout = 1;
    }
    mfadeout = 1;
    plat_audio_unlock();
}

int snd_cur_music(void) { return mnext ? mnext_track : mtrack; }

void audio_mix(short *out, int n)
{
    int i, k;
    static int acc[2048];
    if (n > 2048) n = 2048;
    memset(acc, 0, n * sizeof(int));
    plat_audio_lock();
    /* music */
    if (mfadeout) {
        if (!mdata || mfade <= 0) {
            if (mdata) free(mdata);
            mdata = mnext; mtrack = mnext_track; mnext = 0; mnext_track = -1;
            mfadeout = 0; mfade = 0;
            if (mdata) { mtotal = (int)tlen[mtrack]; mloop = tloop[mtrack] == 0xFFFFFFFFu ? -1 : (int)tloop[mtrack]; mblock = 0; mpos = 0; decode_block(0); }
        }
    }
    if (mdata) {
        for (i = 0; i < n; i++) {
            int s;
            if (mpos >= mtotal) {
                if (mloop < 0) { free(mdata); mdata = 0; mtrack = -2; break; }
                mpos = mloop; mblock = -1;
            }
            if ((mpos >> 10) != mblock) { mblock = mpos >> 10; decode_block(mblock); }
            s = mbuf[mpos & 1023];
            mpos++;
            if (mfadeout) { if (mfade > 0) mfade -= 2; } else if (mfade < 8192) mfade++;
            acc[i] += (s * (mfade >> 4)) >> 10;     /* music sits at ~half volume */
        }
    }
    /* effects */
    for (k = 0; k < NVOICE; k++) if (V[k].d) {
        const short *d = V[k].d;
        int p = V[k].pos, e = V[k].n, v = V[k].vol;
        for (i = 0; i < n && p < e; i++, p++) acc[i] += (d[p] * v) >> 8;
        V[k].pos = p;
        if (p >= e) V[k].d = 0;
    }
    plat_audio_unlock();
    for (i = 0; i < n; i++) {
        int s = acc[i];
        if (s > 24000) s = 24000 + ((s - 24000) >> 2);          /* soft knee */
        else if (s < -24000) s = -24000 + ((s + 24000) >> 2);
        if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
        out[i] = (short)s;
    }
}
