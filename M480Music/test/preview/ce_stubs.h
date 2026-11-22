/* PC(와인) 화면 미리보기용: WinCE 전용 API 대체 */
#ifndef CE_STUBS_H
#define CE_STUBS_H
#include <windows.h>
#define WinMain wWinMain
static __inline void SystemIdleTimerReset(void) {}
#define GetProcAddressW(h, n) ((FARPROC)0)
#endif
