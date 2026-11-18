/* source.h - 음악 파일 디코더(MP3: Helix 고정소수점, WAV: PCM) */
#ifndef M480_SOURCE_H
#define M480_SOURCE_H

#include "fileio.h"

#define SRC_NONE 0
#define SRC_MP3  1
#define SRC_WAV  2

#define SRC_INBUF_SIZE (64 * 1024)

typedef struct Source {
    int type;
    MFile *f;
    unsigned long fileSize;
    unsigned long dataStart;   /* ID3/RIFF 헤더 이후 오디오 데이터 시작 */
    unsigned long dataEnd;     /* ID3v1(TAG) 이전 / WAV data 청크 끝 */
    unsigned long filePos;     /* 다음에 읽을 파일 위치 */

    int rate;                  /* 원본 샘플레이트 */
    int channels;              /* 원본 채널 수(1/2) */
    int bitrate;               /* kbps (MP3) */
    unsigned long durationMs;  /* 추정 길이 */

    /* MP3 */
    void *mp3;
    unsigned char *inbuf;
    unsigned char *readPtr;
    int bytesLeft;
    int eofInput;
    int errorRun;

    /* WAV */
    int wavBits;
    int wavBlockAlign;

    /* 디코딩된 PCM (인터리브) */
    short pcm[1152 * 2];
    int pcmFrames;
    int pcmPos;

    unsigned long framesDecoded; /* 현재 위치(원본 샘플레이트 기준 프레임) */
} Source;

/* 성공 시 1. 실패 시 0 */
int  src_open(Source *s, const mchar *path);
void src_close(Source *s);
/* 인터리브 short로 최대 maxFrames 프레임을 꺼냄. 0이면 곡 끝 */
int  src_read(Source *s, short *dst, int maxFrames);
/* 위치 이동(밀리초). 성공 시 1 */
int  src_seek_ms(Source *s, unsigned long ms);
unsigned long src_pos_ms(Source *s);

#endif
