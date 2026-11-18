/* force-included when building FFmpeg for Windows CE */
#ifndef CE_COMPAT_H
#define CE_COMPAT_H
#ifndef __ASSEMBLER__
#include "errno.h"
/* file-system helpers referenced by libavformat/os_support.h (never called: no file protocol) */
#define _wunlink(f) (-1)
#define _wmkdir(f) (-1)
#define _wrmdir(f) (-1)
#define _waccess(f, m) (-1)
#define _wstati64(f, s) ((void)(s), -1)
#define _stati64(f, s) ((void)(s), -1)
#define _wsopen(...) (-1)
#define _sopen(...) (-1)
long ce_clock(void);
#define clock() ce_clock()
#endif /* __ASSEMBLER__ */
#endif
