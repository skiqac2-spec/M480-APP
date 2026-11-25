/* launcher build: no emulator core inside, only the menus */
#include "host.h"
int core_fps_x10, core_speed_pct;
void core_var(const char *k, const char *v) { (void)k; (void)v; }
int  core_init(void) { return 0; }
int  core_load(const char *path) { (void)path; return -1; }
void core_unload(void) {}
int  core_loaded(void) { return 0; }
void core_reset(void) {}
void core_apply_settings(void) {}
void core_run_frame(void) {}
void core_set_pad(unsigned mask) { (void)mask; }
int  core_state_save(int slot) { (void)slot; return -1; }
int  core_state_load(int slot) { (void)slot; return -1; }
int  core_state_exists(int slot) { (void)slot; return 0; }
void core_flush_memcard(int force) { (void)force; }
int  core_disk_count(void) { return 0; }
int  core_disk_index(void) { return 0; }
int  core_disk_select(int idx) { (void)idx; return -1; }
int  core_is_pal(void) { return 0; }
const char *core_game_name(void) { return ""; }
void core_screen_save(void) {}
void core_redraw(void) {}
void (*core_frame_hook)(void);
