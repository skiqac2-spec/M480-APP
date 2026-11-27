/* Touch + keyboard UI (Korean) for the 320x320 screen of the SCH-M480. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host.h"

/* ---- palette ----------------------------------------------------------- */
#define C_BG     RGB565(14, 18, 24)
#define C_TITLE  RGB565(24, 44, 78)
#define C_ROW    RGB565(26, 32, 42)
#define C_ROW2   RGB565(32, 39, 51)
#define C_SEL    RGB565(38, 110, 226)
#define C_LINE   RGB565(48, 58, 74)
#define C_TEXT   RGB565(240, 244, 250)
#define C_DIM    RGB565(150, 164, 184)
#define C_ACC    RGB565(255, 184, 28)
#define C_FOOT   RGB565(20, 26, 36)
#define C_BTN    RGB565(48, 60, 82)
#define C_BTNP   RGB565(38, 110, 226)
#define C_OK     RGB565(60, 200, 110)
#define C_BAD    RGB565(236, 86, 80)

#define TITLE_H 34
#define FOOT_H  44
#define ROW_H   40
#define LIST_Y  TITLE_H
#define LIST_H  (SCR_H - TITLE_H - FOOT_H)

#define AVG565(a, b) ((uint16_t)((((a) & 0xF7DE) >> 1) + (((b) & 0xF7DE) >> 1) + ((a) & (b) & 0x0821)))

static int want_quit;

/* ---- tiny UTF-16 helpers ------------------------------------------------ */
static int wlen(const wch *s) { int n = 0; while (s[n]) n++; return n; }
static void wcpy(wch *d, const wch *s, int max)
{
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static void wcat(wch *d, const wch *s, int max)
{
    int n = wlen(d);
    if (n < max) wcpy(d + n, s, max - n);
}
static void wcat_a(wch *d, const char *s, int max)
{
    int n = wlen(d);
    while (*s && n < max - 1) d[n++] = (unsigned char)*s++;
    d[n] = 0;
}

/* ---- drawing -------------------------------------------------------------- */
static void fill(int x, int y, int w, int h, uint16_t c)
{
    int i, j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    if (w <= 0 || h <= 0) return;
    for (j = 0; j < h; j++) {
        uint16_t *p = g_fb + (y + j) * SCR_W + x;
        for (i = 0; i < w; i++) p[i] = c;
    }
}

static void frame(int x, int y, int w, int h, uint16_t c)
{
    fill(x, y, w, 1, c); fill(x, y + h - 1, w, 1, c);
    fill(x, y, 1, h, c); fill(x + w - 1, y, 1, h, c);
}

static void disc(int cx, int cy, int r, uint16_t c)
{
    int x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) fill(cx + x, cy + y, 1, 1, c);
}

static void tri_right(int x, int y, int s, uint16_t c)      /* play arrow */
{
    int i;
    for (i = 0; i < s; i++) fill(x + i, y + i / 2, 1, s - (i / 2) * 2, c);
}

static void tri_left(int x, int y, int s, uint16_t c)       /* back arrow */
{
    int i;
    for (i = 0; i < s; i++) fill(x + s - 1 - i, y + i / 2, 1, s - (i / 2) * 2, c);
}

enum { IC_NONE, IC_FOLDER, IC_DISC, IC_CARD, IC_UP, IC_PLAY, IC_GEAR, IC_HELP, IC_EXIT,
       IC_SAVE, IC_LOAD, IC_KEY };

static void icon(int ic, int x, int y)     /* 20x20 box at x,y */
{
    switch (ic) {
    case IC_FOLDER:
        fill(x + 1, y + 4, 8, 3, C_ACC); fill(x + 1, y + 6, 18, 11, C_ACC);
        fill(x + 2, y + 8, 16, 1, RGB565(200, 140, 10));
        break;
    case IC_DISC:
        disc(x + 10, y + 10, 9, RGB565(190, 200, 215)); disc(x + 10, y + 10, 6, RGB565(120, 150, 220));
        disc(x + 10, y + 10, 2, C_BG);
        break;
    case IC_CARD:
        fill(x + 4, y + 2, 12, 16, RGB565(90, 170, 240)); fill(x + 4, y + 2, 3, 3, C_BG);
        fill(x + 7, y + 12, 6, 4, C_TEXT);
        break;
    case IC_UP:
        tri_left(x + 3, y + 4, 12, C_DIM);
        break;
    case IC_PLAY:
        disc(x + 10, y + 10, 9, C_OK); tri_right(x + 7, y + 5, 10, C_TEXT);
        break;
    case IC_GEAR:
        disc(x + 10, y + 10, 8, C_DIM); fill(x + 9, y, 3, 20, C_DIM); fill(x, y + 9, 20, 3, C_DIM);
        disc(x + 10, y + 10, 3, C_BG);
        break;
    case IC_HELP:
        disc(x + 10, y + 10, 9, RGB565(90, 170, 240)); fill(x + 9, y + 9, 3, 7, C_TEXT);
        fill(x + 9, y + 5, 3, 2, C_TEXT);
        break;
    case IC_EXIT:
        disc(x + 10, y + 10, 9, C_BAD); fill(x + 5, y + 9, 10, 3, C_TEXT);
        break;
    case IC_SAVE:
        fill(x + 2, y + 2, 16, 16, RGB565(90, 170, 240)); fill(x + 5, y + 2, 10, 6, C_TEXT);
        fill(x + 5, y + 11, 10, 7, C_BG);
        break;
    case IC_LOAD:
        fill(x + 2, y + 2, 16, 16, C_OK); tri_right(x + 6, y + 5, 10, C_TEXT);
        break;
    case IC_KEY:
        fill(x + 1, y + 4, 18, 13, C_DIM); fill(x + 3, y + 6, 3, 3, C_BG); fill(x + 8, y + 6, 3, 3, C_BG);
        fill(x + 13, y + 6, 3, 3, C_BG); fill(x + 5, y + 12, 10, 3, C_BG);
        break;
    }
}

static void text(int x, int y, int w, int h, const wch *s, uint16_t c, int flags)
{
    if (s && s[0]) plat_text(x, y, w, h, s, c, flags);
}

static void present_all(void) { plat_present(0, 0, SCR_W, SCR_H); }

/* ---- list widget ---------------------------------------------------------- */
struct item { wch label[200]; wch value[28]; unsigned char ic, dim, two; };   /* two: long names over two lines */

static void item_set(struct item *it, int ic, const wch *label, const wch *value)
{
    memset(it, 0, sizeof(*it));
    it->ic = (unsigned char)ic;
    wcpy(it->label, label, 200);
    if (value) wcpy(it->value, value, 28);
}

static void draw_title(const wch *title)
{
    fill(0, 0, SCR_W, TITLE_H, C_TITLE);
    fill(0, TITLE_H - 2, SCR_W, 2, C_ACC);
    text(10, 0, SCR_W - 20, TITLE_H - 2, title, C_TEXT, TF_LEFT | TF_BIG);
}

static void draw_foot(const wch *left, const wch *right, int pressed)
{
    fill(0, SCR_H - FOOT_H, SCR_W, FOOT_H, C_FOOT);
    fill(0, SCR_H - FOOT_H, SCR_W, 1, C_LINE);
    if (left) {
        fill(6, SCR_H - FOOT_H + 6, 110, FOOT_H - 12, pressed == 1 ? C_BTNP : C_BTN);
        tri_left(14, SCR_H - FOOT_H + 16, 12, C_TEXT);
        text(30, SCR_H - FOOT_H + 6, 82, FOOT_H - 12, left, C_TEXT, TF_CENTER);
    }
    if (right)
        text(124, SCR_H - FOOT_H, SCR_W - 132, FOOT_H, right, C_DIM, TF_RIGHT | TF_SMALL);
}

/* A file name in a list row: one line if it fits, otherwise two lines in the small font.
 * When even two lines are not enough the middle is dropped and the END is kept, because
 * names of a series ("Super Robot Taisen ...") differ at the end. */
static void draw_name2(int x, int y, int w, const wch *name, uint16_t c)
{
    wch l1[200], l2[200];
    int n = wlen(name), cut, i, sp;

    if (plat_text_width(name, n, 0) <= w) { text(x, y, w, ROW_H - 1, name, c, TF_LEFT); return; }
    if (plat_text_width(name, n, TF_SMALL) <= w) { text(x, y, w, ROW_H - 1, name, c, TF_LEFT | TF_SMALL); return; }
    /* first line: as many characters as fit, broken after a space if there is one nearby */
    cut = n;
    while (cut > 1 && plat_text_width(name, cut, TF_SMALL) > w) cut--;
    for (sp = cut; sp > cut / 2 && name[sp - 1] != ' '; sp--) ;
    if (sp > cut / 2 && cut < n) cut = sp;
    for (i = 0; i < cut && i < 199; i++) l1[i] = name[i];
    l1[i] = 0;
    while (name[cut] == ' ') cut++;
    /* second line: the rest, or "..." + the end of the name */
    if (plat_text_width(name + cut, -1, TF_SMALL) <= w) wcpy(l2, name + cut, 200);
    else {
        int k = n;
        while (k > cut && plat_text_width(name + k - 1, n - k + 1, TF_SMALL) + plat_text_width(u"…", 1, TF_SMALL) <= w) k--;
        if (k > 0 && name[k - 1] != ' ') {           /* start at a whole word when one is close */
            int j = k;
            while (j < n && name[j] != ' ') j++;
            if (j < n && j - k <= 8) k = j;
        }
        while (name[k] == ' ' && k < n) k++;
        l2[0] = u'…'; wcpy(l2 + 1, name + k, 199);
    }
    text(x, y + 2, w, 18, l1, c, TF_LEFT | TF_SMALL);
    text(x, y + 19, w, 18, l2, c, TF_LEFT | TF_SMALL);
}

static void draw_list(const wch *title, struct item *it, int n, int sel, int scroll,
                      const wch *back, const wch *hint, int foot_pressed)
{
    int i, total = n * ROW_H;
    fill(0, LIST_Y, SCR_W, LIST_H, C_BG);
    for (i = 0; i < n; i++) {
        int y = LIST_Y + i * ROW_H - scroll;
        int tx = 12;
        if (y + ROW_H <= LIST_Y || y >= LIST_Y + LIST_H) continue;
        fill(0, y, SCR_W, ROW_H - 1, i == sel ? C_SEL : ((i & 1) ? C_ROW2 : C_ROW));
        if (it[i].ic) { icon(it[i].ic, 10, y + 10); tx = 40; }
        if (it[i].two) {
            draw_name2(tx, y, SCR_W - tx - (it[i].value[0] ? 84 : 12), it[i].label, it[i].dim ? C_DIM : C_TEXT);
            if (it[i].value[0]) text(SCR_W - 84, y, 76, ROW_H - 1, it[i].value, i == sel ? C_TEXT : C_ACC, TF_RIGHT | TF_SMALL);
        } else if (it[i].value[0]) {
            text(tx, y, SCR_W - tx - 118, ROW_H - 1, it[i].label, it[i].dim ? C_DIM : C_TEXT, TF_LEFT);
            text(SCR_W - 116, y, 106, ROW_H - 1, it[i].value, i == sel ? C_TEXT : C_ACC, TF_RIGHT);
        } else
            text(tx, y, SCR_W - tx - 10, ROW_H - 1, it[i].label, it[i].dim ? C_DIM : C_TEXT, TF_LEFT);
    }
    if (total > LIST_H) {                         /* scroll indicator */
        int bh = LIST_H * LIST_H / total, by = LIST_Y + scroll * LIST_H / total;
        fill(SCR_W - 4, by, 3, bh, C_DIM);
    }
    draw_title(title);
    draw_foot(back, hint, foot_pressed);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* returns the activated row, or -1 for back */
static int ui_list(const wch *title, struct item *it, int n, int *psel,
                   const wch *back, const wch *hint)
{
    struct event ev;
    int sel = psel ? clampi(*psel, 0, n > 0 ? n - 1 : 0) : 0;
    int maxs = n * ROW_H - LIST_H;
    int scroll, dirty = 1, down = 0, down_y = 0, down_scroll = 0, drag = 0, foot = 0;
    int result = -2;

    if (maxs < 0) maxs = 0;
    scroll = 0;                                   /* scroll only as far as needed to show the selection */
    if (sel * ROW_H + ROW_H > LIST_H) scroll = clampi(sel * ROW_H + ROW_H - LIST_H, 0, maxs);

    while (result == -2 && !want_quit) {
        if (dirty) {
            draw_list(title, it, n, sel, scroll, back, hint, foot);
            present_all();
            dirty = 0;
        }
        if (!plat_poll(&ev)) { plat_wait_event(200); continue; }
        switch (ev.type) {
        case EV_QUIT: want_quit = 1; break;
        case EV_KEYDOWN:
            if (ev.key == K_UP && n) { sel = (sel + n - 1) % n; }
            else if (ev.key == K_DOWN && n) { sel = (sel + 1) % n; }
            else if (ev.key == K_LEFT && n) { sel = clampi(sel - 5, 0, n - 1); }
            else if (ev.key == K_RIGHT && n) { sel = clampi(sel + 5, 0, n - 1); }
            else if ((ev.key == K_ENTER || ev.key == K_SPACE || ev.key == K_F1) && n) { result = sel; break; }
            else if (ev.key == K_ESC || ev.key == K_BACK || ev.key == K_F2) { result = -1; break; }
            else break;
            if (sel * ROW_H < scroll) scroll = sel * ROW_H;
            if (sel * ROW_H + ROW_H > scroll + LIST_H) scroll = sel * ROW_H + ROW_H - LIST_H;
            scroll = clampi(scroll, 0, maxs);
            dirty = 1;
            break;
        case EV_TDOWN:
            down = 1; drag = 0; down_y = ev.y; down_scroll = scroll;
            if (ev.y >= SCR_H - FOOT_H) { if (ev.x < 124 && back) { foot = 1; dirty = 1; } }
            else if (ev.y >= LIST_Y) {
                int r = (ev.y - LIST_Y + scroll) / ROW_H;
                if (r >= 0 && r < n) { sel = r; dirty = 1; }
            }
            break;
        case EV_TMOVE:
            if (!down || foot || down_y < LIST_Y || down_y >= SCR_H - FOOT_H) break;
            if (!drag && abs(ev.y - down_y) > 10) drag = 1;
            if (drag) {
                int ns = clampi(down_scroll - (ev.y - down_y), 0, maxs);
                if (ns != scroll) { scroll = ns; dirty = 1; }
            }
            break;
        case EV_TUP:
            if (!down) break;
            down = 0;
            if (foot) {
                foot = 0; dirty = 1;
                if (ev.y >= SCR_H - FOOT_H - 10 && ev.x < 134) result = -1;
            } else if (!drag && down_y >= LIST_Y && down_y < SCR_H - FOOT_H) {
                int r = (down_y - LIST_Y + scroll) / ROW_H;
                if (r >= 0 && r < n) { sel = r; result = r; }
            }
            break;
        }
    }
    if (psel) *psel = sel;
    return result == -2 ? -1 : result;
}

/* ---- message box / busy ---------------------------------------------------- */
static void ui_busy(const wch *msg)
{
    fill(0, 0, SCR_W, SCR_H, C_BG);
    disc(SCR_W / 2, 120, 22, C_TITLE); disc(SCR_W / 2, 120, 16, RGB565(120, 150, 220));
    disc(SCR_W / 2, 120, 5, C_BG);
    text(10, 160, SCR_W - 20, 60, msg, C_TEXT, TF_CENTER | TF_WRAP);
    present_all();
}

/* returns 1 if the first (primary) button was chosen */
/* Message / question box.  Made hard to hit by accident:
 *  - touches during the first 0.4 s are ignored (the tap that opened it cannot answer it)
 *  - a button only counts if the finger is lifted on that same button
 *  - with two buttons the second one (cancel) is pre-selected for the keys */
static int ui_msg(const wch *title, const wch *body, const wch *b1, const wch *b2)
{
    struct event ev;
    int dirty = 1, sel = b2 ? 1 : 0, res = -1, down = -1;
    const int by = 236, bh = 48;
    unsigned t0 = plat_ticks();

    while (plat_poll(&ev)) if (ev.type == EV_QUIT) want_quit = 1;   /* drop input already queued */
    while (res < 0 && !want_quit) {
        if (dirty) {
            int w1 = b2 ? 142 : 296;
            fill(0, 0, SCR_W, SCR_H, C_BG);
            draw_title(title);
            text(14, TITLE_H + 10, SCR_W - 28, by - TITLE_H - 20, body, C_TEXT, TF_LEFT | TF_WRAP);
            fill(12, by, w1, bh, (sel == 0) ? C_SEL : C_BTN);
            text(12, by, w1, bh, b1, C_TEXT, TF_CENTER);
            if (b2) {
                fill(166, by, 142, bh, (sel == 1) ? C_SEL : C_BTN);
                text(166, by, 142, bh, b2, C_TEXT, TF_CENTER);
            }
            present_all();
            dirty = 0;
        }
        if (!plat_poll(&ev)) { plat_wait_event(200); continue; }
        switch (ev.type) {
        case EV_QUIT: want_quit = 1; break;
        case EV_KEYDOWN:
            if ((ev.key == K_LEFT || ev.key == K_RIGHT || ev.key == K_UP || ev.key == K_DOWN) && b2) { sel ^= 1; dirty = 1; }
            else if (ev.key == K_ENTER || ev.key == K_SPACE || ev.key == K_F1) res = (sel == 0);
            else if (ev.key == K_ESC || ev.key == K_BACK || ev.key == K_F2) res = b2 ? 0 : 1;
            break;
        case EV_TDOWN:
            down = -1;
            if (plat_ticks() - t0 < 400) break;
            if (ev.y >= by - 6 && ev.y < by + bh + 10) {
                down = (b2 && ev.x >= 160) ? 1 : 0;
                sel = down; dirty = 1;
            }
            break;
        case EV_TMOVE:
            if (down >= 0) {
                int on = ev.y >= by - 16 && ev.y < by + bh + 20 && (b2 ? ((ev.x >= 160) == (down == 1)) : 1);
                if (!on) { down = -1; dirty = 1; }       /* slid off: cancelled */
            }
            break;
        case EV_TUP:
            if (down >= 0 && ev.y >= by - 16 && ev.y < by + bh + 20 && (b2 ? ((ev.x >= 160) == (down == 1)) : 1))
                res = (down == 0);
            down = -1;
            break;
        }
    }
    return res > 0;
}

/* ---- key names -------------------------------------------------------------- */
static void key_name(int k, wch *out, int max)
{
    out[0] = 0;
    if (k == 0) { wcpy(out, u"-", max); return; }
    if ((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9')) { out[0] = (wch)k; out[1] = 0; return; }
    switch (k) {
    case K_UP: wcpy(out, u"방향 위", max); return;
    case K_DOWN: wcpy(out, u"방향 아래", max); return;
    case K_LEFT: wcpy(out, u"방향 왼쪽", max); return;
    case K_RIGHT: wcpy(out, u"방향 오른쪽", max); return;
    case K_ENTER: wcpy(out, u"엔터", max); return;
    case K_SPACE: wcpy(out, u"스페이스", max); return;
    case K_BACK: wcpy(out, u"지우기", max); return;
    case K_ESC: wcpy(out, u"취소/뒤로", max); return;
    case K_TAB: wcpy(out, u"탭", max); return;
    case K_F1: wcpy(out, u"왼쪽 소프트키", max); return;
    case K_F2: wcpy(out, u"오른쪽 소프트키", max); return;
    case 0xBC: wcpy(out, u",", max); return;
    case 0xBE: wcpy(out, u".", max); return;
    }
    {
        char b[16];
        snprintf(b, sizeof(b), "#%d", k);
        wcpy(out, u"키 ", max); wcat_a(out, b, max);
    }
}

static const wch *bind_label(int i)
{
    return i == KEY_MENU_IDX ? u"메뉴 열기" : sys.btn_names[i];
}

static int capture_key(int idx)
{
    struct event ev;
    wch msg[160];
    wcpy(msg, u"[", 160); wcat(msg, bind_label(idx), 160);
    wcat(msg, u"] 에 쓸 키를 지금 누르세요.\n\n화면을 터치하면 취소됩니다.", 160);
    fill(0, 0, SCR_W, SCR_H, C_BG);
    draw_title(u"키 설정");
    icon(IC_KEY, SCR_W / 2 - 10, 70);
    text(14, 110, SCR_W - 28, 150, msg, C_TEXT, TF_CENTER | TF_WRAP);
    present_all();
    while (plat_poll(&ev)) ;                    /* drop pending input */
    for (;;) {
        if (want_quit) return 0;
        if (!plat_poll(&ev)) { plat_wait_event(200); continue; }
        if (ev.type == EV_QUIT) { want_quit = 1; return 0; }
        if (ev.type == EV_TUP) return 0;
        if (ev.type == EV_KEYDOWN && ev.key > 0 && ev.key < 256) return ev.key;
    }
}

static void menu_keys(void)
{
    struct item it[BIND_COUNT + 1];
    int map[BIND_COUNT + 1];
    int sel = 0, i, j, r, n;
    for (;;) {
        n = 0;
        for (i = 0; i < BIND_COUNT; i++) {
            wch v[28], v2[28];
            if (!bind_label(i)) continue;
            key_name(cfg.keys[i][0], v, 28);
            if (cfg.keys[i][1]) { key_name(cfg.keys[i][1], v2, 28); wcat(v, u"/", 28); wcat(v, v2, 28); }
            item_set(&it[n], IC_NONE, bind_label(i), v);
            map[n++] = i;
        }
        item_set(&it[n], IC_NONE, u"키 배치를 처음 상태로", NULL);
        map[n++] = -1;
        r = ui_list(u"키 설정", it, n, &sel, u"뒤로", u"바꿀 항목을 누르세요");
        if (r < 0) break;
        if (map[r] < 0) {
            struct settings keep = cfg;
            cfg_defaults();
            memcpy(keep.keys, cfg.keys, sizeof(cfg.keys));
            cfg = keep;
            continue;
        }
        j = capture_key(map[r]);
        if (j) {
            int a, b;
            for (a = 0; a < BIND_COUNT; a++)
                for (b = 0; b < 2; b++)
                    if (cfg.keys[a][b] == j) cfg.keys[a][b] = 0;
            cfg.keys[map[r]][0] = j; cfg.keys[map[r]][1] = 0;
            for (a = 0; a < BIND_COUNT; a++)          /* keep slot 0 filled when possible */
                if (!cfg.keys[a][0] && cfg.keys[a][1]) { cfg.keys[a][0] = cfg.keys[a][1]; cfg.keys[a][1] = 0; }
        }
    }
    cfg_save();
}

/* ---- help ---------------------------------------------------------------------- */
static void menu_help(void)
{
    static const wch *const pages[] = {
        u"1. 게임 넣기\n\n"
        u"게임 파일을 메모리 카드의 아무 폴더에나 넣으세요.\n"
        u"예) \\Storage Card\\Games\\\n\n"
        u"[게임 고르기]에서 폴더를 눌러 들어가 게임을 누르면 "
        u"어떤 게임기용인지 알아서 판단해 바로 시작합니다.\n\n"
        u"zip으로 압축된 게임도 그대로 됩니다 (PS1 제외).",

        u"2. 되는 게임기와 파일\n\n"
        u"PS1: cue+bin, img, iso, pbp\n"
        u"GBA: gba\n"
        u"패미컴: nes\n"
        u"슈퍼패미컴: smc, sfc\n"
        u"게임보이/컬러: gb, gbc\n"
        u"메가드라이브·마스터시스템·게임기어: md, gen, bin, sms, gg\n\n"
        u"목록에 보이지 않는 파일은 지원하지 않는 형식입니다.",

        u"3. 조작 (기본 키)\n\n"
        u"방향: 방향키 또는 W A S D\n"
        u"버튼 4개: I(위) J(왼쪽) K(아래) L(오른쪽)\n"
        u"어깨 버튼: U, O  (PS1은 Y, P 추가)\n"
        u"START 엔터   SELECT 스페이스\n"
        u"메뉴: 지우기(Del) 키 또는 화면 아래 [메뉴]\n\n"
        u"게임기별 버튼 이름 확인과 변경은 [설정] > [키 설정].",

        u"4. 화면 터치 버튼\n\n"
        u"게임 화면 왼쪽 아래 십자키, 오른쪽 아래 버튼을 손가락이나 펜으로 누르세요. "
        u"누른 채로 미끄러뜨리면 방향이 바뀝니다. 두 버튼 사이를 누르면 둘 다 눌립니다.\n\n"
        u"[설정]에서 켜고 끄기, 투명도(0~100%), 크기(5단계), 십자키 방식(대각선 8방향/십자 4방향)을 바꿀 수 있습니다. "
        u"100%는 보이지 않지만 누르면 동작합니다.",

        u"5. 저장하기\n\n"
        u"게임 자체의 저장 기능은 그대로 쓸 수 있습니다.\n\n"
        u"게임 도중 [메뉴]에서 [지금 상태 저장]을 누르면 "
        u"어느 장면에서든 저장되고, [저장한 곳에서 다시]로 돌아옵니다.\n\n"
        u"게임을 끝낼 때는 자동으로 저장되어 [이어하기]로 계속할 수 있습니다.",

        u"6. 느릴 때\n\n"
        u"게임 중 [메뉴] > [설정]에서\n"
        u"· 화면 건너뛰기를 [자동] 또는 [2]로\n"
        u"· PS1은 성능 모드를 [빠름]이나 [최고속]으로\n"
        u"· 그래도 느리면 소리를 [끔]으로\n\n"
        u"PS1의 3D 게임, 슈퍼패미컴의 특수칩 게임은 이 기기에서 느릴 수 있습니다.",

        u"7. BIOS (선택 사항)\n\n"
        u"BIOS 파일 없이도 실행됩니다. 다만 PS1은 실제 BIOS가 있으면 "
        u"더 많은 게임이 정상 동작합니다.\n\n"
        u"가지고 있는 기기에서 추출한 BIOS 파일을 프로그램 폴더 안의 "
        u"bios 폴더에 넣으세요.\n"
        u"PS1: scph1001.bin 등\nGBA: gba_bios.bin",
    };
    const int np = (int)(sizeof(pages) / sizeof(pages[0]));
    struct event ev;
    int p = 0, dirty = 1, down_x = -1;

    while (!want_quit) {
        if (dirty) {
            wch pg[16]; char b[16];
            fill(0, 0, SCR_W, SCR_H, C_BG);
            draw_title(u"사용 방법");
            text(14, TITLE_H + 8, SCR_W - 28, SCR_H - TITLE_H - FOOT_H - 12, pages[p], C_TEXT, TF_LEFT | TF_WRAP | TF_SMALL);
            draw_foot(u"뒤로", NULL, 0);
            fill(204, SCR_H - FOOT_H + 6, 110, FOOT_H - 12, C_BTN);
            snprintf(b, sizeof(b), " (%d/%d)", p + 1, np);
            wcpy(pg, p + 1 < np ? u"다음" : u"처음", 16); wcat_a(pg, b, 16);
            text(204, SCR_H - FOOT_H + 6, 110, FOOT_H - 12, pg, C_TEXT, TF_CENTER);
            present_all();
            dirty = 0;
        }
        if (!plat_poll(&ev)) { plat_wait_event(200); continue; }
        if (ev.type == EV_QUIT) { want_quit = 1; break; }
        if (ev.type == EV_KEYDOWN) {
            if (ev.key == K_ESC || ev.key == K_BACK || ev.key == K_F2) break;
            if (ev.key == K_RIGHT || ev.key == K_DOWN || ev.key == K_ENTER || ev.key == K_SPACE) { p = (p + 1) % np; dirty = 1; }
            if (ev.key == K_LEFT || ev.key == K_UP) { p = (p + np - 1) % np; dirty = 1; }
        }
        if (ev.type == EV_TDOWN) down_x = (ev.y >= SCR_H - FOOT_H) ? ev.x : -2;
        if (ev.type == EV_TUP) {
            if (down_x >= 0 && down_x < 124) break;
            if (down_x >= 0 || down_x == -2) { p = (p + 1) % np; dirty = 1; }
            down_x = -1;
        }
    }
}

/* ---- settings ------------------------------------------------------------------- */
enum { ST_AHQ = 100, ST_VOL, ST_DPAD };
enum { ST_SPEED, ST_FSKIP, ST_SOUND, ST_CDDA, ST_STRETCH, ST_FPS, ST_TOUCH, ST_TALPHA, ST_TSIZE, ST_KEYS, ST_REGION, ST_BIOS, ST_RESET };

static void menu_settings(void)
{
    static const wch *const fs[] = { u"끔", u"자동", u"1", u"2", u"3" };
    static const wch *const spd[] = { u"화질 우선", u"균형", u"빠름", u"최고속" };
    static const wch *const reg[] = { u"자동", u"NTSC", u"PAL" };
    struct item it[20];
    int id[20];
    int sel = 0, r, n, ps1 = (sys.flags & SF_PS1OPTS) != 0;
    wch title[48];

    wcpy(title, u"설정 - ", 48); wcat(title, sys.name, 48);
    for (;;) {
        n = 0;
        if (ps1) { item_set(&it[n], IC_NONE, u"성능 모드", spd[cfg.speed]); id[n++] = ST_SPEED; }
        if (sys.flags & SF_FRAMESKIP) { item_set(&it[n], IC_NONE, u"화면 건너뛰기", fs[cfg.frameskip]); id[n++] = ST_FSKIP; }
        item_set(&it[n], IC_NONE, u"소리", cfg.sound ? u"켬" : u"끔"); id[n++] = ST_SOUND;
        if (cfg.sound) {
            static const wch *const vol[] = { u"보통", u"작게", u"크게" };
            item_set(&it[n], IC_NONE, u"  소리 음질", cfg.audio_hq ? u"높음 (블루투스)" : u"보통 (권장)"); id[n++] = ST_AHQ;
            item_set(&it[n], IC_NONE, u"  소리 크기", vol[cfg.volume]); id[n++] = ST_VOL;
        }
        if (ps1) { item_set(&it[n], IC_NONE, u"CD 음악/음성", cfg.cdda ? u"켬" : u"끔"); id[n++] = ST_CDDA; }
        item_set(&it[n], IC_NONE, u"화면 크게 채우기", cfg.stretch ? u"켬" : u"끔"); id[n++] = ST_STRETCH;
        item_set(&it[n], IC_NONE, u"속도(FPS) 표시", cfg.show_fps ? u"켬" : u"끔"); id[n++] = ST_FPS;
        item_set(&it[n], IC_NONE, u"화면 터치 버튼", cfg.touchpad ? u"켬" : u"끔"); id[n++] = ST_TOUCH;
        if (cfg.touchpad) {
            static const wch *const ta[] = { u"0% (진하게)", u"25%", u"50%", u"75%", u"100% (안 보임)" };
            item_set(&it[n], IC_NONE, u"  터치 버튼 투명도", ta[cfg.touch_alpha]); id[n++] = ST_TALPHA;
            static const wch *const tsz[] = { u"아주 작게", u"작게", u"보통", u"크게", u"아주 크게" };
            item_set(&it[n], IC_NONE, u"  터치 버튼 크기", tsz[cfg.touch_size]); id[n++] = ST_TSIZE;
            item_set(&it[n], IC_NONE, u"  십자키 방식", cfg.dpad_4way ? u"십자 (4방향)" : u"대각선 (8방향)"); id[n++] = ST_DPAD;
        }
        item_set(&it[n], IC_KEY, u"키 설정", NULL); id[n++] = ST_KEYS;
        if (ps1) { item_set(&it[n], IC_NONE, u"영상 방식 *", reg[cfg.region]); id[n++] = ST_REGION; }
        if (ps1) {
            item_set(&it[n], IC_NONE, u"BIOS *", cfg.use_bios ? (sys.bios_found && sys.bios_found() ? u"파일 사용" : u"파일 없음") : u"내장만 사용");
            id[n++] = ST_BIOS;
        }
        item_set(&it[n], IC_NONE, u"모든 설정 처음 상태로", NULL); id[n++] = ST_RESET;
        r = ui_list(title, it, n, &sel, u"뒤로", ps1 ? u"* 다음 게임부터 적용" : NULL);
        if (r < 0) break;
        switch (id[r]) {
        case ST_SPEED: cfg.speed = (cfg.speed + 1) % 4; break;
        case ST_FSKIP: cfg.frameskip = (cfg.frameskip + 1) % 5; break;
        case ST_SOUND: cfg.sound ^= 1; break;
        case ST_AHQ: cfg.audio_hq ^= 1; break;
        case ST_VOL: cfg.volume = (cfg.volume + 1) % 3; break;
        case ST_CDDA: cfg.cdda ^= 1; break;
        case ST_STRETCH: cfg.stretch ^= 1; break;
        case ST_FPS: cfg.show_fps ^= 1; break;
        case ST_TOUCH: cfg.touchpad ^= 1; break;
        case ST_TALPHA: cfg.touch_alpha = (cfg.touch_alpha + 1) % 5; break;
        case ST_TSIZE: cfg.touch_size = (cfg.touch_size + 1) % 5; break;
        case ST_DPAD: cfg.dpad_4way ^= 1; break;
        case ST_KEYS: menu_keys(); break;
        case ST_REGION: cfg.region = (cfg.region + 1) % 3; break;
        case ST_BIOS: cfg.use_bios ^= 1; break;
        case ST_RESET:
            if (ui_msg(u"설정 초기화", u"모든 설정과 키 배치를 처음 상태로 되돌릴까요?", u"되돌리기", u"취소"))
                cfg_defaults();
            break;
        }
        core_apply_settings();
    }
    cfg_save();
}

/* ---- file browser ----------------------------------------------------------------- */
static int ext_is(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext), i;
    if (n <= e) return 0;
    for (i = 0; i < e; i++) {
        char c = name[n - e + i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != ext[i]) return 0;
    }
    return 1;
}

static int same_base(const char *a, const char *b)   /* compare names without extension, case-insensitive */
{
    const char *ea = strrchr(a, '.'), *eb = strrchr(b, '.');
    size_t na = ea ? (size_t)(ea - a) : strlen(a), nb = eb ? (size_t)(eb - b) : strlen(b), i;
    if (na != nb) return 0;
    for (i = 0; i < na; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return 1;
}

static int cmp_ent(const void *a, const void *b)
{
    const struct dirent_h *x = a, *y = b;
    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
    if (x->is_card != y->is_card) return y->is_card - x->is_card;
    return strcmp(x->name, y->name);
}

static int is_root(const char *d) { return d[0] == 0 || (d[0] == PSEP && d[1] == 0); }

static void path_up(char *d)
{
    char *p = strrchr(d, PSEP);
    if (!p || p == d) { d[0] = PSEP; d[1] = 0; if (PSEP == '\\') d[0] = 0; }
    else *p = 0;
}

/* lets the user pick a game file; returns 1 and fills out[] */
static int browse(char *out, const wch *title)
{
    char dir[HPATH];
    int sel = 0;

    snprintf(dir, sizeof(dir), "%s", cfg.last_dir);
    for (;;) {
        struct dirent_h *ents = NULL;
        struct item *it;
        int *map;
        int n, i, j, cnt = 0, r, up = !is_root(dir), ngames = 0;
        wch t[100];

        n = plat_list_dir(dir, &ents);
        if (n < 0) {                                  /* folder vanished: go to the root */
            if (is_root(dir)) n = 0; else { dir[0] = 0; if (PSEP == '/') { dir[0] = '/'; dir[1] = 0; } continue; }
        }
        if (n > 1) qsort(ents, n, sizeof(*ents), cmp_ent);
        it = malloc(sizeof(*it) * (n + 2));
        map = malloc(sizeof(int) * (n + 2));
        if (!it || !map) { free(it); free(map); free(ents); return 0; }

        if (up) { item_set(&it[cnt], IC_UP, u"위 폴더로", NULL); map[cnt++] = -1; }
        for (i = 0; i < n; i++) {
            int si = -1;
            if (ents[i].is_dir) {
                if (ents[i].name[0] == '.') continue;
            } else {
                si = sysreg_classify(dir, ents[i].name);
                if (si == -1) continue;
                if (ext_is(ents[i].name, ".bin") || ext_is(ents[i].name, ".img")) {
                    int dup = 0;                      /* hide data tracks that have a .cue/.ccd sheet */
                    for (j = 0; j < n && !dup; j++)
                        if (j != i && !ents[j].is_dir && same_base(ents[i].name, ents[j].name) &&
                            (ext_is(ents[j].name, ".cue") || ext_is(ents[j].name, ".ccd")))
                            dup = 1;
                    if (dup) continue;
                }
                ngames++;
            }
            memset(&it[cnt], 0, sizeof(it[cnt]));
            it[cnt].ic = ents[i].is_dir ? (ents[i].is_card ? IC_CARD : IC_FOLDER) : IC_DISC;
            plat_path_to_wch(ents[i].name, it[cnt].label, 200);
            if (!ents[i].is_dir) {
                int k = wlen(it[cnt].label);
                while (k > 0 && it[cnt].label[k - 1] != '.') k--;
                if (k > 1) it[cnt].label[k - 1] = 0;          /* the system tag replaces the extension */
                wcpy(it[cnt].value, si >= 0 ? sysreg_tag(si) : u"ZIP", 28);
                it[cnt].two = 1;
            } else
                it[cnt].two = 1;
            map[cnt++] = i;
        }
        if (cnt == (up ? 1 : 0)) {
            item_set(&it[cnt], IC_NONE, u"(이 폴더에는 게임이 없습니다)", NULL);
            it[cnt].dim = 1; map[cnt++] = -2;
        }
        if (is_root(dir)) wcpy(t, title, 100);
        else {
            const char *b = strrchr(dir, PSEP);
            plat_path_to_wch(b ? b + 1 : dir, t, 100);
        }
        if (sel >= cnt) sel = 0;
        r = ui_list(t, it, cnt, &sel, u"뒤로", ngames ? u"게임을 누르면 시작" : u"폴더를 눌러 들어가세요");
        if (r < 0) { free(it); free(map); free(ents); return 0; }
        i = map[r];
        if (i == -1) { path_up(dir); sel = 0; }
        else if (i >= 0 && ents[i].is_dir) {
            size_t l = strlen(dir);
            if (l + strlen(ents[i].name) + 2 < sizeof(dir)) {
                if (!(l && dir[l - 1] == PSEP)) dir[l++] = PSEP;
                strcpy(dir + l, ents[i].name);
            }
            sel = 0;
        } else if (i >= 0) {
            size_t l = strlen(dir);
            snprintf(out, HPATH, "%s%s%s", dir, (l && dir[l - 1] == PSEP) ? "" : (PSEP == '\\' ? "\\" : "/"), ents[i].name);
            snprintf(cfg.last_dir, HPATH, "%s", dir);
            free(it); free(map); free(ents);
            return 1;
        }
        free(it); free(map); free(ents);
    }
}

/* ---- in-game ------------------------------------------------------------------------ */
#define AUTO_SLOT 9
#define COMPACT (BAR_H < 50)                     /* Game Boy layout: one 32 pixel row */
#define STAT_H (COMPACT ? 0 : 20)
#define BTN_Y (BAR_Y + STAT_H)
#define BTN_H (BAR_H - STAT_H)
#define MENU_W 62
#define BTN_W 43

static wch bar_msg[64];
static unsigned bar_msg_until;

static int nbar(void)
{
    int n = 0;
    while (n < 6 && sys.bar[n] >= 0) n++;
    return n;
}

static void draw_bar(int pressed)
{
    int i, n = nbar(), sx, sw;
    wch st[64];
    uint16_t fc;

    fill(0, BAR_Y, SCR_W, BAR_H, C_FOOT);
    if (COMPACT) { sx = MENU_W + n * BTN_W + 4; sw = SCR_W - sx - 4; }
    else { sx = 6; sw = SCR_W - 12; }
    if (bar_msg[0])
        text(sx, BAR_Y, sw, COMPACT ? BAR_H : STAT_H, bar_msg, C_ACC, TF_LEFT | TF_SMALL);
    else {
        if (!COMPACT) {
            plat_path_to_wch(core_game_name(), st, 40);
            text(sx, BAR_Y, 194, STAT_H, st, C_DIM, TF_LEFT | TF_SMALL);
        }
        if (cfg.show_fps && core_fps_x10 > 0) {
            char b[32];
            snprintf(b, sizeof(b), "%d.%dfps %d%%", core_fps_x10 / 10, core_fps_x10 % 10, core_speed_pct);
            st[0] = 0; wcat_a(st, b, 64);
            fc = core_speed_pct >= 95 ? C_OK : core_speed_pct >= 70 ? C_ACC : C_BAD;
            if (COMPACT) text(sx, BAR_Y, sw, BAR_H, st, fc, TF_RIGHT | TF_SMALL);
            else text(200, BAR_Y, 114, STAT_H, st, fc, TF_RIGHT | TF_SMALL);
        }
    }
    for (i = 0; i <= n; i++) {
        int x = i ? MENU_W + (i - 1) * BTN_W : 0, w = i ? BTN_W : MENU_W;
        uint16_t c = i == pressed ? C_BTNP : (i == 0 ? C_TITLE : C_BTN);
        fill(x + 1, BTN_Y + 1, w - 2, BTN_H - 2, c);
        text(x, BTN_Y, w, BTN_H, i ? sys.bar_names[i - 1] : u"메뉴", C_TEXT, TF_CENTER | (i ? TF_SMALL : 0));
    }
    plat_present(0, BAR_Y, SCR_W, BAR_H);
}

static void bar_say(const wch *msg)
{
    wcpy(bar_msg, msg, 64);
    bar_msg_until = plat_ticks() + 2500;
}

/* 0 = menu, 1.. = sys.bar[i-1], -1 = nothing */
static int bar_hit(int x, int y)
{
    int n = nbar();
    if (y < BTN_Y - 6) return -1;
    if (x < MENU_W) return 0;
    x = (x - MENU_W) / BTN_W;
    return x < n ? x + 1 : -1;
}

static void slot_label(wch *out, int max, const wch *prefix)
{
    char b[8];
    wcpy(out, prefix, max);
    snprintf(b, sizeof(b), " %d", cfg.slot + 1);
    wcat_a(out, b, max);
}

enum { PM_RESUME, PM_QUIT_GAME, PM_NEW_GAME };

/* in-game pause menu; returns PM_* */
static int pause_menu(char *newgame)
{
    struct item it[10];
    int sel = 0, r, n, ndisk;
    wch v[28];

    plat_audio_pause(1);
    core_screen_save();
    core_flush_memcard(1);
    for (;;) {
        if (want_quit) return PM_QUIT_GAME;
        ndisk = core_disk_count();
        n = 0;
        item_set(&it[n++], IC_PLAY, u"게임으로 돌아가기", NULL);
        slot_label(v, 28, u"칸");
        item_set(&it[n++], IC_SAVE, u"지금 상태 저장", v);
        item_set(&it[n++], IC_LOAD, u"저장한 곳에서 다시", core_state_exists(cfg.slot) ? v : u"(비어 있음)");
        if (!core_state_exists(cfg.slot)) it[n - 1].dim = 1;
        item_set(&it[n++], IC_NONE, u"저장 칸 바꾸기", v);
        item_set(&it[n++], IC_GEAR, u"설정", NULL);
        if (ndisk > 1) {
            char b[16]; wch dv[28];
            snprintf(b, sizeof(b), "%d / %d", core_disk_index() + 1, ndisk);
            dv[0] = 0; wcat_a(dv, b, 28);
            item_set(&it[n++], IC_DISC, u"디스크 바꾸기", dv);
        } else
            item_set(&it[n++], IC_DISC, u"다른 게임 고르기", NULL);
        item_set(&it[n++], IC_NONE, u"게임 처음부터 다시", NULL);
        item_set(&it[n++], IC_EXIT, u"게임 끝내기 (자동 저장)", NULL);

        r = ui_list(u"잠시 멈춤", it, n, &sel, u"계속", NULL);
        if (r < 0 || r == 0) break;
        if (r == 1) {
            wch q[160];
            slot_label(q, 160, u"칸");
            wcat(q, core_state_exists(cfg.slot)
                     ? u"에 지금 상태를 저장할까요?\n\n이 칸에 이미 저장된 내용은 지워지고 새로 저장됩니다."
                     : u"에 지금 상태를 저장할까요?", 160);
            if (!ui_msg(u"저장 확인", q, u"저장", u"취소")) continue;
            ui_busy(u"저장하는 중...");
            if (core_state_save(cfg.slot) == 0) { slot_label(bar_msg, 64, u"저장했습니다 - 칸"); bar_msg_until = plat_ticks() + 2500; break; }
            ui_msg(u"저장 실패", u"상태를 저장하지 못했습니다.\n저장 공간이 부족한지 확인해 주세요.", u"확인", NULL);
        } else if (r == 2) {
            wch q[160];
            if (!core_state_exists(cfg.slot)) continue;
            slot_label(q, 160, u"칸");
            wcat(q, u"에 저장한 곳으로 돌아갈까요?\n\n저장하지 않은 지금 진행은 사라집니다.", 160);
            if (!ui_msg(u"불러오기 확인", q, u"불러오기", u"취소")) continue;
            ui_busy(u"불러오는 중...");
            if (core_state_load(cfg.slot) == 0) { slot_label(bar_msg, 64, u"불러왔습니다 - 칸"); bar_msg_until = plat_ticks() + 2500; break; }
            ui_msg(u"불러오기 실패", u"저장된 상태를 읽지 못했습니다.", u"확인", NULL);
        } else if (r == 3) {
            cfg.slot = (cfg.slot + 1) % 5;
        } else if (r == 4) {
            menu_settings();
        } else if (r == 5) {
            if (ndisk > 1) {
                core_disk_select((core_disk_index() + 1) % ndisk);
                bar_say(u"디스크를 바꿨습니다");
                break;
            }
            if (browse(newgame, u"게임 고르기")) return PM_NEW_GAME;
        } else if (r == 6) {
            if (ui_msg(u"다시 시작", u"게임을 처음부터 다시 시작할까요?\n저장하지 않은 진행은 사라집니다.", u"다시 시작", u"취소")) {
                core_reset();
                break;
            }
        } else if (r == 7) {
            return PM_QUIT_GAME;
        }
    }
    return want_quit ? PM_QUIT_GAME : PM_RESUME;
}

/* ---- on-screen touch controls -------------------------------------------------------
 * A D-pad (left) and the face buttons (right) are drawn over the lower part of the game
 * picture.  The controls are rendered once into a sprite; every new game frame they are
 * blended on top at the chosen transparency.  Only the pixels of the controls are touched,
 * stored as horizontal runs, so the cost is a few thousand pixels per frame. */
#define OV_KEY 0xF81F                    /* "nothing here" colour while rendering the sprite */
enum { OV_BASE = 0, OV_UP, OV_DOWN, OV_LEFT, OV_RIGHT, OV_F0, OV_F1, OV_F2, OV_F3, OV_N };
static const unsigned char face_pb[4] = { PB_TRIANGLE, PB_CIRCLE, PB_CROSS, PB_SQUARE };

struct ov_run { short y, x, n; unsigned char id; };
static struct ov_run *ov_runs;
static int ov_nruns, ov_npix, ov_on, ov_alpha;
static uint16_t *ov_img, *ov_bg, *ov_last, *ov_pre, *ov_pre_lit;
static void ov_prepare(void);         /* per run pixel: sprite, saved background, last output */
static int ov_dx, ov_dy, ov_dr;                    /* D-pad centre and radius */
static int ov_fx[4], ov_fy[4], ov_fr, ov_nface;    /* face buttons */
static unsigned ov_lit;                            /* bit per OV_* id drawn highlighted */

static void ov_free(void)
{
    free(ov_runs); free(ov_img); free(ov_bg); free(ov_last); free(ov_pre); free(ov_pre_lit);
    ov_runs = NULL; ov_img = ov_bg = ov_last = ov_pre = ov_pre_lit = NULL;
    ov_nruns = ov_npix = 0; ov_on = 0;
    core_frame_hook = NULL;
}

static int isqrt(int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }

/* which control a sprite pixel belongs to (for highlighting) */
static int ov_pixel_id(int x, int y)
{
    int dx = x - ov_dx, dy = y - ov_dy, w = ov_dr * 32 / 100, i, best = -1, bd = 1 << 30;
    if (dx * dx + dy * dy <= (ov_dr + 2) * (ov_dr + 2)) {
        if (dx >= -w && dx <= w && dy < -w) return OV_UP;
        if (dx >= -w && dx <= w && dy > w) return OV_DOWN;
        if (dy >= -w && dy <= w && dx < -w) return OV_LEFT;
        if (dy >= -w && dy <= w && dx > w) return OV_RIGHT;
        return OV_BASE;
    }
    for (i = 0; i < 4; i++) {
        int d;
        if (!sys.pad_names[i]) continue;
        d = (x - ov_fx[i]) * (x - ov_fx[i]) + (y - ov_fy[i]) * (y - ov_fy[i]);
        if (d < bd) { bd = d; best = i; }
    }
    return best >= 0 ? OV_F0 + best : OV_BASE;
}

static void ov_layout(void)
{
    static const unsigned char dr[5] = { 36, 41, 47, 56, 66 }, fr[5] = { 15, 17, 20, 24, 28 };
    int d, cx, cy, i;
    ov_dr = dr[cfg.touch_size];
    ov_fr = fr[cfg.touch_size];
    ov_dx = ov_dr + 6;
    ov_dy = GAME_H - ov_dr - 6;
    ov_nface = 0;
    for (i = 0; i < 4; i++) if (sys.pad_names[i]) ov_nface++;
    d = ov_fr * 165 / 100;
    cx = SCR_W - d - ov_fr - 6;
    cy = GAME_H - d - ov_fr - 6;
    if (ov_nface <= 2) {                   /* A upper right, B lower left (Nintendo layout) */
        cy = GAME_H - ov_fr * 2 - 8;
        ov_fx[1] = cx + d * 70 / 100; ov_fy[1] = cy - ov_fr * 55 / 100;
        ov_fx[2] = cx - d * 70 / 100; ov_fy[2] = cy + ov_fr * 55 / 100;
        ov_fx[0] = ov_fx[3] = -1000; ov_fy[0] = ov_fy[3] = -1000;
    } else {                               /* diamond: top, right, bottom, left */
        ov_fx[0] = cx;     ov_fy[0] = cy - d;
        ov_fx[1] = cx + d; ov_fy[1] = cy;
        ov_fx[2] = cx;     ov_fy[2] = cy + d;
        ov_fx[3] = cx - d; ov_fy[3] = cy;
    }
}

static uint16_t face_color(int i)
{
    if (sys.flags & SF_PS1OPTS) {
        static const uint16_t c[4] = { RGB565(60, 200, 150), RGB565(240, 80, 90), RGB565(110, 160, 250), RGB565(230, 130, 200) };
        return c[i];
    }
    return C_TEXT;
}

static void ov_draw_sprite(void)
{
    int i, r = ov_dr, w = ov_dr * 32 / 100, a = ov_dr * 22 / 100;
    uint16_t edge = RGB565(20, 24, 30), arm = RGB565(78, 88, 104), ring = RGB565(120, 132, 150);

    /* D-pad: ring, base disc, cross, arrows */
    disc(ov_dx, ov_dy, r, edge);
    disc(ov_dx, ov_dy, r - 2, RGB565(40, 46, 58));
    fill(ov_dx - w, ov_dy - r + 6, w * 2 + 1, (r - 6) * 2 + 1, arm);
    fill(ov_dx - r + 6, ov_dy - w, (r - 6) * 2 + 1, w * 2 + 1, arm);
    for (i = 0; i < a; i++) {            /* arrow heads */
        fill(ov_dx - i, ov_dy - r + 12 + i, i * 2 + 1, 1, ring);
        fill(ov_dx - i, ov_dy + r - 12 - i, i * 2 + 1, 1, ring);
        fill(ov_dx - r + 12 + i, ov_dy - i, 1, i * 2 + 1, ring);
        fill(ov_dx + r - 12 - i, ov_dy - i, 1, i * 2 + 1, ring);
    }
    disc(ov_dx, ov_dy, w * 6 / 10, RGB565(56, 64, 78));
    if (!cfg.dpad_4way) {                    /* 8 directions: small markers between the arms */
        int k = r * 66 / 100, m = r * 9 / 100 + 1;
        disc(ov_dx - k * 7 / 10, ov_dy - k * 7 / 10, m, ring);
        disc(ov_dx + k * 7 / 10, ov_dy - k * 7 / 10, m, ring);
        disc(ov_dx - k * 7 / 10, ov_dy + k * 7 / 10, m, ring);
        disc(ov_dx + k * 7 / 10, ov_dy + k * 7 / 10, m, ring);
    }

    /* face buttons */
    for (i = 0; i < 4; i++) {
        if (!sys.pad_names[i]) continue;
        disc(ov_fx[i], ov_fy[i], ov_fr, edge);
        disc(ov_fx[i], ov_fy[i], ov_fr - 2, ring);
        disc(ov_fx[i], ov_fy[i], ov_fr - 4, RGB565(48, 56, 70));
        text(ov_fx[i] - ov_fr, ov_fy[i] - ov_fr, ov_fr * 2, ov_fr * 2, sys.pad_names[i], face_color(i), TF_CENTER | TF_BIG);
    }
}

/* renders the controls into g_fb (whose game area is restored afterwards by the caller) and
 * turns them into runs */
static void ov_build(void)
{
    int x, y, pass;
    ov_free();
    if (!cfg.touchpad || (sys.flags & SF_NOCORE)) return;
    ov_layout();
    ov_alpha = cfg.touch_alpha;
    {   int i; for (i = 0; i < SCR_W * GAME_H; i++) g_fb[i] = OV_KEY; }
    ov_draw_sprite();
    for (pass = 0; pass < 2; pass++) {      /* pass 0 counts, pass 1 fills */
        int nr = 0, np = 0;
        for (y = 0; y < GAME_H; y++) {
            const uint16_t *row = g_fb + y * SCR_W;
            x = 0;
            while (x < SCR_W) {
                int x0, id;
                while (x < SCR_W && row[x] == OV_KEY) x++;
                if (x >= SCR_W) break;
                x0 = x; id = ov_pixel_id(x, y);
                while (x < SCR_W && row[x] != OV_KEY && ov_pixel_id(x, y) == id) x++;
                if (pass) {
                    ov_runs[nr].y = (short)y; ov_runs[nr].x = (short)x0; ov_runs[nr].n = (short)(x - x0);
                    ov_runs[nr].id = (unsigned char)id;
                    memcpy(ov_img + np, row + x0, (x - x0) * 2);
                }
                nr++; np += x - x0;
            }
        }
        if (!pass) {
            ov_nruns = nr; ov_npix = np;
            ov_runs = malloc(sizeof(*ov_runs) * (nr + 1));
            ov_img = malloc(np * 2 + 2); ov_bg = malloc(np * 2 + 2); ov_last = malloc(np * 2 + 2);
            if (!ov_runs || !ov_img || !ov_bg || !ov_last) { ov_free(); memset(g_fb, 0, SCR_W * GAME_H * 2); return; }
            memset(ov_last, 0, np * 2 + 2);
            for (x = 0; x < np; x++) ov_last[x] = OV_KEY;   /* "never drawn": take the frame as background */
        }
    }
    memset(g_fb, 0, SCR_W * GAME_H * 2);
    ov_on = 1;
    ov_prepare();
}

/* opacity steps for transparency 0/25/50/75%; 100% = not drawn (touch still works).
 * The sprite's share of every pixel is computed once (ov_pre / ov_pre_lit), so per frame and
 * pixel only the background's share is added:  out = pre + part(bg).
 * The core may leave pixels untouched between frames (borders): then the saved background
 * is used again instead of blending over our own previous output. */
static void ov_prepare(void)
{
    int k;
    free(ov_pre); free(ov_pre_lit);
    ov_pre = malloc(ov_npix * 2 + 2); ov_pre_lit = malloc(ov_npix * 2 + 2);
    if (!ov_pre || !ov_pre_lit) { free(ov_pre); free(ov_pre_lit); ov_pre = ov_pre_lit = NULL; return; }
    for (k = 0; k < ov_npix; k++) {
        uint16_t fg = ov_img[k];
        ov_pre_lit[k] = AVG565(fg, RGB565(120, 190, 255));                 /* pressed: solid, bluish */
        switch (ov_alpha) {
        case 0: ov_pre[k] = fg; break;                                      /* 100% sprite */
        case 1: ov_pre[k] = (uint16_t)(fg - ((fg & 0xE79C) >> 2)); break;   /* 75% sprite */
        case 2: ov_pre[k] = (uint16_t)((fg & 0xF7DE) >> 1); break;          /* 50% */
        default: ov_pre[k] = (uint16_t)((fg & 0xE79C) >> 2); break;         /* 25% */
        }
    }
}

static void ov_blend(void)
{
    int r, k = 0, a = ov_alpha;
    unsigned lit = ov_lit;
    if (!ov_on || a >= 4 || !ov_pre) return;
    for (r = 0; r < ov_nruns; r++) {
        const struct ov_run *ru = ov_runs + r;
        uint16_t *d = g_fb + ru->y * SCR_W + ru->x, *last = ov_last + k, *bgs = ov_bg + k;
        int n = ru->n, i;
        if ((lit >> ru->id) & 1) {
            const uint16_t *p = ov_pre_lit + k;
            for (i = 0; i < n; i++) {
                uint16_t cur = d[i];
                if (cur != last[i]) bgs[i] = cur;
                d[i] = last[i] = p[i];
            }
        } else {
            const uint16_t *p = ov_pre + k;
            switch (a) {
            case 0:
                for (i = 0; i < n; i++) { uint16_t cur = d[i]; if (cur != last[i]) bgs[i] = cur; d[i] = last[i] = p[i]; }
                break;
            case 1:
                for (i = 0; i < n; i++) {
                    uint16_t cur = d[i], o;
                    if (cur == last[i]) cur = bgs[i]; else bgs[i] = cur;
                    o = (uint16_t)(p[i] + ((cur & 0xE79C) >> 2));
                    d[i] = last[i] = o;
                }
                break;
            case 2:
                for (i = 0; i < n; i++) {
                    uint16_t cur = d[i], o;
                    if (cur == last[i]) cur = bgs[i]; else bgs[i] = cur;
                    o = (uint16_t)(p[i] + ((cur & 0xF7DE) >> 1));
                    d[i] = last[i] = o;
                }
                break;
            default:
                for (i = 0; i < n; i++) {
                    uint16_t cur = d[i], o;
                    if (cur == last[i]) cur = bgs[i]; else bgs[i] = cur;
                    o = (uint16_t)(p[i] + cur - ((cur & 0xE79C) >> 2));
                    d[i] = last[i] = o;
                }
                break;
            }
        }
        k += n;
    }
}

/* puts the original picture back where the controls were (before a rebuild or when turned off) */
static void ov_unblend(void)
{
    int r, k = 0;
    if (!ov_on) return;
    for (r = 0; r < ov_nruns; r++) {
        uint16_t *d = g_fb + ov_runs[r].y * SCR_W + ov_runs[r].x;
        int i;
        for (i = 0; i < ov_runs[r].n; i++, k++)
            if (d[i] == ov_last[k]) d[i] = ov_bg[k];
    }
}

/* PB_* mask for a touch at x,y inside the game area */
static unsigned ov_hit(int x, int y)
{
    int dx = x - ov_dx, dy = y - ov_dy, i, best = -1, second = -1, bd = 1 << 30, sd = 1 << 30;
    unsigned m = 0;
    if (!ov_on) return 0;
    if (dx * dx + dy * dy <= (ov_dr * 15 / 10) * (ov_dr * 15 / 10) && x < SCR_W / 2) {
        int dead = ov_dr / 5;
        if (dx * dx + dy * dy < dead * dead) return 0;
        if (cfg.dpad_4way) {                       /* plus only: the stronger direction wins */
            if (abs(dx) >= abs(dy)) m = 1u << (dx < 0 ? PB_LEFT : PB_RIGHT);
            else m = 1u << (dy < 0 ? PB_UP : PB_DOWN);
            return m;
        }
        /* 8 directions: a diagonal when both parts are large enough */
        if (abs(dx) * 10 > abs(dy) * 4) m |= 1u << (dx < 0 ? PB_LEFT : PB_RIGHT);
        if (abs(dy) * 10 > abs(dx) * 4) m |= 1u << (dy < 0 ? PB_UP : PB_DOWN);
        return m;
    }
    for (i = 0; i < 4; i++) {
        int d;
        if (!sys.pad_names[i]) continue;
        d = (x - ov_fx[i]) * (x - ov_fx[i]) + (y - ov_fy[i]) * (y - ov_fy[i]);
        if (d < bd) { second = best; sd = bd; bd = d; best = i; }
        else if (d < sd) { sd = d; second = i; }
    }
    if (best < 0 || bd > (ov_fr * 2) * (ov_fr * 2)) return 0;
    m = 1u << face_pb[best];
    /* touching between two neighbouring buttons presses both */
    if (second >= 0 && isqrt(sd) - isqrt(bd) < ov_fr * 4 / 10 && sd < (ov_fr * 17 / 10) * (ov_fr * 17 / 10))
        m |= 1u << face_pb[second];
    return m;
}

static unsigned ov_lit_for(unsigned m)
{
    unsigned lit = 0;
    int i;
    if (m & (1u << PB_UP)) lit |= 1u << OV_UP;
    if (m & (1u << PB_DOWN)) lit |= 1u << OV_DOWN;
    if (m & (1u << PB_LEFT)) lit |= 1u << OV_LEFT;
    if (m & (1u << PB_RIGHT)) lit |= 1u << OV_RIGHT;
    for (i = 0; i < 4; i++) if (m & (1u << face_pb[i])) lit |= 1u << (OV_F0 + i);
    return lit;
}

/* (re)creates the controls for the current settings; the game picture is restored afterwards */
static void ov_setup(void)
{
    ov_unblend();
    core_screen_save();
    ov_build();
    core_frame_hook = ov_on ? ov_blend : NULL;
    core_redraw();
    if (ov_on) { ov_blend(); plat_present(0, 0, SCR_W, GAME_H); }
}

static unsigned key_to_mask(int key)
{
    unsigned m = 0; int i;
    for (i = 0; i < PB_COUNT; i++)
        if (cfg.keys[i][0] == key || cfg.keys[i][1] == key) m |= 1u << i;
    return m;
}

static int is_menu_key(int key)
{
    return key && (cfg.keys[KEY_MENU_IDX][0] == key || cfg.keys[KEY_MENU_IDX][1] == key);
}

/* runs the loaded game until the user leaves it; returns 1 if newgame[] should be started next */
static int run_game(char *newgame)
{
    struct event ev;
    unsigned kmask = 0, tmask = 0, pmask = 0, last_bar = 0, last_awake = 0, logged = 0, last_pad = 0;
    int pressed = -1, open_menu = 0, bar_dirty = 1, last_fps = -1, r, zone = 0;   /* zone: 0 none, 1 bar, 2 pad */
    int s_touch = cfg.touchpad, s_alpha = cfg.touch_alpha, s_size = cfg.touch_size, s_4way = cfg.dpad_4way;

    fill(0, BAR_Y, SCR_W, BAR_H, C_FOOT);
    ov_setup();
    plat_audio_pause(0);
    for (;;) {
        while (plat_poll(&ev)) {
            switch (ev.type) {
            case EV_QUIT: want_quit = 1; break;
            case EV_SUSPEND: open_menu = 1; break;
            case EV_KEYDOWN:
                if (is_menu_key(ev.key)) open_menu = 1;
                else kmask |= key_to_mask(ev.key);
                break;
            case EV_KEYUP: kmask &= ~key_to_mask(ev.key); break;
            case EV_TDOWN: case EV_TMOVE: {
                if (ev.type == EV_TDOWN) zone = ev.y >= BAR_Y ? 1 : (ov_on ? 2 : 0);
                if (zone == 1) {
                    int b = bar_hit(ev.x, ev.y);
                    if (ev.type == EV_TMOVE && pressed < 0 && b < 0) break;
                    if (b != pressed) {
                        pressed = b; bar_dirty = 1;
                        tmask = (b > 0) ? 1u << sys.bar[b - 1] : 0;
                    }
                } else if (zone == 2) {
                    /* sliding keeps working across the game area; dragging into the bar ends it */
                    pmask = ev.y < BAR_Y ? ov_hit(ev.x, ev.y) : 0;
                    ov_lit = ov_lit_for(pmask);
                }
                break;
            }
            case EV_TUP:
                if (zone == 1) {
                    if (pressed == 0) open_menu = 1;
                    if (pressed >= 0) { pressed = -1; tmask = 0; bar_dirty = 1; }
                }
                pmask = 0; ov_lit = 0; zone = 0;
                break;
            }
        }
        if (want_quit) break;
        if (open_menu) {
            open_menu = 0; kmask = tmask = pmask = 0; pressed = -1; zone = 0; ov_lit = 0;
            core_set_pad(0);
            ov_unblend();
            r = pause_menu(newgame);
            if (r != PM_RESUME) {
                if (r == PM_NEW_GAME) { ov_free(); return 1; }
                break;
            }
            fill(0, BAR_Y, SCR_W, BAR_H, C_FOOT);
            core_redraw();
            if (s_touch != cfg.touchpad || s_alpha != cfg.touch_alpha || s_size != cfg.touch_size || s_4way != cfg.dpad_4way) {
                s_touch = cfg.touchpad; s_alpha = cfg.touch_alpha; s_size = cfg.touch_size; s_4way = cfg.dpad_4way;
                ov_on = 0;                     /* picture was restored without the controls */
            }
            ov_setup();
            bar_dirty = 1;
            while (plat_poll(&ev)) if (ev.type == EV_QUIT) want_quit = 1;
            plat_audio_pause(0);
        }
        if ((kmask | tmask | pmask) != last_pad) {
            last_pad = kmask | tmask | pmask;
            if (logged < 40) { plat_log("pad %04x\n", last_pad); logged++; }
        }
        core_set_pad(kmask | tmask | pmask);
        core_run_frame();

        {
            unsigned now = plat_ticks();
            if (bar_msg[0] && (int)(now - bar_msg_until) > 0) { bar_msg[0] = 0; bar_dirty = 1; }
            if (cfg.show_fps && core_fps_x10 != last_fps && now - last_bar > 900) { last_fps = core_fps_x10; bar_dirty = 1; }
            if (bar_dirty) { draw_bar(pressed); bar_dirty = 0; last_bar = now; }
            if (now - last_awake > 5000) { plat_keep_awake(); last_awake = now; }
        }
    }
    ov_free();
    return 0;
}

static void leave_game(void)
{
    plat_audio_pause(1);
    if (core_loaded()) {
        ui_busy(u"자동 저장하는 중...");
        core_state_save(AUTO_SLOT);
        core_unload();
    }
    cfg_save();
}

static void launch(const char *path, int resume);

static void play(const char *path, int resume)
{
    char next[HPATH], real[HPATH];
    const char *load = path;
    int again;

    if (ext_is(path, ".zip")) {
        ui_busy(u"압축을 푸는 중...\n잠시만 기다려 주세요");
        if (zip_extract_rom(path, real) < 0) {
            ui_msg(u"실행할 수 없음", u"압축 파일을 풀지 못했습니다.\n\n파일이 손상되었거나 저장 공간이 부족할 수 있습니다.", u"확인", NULL);
            return;
        }
        load = real;
    } else
        ui_busy(u"게임을 불러오는 중...\n잠시만 기다려 주세요");
    if (core_load(load) != 0) {
        ui_msg(u"실행할 수 없음",
               u"이 파일은 실행할 수 없습니다.\n\n"
               u"· 게임 파일이 맞는지\n"
               u"· PS1은 .cue 파일과 .bin 파일이 같은 폴더에 있는지\n확인해 주세요.", u"확인", NULL);
        return;
    }
    snprintf(cfg.last_game, HPATH, "%s", path);
    cfg_save();
    if (resume && core_state_exists(AUTO_SLOT)) {
        if (core_state_load(AUTO_SLOT) == 0) bar_say(u"지난번 하던 곳에서 이어합니다");
    }
    again = run_game(next);
    leave_game();
    if (again && !want_quit) launch(next, 0);
}

/* starts a game: here if it belongs to this executable's system, otherwise in its own emulator */
static void launch(const char *path, int resume)
{
    char dir[HPATH], exe[HPATH], args[HPATH + 8];
    const char *name;
    char *b;
    int idx;

    snprintf(dir, sizeof(dir), "%s", path);
    b = strrchr(dir, PSEP);
    if (b) { *b = 0; name = b + 1; } else { dir[0] = 0; name = path; }
    idx = sysreg_classify(dir, name);
    if (idx == -2) idx = zip_extract_rom(path, NULL);
    if (idx < 0) {
        ui_msg(u"실행할 수 없음", u"지원하지 않는 파일이거나, 이 게임기용 에뮬레이터 파일이 설치되어 있지 않습니다.", u"확인", NULL);
        return;
    }
    if (idx == sysreg_self()) { play(path, resume); return; }

    snprintf(exe, sizeof(exe), "%s%cemu_%s.exe", plat_app_dir(), PSEP, sysreg_id(idx));
    snprintf(args, sizeof(args), "%s\"%s\"", resume ? "/r " : "", path);
    cfg_save();
    ui_busy(u"게임기를 준비하는 중...");
    if (plat_spawn(exe, args) == 0) { want_quit = 1; return; }
    ui_msg(u"실행할 수 없음", u"에뮬레이터를 시작하지 못했습니다.\n\n메모리가 부족할 수 있습니다. 다른 프로그램을 끄고 다시 해 보세요.", u"확인", NULL);
}

/* ---- home --------------------------------------------------------------------------- */
void ui_main(void)
{
    struct item it[6];
    int sel = 0, r, n, has_last, nocore = (sys.flags & SF_NOCORE) != 0;
    char path[HPATH];

    cfg_load();
    {
        int mb = plat_free_mem_mb();
        plat_log("free memory: %d MB\n", mb);
        if (mb >= 0 && mb < 30 && !g_start_game[0])
            ui_msg(u"메모리 부족",
                   u"남은 메모리가 적어서 게임이 실행되지 않을 수 있습니다.\n\n"
                   u"다른 프로그램을 모두 끄거나 기기를 다시 켠 뒤 실행해 보세요.", u"그래도 계속", NULL);
    }
    if (!nocore) {
        ui_busy(u"준비하는 중...");
        core_init();
    }
    if (g_start_game[0]) launch(g_start_game, g_start_resume);

    while (!want_quit) {
        int idx_resume = -1, idx_pick, idx_set = -1, idx_help, idx_exit;
        has_last = cfg.last_game[0] && plat_file_exists(cfg.last_game);
        n = 0;
        if (has_last) {
            const char *b = strrchr(cfg.last_game, PSEP);
            wch nm[28];
            plat_path_to_wch(b ? b + 1 : cfg.last_game, nm, 28);
            idx_resume = n;
            item_set(&it[n++], IC_PLAY, u"이어하기", nm);
        }
        idx_pick = n; item_set(&it[n++], IC_DISC, u"게임 고르기", NULL);
        if (!nocore) { idx_set = n; item_set(&it[n++], IC_GEAR, u"설정", NULL); }
        idx_help = n; item_set(&it[n++], IC_HELP, u"사용 방법", NULL);
        idx_exit = n; item_set(&it[n++], IC_EXIT, u"끝내기", NULL);

        r = ui_list(sys.name, it, n, &sel, NULL,
                    sys.bios_found ? (sys.bios_found() ? u"BIOS 파일 사용 중" : u"내장 BIOS 사용 중") : NULL);
        if (r < 0) {
            if (want_quit) break;
            continue;
        }
        if (r == idx_resume) launch(cfg.last_game, 1);
        else if (r == idx_pick) { if (browse(path, u"게임 고르기")) launch(path, 0); }
        else if (r == idx_set) menu_settings();
        else if (r == idx_help) menu_help();
        else if (r == idx_exit) break;
    }
    if (core_loaded()) leave_game();
    cfg_save();
}
