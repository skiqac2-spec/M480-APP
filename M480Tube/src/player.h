/* M480Tube - video player (FFmpeg decode, waveOut audio, GDI output) */
#ifndef M480_PLAYER_H
#define M480_PLAYER_H

#include <windows.h>
#include "yt.h"

typedef struct Player Player;

enum { PS_LOADING, PS_PLAYING, PS_PAUSED, PS_BUFFERING, PS_ENDED, PS_ERROR };
enum { ORI_PORTRAIT, ORI_LAND_LEFT, ORI_LAND_RIGHT };
/* result of player_tap */
enum { TAP_NONE, TAP_BACK };

/* takes ownership of nothing; copies what it needs from s */
Player *player_open(HWND hwnd, const YtStreams *s, int orientation, int show_stats);
void    player_close(Player *p);
void    player_paint(Player *p, HDC dc);       /* WM_PAINT */
void    player_resize(Player *p);              /* WM_SIZE */
int     player_tap(Player *p, int x, int y);   /* WM_LBUTTONDOWN, screen coords */
void    player_drag(Player *p, int x, int y, int up); /* WM_MOUSEMOVE/LBUTTONUP while seeking */
void    player_key(Player *p, int vk);
void    player_tick(Player *p);                /* ~4x per second from UI timer */
int     player_orientation(Player *p);
#ifndef _WIN32_WCE
void    player_debug(Player *p, const char *what, long arg);
#endif

#endif
