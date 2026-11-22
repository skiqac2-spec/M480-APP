/* PC test: drive the launcher UI and dump frames */
#include "../src/ui.h"
#include "../src/host.h"
#include "../src/pal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
void pal_audio_lock(void) {}
void pal_audio_unlock(void) {}
static Surf *frame;
static void shot(const char *fn)
{
    FILE *f = fopen(fn, "wb");
    int i, n = frame->w * frame->h;
    fprintf(f, "P6 %d %d 255\n", frame->w, frame->h);
    for (i = 0; i < n; i++) { u32 c = c888(frame->px[i]); fputc(c >> 16, f); fputc((c >> 8) & 255, f); fputc(c & 255, f); }
    fclose(f);
}
int main(int argc, char **argv)
{
    char line[256];
    FILE *sc = fopen(argv[2], "r");
    frame = surf_alloc(320, 320, 0);
    ui_init(320, 320, argv[1], NULL, 0, 0, 0);
    while (fgets(line, sizeof line, sc)) {
        char cmd[32], arg[200];
        int i;
        if (sscanf(line, "%31s %199s", cmd, arg) < 1) continue;
        if (!strcmp(cmd, "key")) { int vk = (int)strtol(arg, NULL, 0); ui_key(vk, 1, 0); ui_key(vk, 0, 0); }
        else if (!strcmp(cmd, "tap")) { int x, y; sscanf(arg, "%d,%d", &x, &y); ui_tap(x, y, 1, 0); ui_tap(x, y, 0, 0); }
        else if (!strcmp(cmd, "run")) { int ms = atoi(arg); for (i = 0; i < ms / 20; i++) { ui_tick(); usleep(20000); } }
        else if (!strcmp(cmd, "shot")) { ui_tick(); ui_draw(frame); shot(arg); printf("shot %s\n", arg); }
        if (ui_quit()) { printf("quit\n"); break; }
    }
    return 0;
}
