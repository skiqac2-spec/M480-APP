/* source.c - MP3(Helix 고정소수점) / WAV 디코더
 * M480 같은 FPU 없는 ARM에서도 가볍게 돌도록 정수 연산만 사용한다. */
#include <stdlib.h>
#include <string.h>
#include "source.h"
#include "helix/mp3dec.h"

typedef long long i64;

static unsigned long rd32le(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned long)p[3] << 24); }
static unsigned rd16le(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned long rd32be(const unsigned char *p) { return ((unsigned long)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

/* Xing TOC (VBR 탐색용) */
typedef struct { int has; unsigned char toc[100]; } XingToc;
static XingToc *xing_of(Source *s) { return (XingToc *)(s->inbuf + SRC_INBUF_SIZE); }

/* ---------------------------------------------------------------- MP3 */

static void mp3_reset_input(Source *s)
{
    s->readPtr = s->inbuf;
    s->bytesLeft = 0;
    s->eofInput = 0;
    s->pcmFrames = s->pcmPos = 0;
}

static void mp3_refill(Source *s)
{
    int space, want, got;
    if (s->eofInput) return;
    if (s->bytesLeft > 0 && s->readPtr != s->inbuf)
        memmove(s->inbuf, s->readPtr, (size_t)s->bytesLeft);
    s->readPtr = s->inbuf;
    space = SRC_INBUF_SIZE - s->bytesLeft;
    want = space;
    if (s->filePos + (unsigned long)want > s->dataEnd)
        want = (int)(s->dataEnd - s->filePos);
    got = want > 0 ? mf_read(s->f, s->inbuf + s->bytesLeft, want) : 0;
    if (got < 0) got = 0;
    s->filePos += (unsigned long)got;
    s->bytesLeft += got;
    if (got < space) {
        s->eofInput = 1;
        /* 마지막 프레임이 끝까지 디코딩되도록 0 패딩(헬릭스는 약간 더 읽을 수 있음) */
        if (s->bytesLeft + 8 <= SRC_INBUF_SIZE) memset(s->inbuf + s->bytesLeft, 0, 8);
    }
}

static int frame_samples(const MP3FrameInfo *fi)
{
    return fi->version == 0 ? 1152 : 576; /* MPEG1 : MPEG2/2.5 */
}

/* 첫 프레임에서 Xing/Info/VBRI 헤더를 찾아 길이 계산 */
static void mp3_parse_vbr(Source *s, const unsigned char *fr, int avail, const MP3FrameInfo *fi)
{
    XingToc *xt = xing_of(s);
    int side, off;
    unsigned long frames = 0;
    xt->has = 0;
    if (fi->version == 0) side = (fi->nChans == 1) ? 17 : 32;
    else side = (fi->nChans == 1) ? 9 : 17;
    off = 4 + side;
    if (off + 120 < avail && (!memcmp(fr + off, "Xing", 4) || !memcmp(fr + off, "Info", 4))) {
        unsigned long flags = rd32be(fr + off + 4);
        const unsigned char *p = fr + off + 8;
        if (flags & 1) { frames = rd32be(p); p += 4; }
        if (flags & 2) { p += 4; }
        if ((flags & 4) && !memcmp(fr + off, "Xing", 4)) { memcpy(xt->toc, p, 100); xt->has = 1; }
    } else if (36 + 18 < avail && !memcmp(fr + 36, "VBRI", 4)) {
        frames = rd32be(fr + 36 + 14);
    }
    if (frames > 0 && fi->samprate > 0)
        s->durationMs = (unsigned long)((i64)frames * frame_samples(fi) * 1000 / fi->samprate);
}

static int mp3_open(Source *s)
{
    MP3FrameInfo fi;
    int tries = 0;
    s->mp3 = MP3InitDecoder();
    if (!s->mp3) return 0;
    mf_seek(s->f, s->dataStart);
    s->filePos = s->dataStart;
    mp3_reset_input(s);
    mp3_refill(s);

    /* 연속된 두 프레임 헤더가 일치하는 지점을 진짜 시작으로 본다 */
    while (tries++ < 64) {
        int off = MP3FindSyncWord(s->readPtr, s->bytesLeft);
        if (off < 0) return 0;
        s->readPtr += off; s->bytesLeft -= off;
        if (s->bytesLeft > 4 && MP3GetNextFrameInfo(s->mp3, &fi, s->readPtr) == ERR_MP3_NONE &&
            fi.layer == 3 && fi.bitrate > 0 && (fi.nChans == 1 || fi.nChans == 2)) {
            int flen = (fi.version == 0 ? 144 : 72) * fi.bitrate / fi.samprate;
            MP3FrameInfo f2;
            int ok = 1;
            if (flen + 4 < s->bytesLeft) {
                /* 패딩 비트 고려해 앞뒤 1바이트 확인 */
                int k; ok = 0;
                for (k = 0; k <= 1 && !ok; k++) {
                    unsigned char *n = s->readPtr + flen + k;
                    if (n[0] == 0xFF && (n[1] & 0xE0) == 0xE0 &&
                        MP3GetNextFrameInfo(s->mp3, &f2, n) == ERR_MP3_NONE &&
                        f2.samprate == fi.samprate) ok = 1;
                }
            }
            if (ok) {
                s->rate = fi.samprate;
                s->channels = fi.nChans;
                s->bitrate = fi.bitrate / 1000;
                s->dataStart = s->dataStart + (unsigned long)(s->readPtr - s->inbuf);
                mp3_parse_vbr(s, s->readPtr, s->bytesLeft, &fi);
                if (!s->durationMs && s->bitrate > 0)
                    s->durationMs = (unsigned long)((i64)(s->dataEnd - s->dataStart) * 8 / s->bitrate);
                return 1;
            }
        }
        s->readPtr++; s->bytesLeft--;
    }
    return 0;
}

/* 프레임 하나 디코딩 → s->pcm (채널 수는 s->channels로 맞춤). 0이면 끝 */
static int mp3_decode_frame(Source *s)
{
    MP3FrameInfo fi;
    int guard = 0;
    for (;;) {
        int off, err;
        if (++guard > 20000) return 0;
        if (s->bytesLeft < 4096 && !s->eofInput) mp3_refill(s);
        if (s->bytesLeft <= 4) return 0;
        off = MP3FindSyncWord(s->readPtr, s->bytesLeft);
        if (off < 0) {
            /* 마지막 몇 바이트는 남겨 두고 버림 */
            if (s->eofInput) return 0;
            s->readPtr += s->bytesLeft - 3; s->bytesLeft = 3;
            mp3_refill(s);
            continue;
        }
        s->readPtr += off; s->bytesLeft -= off;

        /* 다른 샘플레이트의 가짜 싱크는 건너뜀 */
        if (MP3GetNextFrameInfo(s->mp3, &fi, s->readPtr) != ERR_MP3_NONE ||
            fi.layer != 3 || fi.samprate != s->rate) {
            s->readPtr++; s->bytesLeft--;
            continue;
        }

        err = MP3Decode(s->mp3, &s->readPtr, &s->bytesLeft, s->pcm, 0);
        if (err == ERR_MP3_NONE) {
            int n;
            MP3GetLastFrameInfo(s->mp3, &fi);
            if (fi.nChans < 1 || fi.outputSamps <= 0) continue;
            n = fi.outputSamps / fi.nChans;
            if (fi.nChans != s->channels) {
                int i;
                if (fi.nChans == 1) {       /* mono → stereo */
                    for (i = n - 1; i >= 0; i--) { s->pcm[2 * i] = s->pcm[2 * i + 1] = s->pcm[i]; }
                } else {                    /* stereo → mono */
                    for (i = 0; i < n; i++) s->pcm[i] = (short)((s->pcm[2 * i] + s->pcm[2 * i + 1]) >> 1);
                }
            }
            s->pcmFrames = n;
            s->pcmPos = 0;
            s->errorRun = 0;
            return 1;
        }
        if (err == ERR_MP3_INDATA_UNDERFLOW) {
            if (s->eofInput) return 0;
            if (s->bytesLeft >= SRC_INBUF_SIZE - 16) { s->readPtr++; s->bytesLeft--; }
            mp3_refill(s);
            continue;
        }
        if (err == ERR_MP3_MAINDATA_UNDERFLOW) continue; /* 탐색 직후 비트 저장소 부족: 다음 프레임 */
        /* 손상된 프레임: 1바이트 밀고 재동기 */
        if (++s->errorRun > 2000) return 0;
        if (s->bytesLeft > 0) { s->readPtr++; s->bytesLeft--; }
    }
}

/* ---------------------------------------------------------------- WAV */

static int wav_open(Source *s, const unsigned char *hdr, int n)
{
    unsigned long pos = 12;
    int fmtOk = 0;
    unsigned char ch[24];
    (void)hdr; (void)n;
    while (pos + 8 <= s->fileSize) {
        unsigned long csz;
        mf_seek(s->f, pos);
        if (mf_read(s->f, ch, 8) != 8) break;
        csz = rd32le(ch + 4);
        if (!memcmp(ch, "fmt ", 4)) {
            unsigned fmt;
            if (mf_read(s->f, ch, 16) != 16) return 0;
            fmt = rd16le(ch);
            s->channels = (int)rd16le(ch + 2);
            s->rate = (int)rd32le(ch + 4);
            s->wavBlockAlign = (int)rd16le(ch + 12);
            s->wavBits = (int)rd16le(ch + 14);
            if (fmt != 1 && fmt != 0xFFFE) return 0;
            if (s->channels < 1 || s->channels > 2) return 0;
            if (s->wavBits != 8 && s->wavBits != 16 && s->wavBits != 24) return 0;
            if (s->rate < 4000 || s->rate > 96000) return 0;
            fmtOk = 1;
        } else if (!memcmp(ch, "data", 4)) {
            if (!fmtOk) return 0;
            s->dataStart = pos + 8;
            s->dataEnd = s->dataStart + csz;
            if (s->dataEnd > s->fileSize || csz == 0) s->dataEnd = s->fileSize;
            s->dataEnd -= (s->dataEnd - s->dataStart) % (unsigned long)s->wavBlockAlign;
            s->durationMs = (unsigned long)((i64)(s->dataEnd - s->dataStart) / s->wavBlockAlign * 1000 / s->rate);
            s->bitrate = s->rate * s->wavBlockAlign * 8 / 1000;
            mf_seek(s->f, s->dataStart);
            s->filePos = s->dataStart;
            return 1;
        }
        pos += 8 + csz + (csz & 1);
    }
    return 0;
}

static int wav_decode(Source *s)
{
    unsigned char raw[576 * 2 * 3];
    int want = (int)(sizeof(raw) / (unsigned)s->wavBlockAlign);
    unsigned long left = (s->dataEnd - s->filePos) / (unsigned long)s->wavBlockAlign;
    int got, frames, i, smp;
    if ((unsigned long)want > left) want = (int)left;
    if (want <= 0) return 0;
    got = mf_read(s->f, raw, want * s->wavBlockAlign);
    frames = got / s->wavBlockAlign;
    if (frames <= 0) return 0;
    s->filePos += (unsigned long)(frames * s->wavBlockAlign);
    smp = frames * s->channels;
    if (s->wavBits == 16) {
        for (i = 0; i < smp; i++) s->pcm[i] = (short)rd16le(raw + 2 * i);
    } else if (s->wavBits == 8) {
        for (i = 0; i < smp; i++) s->pcm[i] = (short)(((int)raw[i] - 128) << 8);
    } else {
        for (i = 0; i < smp; i++) s->pcm[i] = (short)rd16le(raw + 3 * i + 1);
    }
    s->pcmFrames = frames;
    s->pcmPos = 0;
    return 1;
}

/* ---------------------------------------------------------------- 공통 */

int src_open(Source *s, const mchar *path)
{
    unsigned char hdr[16];
    int n;
    memset(s, 0, sizeof(*s));
    s->f = mf_open(path);
    if (!s->f) return 0;
    s->fileSize = mf_size(s->f);
    s->dataEnd = s->fileSize;
    n = mf_read(s->f, hdr, 12);
    if (n == 12 && !memcmp(hdr, "RIFF", 4) && !memcmp(hdr + 8, "WAVE", 4)) {
        s->type = SRC_WAV;
        if (wav_open(s, hdr, n)) return 1;
        src_close(s);
        return 0;
    }

    /* MP3: ID3v2 건너뛰기 (앨범아트 속 가짜 싱크 방지) */
    s->inbuf = (unsigned char *)malloc(SRC_INBUF_SIZE + sizeof(XingToc) + 16);
    if (!s->inbuf) { src_close(s); return 0; }
    s->dataStart = 0;
    while (n >= 10 && !memcmp(hdr, "ID3", 3)) {
        unsigned long sz = ((unsigned long)(hdr[6] & 0x7F) << 21) | ((hdr[7] & 0x7F) << 14) |
                           ((hdr[8] & 0x7F) << 7) | (hdr[9] & 0x7F);
        s->dataStart += 10 + sz + ((hdr[5] & 0x10) ? 10 : 0);
        if (s->dataStart >= s->fileSize) break;
        mf_seek(s->f, s->dataStart);
        n = mf_read(s->f, hdr, 10);
    }
    /* ID3v1 */
    if (s->fileSize > 128) {
        unsigned char tag[3];
        mf_seek(s->f, s->fileSize - 128);
        if (mf_read(s->f, tag, 3) == 3 && !memcmp(tag, "TAG", 3)) s->dataEnd = s->fileSize - 128;
    }
    if (s->dataStart >= s->dataEnd) s->dataStart = 0;
    s->type = SRC_MP3;
    if (mp3_open(s)) return 1;
    src_close(s);
    return 0;
}

void src_close(Source *s)
{
    if (s->mp3) MP3FreeDecoder(s->mp3);
    if (s->inbuf) free(s->inbuf);
    if (s->f) mf_close(s->f);
    memset(s, 0, sizeof(*s));
}

int src_read(Source *s, short *dst, int maxFrames)
{
    int done = 0;
    while (done < maxFrames) {
        int avail, take;
        if (s->pcmPos >= s->pcmFrames) {
            int ok = (s->type == SRC_MP3) ? mp3_decode_frame(s) : wav_decode(s);
            if (!ok) break;
        }
        avail = s->pcmFrames - s->pcmPos;
        take = maxFrames - done;
        if (take > avail) take = avail;
        memcpy(dst + done * s->channels, s->pcm + s->pcmPos * s->channels,
               (size_t)(take * s->channels) * sizeof(short));
        s->pcmPos += take;
        done += take;
        s->framesDecoded += (unsigned long)take;
    }
    return done;
}

int src_seek_ms(Source *s, unsigned long ms)
{
    unsigned long target;
    if (!s->f) return 0;
    if (s->durationMs && ms >= s->durationMs) ms = s->durationMs > 1000 ? s->durationMs - 1000 : 0;
    if (s->type == SRC_WAV) {
        unsigned long fr = (unsigned long)((i64)ms * s->rate / 1000);
        target = s->dataStart + fr * (unsigned long)s->wavBlockAlign;
        if (target > s->dataEnd) target = s->dataEnd;
        mf_seek(s->f, target);
        s->filePos = target;
        s->pcmFrames = s->pcmPos = 0;
        s->framesDecoded = fr;
        return 1;
    } else {
        XingToc *xt = xing_of(s);
        unsigned long span = s->dataEnd - s->dataStart;
        if (!s->durationMs) return 0;
        if (xt->has) {
            /* Xing TOC: 0~99 퍼센트 지점의 바이트 위치(0~255) */
            int pctQ8 = (int)((i64)ms * 100 * 256 / s->durationMs);
            int a = pctQ8 >> 8, frac = pctQ8 & 255;
            int fa, fb;
            if (a > 99) { a = 99; frac = 255; }
            fa = xt->toc[a];
            fb = (a < 99) ? xt->toc[a + 1] : 256;
            target = s->dataStart + (unsigned long)((i64)span * (fa * 256 + (fb - fa) * frac) / (256 * 256));
        } else {
            target = s->dataStart + (unsigned long)((i64)span * ms / s->durationMs);
        }
        if (target >= s->dataEnd) target = s->dataStart;
        mf_seek(s->f, target);
        s->filePos = target;
        mp3_reset_input(s);
        mp3_refill(s);
        s->framesDecoded = (unsigned long)((i64)ms * s->rate / 1000);
        return 1;
    }
}

unsigned long src_pos_ms(Source *s)
{
    if (!s->rate) return 0;
    return (unsigned long)((i64)s->framesDecoded * 1000 / s->rate);
}
