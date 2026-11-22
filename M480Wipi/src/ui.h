#ifndef UI_H
#define UI_H
#include "gfx.h"

/* virtual-key codes used by the UI (same values as Windows VK_*) */
#define VK_BACK_   0x08
#define VK_TAB_    0x09
#define VK_RETURN_ 0x0D
#define VK_ESCAPE_ 0x1B
#define VK_SPACE_  0x20
#define VK_LEFT_   0x25
#define VK_UP_     0x26
#define VK_RIGHT_  0x27
#define VK_DOWN_   0x28
#define VK_F1_     0x70
#define VK_F2_     0x71
#define VK_F3_     0x72
#define VK_F4_     0x73

void ui_init(int fw, int fh, const char *appdir, const char *start_game, int start_carrier, int start_w, int start_h);
void ui_key(int vk, int down, int repeat);
void ui_tap(int x, int y, int down, int drag);
int ui_tick(void);          /* 1 = frame needs presenting */
void ui_draw(Surf *frame);
int ui_idle_ms(void);
int ui_quit(void);          /* 1 = close the program */
/* when a game was chosen the front-end restarts the program with these arguments */
int ui_restart(char *game, int max, int *carrier, int *w, int *h);
void ui_pause(int paused);
#endif
