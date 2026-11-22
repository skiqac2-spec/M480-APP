#ifndef AUDIO_H
#define AUDIO_H
#include "types.h"
#define AUDIO_RATE 22050
int snd_load(const u8 *d, u32 len);       /* 0 = unsupported */
void snd_play(int h, int loops);          /* loops: 1 once, -1 forever */
void snd_stop(int h);
void snd_pause(int h, int paused);
void snd_volume(int h, int v);            /* 0..100 */
int snd_playing(int h);
int snd_position_ms(int h);
int snd_duration_ms(int h);
void snd_free(int h);
void snd_tone(int note, int ms, int vol);
void snd_stop_all(void);
void snd_set_master(int v);               /* 0..100 */
void audio_render(s16 *out, int n);       /* called by the platform audio thread */
/* platform lock (pal) */
void pal_audio_lock(void);
void pal_audio_unlock(void);
#endif
