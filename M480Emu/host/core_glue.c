/* libretro host glue: connects the PCSX-ReARMed core to our tiny platform layer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <zlib.h>
#include "libretro.h"
#include "host.h"

int core_fps_x10, core_speed_pct;
int ce_skip_frame;
void (*core_frame_hook)(void);                 /* read by cores patched for front-end frame skipping */

static struct retro_disk_control_ext_callback disk_cb;
static retro_audio_buffer_status_callback_t audio_status_cb;
static bool vars_dirty;
static int  loaded, inited;
static unsigned pad_mask;
static char game_path[HPATH], game_name[HPATH];
static char dir_bios[HPATH], dir_saves[HPATH], dir_states[HPATH];

/* last presented game frame geometry inside g_fb */
static int vp_x, vp_y, vp_w, vp_h;
static int fb_req_w, fb_req_h;
static int got_frame;

/* pacing */
static double frame_ms = 1000.0 / 60.0;
static double due_ms;
static int behind;
static unsigned fps_t0; static int fps_frames, fps_shown;

/* memory card */
static uint32_t mcd_crc; static unsigned mcd_last_check;

/* ------------------------------------------------------------------ */
static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    (void)level;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    plat_log("%s", buf);
}

struct kv { const char *key; const char *val; };
static struct kv vars[48];
static int nvars;

void core_var(const char *k, const char *v)
{
    int i;
    for (i = 0; i < nvars; i++)
        if (!strcmp(vars[i].key, k)) {
            if (strcmp(vars[i].val, v)) { vars[i].val = v; vars_dirty = true; }
            return;
        }
    if (nvars >= (int)(sizeof(vars) / sizeof(vars[0]))) return;
    vars[nvars].key = k; vars[nvars].val = v; nvars++;
    vars_dirty = true;
}

void core_apply_settings(void)
{
    if (sys.apply) sys.apply();
}

static void clear_viewport(void)
{
    memset(g_fb, 0, SCR_W * GAME_H * 2);
}

static void set_audio_rate(double rate);

static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = dir_bios;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = dir_saves;
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = log_cb;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        int i;
        v->value = NULL;
        for (i = 0; i < nvars; i++)
            if (!strcmp(vars[i].key, v->key)) { v->value = vars[i].val; return true; }
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = vars_dirty;
        vars_dirty = false;
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
        audio_status_cb = data ?
            ((struct retro_audio_buffer_status_callback *)data)->callback : NULL;
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        struct retro_system_av_info *av = data;
        if (av->timing.fps > 1.0) frame_ms = 1000.0 / av->timing.fps;
        set_audio_rate(av->timing.sample_rate);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        return true;
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
        *(unsigned *)data = 1;
        return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
        disk_cb = *(struct retro_disk_control_ext_callback *)data;
        return true;
    case RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER: {
        struct retro_framebuffer *fb = data;
        int w = fb->width, h = fb->height;
        if (w < 16 || w > SCR_W || h < 16 || h > GAME_H)
            return false;
        if (cfg.stretch && w < SCR_W)
            return false;                 /* widened by scale_frame() instead */
        if (w != fb_req_w || h != fb_req_h) {
            fb_req_w = w; fb_req_h = h;
            clear_viewport();
            plat_present(0, 0, SCR_W, GAME_H);
        }
        fb->data = g_fb + ((GAME_H - h) / 2) * SCR_W + (SCR_W - w) / 2;
        fb->pitch = SCR_W * 2;
        fb->format = RETRO_PIXEL_FORMAT_RGB565;
        fb->memory_flags = RETRO_MEMORY_TYPE_CACHED;
        return true;
    }
    default:
        return false;
    }
}

/* RGB565 helpers */
#define AVG565(a, b) ((uint16_t)((((a) & 0xF7DE) >> 1) + (((b) & 0xF7DE) >> 1) + ((a) & (b) & 0x0821)))

/* 256 -> 320: every 4 source pixels become 5; one blended pixel per group keeps it cheap */
static void line_4to5(uint16_t *d, const uint16_t *s, int groups)
{
    while (groups--) {
        uint16_t b = s[1], c = s[2];
        d[0] = s[0];
        d[1] = b;
        d[2] = AVG565(b, c);
        d[3] = c;
        d[4] = s[3];
        s += 4; d += 5;
    }
}

/* 240 -> 320: every 3 source pixels become 4 */
static void line_3to4(uint16_t *d, const uint16_t *s, int groups)
{
    while (groups--) {
        uint16_t a = s[0], b = s[1];
        d[0] = a;
        d[1] = AVG565(a, b);
        d[2] = b;
        d[3] = s[2];
        s += 3; d += 4;
    }
}

static void line_2x(uint16_t *d, const uint16_t *s, int n)
{
    while (n--) { uint16_t a = *s++; d[0] = a; d[1] = a; d += 2; }
}

static short xmap[SCR_W];
static int xmap_sw, xmap_dw;

/* decides the on-screen size of a sw x sh frame */
static void target_size(int sw, int sh, int *dw, int *dh)
{
    int w = sw, h = sh;
    if (cfg.stretch) {
        if (sys.scale == SC_2X && sw * 2 <= SCR_W && sh * 2 <= GAME_H) { w = sw * 2; h = sh * 2; }
        else if (sys.scale == SC_UNIFORM && sw < SCR_W && sh * SCR_W / sw <= GAME_H) { w = SCR_W; h = sh * SCR_W / sw; }
        else if (sw >= 240 && sw < SCR_W) w = SCR_W;
    }
    if (w > SCR_W) { w = SCR_W; if (sw >= 2 * SCR_W - 128 && sh > GAME_H) h = sh / 2; }
    if (h > GAME_H) h = (sh / 2 <= GAME_H && sh / 2 >= 160) ? sh / 2 : GAME_H;
    *dw = w; *dh = h;
}

/* copies/scales a frame the core rendered into its own buffer into the game viewport */
static void scale_frame(const uint16_t *src, int sw, int sh, int spitch)
{
    int dw, dh, x, y, ox, oy, last_sy = -1;
    unsigned ys;

    target_size(sw, sh, &dw, &dh);
    ox = (SCR_W - dw) / 2; oy = (GAME_H - dh) / 2;
    ys = ((unsigned)sh << 16) / dh;
    if (dw != vp_w || dh != vp_h) { clear_viewport(); plat_present(0, 0, SCR_W, GAME_H); }
    if (xmap_sw != sw || xmap_dw != dw) {
        unsigned xs = ((unsigned)sw << 16) / dw, fx = 0;
        for (x = 0; x < dw; x++, fx += xs) xmap[x] = (short)(fx >> 16);
        xmap_sw = sw; xmap_dw = dw;
    }
    for (y = 0; y < dh; y++) {
        int sy = (int)((y * ys) >> 16);
        const uint16_t *s = (const uint16_t *)((const char *)src + sy * spitch);
        uint16_t *d = g_fb + (oy + y) * SCR_W + ox;
        if (sy == last_sy) { memcpy(d, d - SCR_W, dw * 2); continue; }   /* repeated line */
        last_sy = sy;
        if (dw == sw) memcpy(d, s, dw * 2);
        else if (sw == 256 && dw == 320) line_4to5(d, s, 64);
        else if (sw == 240 && dw == 320) line_3to4(d, s, 80);
        else if (dw == sw * 2) line_2x(d, s, sw);
        else for (x = 0; x < dw; x++) d[x] = s[xmap[x]];
    }
    vp_x = ox; vp_y = oy; vp_w = dw; vp_h = dh;
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
    const uint16_t *p = data;
    if (data == NULL || w == 0 || h == 0)
        return;                                         /* duplicated / skipped frame */
    if (p >= g_fb && p < g_fb + SCR_W * GAME_H) {       /* rendered straight into our buffer */
        int off = (int)(p - g_fb);
        vp_x = off % SCR_W; vp_y = off / SCR_W; vp_w = w; vp_h = h;
    } else
        scale_frame(p, w, h, (int)pitch);
    got_frame = 1;
    if (core_frame_hook) {                              /* touch controls drawn over the picture */
        core_frame_hook();
        plat_present(0, 0, SCR_W, GAME_H);
    } else
        plat_present(vp_x, vp_y, vp_w, vp_h);
}

static uint16_t *screen_keep;   /* GAME_H_MAX lines */

void core_screen_save(void)
{
    if (!screen_keep) screen_keep = malloc(SCR_W * GAME_H_MAX * 2);
    if (screen_keep) memcpy(screen_keep, g_fb, SCR_W * GAME_H * 2);
}

void core_redraw(void)
{
    if (screen_keep) memcpy(g_fb, screen_keep, SCR_W * GAME_H * 2);
    else clear_viewport();
    plat_present(0, 0, SCR_W, GAME_H);
}

/* audio: cores produce all kinds of rates (32000, 48000, 65536...); the phone gets 44100.
 * The resampler also keeps the sound continuous: when the emulation cannot quite keep up, or
 * runs slightly fast, the conversion ratio follows the amount of queued audio (a little lower
 * or higher pitch) instead of letting the queue run dry and crackle. */
#define OUT_RATE 44100
#define AUDIO_TARGET_MS 140
static unsigned rs_base = 0x10000, rs_step = 0x10000, rs_pos;      /* 16.16 source position */
static int rs_stretch = 1024;                                      /* 1024 = 1.0, larger = more output */
static int16_t rs_last[2];
static int16_t rs_buf[2048 * 2];

static void set_audio_rate(double rate)
{
    if (rate < 4000.0 || rate > 200000.0) rate = OUT_RATE;
    rs_base = (unsigned)(rate * 65536.0 / OUT_RATE + 0.5);
    rs_step = rs_base; rs_stretch = 1024;
    rs_pos = 0; rs_last[0] = rs_last[1] = 0;
}

static void audio_adapt(void)
{
    int fill = plat_audio_fill();                  /* queued milliseconds */
    int want = 1024 + (AUDIO_TARGET_MS - fill) * 1000 / AUDIO_TARGET_MS;
    if (want < 1000) want = 1000 - (1000 - want) / 8;                  /* gentle when the queue is long */
    if (want < 980) want = 980;
    if (want > 1800) want = 1800;
    rs_stretch += (want - rs_stretch) / 8;                             /* smooth: no warble */
    rs_step = (unsigned)(((unsigned long long)rs_base << 10) / (unsigned)rs_stretch);
}

static size_t audio_cb(const int16_t *data, size_t frames)
{
    size_t n = 0;
    if (!cfg.sound || !frames) return frames;
    audio_adapt();
    /* linear interpolation between source frames; rs_pos is relative to data[-1] (= rs_last) */
    for (;;) {
        unsigned i = rs_pos >> 16, f = (rs_pos & 0xffff) >> 4;       /* 12-bit fraction */
        const int16_t *a, *b;
        if (i >= frames) break;
        a = i ? data + (i - 1) * 2 : rs_last;
        b = data + i * 2;
        rs_buf[n * 2] = (int16_t)(a[0] + (((b[0] - a[0]) * (int)f) >> 12));
        rs_buf[n * 2 + 1] = (int16_t)(a[1] + (((b[1] - a[1]) * (int)f) >> 12));
        rs_pos += rs_step;
        if (++n == 2048) { plat_audio_write(rs_buf, (int)n); n = 0; }
    }
    if (n) plat_audio_write(rs_buf, (int)n);
    rs_pos -= (unsigned)frames << 16;
    rs_last[0] = data[(frames - 1) * 2]; rs_last[1] = data[(frames - 1) * 2 + 1];
    return frames;
}
static int16_t one_buf[512 * 2]; static int one_n;
static void audio1_cb(int16_t l, int16_t r)
{
    one_buf[one_n * 2] = l; one_buf[one_n * 2 + 1] = r;
    if (++one_n == 512) { audio_cb(one_buf, 512); one_n = 0; }
}
static void poll_cb(void) {}

static const unsigned char pb2retro[PB_COUNT] = {
    [PB_UP] = RETRO_DEVICE_ID_JOYPAD_UP, [PB_DOWN] = RETRO_DEVICE_ID_JOYPAD_DOWN,
    [PB_LEFT] = RETRO_DEVICE_ID_JOYPAD_LEFT, [PB_RIGHT] = RETRO_DEVICE_ID_JOYPAD_RIGHT,
    [PB_TRIANGLE] = RETRO_DEVICE_ID_JOYPAD_X, [PB_CIRCLE] = RETRO_DEVICE_ID_JOYPAD_A,
    [PB_CROSS] = RETRO_DEVICE_ID_JOYPAD_B, [PB_SQUARE] = RETRO_DEVICE_ID_JOYPAD_Y,
    [PB_L1] = RETRO_DEVICE_ID_JOYPAD_L, [PB_R1] = RETRO_DEVICE_ID_JOYPAD_R,
    [PB_L2] = RETRO_DEVICE_ID_JOYPAD_L2, [PB_R2] = RETRO_DEVICE_ID_JOYPAD_R2,
    [PB_START] = RETRO_DEVICE_ID_JOYPAD_START, [PB_SELECT] = RETRO_DEVICE_ID_JOYPAD_SELECT,
};
static unsigned retro_mask;

void core_set_pad(unsigned mask)
{
    unsigned m = 0; int i;
    for (i = 0; i < PB_COUNT; i++)
        if (mask & (1u << i)) m |= 1u << pb2retro[i];
    pad_mask = mask; retro_mask = m;
}

static int16_t input_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (port != 0 || device != RETRO_DEVICE_JOYPAD)
        return 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
        return (int16_t)retro_mask;
    return (retro_mask >> id) & 1;
}

/* ------------------------------------------------------------------ */
static void mk_path(char *out, const char *dir, const char *name, const char *ext)
{
    snprintf(out, HPATH, "%s%c%s%s", dir, PSEP, name, ext);
}

static char dir_temp[HPATH];
static void *rom_data; static size_t rom_size;

int core_init(void)
{
    char base[HPATH];
    if (inited) return 0;
    snprintf(dir_bios, sizeof(dir_bios), "%s%cbios", plat_app_dir(), PSEP);
    snprintf(base, sizeof(base), "%s%csaves", plat_app_dir(), PSEP); plat_mkdir(base);
    snprintf(dir_saves, sizeof(dir_saves), "%s%c%s", base, PSEP, sys.id);
    snprintf(base, sizeof(base), "%s%cstates", plat_app_dir(), PSEP); plat_mkdir(base);
    snprintf(dir_states, sizeof(dir_states), "%s%c%s", base, PSEP, sys.id);
    snprintf(dir_temp, sizeof(dir_temp), "%s%ctemp", plat_app_dir(), PSEP);
    plat_mkdir(dir_bios); plat_mkdir(dir_saves); plat_mkdir(dir_states); plat_mkdir(dir_temp);

    core_apply_settings();
    retro_set_environment(env_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio1_cb);
    retro_set_audio_sample_batch(audio_cb);
    retro_set_input_poll(poll_cb);
    retro_set_input_state(input_cb);
    retro_init();
    inited = 1;
    return 0;
}

static void mcd_path(char *out) { mk_path(out, dir_saves, game_name, !strcmp(sys.id, "ps1") ? ".mcd" : ".sav"); }

static void mcd_load(void)
{
    char path[HPATH];
    void *mem = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    FILE *f;
    if (!mem || !size) return;
    mcd_path(path);
    f = fopen(path, "rb");
    if (f) {
        if (fread(mem, 1, size, f) != size) plat_log("memcard: short read\n");
        fclose(f);
    }
    mcd_crc = crc32(0, mem, size);
}

void core_flush_memcard(int force)
{
    char path[HPATH];
    void *mem; size_t size; uint32_t c; FILE *f;
    unsigned now = plat_ticks();
    if (!loaded) return;
    if (!force && now - mcd_last_check < 5000) return;
    mcd_last_check = now;
    mem = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (!mem || !size) return;
    c = crc32(0, mem, size);
    if (c == mcd_crc) return;
    mcd_path(path);
    f = fopen(path, "wb");
    if (f) {
        if (fwrite(mem, 1, size, f) == size) mcd_crc = c;
        fclose(f);
    }
}

int core_load(const char *path)
{
    struct retro_game_info info;
    struct retro_system_av_info av;
    const char *b, *e;
    size_t n;

    core_init();
    core_unload();

    b = strrchr(path, PSEP); b = b ? b + 1 : path;
    e = strrchr(b, '.'); n = e ? (size_t)(e - b) : strlen(b);
    if (n >= sizeof(game_name)) n = sizeof(game_name) - 1;
    memcpy(game_name, b, n); game_name[n] = 0;
    snprintf(game_path, sizeof(game_path), "%s", path);

    core_apply_settings();
    memset(&info, 0, sizeof(info));
    info.path = game_path;
    {   /* cores that do not open the file themselves get its contents */
        struct retro_system_info si;
        memset(&si, 0, sizeof(si));
        retro_get_system_info(&si);
        if (!si.need_fullpath) {
            FILE *f = fopen(game_path, "rb");
            long sz = -1;
            if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET); }
            if (!f || sz <= 0 || sz > 64 * 1024 * 1024) { if (f) fclose(f); return -1; }
            rom_size = (size_t)sz;
            rom_data = plat_alloc_big(rom_size);
            if (!rom_data || fread(rom_data, 1, rom_size, f) != rom_size) {
                fclose(f); plat_free_big(rom_data, rom_size); rom_data = NULL; return -1;
            }
            fclose(f);
            info.data = rom_data; info.size = rom_size;
        }
    }
    fb_req_w = fb_req_h = 0; got_frame = 0; vp_w = vp_h = 0;
    clear_viewport();
    if (!retro_load_game(&info)) {
        plat_log("load failed: %s\n", path);
        if (rom_data) { plat_free_big(rom_data, rom_size); rom_data = NULL; }
        return -1;
    }
    loaded = 1;
    retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    retro_get_system_av_info(&av);
    frame_ms = 1000.0 / (av.timing.fps > 1.0 ? av.timing.fps : 60.0);
    set_audio_rate(av.timing.sample_rate);
    plat_log("av: %ux%u %.2f fps, %.0f Hz\n", av.geometry.base_width, av.geometry.base_height,
             av.timing.fps, av.timing.sample_rate);
    mcd_load();
    due_ms = 0; fps_t0 = 0; core_fps_x10 = 0; core_speed_pct = 0;
    return 0;
}

void core_unload(void)
{
    if (!loaded) return;
    core_flush_memcard(1);
    retro_unload_game();
    loaded = 0;
    if (rom_data) { plat_free_big(rom_data, rom_size); rom_data = NULL; }
}

int core_loaded(void) { return loaded; }
void core_reset(void) { if (loaded) retro_reset(); }
int core_is_pal(void) { return loaded && retro_get_region() == RETRO_REGION_PAL; }
const char *core_game_name(void) { return game_name; }

void core_run_frame(void)
{
    unsigned now;
    double t;
    if (!loaded) return;

    now = plat_ticks();
    t = (double)now;
    if (due_ms == 0 || t > due_ms + 150.0 || t < due_ms - 150.0)
        due_ms = t;                                     /* (re)sync */
    behind = t > due_ms + 3.0;

    if (audio_status_cb)
        audio_status_cb(true, behind ? 0 : 50, behind);
    if (sys.flags & SF_HOSTSKIP) {                      /* cores without their own frame skipping */
        static int skipped;
        int want = 0;
        if (cfg.frameskip == 1) want = behind && skipped < 4;
        else if (cfg.frameskip >= 2) want = skipped < cfg.frameskip - 1;
        ce_skip_frame = want;
        skipped = want ? skipped + 1 : 0;
    }

    retro_run();
    plat_audio_pump();

    due_ms += frame_ms;
    now = plat_ticks();
    if ((double)now < due_ms) {
        int ms = (int)(due_ms - (double)now);
        if (ms > 0) plat_sleep(ms > 50 ? 50 : ms);
        plat_audio_pump();
    }

    fps_frames++;
    now = plat_ticks();
    if (fps_t0 == 0) { fps_t0 = now; fps_frames = 0; }
    else if (now - fps_t0 >= 1000) {
        core_fps_x10 = (int)(fps_frames * 10000u / (now - fps_t0));
        core_speed_pct = (int)(core_fps_x10 * frame_ms / 100.0 + 0.5);
        fps_t0 = now; fps_frames = 0;
    }
    core_flush_memcard(0);
}

/* ---- save states: zlib-deflated serialize blob ----------------------- */
static void state_path(char *out, int slot)
{
    char ext[8];
    snprintf(ext, sizeof(ext), ".st%d", slot);
    mk_path(out, dir_states, game_name, ext);
}

int core_state_exists(int slot)
{
    char path[HPATH];
    if (!game_name[0]) return 0;
    state_path(path, slot);
    return plat_file_exists(path);
}

int core_state_save(int slot)
{
    char path[HPATH];
    size_t size;
    unsigned char *buf, *out;
    z_stream z;
    FILE *f;
    int ret = -1, zr;
    uint32_t hdr[2];

    if (!loaded) return -1;
    size = retro_serialize_size();
    buf = plat_alloc_big(size);
    out = malloc(65536);
    if (!buf || !out) goto done;
    if (!retro_serialize(buf, size)) goto done;

    state_path(path, slot);
    f = fopen(path, "wb");
    if (!f) goto done;
    hdr[0] = 0x31535350; /* "PSS1" */
    hdr[1] = (uint32_t)size;
    fwrite(hdr, 1, sizeof(hdr), f);
    memset(&z, 0, sizeof(z));
    if (deflateInit(&z, 1) == Z_OK) {
        z.next_in = buf; z.avail_in = (uInt)size;
        ret = 0;
        do {
            z.next_out = out; z.avail_out = 65536;
            zr = deflate(&z, Z_FINISH);
            if (zr == Z_STREAM_ERROR) { ret = -1; break; }
            if (fwrite(out, 1, 65536 - z.avail_out, f) != 65536 - z.avail_out) { ret = -1; break; }
        } while (zr != Z_STREAM_END);
        deflateEnd(&z);
    }
    if (fclose(f) != 0) ret = -1;
done:
    if (buf) plat_free_big(buf, size);
    free(out);
    return ret;
}

int core_state_load(int slot)
{
    char path[HPATH];
    size_t size, got;
    unsigned char *buf = NULL, *in = NULL;
    z_stream z;
    FILE *f;
    int ret = -1, zr = Z_OK;
    uint32_t hdr[2];

    if (!loaded) return -1;
    state_path(path, slot);
    f = fopen(path, "rb");
    if (!f) return -1;
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) || hdr[0] != 0x31535350) goto done;
    size = hdr[1];
    if (size == 0 || size > 64u * 1024 * 1024) goto done;
    buf = plat_alloc_big(size);
    in = malloc(65536);
    if (!buf || !in) goto done;
    memset(&z, 0, sizeof(z));
    if (inflateInit(&z) != Z_OK) goto done;
    z.next_out = buf; z.avail_out = (uInt)size;
    while (zr != Z_STREAM_END && (got = fread(in, 1, 65536, f)) > 0) {
        z.next_in = in; z.avail_in = (uInt)got;
        zr = inflate(&z, Z_NO_FLUSH);
        if (zr != Z_OK && zr != Z_STREAM_END) break;
    }
    inflateEnd(&z);
    if (zr == Z_STREAM_END && z.total_out == size && retro_unserialize(buf, size))
        ret = 0;
done:
    fclose(f);
    if (buf) plat_free_big(buf, hdr[1]);
    free(in);
    due_ms = 0;
    return ret;
}

/* ---- disc swapping ----------------------------------------------------- */
int core_disk_count(void) { return (loaded && disk_cb.get_num_images) ? (int)disk_cb.get_num_images() : 0; }
int core_disk_index(void) { return (loaded && disk_cb.get_image_index) ? (int)disk_cb.get_image_index() : 0; }

int core_disk_select(int idx)
{
    if (!loaded || !disk_cb.set_eject_state) return -1;
    disk_cb.set_eject_state(true);
    disk_cb.set_image_index((unsigned)idx);
    disk_cb.set_eject_state(false);
    return 0;
}
