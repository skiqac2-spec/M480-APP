/* PC test driver: run a game headless with a key script, save screenshots */
#include "../src/app.h"
#include "../src/host.h"
#include "../src/pal.h"
#include "../src/audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
void pal_audio_lock(void) {}
void pal_audio_unlock(void) {}
static int frames;
static void flush(void) { frames++; }
static void shot(const char *fn)
{
    FILE *f = fopen(fn, "wb");
    int i, n = host.screen->w * host.screen->h;
    fprintf(f, "P6 %d %d 255\n", host.screen->w, host.screen->h);
    for (i = 0; i < n; i++) { u32 c = c888(host.screen->px[i]); fputc(c >> 16, f); fputc((c >> 8) & 255, f); fputc(c & 255, f); }
    fclose(f);
    printf("[shot %s frames=%d]\n", fn, frames);
}
int main(int argc, char **argv)
{
    char err[256];
    u32 start;
    FILE *sc = argc > 3 ? fopen(argv[3], "r") : NULL;
    char line[256];
    int next_t = -1, done = 0;
    char cmd[64], arg[200];
    if (argc < 3) { fprintf(stderr, "main_host game carrier [script]\n"); return 1; }
    host.flush = flush;
    if (!app_start(argv[1], atoi(argv[2]), 0, 0, "/tmp/m480save", err, sizeof err)) { printf("START FAIL: %s\n", err); return 1; }
    start = pal_ms();
    while (!done) {
        u32 now = pal_ms() - start;
        static s16 abuf[512];
        if (!app_step(15)) { printf("[game ended at %u ms]\n", now); break; }
        audio_render(abuf, 330);
        while (sc && (next_t < 0 || (u32)next_t <= now)) {
            if (next_t >= 0) {
                if (!strcmp(cmd, "shot")) shot(arg);
                else if (!strcmp(cmd, "press")) ev_push(EV_KEY_PRESS, atoi(arg), 0, 0);
                else if (!strcmp(cmd, "release")) ev_push(EV_KEY_RELEASE, atoi(arg), 0, 0);
                else if (!strcmp(cmd, "tap")) { ev_push(EV_KEY_PRESS, atoi(arg), 0, 0); ev_push(EV_KEY_RELEASE, atoi(arg), 0, 0); }
                else if (!strcmp(cmd, "quit")) { done = 1; break; }
                next_t = -1;
            }
            if (!fgets(line, sizeof line, sc)) { fclose(sc); sc = NULL; break; }
            if (sscanf(line, "%d %63s %199s", &next_t, cmd, arg) < 2) next_t = -1;
        }
        if (!sc && !done && now > 20000) break;
        { int idle = app_idle_ms(); if (idle > 0) usleep((idle > 15 ? 15 : idle) * 1000); }
    }
    printf("frames=%d\n", frames);
    return 0;
}
