/* PS1 emulator host for Samsung SCH-M480 (Windows Mobile 6.1, 320x320)
 * Shared declarations between the libretro glue, the UI and the platform layer.
 * License: GPLv2+ (links PCSX-ReARMed).
 */
#ifndef HOST_H
#define HOST_H

#include <stdint.h>
#include <stddef.h>

typedef __CHAR16_TYPE__ wch;          /* UTF-16 UI text (u"..." literals) */

#define SCR_W   320
#define SCR_H   320
#define GAME_H  (sys.game_h)          /* game viewport: 320 x game_h at the top (256 or 288) */
#define GAME_H_MAX 288
#define BAR_Y   GAME_H                /* touch bar: the rest at the bottom */
#define BAR_H   (SCR_H - GAME_H)

#define RGB565(r,g,b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

#define HPATH 260

/* ---- events ---------------------------------------------------------- */
enum { EV_NONE, EV_KEYDOWN, EV_KEYUP, EV_TDOWN, EV_TMOVE, EV_TUP, EV_QUIT, EV_SUSPEND };
struct event { int type; int key; int x, y; };

/* Windows virtual-key values (used on every platform as the key namespace) */
#define K_BACK   0x08
#define K_TAB    0x09
#define K_ENTER  0x0D
#define K_ESC    0x1B
#define K_SPACE  0x20
#define K_LEFT   0x25
#define K_UP     0x26
#define K_RIGHT  0x27
#define K_DOWN   0x28
#define K_F1     0x70   /* left soft key on WM  */
#define K_F2     0x71   /* right soft key on WM */

/* ---- text flags ------------------------------------------------------- */
#define TF_LEFT    0
#define TF_CENTER  1
#define TF_RIGHT   2
#define TF_BIG     4     /* large font  */
#define TF_SMALL   8     /* small font  */
#define TF_WRAP    16    /* word wrap inside the box */

/* ---- platform layer (plat_ce.c / plat_posix.c) ------------------------ */
extern uint16_t *g_fb;                               /* SCR_W x SCR_H RGB565 */
int      plat_init(void);
void     plat_exit(void);
void     plat_present(int x, int y, int w, int h);
void     plat_text(int x, int y, int w, int h, const wch *s, uint16_t color, int flags);
int      plat_text_width(const wch *s, int n, int flags);   /* pixel width of the first n chars (n < 0: all) */
int      plat_poll(struct event *ev);                /* 0 = no event pending */
void     plat_wait_event(int timeout_ms);            /* sleep until an event or timeout */
unsigned plat_ticks(void);                           /* milliseconds */
void     plat_sleep(int ms);
void     plat_keep_awake(void);
int      plat_free_mem_mb(void);                     /* free RAM in MB (-1 unknown) */
const char *plat_app_dir(void);                      /* no trailing separator */
void     plat_mkdir(const char *path);
void     plat_remove(const char *path);
void     plat_path_to_wch(const char *path, wch *out, int max);
void     plat_log(const char *fmt, ...);
void    *plat_alloc_big(size_t size);                /* zeroed, page aligned */
void     plat_free_big(void *p, size_t size);

struct dirent_h { char name[HPATH]; unsigned char is_dir, is_card; };
int      plat_list_dir(const char *path, struct dirent_h **out);   /* malloc'd, returns count */
int      plat_file_exists(const char *path);

int      plat_audio_open(int rate);                  /* stereo s16 */
void     plat_audio_close(void);
void     plat_audio_write(const int16_t *pcm, int frames);
int      plat_audio_fill(void);                      /* queued audio in milliseconds */
void     plat_audio_pump(void);
int      plat_audio_rate(void);                      /* output rate the device is (or will be) opened with */                      /* hand finished buffers back to the device */
void     plat_audio_pause(int on);

#define PSEP '\\'
#ifdef HOST_POSIX
#undef  PSEP
#define PSEP '/'
#endif

/* ---- per-system definition (one emulator core per executable) --------------- */
enum { PB_UP, PB_DOWN, PB_LEFT, PB_RIGHT, PB_TRIANGLE, PB_CIRCLE, PB_CROSS, PB_SQUARE,
       PB_L1, PB_R1, PB_L2, PB_R2, PB_START, PB_SELECT, PB_COUNT };
enum { SC_WIDTH, SC_UNIFORM, SC_2X };            /* how "fill the screen" scales */
#define SF_FRAMESKIP 1                           /* core can skip frames */
#define SF_PS1OPTS   2                           /* PlayStation-only settings */
#define SF_NOCORE    4
#define SF_HOSTSKIP  8                           /* frame skipping is driven by the front end */                           /* launcher: no emulator inside */
struct sysdef {
    const char *id;                              /* "ps1", "gba", ... also names folders/files */
    const wch *name;                             /* title shown in menus */
    int game_h;                                  /* 256, or 288 for the Game Boy's exact 2x */
    int scale;                                   /* SC_* */
    int flags;                                   /* SF_* */
    const wch *btn_names[PB_COUNT];              /* NULL = button not used by this system */
    signed char bar[7];                          /* touch-bar buttons (PB_*), -1 terminated */
    const wch *bar_names[6];
    const wch *pad_names[4];                     /* on-screen face buttons: top, right, bottom, left (NULL = none) */
    void (*apply)(void);                         /* cfg -> core variables through core_var() */
    int (*bios_found)(void);                     /* NULL when the system needs no BIOS */
};
extern const struct sysdef sys;
void core_var(const char *key, const char *val);

/* every system the package knows; a game file is routed to "emu_<id>.exe" */
int  sysreg_count(void);
const char *sysreg_id(int idx);
const wch  *sysreg_tag(int idx);
int  sysreg_available(int idx);                  /* its executable is installed */
int  sysreg_classify(const char *dir, const char *name);   /* system index, -1: not a game, -2: zip */
int  sysreg_self(void);                          /* index of this executable's system, -1 for the launcher */
/* zip support: extracts the first ROM in the archive to the temp folder, returns its system index */
int  zip_extract_rom(const char *zip_path, char *out_path);

int  plat_spawn(const char *exe_path, const char *args);   /* start another program; 0 ok */
extern char g_start_game[HPATH];                 /* game given on the command line ("" if none) */
extern int  g_start_resume;

/* ---- settings --------------------------------------------------------- */
#define KEY_MENU_IDX PB_COUNT          /* extra binding: open the in-game menu */
#define BIND_COUNT (PB_COUNT + 1)

struct settings {
    int frameskip;      /* 0=off 1=auto 2..4 = fixed 1..3 */
    int sound;          /* 0/1 */
    int speed;          /* 0=quality 1=balanced 2=fast 3=fastest */
    int show_fps;
    int region;         /* 0 auto 1 NTSC 2 PAL */
    int use_bios;       /* 1 = real BIOS if found, 0 = always built-in */
    int cdda;           /* CD audio + XA on/off */
    int stretch;        /* widen narrow (256 pixel) games to the full 320 */
    int slot;           /* current save-state slot 0..4 */
    int touchpad;       /* on-screen D-pad and buttons over the game picture */
    int touch_alpha;    /* transparency step 0..4 = 0%, 25%, 50%, 75%, 100% (invisible but working) */
    int touch_size;     /* 0 normal, 1 large */
    int audio_hq;       /* 0: 22050 Hz output (clean on the M480's own amplifier), 1: 44100 Hz (Bluetooth) */
    int volume;         /* 0 normal (70%), 1 quiet (45%), 2 loud (100%) */
    int keys[BIND_COUNT][2];
    char last_dir[HPATH];
    char last_game[HPATH];
};
extern struct settings cfg;
void cfg_defaults(void);
void cfg_load(void);
void cfg_save(void);

/* ---- core glue (core_glue.c) ------------------------------------------ */
int  core_init(void);                      /* once */
int  core_load(const char *path);          /* 0 ok */
void core_unload(void);
int  core_loaded(void);
void core_reset(void);
void core_apply_settings(void);            /* settings changed -> tell the core */
void core_run_frame(void);                 /* emulate one frame, presents video */
void core_set_pad(unsigned mask);          /* bit n = PB_* pressed */
int  core_state_save(int slot);            /* 0 ok */
int  core_state_load(int slot);
int  core_state_exists(int slot);
void core_flush_memcard(int force);
int  core_disk_count(void);
int  core_disk_index(void);
int  core_disk_select(int idx);
int  core_is_pal(void);
const char *core_game_name(void);          /* file name without directory/extension */
void core_screen_save(void);                /* remember the game picture before a menu draws over it */
void core_redraw(void);                    /* restore + re-present the game picture */
extern void (*core_frame_hook)(void);      /* called after each new game frame, before it is shown */
extern int core_fps_x10;                   /* measured emulated fps * 10 */
extern int core_speed_pct;                 /* percent of full speed */

/* ---- ui (ui.c) --------------------------------------------------------- */
void ui_main(void);                        /* runs until the user quits */

#endif
