/* launcher: game list, carrier picker, key mapping editor, in-game menu, screen composition */
#include "ui.h"
#include "host.h"
#include "app.h"
#include "pkg.h"
#include "pal.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void surf_set_screen(Surf *s);
void vm_log(const char *fmt, ...);

/* ---------------- text helpers ---------------- */
static int utf8_to_u16(const char *s, u16 *out, int max)
{
    const u8 *p = (const u8 *)s;
    int n = 0;
    while (*p && n < max) {
        if (*p < 0x80) out[n++] = *p++;
        else if ((*p & 0xE0) == 0xC0 && p[1]) { out[n++] = (u16)(((p[0] & 0x1F) << 6) | (p[1] & 0x3F)); p += 2; }
        else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { out[n++] = (u16)(((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F)); p += 3; }
        else p++;
    }
    return n;
}

#define F_TITLE ((17 << 4) | 1)
#define F_ITEM  (16 << 4)
#define F_SMALL (12 << 4)
#define F_BOLD  ((16 << 4) | 1)

static GC gc;
static Surf *fr;

static void col(u32 rgb) { gc.rgb = rgb; gc.c = C565(rgb); }
static void rect(int x, int y, int w, int h, u32 rgb) { col(rgb); g_fill(&gc, x, y, w, h); }
static int text_w(const char *s, int fid) { u16 b[256]; int n = utf8_to_u16(s, b, 256); return g_text_width(b, n, fid); }
static void text(int x, int y, const char *s, int fid, u32 rgb)
{
    u16 b[256];
    int n = utf8_to_u16(s, b, 256);
    col(rgb);
    g_text(&gc, b, n, x, y, fid);
}
static void text_c(int cx, int y, const char *s, int fid, u32 rgb) { text(cx - text_w(s, fid) / 2, y, s, fid, rgb); }
static void text_r(int rx, int y, const char *s, int fid, u32 rgb) { text(rx - text_w(s, fid), y, s, fid, rgb); }
/* clip a string to a width, adding ".." */
static void text_fit(int x, int y, int maxw, const char *s, int fid, u32 rgb)
{
    u16 b[256];
    int n = utf8_to_u16(s, b, 256), w = g_text_width(b, n, fid);
    col(rgb);
    if (w <= maxw) { g_text(&gc, b, n, x, y, fid); return; }
    {
        u16 dots[2] = { '.', '.' };
        int dw = g_text_width(dots, 2, fid);
        while (n > 0 && g_text_width(b, n, fid) + dw > maxw) n--;
        g_text(&gc, b, n, x, y, fid);
        g_text(&gc, dots, 2, x + g_text_width(b, n, fid), y, fid);
    }
}

/* ---------------- config ---------------- */
static char appdir[400], cfgpath[500], savebase[500];
static u8 keymap[256];          /* VK -> phone key + 1 */
static u8 keymap_game[256];
static int has_game_keys;
static int scale_full, sound_on = 1, volume = 80;
typedef struct { char key[80]; char val[80]; } KV;
static KV kvs[256];
static int nkv;

static const char *kv_get(const char *k)
{
    int i;
    for (i = 0; i < nkv; i++) if (!strcmp(kvs[i].key, k)) return kvs[i].val;
    return NULL;
}
static void kv_set(const char *k, const char *v)
{
    int i;
    for (i = 0; i < nkv; i++) if (!strcmp(kvs[i].key, k)) { snprintf(kvs[i].val, sizeof kvs[i].val, "%s", v); return; }
    if (nkv < 256) { snprintf(kvs[nkv].key, 80, "%s", k); snprintf(kvs[nkv].val, 80, "%s", v); nkv++; }
}

static void default_keys(u8 *m)
{
    static const char grid[] = "WERSDFXCV";
    int i;
    memset(m, 0, 256);
    m[VK_UP_] = PK_UP + 1; m[VK_DOWN_] = PK_DOWN + 1; m[VK_LEFT_] = PK_LEFT + 1; m[VK_RIGHT_] = PK_RIGHT + 1;
    m[VK_RETURN_] = PK_FIRE + 1;
    m[VK_F1_] = PK_SOFT1 + 1; m[VK_F2_] = PK_SOFT2 + 1;
    m[VK_BACK_] = PK_CLEAR + 1;
    m[VK_F3_] = PK_SEND + 1;
    for (i = 0; i < 10; i++) { m['0' + i] = (u8)(i + 1); m[0x60 + i] = (u8)(i + 1); }
    for (i = 0; i < 9; i++) m[(u8)grid[i]] = (u8)(i + 2);
    m[VK_SPACE_] = 0 + 1;
    m['Z'] = PK_STAR + 1; m['B'] = PK_POUND + 1;
    m[0x77] = PK_STAR + 1; m[0x78] = PK_POUND + 1;      /* VK_F8 / VK_F9: '*' '#' on many WM phones */
    m[0x6A] = PK_STAR + 1;                              /* VK_MULTIPLY */
}

static void load_keys(const char *path, u8 *m)
{
    u32 len;
    char *d = (char *)pal_load(path, &len), *p;
    if (!d) return;
    memset(m, 0, 256);
    for (p = d; p && *p;) {
        int vk, pk;
        if (sscanf(p, "%x=%d", &vk, &pk) == 2 && vk > 0 && vk < 256 && pk >= 0 && pk < PK_COUNT) m[vk] = (u8)(pk + 1);
        p = strchr(p, '\n');
        if (p) p++;
    }
    free(d);
}
static void save_keys(const char *path, const u8 *m)
{
    char buf[4096];
    int i, o = 0;
    o += snprintf(buf + o, sizeof buf - o, "# M480 WIPI key map: VK(hex)=phone key\r\n");
    for (i = 1; i < 256; i++) if (m[i]) o += snprintf(buf + o, sizeof buf - o, "%02X=%d\r\n", i, m[i] - 1);
    pal_save(path, buf, (u32)o);
}

static void load_cfg(void)
{
    u32 len;
    char *d = (char *)pal_load(cfgpath, &len), *p;
    const char *v;
    char kp[520];
    nkv = 0;
    if (d) {
        for (p = d; p && *p;) {
            char *e = strchr(p, '\n'), *eq;
            char line[200];
            int n = e ? (int)(e - p) : (int)strlen(p);
            if (n > 199) n = 199;
            memcpy(line, p, n); line[n] = 0;
            if (n && line[n - 1] == '\r') line[n - 1] = 0;
            eq = strchr(line, '=');
            if (eq && line[0] != '#') { *eq = 0; kv_set(line, eq + 1); }
            p = e ? e + 1 : NULL;
        }
        free(d);
    }
    if ((v = kv_get("scale")) != NULL) scale_full = atoi(v);
    if ((v = kv_get("sound")) != NULL) sound_on = atoi(v);
    if ((v = kv_get("volume")) != NULL) volume = atoi(v);
    default_keys(keymap);
    snprintf(kp, sizeof kp, "%s/keys.ini", appdir);
    load_keys(kp, keymap);
}
static void save_cfg(void)
{
    char buf[16384], t[16];
    int i, o = 0;
    snprintf(t, sizeof t, "%d", scale_full); kv_set("scale", t);
    snprintf(t, sizeof t, "%d", sound_on); kv_set("sound", t);
    snprintf(t, sizeof t, "%d", volume); kv_set("volume", t);
    o += snprintf(buf, sizeof buf, "# M480 WIPI settings\r\n");
    for (i = 0; i < nkv && o < (int)sizeof buf - 200; i++) o += snprintf(buf + o, sizeof buf - o, "%s=%s\r\n", kvs[i].key, kvs[i].val);
    pal_save(cfgpath, buf, (u32)o);
}

static const char *pk_name(int pk)
{
    static const char *n[PK_COUNT] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "#",
        "위 ↑", "아래 ↓", "왼쪽 ←", "오른쪽 →", "확인(OK)", "왼쪽 소프트키", "오른쪽 소프트키", "지움(CLR)", "통화", "종료", "볼륨+", "볼륨-" };
    return pk >= 0 && pk < PK_COUNT ? n[pk] : "?";
}
static void vk_name(int vk, char *out, int max)
{
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { snprintf(out, max, "%c", vk); return; }
    if (vk >= 0x60 && vk <= 0x69) { snprintf(out, max, "Num%d", vk - 0x60); return; }
    switch (vk) {
    case VK_UP_: snprintf(out, max, "↑"); return;
    case VK_DOWN_: snprintf(out, max, "↓"); return;
    case VK_LEFT_: snprintf(out, max, "←"); return;
    case VK_RIGHT_: snprintf(out, max, "→"); return;
    case VK_RETURN_: snprintf(out, max, "OK"); return;
    case VK_BACK_: snprintf(out, max, "Back"); return;
    case VK_SPACE_: snprintf(out, max, "Space"); return;
    case VK_ESCAPE_: snprintf(out, max, "Esc"); return;
    case VK_TAB_: snprintf(out, max, "Tab"); return;
    case VK_F1_: snprintf(out, max, "좌소프트"); return;
    case VK_F2_: snprintf(out, max, "우소프트"); return;
    case VK_F3_: snprintf(out, max, "통화"); return;
    case VK_F4_: snprintf(out, max, "종료"); return;
    case 0x77: snprintf(out, max, "*키"); return;
    case 0x78: snprintf(out, max, "#키"); return;
    case 0x6A: snprintf(out, max, "*"); return;
    case 0xBC: snprintf(out, max, ","); return;
    case 0xBE: snprintf(out, max, "."); return;
    case 0x10: snprintf(out, max, "Shift"); return;
    case 0x11: snprintf(out, max, "Ctrl"); return;
    case 0x12: snprintf(out, max, "Alt"); return;
    }
    snprintf(out, max, "0x%02X", vk);
}

/* ---------------- game list ---------------- */
typedef struct { char path[400]; char name[128]; int carrier, kind; } Game;
static Game *games;
static int ngames, capgames;
static int sel, top;

static int is_game_file(const char *n)
{
    size_t l = strlen(n);
    const char *e = l > 4 ? n + l - 4 : "";
    char x[5];
    int i;
    for (i = 0; i < 4 && e[i]; i++) x[i] = (char)(e[i] >= 'A' && e[i] <= 'Z' ? e[i] + 32 : e[i]);
    x[i] = 0;
    return !strcmp(x, ".jar") || !strcmp(x, ".zip");
}

typedef struct { const char *dir; int depth; int found_jar; } ScanCtx;
static void add_game(const char *path)
{
    char name[128];
    int carrier = 0, kind = 0, i;
    char ck[200];
    const char *cv;
    for (i = 0; i < ngames; i++) if (!strcmp(games[i].path, path)) return;
    if (ngames == capgames) { capgames = capgames ? capgames * 2 : 32; games = (Game *)realloc(games, capgames * sizeof(Game)); }
    snprintf(ck, sizeof ck, "list.%s", path);
    cv = kv_get(ck);
    if (cv && sscanf(cv, "%d,%d,", &carrier, &kind) == 2 && strchr(strchr(cv, ',') + 1, ',')) {
        snprintf(name, sizeof name, "%s", strchr(strchr(cv, ',') + 1, ',') + 1);
    } else {
        if (!pkg_probe(path, name, sizeof name, &carrier, &kind)) return;
        {
            char v[200];
            snprintf(v, sizeof v, "%d,%d,%.60s", carrier, kind, name);
            kv_set(ck, v);
        }
    }
    snprintf(games[ngames].path, sizeof games[ngames].path, "%s", path);
    snprintf(games[ngames].name, sizeof games[ngames].name, "%s", name);
    games[ngames].carrier = carrier;
    games[ngames].kind = kind;
    ngames++;
}

static void scan_cb(void *u, const char *name, int is_dir, u32 size);
static void scan_dir(const char *dir, int depth)
{
    ScanCtx c;
    c.dir = dir; c.depth = depth; c.found_jar = 0;
    pal_listdir(dir, scan_cb, &c);
}
static void scan_cb(void *u, const char *name, int is_dir, u32 size)
{
    ScanCtx *c = (ScanCtx *)u;
    char p[500];
    (void)size;
    snprintf(p, sizeof p, "%s/%s", c->dir, name);
    if (is_dir) {
        if (c->depth < 2) scan_dir(p, c->depth + 1);
    } else if (is_game_file(name)) add_game(p);
}

static int cmp_game(const void *a, const void *b) { return strcmp(((const Game *)a)->name, ((const Game *)b)->name); }

static void scan_games(void)
{
    static const char *roots[] = { "/Storage Card/WIPI", "/Storage Card/wipi", "/Storage Card/Games/WIPI", "/Storage Card/게임",
        "/My Documents/WIPI", "/Storage Card/M480WIPI/games", NULL };
    char p[500];
    int i;
    ngames = 0;
    snprintf(p, sizeof p, "%s/games", appdir);
    scan_dir(p, 0);
    for (i = 0; roots[i]; i++) scan_dir(roots[i], 0);
    qsort(games, ngames, sizeof(Game), cmp_game);
    save_cfg();
}

/* ---------------- state ---------------- */
enum { M_LIST, M_CARRIER, M_KEYS, M_MENU, M_GAME, M_MSG, M_OPTIONS };
static int mode, prev_mode;
static int fw, fh;
static int need_draw = 1;
static int quit_flag, restart_flag;
static char msg[512];
static int msg_back;

/* carrier dialog */
static int dlg_sel, dlg_carrier, dlg_size;
static const int sizes[][2] = { { 0, 0 }, { 240, 320 }, { 176, 208 }, { 176, 220 }, { 128, 160 }, { 240, 400 }, { 320, 240 }, { 120, 160 } };
#define NSIZES 8

/* key editor */
static int ke_sel, ke_top, ke_wait, ke_scope_game;
static u8 *ke_map;

/* menus */
static int menu_sel, opt_sel;
static u32 quit_deadline;

/* game */
static int game_w, game_h;
static int gx, gy, gw, gh;          /* game rectangle in the frame */
static int side;                    /* side panel width */
static int pressed_btn = -1;
static int ptr_down;

static void set_mode(int m) { prev_mode = mode; mode = m; need_draw = 1; }

static void show_msg(const char *s, int back) { snprintf(msg, sizeof msg, "%s", s); msg_back = back; set_mode(M_MSG); }

void ui_init(int w, int h, const char *dir, const char *start_game, int start_carrier, int sw, int sh)
{
    fw = w; fh = h;
    snprintf(appdir, sizeof appdir, "%s", dir);
    snprintf(cfgpath, sizeof cfgpath, "%s/m480wipi.ini", appdir);
    snprintf(savebase, sizeof savebase, "%s/save", appdir);
    load_cfg();
    snd_set_master(sound_on ? volume : 0);
    if (start_game && start_game[0]) {
        char err[300];
        game_w = sw; game_h = sh;
        if (app_start(start_game, start_carrier, sw, sh, savebase, err, sizeof err)) {
            char kp[700];
            memcpy(keymap_game, keymap, 256);
            snprintf(kp, sizeof kp, "%s/%s/keys.ini", savebase, app_id());
            has_game_keys = 0;
            { void *f = pal_fopen(kp, 'r'); if (f) { pal_fclose(f); load_keys(kp, keymap_game); has_game_keys = 1; } }
            set_mode(M_GAME);
            return;
        }
        show_msg(err, M_LIST);
    } else set_mode(M_LIST);
    scan_games();
    {
        const char *v = kv_get("last");
        int i;
        if (v) for (i = 0; i < ngames; i++) if (!strcmp(games[i].path, v)) sel = i;
    }
}

int ui_quit(void) { return quit_flag; }
int ui_restart(char *game, int max, int *carrier, int *w, int *h)
{
    (void)game; (void)max; (void)carrier; (void)w; (void)h;
    if (!restart_flag) return 0;
    if (game) game[0] = 0;
    return 1;
}

/* ---------------- game start ---------------- */
static void open_carrier_dialog(void)
{
    char k[480];
    const char *v;
    Game *g = &games[sel];
    dlg_carrier = g->carrier;
    snprintf(k, sizeof k, "carrier.%s", g->path);
    if ((v = kv_get(k)) != NULL) dlg_carrier = atoi(v);
    dlg_size = 0;
    snprintf(k, sizeof k, "size.%s", g->path);
    if ((v = kv_get(k)) != NULL) dlg_size = atoi(v);
    if (dlg_size < 0 || dlg_size >= NSIZES) dlg_size = 0;
    dlg_sel = dlg_carrier;
    set_mode(M_CARRIER);
}

static void start_game(void)
{
    char k[480], v[16], err[300];
    Game *g = &games[sel];
    snprintf(k, sizeof k, "carrier.%s", g->path); snprintf(v, sizeof v, "%d", dlg_carrier); kv_set(k, v);
    snprintf(k, sizeof k, "size.%s", g->path); snprintf(v, sizeof v, "%d", dlg_size); kv_set(k, v);
    kv_set("last", g->path);
    save_cfg();
    game_w = sizes[dlg_size][0]; game_h = sizes[dlg_size][1];
    if (!app_start(g->path, dlg_carrier, game_w, game_h, savebase, err, sizeof err)) {
        /* VM state may be half initialised: come back through a restart */
        show_msg(err, -1);
        return;
    }
    {
        char kp[700];
        memcpy(keymap_game, keymap, 256);
        snprintf(kp, sizeof kp, "%s/%s/keys.ini", savebase, app_id());
        has_game_keys = 0;
        { void *f = pal_fopen(kp, 'r'); if (f) { pal_fclose(f); load_keys(kp, keymap_game); has_game_keys = 1; } }
    }
    set_mode(M_GAME);
}

/* ---------------- layout of the game screen ---------------- */
#define NBTN 10
static const struct { int pk; const char *label; } side_btn[NBTN] = {
    { PK_SOFT1, "◀소프트" }, { PK_CLEAR, "지움" }, { PK_STAR, "*" }, { PK_POUND, "#" }, { -1, "메뉴" },
    { PK_SOFT2, "소프트▶" }, { PK_FIRE, "확인" }, { 0, "0" }, { 5, "5" }, { PK_SEND, "통화" }
};

static void layout(void)
{
    Surf *s = host.screen;
    int sw = s ? s->w : 240, sh = s ? s->h : 320;
    if (scale_full) { gx = 0; gy = 0; gw = fw; gh = fh; side = 0; return; }
    if (sw <= fw && sh <= fh && (fw - sw) >= 48) { gw = sw; gh = sh; }
    else {
        /* scale to fit, keeping aspect */
        if (sw * fh <= sh * fw) { gh = fh; gw = sw * fh / sh; }
        else { gw = fw; gh = sh * fw / sw; }
        if (sw * 2 <= fw && sh * 2 <= fh) { gw = sw * 2; gh = sh * 2; }
    }
    gx = (fw - gw) / 2; gy = (fh - gh) / 2;
    side = gx;
}

static int btn_rect(int i, int *x, int *y, int *w, int *h)
{
    int col = i / 5, row = i % 5, bh;
    if (side < 22) return 0;
    bh = fh / 5;
    *w = side - 4; *h = bh - 4;
    *x = col == 0 ? 2 : gx + gw + 2;
    *y = row * bh + 2;
    return 1;
}

static void draw_game(void)
{
    int i;
    Surf *s = host.screen;
    if (!s) return;
    layout();
    if (gw == s->w && gh == s->h) {
        int y;
        for (y = 0; y < gh; y++) memcpy(fr->px + (size_t)(gy + y) * fr->w + gx, s->px + (size_t)y * s->w, (size_t)gw * 2);
    } else g_scale_to(fr, gx, gy, gw, gh, s);
    if (side > 0 || gy > 0) {
        rect(0, 0, gx, fh, 0x101418);
        rect(gx + gw, 0, fw - gx - gw, fh, 0x101418);
        if (gy > 0) { rect(gx, 0, gw, gy, 0); rect(gx, gy + gh, gw, fh - gy - gh, 0); }
        for (i = 0; i < NBTN; i++) {
            int x, y, w, h;
            if (!btn_rect(i, &x, &y, &w, &h)) break;
            rect(x, y, w, h, pressed_btn == i ? 0x4060A0 : 0x262C36);
            col(0x404858); g_rect(&gc, x, y, w - 1, h - 1);
            {
                const char *l = side_btn[i].label;
                int fid = text_w(l, F_SMALL) > w - 2 ? (10 << 4) : F_SMALL;
                text_c(x + w / 2, y + h / 2 - 7, l, fid, 0xD0D8E8);
            }
        }
    }
}

/* ---------------- drawing ---------------- */
static void title_bar(const char *t, const char *right)
{
    rect(0, 0, fw, 24, 0x1E3A66);
    text(6, 3, t, F_TITLE, 0xFFFFFF);
    if (right) text_r(fw - 6, 6, right, F_SMALL, 0xB8C8E8);
}
static void soft_bar(const char *l, const char *r)
{
    rect(0, fh - 22, fw, 22, 0x202833);
    if (l) text(6, fh - 19, l, F_ITEM, 0xFFFFFF);
    if (r) text_r(fw - 6, fh - 19, r, F_ITEM, 0xFFFFFF);
}
static const char *carrier_tag(int c) { return c == CARRIER_SKT ? "SKT" : c == CARRIER_KTF ? "KTF" : "LGT"; }
static u32 carrier_col(int c) { return c == CARRIER_SKT ? 0xE04040 : c == CARRIER_KTF ? 0x3080E0 : 0xD040B0; }

#define ROWH 40
static void draw_list(void)
{
    int rows = (fh - 24 - 22) / ROWH, i;
    char cnt[32];
    rect(0, 0, fw, fh, 0x0C1018);
    snprintf(cnt, sizeof cnt, "%d개", ngames);
    title_bar("M480 WIPI", cnt);
    if (!ngames) {
        text(10, 40, "게임을 찾지 못했습니다.", F_ITEM, 0xFFFFFF);
        text(10, 66, "게임 파일(.jar / .zip)을 다음 폴더에", F_SMALL, 0xC0C8D0);
        text(10, 84, "넣어 주세요:", F_SMALL, 0xC0C8D0);
        text(16, 104, "\\Storage Card\\WIPI", F_SMALL, 0x80D0FF);
        text(16, 122, "(프로그램폴더)\\games", F_SMALL, 0x80D0FF);
        text(10, 146, "통신사별 파일 예시:", F_SMALL, 0xC0C8D0);
        text(16, 164, "SKT: 게임.jar + 게임.msd", F_SMALL, 0xE0E0E0);
        text(16, 182, "KTF: zip 안에 jar + __adf__", F_SMALL, 0xE0E0E0);
        text(16, 200, "LGT: zip 안에 jar + app_info", F_SMALL, 0xE0E0E0);
    }
    if (sel < top) top = sel;
    if (sel >= top + rows) top = sel - rows + 1;
    for (i = 0; i < rows && top + i < ngames; i++) {
        Game *g = &games[top + i];
        int y = 24 + i * ROWH, on = top + i == sel;
        char sub[200];
        const char *fn = strrchr(g->path, '/');
        rect(0, y, fw, ROWH, on ? 0x284C80 : ((i & 1) ? 0x121822 : 0x0C1018));
        rect(6, y + 7, 34, 18, carrier_col(g->carrier));
        text_c(23, y + 9, carrier_tag(g->carrier), (12 << 4) | 1, 0xFFFFFF);
        text_fit(46, y + 3, fw - 52, g->name, F_ITEM, 0xFFFFFF);
        snprintf(sub, sizeof sub, "%s · %s", g->kind == PKG_JAVA ? "Java" : "지원 안 됨(네이티브)", fn ? fn + 1 : g->path);
        text_fit(46, y + 22, fw - 52, sub, F_SMALL, g->kind == PKG_JAVA ? 0x9AA8BC : 0xE08060);
    }
    soft_bar("메뉴", "종료");
}

static void draw_box(int x, int y, int w, int h, const char *title)
{
    rect(x + 3, y + 3, w, h, 0x000000);
    rect(x, y, w, h, 0x1A2230);
    col(0x5070A0); g_rect(&gc, x, y, w - 1, h - 1);
    rect(x + 1, y + 1, w - 2, 22, 0x2A4A7A);
    text(x + 8, y + 4, title, F_BOLD, 0xFFFFFF);
}

/* carrier dialog geometry: everything stacked so nothing can overlap */
static int dlg_x, dlg_y, dlg_w, dlg_h, dlg_rows_y, dlg_rowh;
static void carrier_geom(void)
{
    dlg_rowh = font_height(F_ITEM) + 10;
    if (dlg_rowh < 26) dlg_rowh = 26;
    dlg_w = fw - 16;
    dlg_h = 24 + (font_height(F_ITEM) + 8) + 4 * (dlg_rowh + 4) + 2 * (font_height(F_SMALL) + 4) + 10;
    if (dlg_h > fh - 26) dlg_h = fh - 26;
    dlg_x = 8;
    dlg_y = (fh - 22 - dlg_h) / 2;
    if (dlg_y < 2) dlg_y = 2;
    dlg_rows_y = dlg_y + 24 + font_height(F_ITEM) + 8;
}
static void draw_carrier(void)
{
    static const char *names[3] = { "SKT (SK-VM/WIPI)", "KTF (WIPI)", "LGT (WIPI)" };
    int i, x, w, iy;
    char line[200];
    Game *g = &games[sel];
    draw_list();
    carrier_geom();
    x = dlg_x; w = dlg_w;
    draw_box(x, dlg_y, w, dlg_h, "통신사 선택");
    text_fit(x + 10, dlg_y + 28, w - 20, g->name, F_ITEM, 0xFFE080);
    for (i = 0; i < 4; i++) {
        int ry = dlg_rows_y + i * (dlg_rowh + 4), on = dlg_sel == i, ty = ry + (dlg_rowh - font_height(F_ITEM)) / 2;
        rect(x + 6, ry, w - 12, dlg_rowh, on ? 0x3A68B0 : 0x232D3E);
        if (i < 3) {
            int tagw = g->carrier == i ? text_w("자동", F_SMALL) + 10 : 0;
            rect(x + 12, ry + dlg_rowh / 2 - 6, 12, 12, carrier_col(i));
            if (dlg_carrier == i) rect(x + 15, ry + dlg_rowh / 2 - 3, 6, 6, 0xFFFFFF);
            text_fit(x + 32, ty, w - 44 - tagw, names[i], F_ITEM, 0xFFFFFF);
            if (tagw) {
                rect(x + w - 12 - tagw, ry + 4, tagw, dlg_rowh - 8, 0x2E7D32);
                text(x + w - 7 - tagw, ry + (dlg_rowh - font_height(F_SMALL)) / 2, "자동", F_SMALL, 0xFFFFFF);
            }
        } else {
            if (dlg_size == 0) snprintf(line, sizeof line, "화면 크기: ◀ 자동 ▶");
            else snprintf(line, sizeof line, "화면 크기: ◀ %dx%d ▶", sizes[dlg_size][0], sizes[dlg_size][1]);
            text_fit(x + 12, ty, w - 24, line, F_ITEM, 0xFFFFFF);
        }
    }
    iy = dlg_rows_y + 4 * (dlg_rowh + 4) + 2;
    text_fit(x + 10, iy, w - 20, g->carrier == dlg_carrier ? "확인: 선택 / 왼쪽 소프트키: 시작" : "자동감지와 다른 통신사를 골랐습니다", F_SMALL, 0xC0C8D0);
    text_fit(x + 10, iy + font_height(F_SMALL) + 4, w - 20, "통신사에 따라 키 코드가 바뀝니다", F_SMALL, 0x8090A8);
    soft_bar("시작", "취소");
}

static u8 *edit_map(void) { return ke_map; }

static void draw_keys(void)
{
    int rows = (fh - 24 - 22 - 22) / 26, i;
    u8 *m = edit_map();
    rect(0, 0, fw, fh, 0x0C1018);
    title_bar("키 설정", ke_scope_game ? "이 게임만" : "모든 게임");
    rect(0, 24, fw, 22, 0x161E2A);
    text(6, 27, ke_wait ? "▶ 지정할 휴대폰 키를 누르세요 (Esc: 취소)" : "확인: 키 지정 · 지움: 해제", F_SMALL, ke_wait ? 0xFFE060 : 0xA8B4C8);
    if (ke_sel < ke_top) ke_top = ke_sel;
    if (ke_sel >= ke_top + rows) ke_top = ke_sel - rows + 1;
    for (i = 0; i < rows && ke_top + i < PK_COUNT; i++) {
        int pk = ke_top + i, y = 46 + i * 26, on = pk == ke_sel, vk, x;
        char nm[16], list[200];
        int o = 0;
        rect(0, y, fw, 26, on ? (ke_wait ? 0x806020 : 0x284C80) : ((i & 1) ? 0x121822 : 0x0C1018));
        text(8, y + 5, pk_name(pk), F_ITEM, 0xFFFFFF);
        list[0] = 0;
        for (vk = 1; vk < 256; vk++) if (m[vk] == pk + 1 && o < 150) { vk_name(vk, nm, sizeof nm); o += snprintf(list + o, sizeof list - o, "%s%s", o ? ", " : "", nm); }
        x = fw / 2 - 10;
        text_fit(x, y + 6, fw - x - 6, list[0] ? list : "-", F_SMALL, list[0] ? 0x90E0FF : 0x606878);
    }
    soft_bar("기본값", "완료");
}

static const char *menu_items[] = { "계속하기", "키 설정", "소리", "화면", "게임 종료" };
#define NMENU 5
static void draw_menu(void)
{
    int x = 30, w = fw - 60, h = 24 + NMENU * 32 + 10, y = (fh - h) / 2, i;
    draw_game();
    draw_box(x, y, w, h, app_name());
    for (i = 0; i < NMENU; i++) {
        int ry = y + 28 + i * 32;
        char t[80];
        rect(x + 6, ry, w - 12, 28, menu_sel == i ? 0x3A68B0 : 0x232D3E);
        if (i == 2) snprintf(t, sizeof t, "소리: %s", sound_on ? "켜짐" : "꺼짐");
        else if (i == 3) snprintf(t, sizeof t, "화면: %s", scale_full ? "꽉 채움" : "비율 맞춤");
        else snprintf(t, sizeof t, "%s", menu_items[i]);
        text(x + 16, ry + 5, t, F_ITEM, 0xFFFFFF);
    }
}

static const char *opt_items[] = { "키 설정 (모든 게임)", "소리", "화면", "게임 목록 새로 고침", "도움말" };
#define NOPT 5
static void draw_options(void)
{
    int x = 20, w = fw - 40, h = 24 + NOPT * 32 + 10, y = (fh - h) / 2, i;
    draw_list();
    draw_box(x, y, w, h, "메뉴");
    for (i = 0; i < NOPT; i++) {
        int ry = y + 28 + i * 32;
        char t[80];
        rect(x + 6, ry, w - 12, 28, opt_sel == i ? 0x3A68B0 : 0x232D3E);
        if (i == 1) snprintf(t, sizeof t, "소리: %s", sound_on ? "켜짐" : "꺼짐");
        else if (i == 2) snprintf(t, sizeof t, "화면: %s", scale_full ? "꽉 채움" : "비율 맞춤");
        else snprintf(t, sizeof t, "%s", opt_items[i]);
        text(x + 16, ry + 5, t, F_ITEM, 0xFFFFFF);
    }
}

static void draw_msg(void)
{
    int x = 14, w = fw - 28, h = 150, y = (fh - h) / 2;
    char line[512], *p = msg;
    int ly = y + 30;
    if (prev_mode == M_GAME) draw_game(); else draw_list();
    draw_box(x, y, w, h, "알림");
    /* simple word wrap on bytes (UTF-8 aware enough for display) */
    while (*p && ly < y + h - 20) {
        int n = 0, last_sp = -1;
        u16 tmp[256];
        while (p[n] && p[n] != '\n') {
            int k;
            memcpy(line, p, n + 1); line[n + 1] = 0;
            k = utf8_to_u16(line, tmp, 256);
            if (g_text_width(tmp, k, F_ITEM) > w - 20) break;
            if (p[n] == ' ') last_sp = n;
            n++;
        }
        if (p[n] && p[n] != '\n' && last_sp > 0) n = last_sp + 1;
        while (n > 0 && ((u8)p[n] & 0xC0) == 0x80) n--;
        if (n == 0) n = 1;
        memcpy(line, p, n); line[n] = 0;
        text(x + 10, ly, line, F_ITEM, 0xFFFFFF);
        ly += 20;
        p += n;
        if (*p == '\n') p++;
    }
    soft_bar("", "확인");
}

void ui_draw(Surf *frame)
{
    fr = frame;
    memset(&gc, 0, sizeof gc);
    gc.s = frame; gc.cx1 = frame->w; gc.cy1 = frame->h; gc.alpha = 255;
    switch (mode) {
    case M_LIST: draw_list(); break;
    case M_CARRIER: draw_carrier(); break;
    case M_KEYS: draw_keys(); break;
    case M_MENU: draw_menu(); break;
    case M_GAME: draw_game(); break;
    case M_MSG: draw_msg(); break;
    case M_OPTIONS: draw_options(); break;
    }
    need_draw = 0;
    host.dirty = 0;
}

/* ---------------- input ---------------- */
static int key_return_mode;
static void open_keys(int game_scope, int ret)
{
    key_return_mode = ret;
    ke_scope_game = game_scope;
    ke_map = ret == M_LIST ? keymap : keymap_game;
    ke_sel = 0; ke_top = 0; ke_wait = 0;
    set_mode(M_KEYS);
}
static void close_keys(void)
{
    char kp[700];
    if (key_return_mode == M_LIST) {
        snprintf(kp, sizeof kp, "%s/keys.ini", appdir);
        save_keys(kp, keymap);
    } else {
        /* in a game: scope decides where it is stored */
        if (ke_scope_game) {
            snprintf(kp, sizeof kp, "%s/%s/keys.ini", savebase, app_id());
            save_keys(kp, keymap_game);
            has_game_keys = 1;
        } else {
            memcpy(keymap, keymap_game, 256);
            snprintf(kp, sizeof kp, "%s/keys.ini", appdir);
            save_keys(kp, keymap);
            snprintf(kp, sizeof kp, "%s/%s/keys.ini", savebase, app_id());
            pal_remove(kp);
            has_game_keys = 0;
        }
    }
    set_mode(key_return_mode);
}

static void game_key(int vk, int down, int repeat)
{
    int pk = keymap_game[vk & 255] - 1;
    if (vk == VK_F4_ || vk == VK_ESCAPE_) { if (down && !repeat) { menu_sel = 0; set_mode(M_MENU); if (sound_on) snd_set_master(0); } return; }
    if (pk < 0) return;
    ev_push(down ? (repeat ? EV_KEY_REPEAT : EV_KEY_PRESS) : EV_KEY_RELEASE, pk, 0, 0);
}

static void menu_do(int i)
{
    switch (i) {
    case 0: set_mode(M_GAME); snd_set_master(sound_on ? volume : 0); ev_push(EV_REPAINT, 0, 0, 0); break;
    case 1: open_keys(has_game_keys, M_MENU); break;
    case 2: sound_on = !sound_on; save_cfg(); need_draw = 1; break;
    case 3: scale_full = !scale_full; save_cfg(); need_draw = 1; break;
    case 4: app_request_quit(); quit_deadline = pal_ms() + 2000; set_mode(M_GAME); break;
    }
}

static void options_do(int i)
{
    switch (i) {
    case 0: open_keys(0, M_LIST); break;
    case 1: sound_on = !sound_on; snd_set_master(sound_on ? volume : 0); save_cfg(); need_draw = 1; break;
    case 2: scale_full = !scale_full; save_cfg(); need_draw = 1; break;
    case 3: { int i2; for (i2 = 0; i2 < nkv; i2++) if (!strncmp(kvs[i2].key, "list.", 5)) kvs[i2].key[0] = '#'; } scan_games(); set_mode(M_LIST); break;
    case 4:
        show_msg("게임 고르기 → 통신사 선택 → 확인. 게임 중 종료키(빨간 버튼)나 화면 옆 '메뉴'를 누르면 키 설정·소리·종료 메뉴가 열립니다. 키 설정에서 휴대폰 키마다 M480 키를 지정할 수 있습니다.", M_LIST);
        break;
    }
}

void ui_key(int vk, int down, int repeat)
{
    vk &= 255;
    if (mode == M_GAME) { game_key(vk, down, repeat); return; }
    if (mode == M_KEYS && ke_wait) {
        if (!down) return;
        if (vk == VK_ESCAPE_) { ke_wait = 0; need_draw = 1; return; }
        ke_map[vk] = (u8)(ke_sel + 1);
        ke_wait = 0;
        need_draw = 1;
        return;
    }
    if (!down) return;
    need_draw = 1;
    switch (mode) {
    case M_LIST:
        if (vk == VK_UP_ && ngames) sel = (sel + ngames - 1) % ngames;
        else if (vk == VK_DOWN_ && ngames) sel = (sel + 1) % ngames;
        else if ((vk == VK_RETURN_ || vk == VK_RIGHT_) && ngames) {
            if (games[sel].kind != PKG_JAVA) show_msg("이 게임은 ARM 기계어(네이티브) 형식이라 아직 실행할 수 없습니다. Java 형식 게임만 지원합니다.", M_LIST);
            else open_carrier_dialog();
        }
        else if (vk == VK_F1_ || vk == VK_TAB_) { opt_sel = 0; set_mode(M_OPTIONS); }
        else if (vk == VK_F2_ || vk == VK_F4_ || vk == VK_ESCAPE_) { save_cfg(); quit_flag = 1; }
        break;
    case M_OPTIONS:
        if (vk == VK_UP_) opt_sel = (opt_sel + NOPT - 1) % NOPT;
        else if (vk == VK_DOWN_) opt_sel = (opt_sel + 1) % NOPT;
        else if (vk == VK_RETURN_ || vk == VK_F1_) options_do(opt_sel);
        else if (vk == VK_F2_ || vk == VK_BACK_ || vk == VK_ESCAPE_) set_mode(M_LIST);
        break;
    case M_CARRIER:
        if (vk == VK_UP_) dlg_sel = (dlg_sel + 3) % 4;
        else if (vk == VK_DOWN_) dlg_sel = (dlg_sel + 1) % 4;
        else if ((vk == VK_LEFT_ || vk == VK_RIGHT_) && dlg_sel == 3) dlg_size = (dlg_size + (vk == VK_LEFT_ ? NSIZES - 1 : 1)) % NSIZES;
        else if (vk == VK_RETURN_ || vk == VK_F1_) {
            if (dlg_sel < 3 && dlg_carrier != dlg_sel && vk == VK_RETURN_) dlg_carrier = dlg_sel;
            else if (dlg_sel == 3 && vk == VK_RETURN_) dlg_size = (dlg_size + 1) % NSIZES;
            else start_game();
        }
        else if (vk == VK_F2_ || vk == VK_BACK_ || vk == VK_ESCAPE_) set_mode(M_LIST);
        break;
    case M_KEYS:
        if (vk == VK_UP_) ke_sel = (ke_sel + PK_COUNT - 1) % PK_COUNT;
        else if (vk == VK_DOWN_) ke_sel = (ke_sel + 1) % PK_COUNT;
        else if (vk == VK_RETURN_) ke_wait = 1;
        else if (vk == VK_BACK_) { int k; for (k = 0; k < 256; k++) if (ke_map[k] == ke_sel + 1) ke_map[k] = 0; }
        else if (vk == VK_F1_) default_keys(ke_map);
        else if (vk == VK_F2_ || vk == VK_ESCAPE_) close_keys();
        else if ((vk == VK_LEFT_ || vk == VK_RIGHT_) && key_return_mode == M_MENU) ke_scope_game = !ke_scope_game;
        break;
    case M_MENU:
        if (vk == VK_UP_) menu_sel = (menu_sel + NMENU - 1) % NMENU;
        else if (vk == VK_DOWN_) menu_sel = (menu_sel + 1) % NMENU;
        else if (vk == VK_RETURN_ || vk == VK_F1_) menu_do(menu_sel);
        else if (vk == VK_F4_ || vk == VK_F2_ || vk == VK_ESCAPE_ || vk == VK_BACK_) menu_do(0);
        break;
    case M_MSG:
        if (vk == VK_RETURN_ || vk == VK_F2_ || vk == VK_F1_ || vk == VK_BACK_ || vk == VK_ESCAPE_) {
            if (msg_back < 0) { restart_flag = 1; quit_flag = 1; }
            else set_mode(msg_back);
        }
        break;
    }
}

void ui_tap(int x, int y, int down, int drag)
{
    int i;
    if (mode == M_GAME) {
        for (i = 0; i < NBTN && !drag; i++) {
            int bx, by, bw, bh;
            if (!btn_rect(i, &bx, &by, &bw, &bh)) break;
            if (x >= bx && y >= by && x < bx + bw && y < by + bh) {
                if (down) {
                    pressed_btn = i;
                    if (side_btn[i].pk < 0) { menu_sel = 0; set_mode(M_MENU); snd_set_master(0); pressed_btn = -1; return; }
                    ev_push(EV_KEY_PRESS, side_btn[i].pk, 0, 0);
                }
                need_draw = 1;
                return;
            }
        }
        if (!down && pressed_btn >= 0) {
            ev_push(EV_KEY_RELEASE, side_btn[pressed_btn].pk, 0, 0);
            pressed_btn = -1;
            need_draw = 1;
            return;
        }
        if (x >= gx && y >= gy && x < gx + gw && y < gy + gh && host.screen) {
            int px = (x - gx) * host.screen->w / gw, py = (y - gy) * host.screen->h / gh;
            if (down && !drag) { ptr_down = 1; ev_push(EV_PTR_PRESS, px, py, 0); }
            else if (drag && ptr_down) ev_push(EV_PTR_DRAG, px, py, 0);
            else if (!down && ptr_down) { ptr_down = 0; ev_push(EV_PTR_RELEASE, px, py, 0); }
        }
        return;
    }
    if (!down || drag) return;
    /* soft bar taps */
    if (y >= fh - 22 && mode != M_MENU) { ui_key(x < fw / 2 ? VK_F1_ : VK_F2_, 1, 0); ui_key(x < fw / 2 ? VK_F1_ : VK_F2_, 0, 0); return; }
    switch (mode) {
    case M_LIST: {
        int idx = top + (y - 24) / ROWH;
        if (y >= 24 && idx < ngames) {
            if (idx == sel) ui_key(VK_RETURN_, 1, 0);
            else { sel = idx; need_draw = 1; }
        }
        break;
    }
    case M_CARRIER: {
        int ry;
        carrier_geom();
        ry = (y - dlg_rows_y) / (dlg_rowh + 4);
        if (y >= dlg_rows_y && ry >= 0 && ry < 4) {
            if (ry < 3) { if (dlg_sel == ry && dlg_carrier == ry) start_game(); else { dlg_sel = ry; dlg_carrier = ry; } }
            else { dlg_sel = 3; dlg_size = (dlg_size + (x < fw / 2 ? NSIZES - 1 : 1)) % NSIZES; }
            need_draw = 1;
        }
        break;
    }
    case M_KEYS: {
        int idx = ke_top + (y - 46) / 26;
        if (y >= 46 && idx < PK_COUNT) {
            if (idx == ke_sel) ke_wait = 1; else ke_sel = idx;
            need_draw = 1;
        } else if (y >= 24 && y < 46 && key_return_mode == M_MENU) { ke_scope_game = !ke_scope_game; need_draw = 1; }
        break;
    }
    case M_MENU: {
        int h = 24 + NMENU * 32 + 10, yy = (fh - h) / 2, idx = (y - yy - 28) / 32;
        if (y >= yy + 28 && idx >= 0 && idx < NMENU) { menu_sel = idx; menu_do(idx); }
        else menu_do(0);
        break;
    }
    case M_OPTIONS: {
        int h = 24 + NOPT * 32 + 10, yy = (fh - h) / 2, idx = (y - yy - 28) / 32;
        if (y >= yy + 28 && idx >= 0 && idx < NOPT) { opt_sel = idx; options_do(idx); }
        else set_mode(M_LIST);
        break;
    }
    case M_MSG: ui_key(VK_RETURN_, 1, 0); break;
    }
}

void ui_pause(int paused)
{
    if (mode == M_GAME || mode == M_MENU) ev_push(paused ? EV_PAUSE : EV_RESUME, 0, 0, 0);
}

int ui_tick(void)
{
    if (mode == M_GAME) {
        if (!app_step(25)) {
            if (host.fail[0]) {
                char m[400];
                snprintf(m, sizeof m, "게임을 시작하지 못했습니다: %s", host.fail);
                host.fail[0] = 0;
                show_msg(m, -1);
                prev_mode = M_LIST;
            } else { restart_flag = 1; quit_flag = 1; }
            return 1;
        }
        if (host.dirty) need_draw = 1;
        if (quit_deadline && (s32)(pal_ms() - quit_deadline) > 0) { restart_flag = 1; quit_flag = 1; }
    }
    return need_draw;
}

int ui_idle_ms(void)
{
    if (mode == M_GAME) return app_idle_ms();
    return 200;
}
