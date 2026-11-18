/* 심연의 미궁 (Abyss Labyrinth) - shared game/platform interface */
#ifndef GAME_H
#define GAME_H
#include <wchar.h>

typedef unsigned short u16;

#define GW_MAX 320
extern int GW;            /* logical width: 240 (portrait screens) .. 320 (square screens) */
extern int GH;            /* logical height (320..427) */
extern u16 *FB;           /* RGB565 framebuffer GW*GH, top-down */

enum { K_UP = 1, K_DOWN, K_LEFT, K_RIGHT, K_OK, K_BACK, K_MENU };

void game_init(int h, u16 *fb, unsigned seed);
void game_key(int k);
void game_tap(int x, int y);
int  game_tick(void);        /* called every 50ms, returns 1 if redraw needed */
void game_render(void);
int  game_wants_quit(void);
void game_suspend(void);     /* app closing: autosave */

/* ---- platform services ---- */
enum { T_LEFT = 0, T_CENTER = 1, T_RIGHT = 2, T_SHADOW = 4, T_BIG = 8 };
void plat_text(int x, int y, const wchar_t *s, u16 col, int flags);
int  plat_text_w(const wchar_t *s, int flags);
int  plat_save(const void *d, int n);
int  plat_load(void *d, int n);
void plat_delsave(void);
/* read-only data files next to the executable (monster art) */
void *plat_fopen(const wchar_t *name);
int  plat_fread(void *f, void *buf, int n);
int  plat_fseek(void *f, long off);
void plat_fclose(void *f);

/* ---- sound (audio.c) ---- */
extern int snd_rate, snd_music_on, snd_sfx_on;
void audio_init(void);
void audio_mix(short *out, int n);       /* called from the platform audio thread */
void snd_play(int id);
void snd_play_v(int id, int vol);        /* vol 0..256 */
void snd_music(int track);               /* -1 = silence */
void plat_audio_lock(void);
void plat_audio_unlock(void);

#endif
