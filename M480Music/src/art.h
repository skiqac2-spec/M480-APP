/* art.h - 앨범아트/곡 정보(ID3) 읽기 */
#ifndef M480_ART_H
#define M480_ART_H

#include <windows.h>

typedef struct ArtResult {
    int gen;                    /* 요청 번호 (오래된 결과 무시용) */
    wchar_t title[128];
    wchar_t artist[128];
    wchar_t album[128];
    int bigSz, smallSz;             /* 정사각 크기(px) */
    unsigned short *bigPx;      /* RGB565, NULL이면 아트 없음 */
    unsigned short *smallPx;
    COLORREF avg;               /* 배경 색조용 평균색 */
} ArtResult;

/* path의 태그/커버를 읽어 ArtResult(malloc)를 만든다. 실패해도 NULL이 아닌 결과를 반환 */
ArtResult *art_load(const wchar_t *path, int bigSz, int smallSz);
void art_free(ArtResult *r);

#endif
