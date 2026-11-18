/* dsp.c - 정수 기반 출력 체인 (FPU 없는 ARM 대상) */
#include <string.h>
#include "dsp.h"

typedef long long i64;

/* 초기화 때 한 번만 쓰는 간단한 삼각/지수 함수 (libm 없이) */
static double my_sin(double x)
{
    double term = x, sum = x; int i;
    for (i = 1; i < 12; i++) { term *= -x * x / ((2 * i) * (2 * i + 1)); sum += term; }
    return sum;
}
static double my_cos(double x) { return my_sin(x + 1.5707963267948966); }
static double my_exp_neg(double w)
{
    double term = 1.0, sum = 1.0; int i;
    for (i = 1; i < 30; i++) { term *= w / i; sum += term; }
    return 1.0 / sum;
}

/* 2차 버터워스 LPF 계수(Q14)를 lpf 상태에 저장 */
/* 4차 버터워스 LPF = 바이쿼드 2단 (계수 Q14). dsp 인스턴스는 하나뿐이라 정적 저장 */
typedef struct { int b0, b1, b2, a1, a2; } Biq;
static Biq g_biq[2];
static int bx1[2][2], bx2[2][2], by1[2][2], by2[2][2]; /* [단][채널] */

static int rnd(double v) { return (int)(v < 0 ? v - 0.5 : v + 0.5); }

static void calc_lpf(int fs, int fc)
{
    static const double qs[2] = { 0.5411961001461970, 1.3065629648763766 };
    double w0 = 2.0 * 3.141592653589793 * fc / fs;
    double cs = my_cos(w0), sn = my_sin(w0);
    int k;
    for (k = 0; k < 2; k++) {
        double alpha = sn / (2.0 * qs[k]);
        double a0 = 1.0 + alpha;
        g_biq[k].b0 = rnd(((1.0 - cs) / 2.0) / a0 * 16384.0);
        g_biq[k].b1 = rnd((1.0 - cs) / a0 * 16384.0);
        g_biq[k].b2 = g_biq[k].b0;
        g_biq[k].a1 = rnd((-2.0 * cs) / a0 * 16384.0);
        g_biq[k].a2 = rnd((1.0 - alpha) / a0 * 16384.0);
    }
}

void dsp_init(Dsp *d, int inRate, int inCh, const DspParams *p)
{
    int fc;
    memset(d, 0, sizeof(*d));
    memset(bx1, 0, sizeof(bx1)); memset(bx2, 0, sizeof(bx2));
    memset(by1, 0, sizeof(by1)); memset(by2, 0, sizeof(by2));
    d->inRate = inRate;
    d->inCh = inCh;
    d->outRate = p->outRate > 0 ? p->outRate : inRate;
    d->outCh = p->outCh;
    d->step = (int)(((i64)d->inRate << 16) / d->outRate);
    d->pos = 0;

    if (p->hpfHz > 0) {
        double a = my_exp_neg(2.0 * 3.141592653589793 * p->hpfHz / inRate);
        d->hpfA = (int)(a * 32768.0);
    }
    fc = p->lpfHz;
    if (fc <= 0 && d->outRate < d->inRate) fc = d->outRate * 40 / 100;   /* 22050 → 약 8.8kHz */
    if (fc > 0 && fc < inRate * 45 / 100) {
        calc_lpf(inRate, fc);
        d->lpfA = 1;
    }
    d->gainQ12 = p->levelPct * 4096 / 100;
    d->ceiling = 29200;               /* 약 -1dBFS */
    d->limGainQ15 = 32768;
    d->limRelease = 1 + (11025 * 4 / d->outRate);
    d->fadeQ15 = 0;
    d->fadeStep = 32768 / (d->outRate / 50 + 1) + 1; /* 약 20ms */ /* 출력 레이트와 무관하게 비슷한 회복 속도 */
}

/* 입력 한 프레임을 필터에 통과시켜 nc 채널 값으로 */
static void feed(Dsp *d, const short *fr, int *dst)
{
    int c, nc;
    int x[2];
    if (d->outCh == 1 && d->inCh == 2) { x[0] = (fr[0] + fr[1]) >> 1; nc = 1; }
    else { x[0] = fr[0]; x[1] = (d->inCh == 2) ? fr[1] : fr[0]; nc = d->inCh; }

    for (c = 0; c < nc; c++) {
        int v = x[c];
        if (d->hpfA) {
            /* y = a*(y1 + x - x1) */
            int y = (int)(((i64)d->hpfA * (d->hpfY[c] + v - d->hpfX[c])) >> 15);
            d->hpfX[c] = v;
            d->hpfY[c] = y;
            v = y;
        }
        if (d->lpfA) {
            int k;
            for (k = 0; k < 2; k++) {
                const Biq *q = &g_biq[k];
                i64 acc = (i64)q->b0 * v + (i64)q->b1 * bx1[k][c] + (i64)q->b2 * bx2[k][c]
                        - (i64)q->a1 * by1[k][c] - (i64)q->a2 * by2[k][c];
                int y = (int)(acc >> 14);
                bx2[k][c] = bx1[k][c]; bx1[k][c] = v;
                by2[k][c] = by1[k][c]; by1[k][c] = y;
                v = y;
            }
        }
        if (v > 60000) v = 60000; else if (v < -60000) v = -60000;
        dst[c] = v;
    }
    if (nc == 1) dst[1] = dst[0];
}

int dsp_process(Dsp *d, const short *in, int inFrames, int *used, short *out, int maxOut)
{
    int u = 0, o = 0;
    int nc = (d->outCh == 1) ? 1 : d->inCh;
    (void)nc;
    if (!d->primed) {
        if (inFrames <= 0) { *used = 0; return 0; }
        feed(d, in, d->cur);
        d->prev[0] = d->cur[0]; d->prev[1] = d->cur[1];
        u = 1;
        d->primed = 1;
        d->pos = 0;
    }
    while (o < maxOut) {
        int s0, s1, peak, lim;
        int fq;
        while (d->pos >= 65536) {
            if (u >= inFrames) goto done;
            d->prev[0] = d->cur[0]; d->prev[1] = d->cur[1];
            feed(d, in + u * d->inCh, d->cur);
            u++;
            d->pos -= 65536;
        }
        fq = d->pos >> 2;  /* Q14 */
        s0 = d->prev[0] + (((d->cur[0] - d->prev[0]) * fq) >> 14);
        s1 = d->prev[1] + (((d->cur[1] - d->prev[1]) * fq) >> 14);
        s0 = (s0 * d->gainQ12) >> 12;
        s1 = (s1 * d->gainQ12) >> 12;
        if (d->fadeQ15 < 32768) {
            s0 = (s0 * d->fadeQ15) >> 15;
            s1 = (s1 * d->fadeQ15) >> 15;
            d->fadeQ15 += d->fadeStep;
            if (d->fadeQ15 > 32768) d->fadeQ15 = 32768;
        }

        /* 리미터: 즉시 감쇠, 천천히 회복 */
        peak = s0 < 0 ? -s0 : s0;
        { int p1 = s1 < 0 ? -s1 : s1; if (p1 > peak) peak = p1; }
        lim = d->limGainQ15;
        if (((peak * (i64)lim) >> 15) > d->ceiling) {
            lim = (int)(((i64)d->ceiling << 15) / peak);
            d->clipCount++;
        } else if (lim < 32768) {
            lim += d->limRelease;
            if (lim > 32768) lim = 32768;
        }
        d->limGainQ15 = lim;
        if (lim != 32768) {
            s0 = (int)(((i64)s0 * lim) >> 15);
            s1 = (int)(((i64)s1 * lim) >> 15);
        }
        if (s0 > 32767) s0 = 32767; else if (s0 < -32768) s0 = -32768;
        if (s1 > 32767) s1 = 32767; else if (s1 < -32768) s1 = -32768;

        if (d->outCh == 1) {
            out[o] = (short)s0;
        } else {
            out[2 * o] = (short)s0;
            out[2 * o + 1] = (short)s1;
        }
        o++;
        d->pos += d->step;
    }
done:
    *used = u;
    return o;
}
