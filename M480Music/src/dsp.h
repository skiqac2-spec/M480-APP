/* dsp.h - M480 안전 출력 체인
 *  저음 컷(HPF) → 안티앨리어싱 LPF → 리샘플 → 레벨 낮춤 → 리미터 → (모노화)
 *  전부 정수 연산 */
#ifndef M480_DSP_H
#define M480_DSP_H

typedef struct Dsp {
    int inRate, inCh, outRate, outCh;
    int step;             /* Q16 입력 증가량 (inRate/outRate) */
    int pos;              /* Q16 보간 위치 */
    int prev[2], cur[2];  /* 보간용 두 샘플 */
    int primed;

    int hpfA;             /* Q15, 0이면 사용 안 함 */
    int hpfX[2], hpfY[2];
    int lpfA;             /* Q15, 0이면 사용 안 함 */
    int lpf1[2], lpf2[2];

    int gainQ12;          /* 출력 레벨 (4096=100%) */
    int ceiling;          /* 리미터 상한 */
    int limGainQ15;       /* 현재 리미터 감쇠량 */
    int limRelease;
    unsigned long clipCount;
    int fadeQ15, fadeStep;  /* 시작/탐색 시 '틱' 소리 방지용 페이드인 */
} Dsp;

typedef struct DspParams {
    int outRate;
    int outCh;      /* 1 or 2 */
    int levelPct;   /* 출력 레벨 % */
    int hpfHz;      /* 0 = 끔 */
    int lpfHz;      /* 0 = 자동(출력 레이트 기준) */
} DspParams;

void dsp_init(Dsp *d, int inRate, int inCh, const DspParams *p);
/* in: inFrames 개 프레임. out: 최대 maxOut 프레임.
 * 소비한 입력 프레임 수를 *used에 넣고, 만든 출력 프레임 수를 반환 */
int dsp_process(Dsp *d, const short *in, int inFrames, int *used, short *out, int maxOut);

#endif
