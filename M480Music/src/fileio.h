/* fileio.h - WinCE(유니코드 경로) / 호스트 테스트 겸용 파일 입출력 */
#ifndef M480_FILEIO_H
#define M480_FILEIO_H

#ifdef _WIN32_WCE
#include <windows.h>
typedef wchar_t mchar;
#else
typedef char mchar;
#endif

typedef struct MFile MFile;

MFile *mf_open(const mchar *path);
void   mf_close(MFile *f);
unsigned long mf_size(MFile *f);
int    mf_seek(MFile *f, unsigned long pos);
/* 읽은 바이트 수 반환 (0 = 끝/오류) */
int    mf_read(MFile *f, void *buf, int n);

#endif
