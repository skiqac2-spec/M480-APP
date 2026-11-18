/* Linux preview harness: drives the game and dumps frames (PPM + text log) */
#include "../src/game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
int harness_mode(void); int harness_bphase(void); void harness_cheat(void); void harness_boss(void); void harness_magic(void); void harness_area(int); void harness_unique(void); void harness_gallery(int,int,int); void harness_bossk(int); void harness_anim(int,int); void harness_town(void (*)(const char *)); void harness_act(int,int); void harness_lvl(int);

static u16 fb[GWMAX * 480];
static FILE *tlog;

int plat_text_w(const wchar_t *s, int fl)
{
    int w = 0;
    for (; *s; s++) w += (*s >= 0x1100) ? 12 : 6;
    if (fl & T_BIG) w = w * 22 / 12;
    return w;
}
void plat_text(int x, int y, const wchar_t *s, u16 c, int fl)
{
    int w = plat_text_w(s, fl);
    if (fl & T_CENTER) x -= w / 2;
    else if (fl & T_RIGHT) x -= w;
    if (tlog) fprintf(tlog, "%d %d %d %d %ls\n", x, y, c, fl, s);
}
static char savepath[] = "/tmp/abyss.sav";
int plat_save(const void *d, int n) { FILE *f = fopen(savepath, "wb"); if (!f) return 0; fwrite(d, 1, n, f); fclose(f); return 1; }
int plat_load(void *d, int n) { FILE *f = fopen(savepath, "rb"); int r; if (!f) return 0; r = (int)fread(d, 1, n, f) == n; fclose(f); return r; }
void plat_delsave(void) { remove(savepath); }

void *plat_fopen(const wchar_t *name) { if (getenv("NOART")) return 0; return fopen(wcscmp(name, L"abyss_snd.dat") == 0 ? "abyss_snd.dat" : wcscmp(name, L"abyss_hero.dat") == 0 ? "abyss_hero.dat" : "abyss_mon.dat", "rb"); }
void plat_audio_lock(void) {}
void plat_audio_unlock(void) {}
static FILE *wavf; static long wavn;
static void audio_rec(int nticks) { static short b[1103]; while (nticks-- > 0) { audio_mix(b, 1102); if (wavf) { fwrite(b, 2, 1102, wavf); wavn += 1102; } } }
int plat_fread(void *f, void *b, int n) { return (int)fread(b, 1, n, (FILE *)f); }
int plat_fseek(void *f, long o) { return fseek((FILE *)f, o, SEEK_SET) == 0; }
void plat_fclose(void *f) { fclose((FILE *)f); }
static void wav_close(void)
{
    unsigned hd[11] = { 0x46464952, 36 + wavn * 2, 0x45564157, 0x20746d66, 16, 0x10001, 22050, 44100, 0x100002, 0x61746164, wavn * 2 };
    if (!wavf) return; fseek(wavf, 0, SEEK_SET); fwrite(hd, 4, 11, wavf); fclose(wavf); wavf = 0;
}
static int H = 320, shot = 0;
static void snap(const char *name)
{
    char p[256];
    FILE *f;
    int i;
    sprintf(p, "/tmp/shots/%02d_%s.txt", shot, name);
    tlog = fopen(p, "w");
    game_render();
    fclose(tlog); tlog = 0;
    sprintf(p, "/tmp/shots/%02d_%s.ppm", shot, name);
    f = fopen(p, "wb");
    fprintf(f, "P6 %d %d 255\n", GW, H);
    for (i = 0; i < GW * H; i++) {
        unsigned char c[3] = { (unsigned char)(((fb[i] >> 11) & 31) << 3), (unsigned char)(((fb[i] >> 5) & 63) << 2), (unsigned char)((fb[i] & 31) << 3) };
        fwrite(c, 1, 3, f);
    }
    fclose(f);
    shot++;
}
static void ticks(int n) { while (n--) { game_tick(); audio_rec(1); } }
static void render_quiet(void) { tlog = 0; game_render(); }

int main(int argc, char **argv)
{
    int i;
    setlocale(LC_ALL, "C.UTF-8");
    if (argc > 1) H = atoi(argv[1]);
    if (getenv("GWIDTH")) GW = atoi(getenv("GWIDTH"));
    remove(savepath);
    game_init(H, fb, 777);
    audio_init();
    if (getenv("WAV")) { wavf = fopen(getenv("WAV"), "wb"); fseek(wavf, 44, SEEK_SET); atexit(wav_close); }
    ticks(40);
    snap("title");
    if (getenv("TOWN")) { harness_town(snap); return 0; }
    if (getenv("KEYTEST")) {
        game_key(K_OK); ticks(10);
        for (i = 0; i < 12; i++) { game_key(K_OK); ticks(3); }
        snap("k0_explore");
        game_key(K_SOFTR); ticks(2); snap("k1_menu_softR");
        game_key(K_SOFTR); ticks(2); snap("k2_closed");
        game_key(K_INV); ticks(2); snap("k3_inv_I");
        game_key(K_CHAR); ticks(2); snap("k4_char_C");
        game_key(K_SOFTL); ticks(2); snap("k5_back");
        game_key(K_SOFTL); ticks(2); snap("k6_char_softL");
        game_key(K_CHAR); game_key(K_MAP); ticks(2); snap("k7_map_M");
        harness_boss(); ticks(40); snap("k8_battle");
        game_key(K_CMD2); ticks(2); snap("k9_battle_S_magic");
        game_key(K_SOFTL); ticks(2); snap("k10_cancel");
        game_key(K_CMD1); ticks(60); snap("k11_attack_A");
        return 0;
    }
    if (getenv("EQ")) {
        extern void harness_equip(int, int, int); extern void harness_charscreen(void);
        static const int sets[6][3] = { { 0, 9, -1 }, { 2, 10, 15 }, { 4, 11, 17 }, { 23, 28, 32 }, { 24, 29, 17 }, { 26, 30, 33 } };
        char nm[32];
        game_key(K_OK);
        for (i = 0; i < 6; i++) {
            harness_equip(sets[i][0], sets[i][1], sets[i][2]);
            harness_boss(); ticks(40);
            sprintf(nm, "eq%d_battle", i); snap(nm);
            harness_charscreen(); sprintf(nm, "eq%d_char", i); snap(nm);
        }
        return 0;
    }
    game_key(K_OK);            /* new game */
    render_quiet();
    snap("explore");
    game_key(K_RIGHT); render_quiet();
    for (i = 0; i < 3; i++) { game_key(K_UP); render_quiet(); }
    snap("explore2");
    game_key(K_OK); /* map */
    snap("automap");
    game_key(K_OK);
    game_key(K_BACK); /* inventory */
    snap("inventory");
    game_key(K_MENU);
    render_quiet();
    game_tap(30, H - 120 + 100); /* char */
    snap("character");
    game_key(K_BACK);
    render_quiet();
    /* walk randomly until battle */
    for (i = 0; i < 400; i++) {
        int k = (i % 7 == 3) ? K_RIGHT : (i % 11 == 5 ? K_LEFT : K_UP);
        game_key(k);
        render_quiet();
        if (getenv("DBG")) fprintf(stderr, "step %d\n", i);
        {
            /* detect battle by tapping nothing: check via rendering into log */
        }
        ticks(1);
        if (i == 399) break;
        /* crude battle detection: K_OK in battle advances; we just check a few times */
        {
            extern int harness_mode(void);
            if (harness_mode() == 2) break;
        }
    }
    snap("battle_intro");
    ticks(40); render_quiet();
    snap("battle_cmd");
    game_key(K_OK); render_quiet();  /* attack (maybe target select) */
    snap("battle_target_or_exec");
    game_key(K_OK); render_quiet();
    ticks(3); render_quiet();
    snap("battle_exec");
    for (i = 0; i < 400 && harness_mode() == 2; i++) {
        ticks(16); render_quiet();
        if (harness_bphase() == 1) { game_key(K_OK); game_key(K_OK); }
        if (harness_bphase() == 6) { snap("battle_result"); game_key(K_OK); game_key(K_OK); game_key(K_OK); game_key(K_OK); }
        if (harness_bphase() == 7) { snap("battle_dead"); break; }
    }
    render_quiet();
    snap("after");
    harness_cheat();
    render_quiet();
    game_key(K_BACK);
    snap("inventory_loot");
    game_key(K_BACK);
    harness_boss();
    ticks(40);
    render_quiet();
    snap("boss");
    game_key(K_OK);
    harness_magic();
    render_quiet();
    snap("magic");
    {
        static const int AF[21] = { 1, 2, 4, 6, 8, 11, 13, 14, 17, 20, 22, 23, 26, 29, 31, 32, 35, 38, 39, 42, 45 };
        char nm[32];
        for (i = 0; i < 21; i++) { harness_area(AF[i]); render_quiet(); sprintf(nm, "area%02d", i); snap(nm); }
    }
    {
        static const int G[][3] = { { 0, 1, 2 }, { 3, 4, 13 }, { 5, 6, 7 }, { 8, 9, 15 }, { 10, 11, 12 }, { 14, 16, 17 },
                                    { 18, 19, 20 }, { 21, 22, 23 }, { 24, 25, 26 }, { 27, 28, 29 }, { 30, 31, 32 }, { 33, 34, 35 },
                                    { 36, 37, 38 }, { 39, 40, 41 }, { 42, 43, 44 }, { 45, 46, 47 }, { 48, 49, 50 } };
        char nm[32];
        for (i = 0; i < 17; i++) { harness_gallery(G[i][0], G[i][1], G[i][2]); ticks(20); render_quiet(); sprintf(nm, "gal%02d", i); snap(nm); }
        for (i = 0; i < 6; i++) { harness_gallery(i * 9 % 51, -1, -1); ticks(20); render_quiet(); sprintf(nm, "solo%02d", i); snap(nm); }
    }
    for (i = 0; i < 5; i++) { char nm[16]; harness_bossk(i); ticks(40); render_quiet(); sprintf(nm, "boss%d", i); snap(nm); }
    {
        static const int MT[6] = { 1, 2, 4, 19, 17, 22 };
        int k, f; char nm[32];
        for (k = 0; k < 6; k++) {
            harness_gallery(MT[k], -1, -1); ticks(20);
            for (f = 0; f < 8; f += 2) { harness_anim(1, f); render_quiet(); sprintf(nm, "anim%d_atk%d", k, f); snap(nm); }
            for (f = 0; f < 4; f++) { ticks(2); render_quiet(); sprintf(nm, "anim%d_idle%d", k, f); snap(nm); }
        }
    }
    {
        char nm[32]; int k;
        harness_lvl(12);
        harness_area(14); harness_gallery(23, 24, 26); ticks(30); render_quiet(); snap("gs_cmd");
        harness_act(0, 0);
        for (k = 0; k < 6; k++) { ticks(3); render_quiet(); sprintf(nm, "gs_atk%d", k); snap(nm); }
        ticks(40); render_quiet(); snap("gs_after");
        harness_gallery(30, 31, 32); ticks(30);
        harness_act(1, 0); for (k = 0; k < 4; k++) { ticks(3); render_quiet(); sprintf(nm, "gs_fire%d", k); snap(nm); }
        harness_gallery(41, 42, 43); ticks(30);
        harness_act(1, 2); for (k = 0; k < 3; k++) { ticks(3); render_quiet(); sprintf(nm, "gs_bolt%d", k); snap(nm); }
        harness_bossk(4); ticks(40); render_quiet(); snap("gs_boss_party");
        harness_act(0, 0); for (k = 0; k < 30; k++) { ticks(3); render_quiet(); if (k % 5 == 0) { sprintf(nm, "gs_bossturn%02d", k); snap(nm); } }
        harness_bossk(0); ticks(40); render_quiet(); snap("gs_boss1");
        if (getenv("GIF")) {
            harness_area(8); harness_gallery(0, 5, 13); ticks(30); render_quiet(); snap("gif000");
            harness_act(0, 0);
            for (k = 1; k < 60; k++) { ticks(1); render_quiet(); sprintf(nm, "gif%03d", k); snap(nm); }
            ticks(10);
            harness_act(1, 2);
            for (k = 60; k < 100; k++) { ticks(1); render_quiet(); sprintf(nm, "gif%03d", k); snap(nm); }
            harness_act(1, 0);
            for (k = 100; k < 135; k++) { ticks(1); render_quiet(); sprintf(nm, "gif%03d", k); snap(nm); }
        }
    }
    return 0;
}
