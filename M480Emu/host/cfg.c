/* settings: plain text key=value file next to the executable */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host.h"

struct settings cfg;

static const char *bind_names[BIND_COUNT] = {
    "up", "down", "left", "right", "triangle", "circle", "cross", "square",
    "l1", "r1", "l2", "r2", "start", "select", "menu"
};

void cfg_defaults(void)
{
    static const int def[BIND_COUNT][2] = {
        { K_UP, 'W' }, { K_DOWN, 'S' }, { K_LEFT, 'A' }, { K_RIGHT, 'D' },
        { 'I', 0 }, { 'L', 0 }, { 'K', 0 }, { 'J', 0 },
        { 'U', 0 }, { 'O', 0 }, { 'Y', 0 }, { 'P', 0 },
        { K_ENTER, 0 }, { K_SPACE, 0 }, { K_BACK, K_ESC }
    };
    char keep_dir[HPATH], keep_game[HPATH];
    memcpy(keep_dir, cfg.last_dir, HPATH); memcpy(keep_game, cfg.last_game, HPATH);
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.last_dir, keep_dir, HPATH); memcpy(cfg.last_game, keep_game, HPATH);
    cfg.frameskip = 1;
    cfg.sound = 1;
    cfg.speed = 2;                 /* PlayStation: "fast" preset by default */
    cfg.show_fps = 1;
    cfg.use_bios = 1;
    cfg.cdda = 1;
    cfg.stretch = 1;
    cfg.touchpad = 1;
    cfg.touch_alpha = 1;
    cfg.touch_size = 0;
    memcpy(cfg.keys, def, sizeof(def));
}

static void cfg_path(char *out, int common)
{
    if (common) snprintf(out, HPATH, "%s%ccommon.cfg", plat_app_dir(), PSEP);
    else snprintf(out, HPATH, "%s%cemu_%s.cfg", plat_app_dir(), PSEP, sys.id);
}

static void load_file(int common)
{
    char path[HPATH], line[HPATH + 64], *v;
    FILE *f;
    int i, a, b;

    cfg_path(path, common);
    f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        v = strchr(line, '=');
        if (!v) continue;
        *v++ = 0;
        if (common) {
            if (!strcmp(line, "last_dir")) snprintf(cfg.last_dir, HPATH, "%s", v);
            else if (!strcmp(line, "last_game")) snprintf(cfg.last_game, HPATH, "%s", v);
            continue;
        }
        if (!strcmp(line, "frameskip")) cfg.frameskip = atoi(v);
        else if (!strcmp(line, "sound")) cfg.sound = atoi(v);
        else if (!strcmp(line, "speed")) cfg.speed = atoi(v);
        else if (!strcmp(line, "show_fps")) cfg.show_fps = atoi(v);
        else if (!strcmp(line, "region")) cfg.region = atoi(v);
        else if (!strcmp(line, "use_bios")) cfg.use_bios = atoi(v);
        else if (!strcmp(line, "cdda")) cfg.cdda = atoi(v);
        else if (!strcmp(line, "stretch")) cfg.stretch = atoi(v);
        else if (!strcmp(line, "slot")) cfg.slot = atoi(v);
        else if (!strcmp(line, "touchpad")) cfg.touchpad = atoi(v);
        else if (!strcmp(line, "touch_alpha")) cfg.touch_alpha = atoi(v);
        else if (!strcmp(line, "touch_size")) cfg.touch_size = atoi(v);
        else if (!strncmp(line, "key_", 4)) {
            for (i = 0; i < BIND_COUNT; i++)
                if (!strcmp(line + 4, bind_names[i]) && sscanf(v, "%d,%d", &a, &b) == 2) {
                    cfg.keys[i][0] = a; cfg.keys[i][1] = b;
                }
        }
    }
    fclose(f);
}

void cfg_load(void)
{
    cfg_defaults();
    load_file(1);
    if (!(sys.flags & SF_NOCORE)) load_file(0);
    if (cfg.frameskip < 0 || cfg.frameskip > 4) cfg.frameskip = 1;
    if (cfg.speed < 0 || cfg.speed > 3) cfg.speed = 1;
    if (cfg.region < 0 || cfg.region > 2) cfg.region = 0;
    if (cfg.slot < 0 || cfg.slot > 4) cfg.slot = 0;
    if (cfg.touch_alpha < 0 || cfg.touch_alpha > 4) cfg.touch_alpha = 1;
    if (cfg.touch_size < 0 || cfg.touch_size > 1) cfg.touch_size = 0;
}

void cfg_save(void)
{
    char path[HPATH];
    FILE *f;
    int i;

    cfg_path(path, 1);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "last_dir=%s\nlast_game=%s\n", cfg.last_dir, cfg.last_game);
        fclose(f);
    }
    if (sys.flags & SF_NOCORE) return;
    cfg_path(path, 0);
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "frameskip=%d\nsound=%d\nspeed=%d\nshow_fps=%d\nregion=%d\nuse_bios=%d\ncdda=%d\nstretch=%d\nslot=%d\ntouchpad=%d\ntouch_alpha=%d\ntouch_size=%d\n",
            cfg.frameskip, cfg.sound, cfg.speed, cfg.show_fps, cfg.region, cfg.use_bios, cfg.cdda, cfg.stretch, cfg.slot,
            cfg.touchpad, cfg.touch_alpha, cfg.touch_size);
    for (i = 0; i < BIND_COUNT; i++)
        fprintf(f, "key_%s=%d,%d\n", bind_names[i], cfg.keys[i][0], cfg.keys[i][1]);
    fclose(f);
}
