/* glue between the VM API natives and the running front-end */
#ifndef HOST_H
#define HOST_H
#include "types.h"
#include "gfx.h"

enum { CARRIER_SKT = 0, CARRIER_KTF = 1, CARRIER_LGT = 2 };

/* phone key ids (match m480.Keys) */
enum { PK_0 = 0, PK_STAR = 10, PK_POUND, PK_UP, PK_DOWN, PK_LEFT, PK_RIGHT, PK_FIRE, PK_SOFT1, PK_SOFT2,
       PK_CLEAR, PK_SEND, PK_END, PK_VOLUP, PK_VOLDN, PK_COUNT };

/* event types (match m480.Sys) */
enum { EV_KEY_PRESS = 1, EV_KEY_RELEASE = 2, EV_KEY_REPEAT = 3, EV_PTR_PRESS = 4, EV_PTR_RELEASE = 5, EV_PTR_DRAG = 6,
       EV_REPAINT = 10, EV_SERIAL = 11, EV_PAUSE = 20, EV_RESUME = 21, EV_QUIT = 22 };

typedef struct Host {
    Surf *screen;            /* game screen surface (handle 1) */
    int carrier;
    char savedir[512];       /* per-game save folder */
    int quit_requested;      /* app called notifyDestroyed / System.exit */
    char fail[256];          /* start failure message */
    int dirty;               /* screen changed since last present */
    void (*flush)(void);
    void (*vibrate)(int ms);
    void (*backlight)(int on);
} Host;
extern Host host;

void ev_push(int type, int a, int b, int c);
int ev_pending(void);
void ev_clear(void);
void surf_set_screen(Surf *s);
void api_reset(void);
#endif
