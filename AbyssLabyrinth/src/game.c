/*
 * 심연의 미궁 (Abyss Labyrinth)
 * 1인칭 그리드 던전 탐색(위저드리) + 드래곤퀘스트식 전투 화면
 * + 디아블로풍 UI(체력/마나 구슬, 허리띠, 아이템 등급색) + 로그라이크 던전
 * Target: Windows Mobile 6.1 (SCH-M480), 240xN logical screen, RGB565
 */
#include "game.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

int GH;
int GW = 240;
u16 *FB;

#ifdef _WIN32
#define VSNW(b, n, f, a) _vsnwprintf(b, n, f, a)
#else
#define VSNW(b, n, f, a) vswprintf(b, n, f, a)
#endif

static void wfmt(wchar_t *b, int n, const wchar_t *f, ...)
{
    va_list a;
    va_start(a, f);
    VSNW(b, n, f, a);
    va_end(a);
    b[n - 1] = 0;
}

/* ================================================================ */
/*  colour / pixel helpers                                          */
/* ================================================================ */
static int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
enum { SX_CLICK, SX_STEP, SX_BUMP, SX_SWING, SX_HIT, SX_CRIT, SX_MISS, SX_GROWL, SX_HURT, SX_MDIE, SX_FIRE, SX_EXPL, SX_BOLT,
       SX_FROST, SX_HEAL, SX_DRINK, SX_COINS, SX_CHEST, SX_PICKUP, SX_LVUP, SX_PORTAL, SX_SHRINE, SX_TRADE, SX_ENC, SX_DEATH };
enum { MU_TITLE, MU_TOWN, MU_ACT1, MU_BATTLE = 7, MU_BOSS, MU_WIN, MU_DIE };
static u16 rgb(int r, int g, int b)
{
    r = clampi(r, 0, 255); g = clampi(g, 0, 255); b = clampi(b, 0, 255);
    return (u16)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
static int CR(u16 c) { return ((c >> 11) & 31) << 3; }
static int CG(u16 c) { return ((c >> 5) & 63) << 2; }
static int CB(u16 c) { return (c & 31) << 3; }
static u16 shade(u16 c, int a) { return rgb(CR(c) * a >> 8, CG(c) * a >> 8, CB(c) * a >> 8); }
static u16 mix(u16 a, u16 b, int t)
{
    return rgb(CR(a) + ((CR(b) - CR(a)) * t >> 8), CG(a) + ((CG(b) - CG(a)) * t >> 8),
               CB(a) + ((CB(b) - CB(a)) * t >> 8));
}

static unsigned hsh(int x, int y)
{
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

#define XO ((GW - 240) / 2)      /* centring offset for layouts designed at 240 wide */
static int clipY0 = 0, clipY1 = 9999;
static void pset(int x, int y, u16 c)
{
    if ((unsigned)x < GW && y >= 0 && y < GH && y >= clipY0 && y <= clipY1) FB[y * GW + x] = c;
}
static void fill(int x, int y, int w, int h, u16 c)
{
    int i, j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GW) w = GW - x;
    if (y + h > GH) h = GH - y;
    for (j = 0; j < h; j++) {
        u16 *p = FB + (y + j) * GW + x;
        for (i = 0; i < w; i++) p[i] = c;
    }
}
static void hline(int x1, int x2, int y, u16 c) { if (x2 >= x1) fill(x1, y, x2 - x1 + 1, 1, c); }
static void vline(int x, int y1, int y2, u16 c) { if (y2 >= y1) fill(x, y1, 1, y2 - y1 + 1, c); }
static void rectl(int x, int y, int w, int h, u16 c)
{
    hline(x, x + w - 1, y, c); hline(x, x + w - 1, y + h - 1, c);
    vline(x, y, y + h - 1, c); vline(x + w - 1, y, y + h - 1, c);
}
static void darken(int x, int y, int w, int h, int a)
{
    int i, j;
    if (y < 0) { h += y; y = 0; }
    if (y + h > GH) h = GH - y;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) {
        u16 *p = FB + (y + j) * GW + x + i;
        *p = shade(*p, a);
    }
}
static void tint(int x, int y, int w, int h, u16 c, int t)
{
    int i, j;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) {
        u16 *p = FB + (y + j) * GW + x + i;
        *p = mix(*p, c, t);
    }
}
static void fcircle(int cx, int cy, int r, u16 c)
{
    int x, y;
    for (y = -r; y <= r; y++) for (x = -r; x <= r; x++)
        if (x * x + y * y <= r * r) pset(cx + x, cy + y, c);
}
static void line(int x0, int y0, int x1, int y1, u16 c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        pset(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        { int e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
    }
}

/* stone texture used for Diablo-like panels */
static void stone(int x, int y, int w, int h, int br)
{
    int i, j;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) {
        int px = x + i, py = y + j;
        unsigned hv = hsh(px >> 1, py >> 1);
        int n = (int)(hv & 15) - 8;
        int blk = ((px / 9 + (py / 7) * 3) & 3) * 3;
        int v = br + n + blk;
        if ((py % 7) == 0 || ((px + (py / 7) * 4) % 9) == 0) v -= 14;
        if ((unsigned)px < GW && (unsigned)py < (unsigned)GH) FB[py * GW + px] = rgb(v + 3, v + 1, v - 3);
    }
}
static const u16 *GOLD_DUMMY;
static void goldframe(int x, int y, int w, int h)
{
    rectl(x, y, w, h, rgb(20, 14, 8));
    rectl(x + 1, y + 1, w - 2, h - 2, rgb(170, 136, 70));
    rectl(x + 2, y + 2, w - 4, h - 4, rgb(90, 66, 30));
}
/* Dragon-Quest style window: black with white rounded border */
static void dqwin(int x, int y, int w, int h)
{
    fill(x, y, w, h, rgb(0, 0, 0));
    rectl(x + 2, y + 2, w - 4, h - 4, rgb(255, 255, 255));
    rectl(x + 3, y + 3, w - 6, h - 6, rgb(255, 255, 255));
    pset(x + 2, y + 2, 0); pset(x + w - 3, y + 2, 0); pset(x + 2, y + h - 3, 0); pset(x + w - 3, y + h - 3, 0);
}

/* ================================================================ */
/*  text                                                            */
/* ================================================================ */
#define C_WHITE rgb(230, 230, 230)
#define C_GOLD rgb(200, 170, 100)
#define C_BLUE rgb(120, 120, 255)
#define C_YEL rgb(255, 255, 110)
#define C_RED rgb(230, 60, 50)
#define C_GRAY rgb(130, 130, 130)
#define C_GREEN rgb(80, 220, 80)

static void txt(int x, int y, const wchar_t *s, u16 c, int fl) { plat_text(x, y, s, c, fl); }
static void txtf(int x, int y, u16 c, int fl, const wchar_t *f, ...)
{
    wchar_t b[128];
    va_list a;
    va_start(a, f);
    VSNW(b, 128, f, a);
    va_end(a);
    b[127] = 0;
    plat_text(x, y, b, c, fl);
}

/* Korean particle helpers */
static int jong(const wchar_t *s)
{
    size_t n = wcslen(s);
    wchar_t c;
    if (!n) return 0;
    c = s[n - 1];
    if (c >= 0xAC00 && c <= 0xD7A3) return (c - 0xAC00) % 28 != 0;
    if (c >= '0' && c <= '9') return c == '0' || c == '1' || c == '3' || c == '6' || c == '7' || c == '8';
    return 0;
}
#define JI(s) (jong(s) ? L"이" : L"가")
#define JUL(s) (jong(s) ? L"을" : L"를")
#define JUN(s) (jong(s) ? L"은" : L"는")

/* ================================================================ */
/*  RNG                                                             */
/* ================================================================ */
static unsigned rs = 12345;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static int rr(int a, int b) { if (b <= a) return a; return a + (int)(rnd() % (unsigned)(b - a + 1)); }
static int chance(int p) { return (int)(rnd() % 100) < p; }

/* ================================================================ */
/*  items                                                           */
/* ================================================================ */
enum { IK_NONE, IK_W, IK_A, IK_H, IK_R, IK_HP, IK_MP, IK_RJ, IK_TP };
#define IS_POT(k) ((k) >= IK_HP && (k) <= IK_RJ)
enum { R_NORM, R_MAGIC, R_RARE, R_UNIQ };
enum { ST_STR, ST_DEX, ST_HP, ST_MP, ST_DMG, ST_DEF, ST_N };
static const wchar_t *STN[ST_N] = { L"힘", L"민첩", L"생명력", L"마나", L"공격력", L"방어력" };

typedef struct {
    unsigned char kind, base, rar, uniq, naff, rn1, rn2, pad;
    unsigned char aff[4];
    short av[4];
    short lo, hi, def, pad2;
} Item;

typedef struct { const wchar_t *n; unsigned char kind, tier; short lo, hi, def; } Base;
static const Base BASES[] = {
    { L"단검", IK_W, 1, 2, 5, 0 }, { L"곤봉", IK_W, 2, 3, 7, 0 }, { L"장검", IK_W, 3, 4, 10, 0 },
    { L"전투 도끼", IK_W, 5, 6, 13, 0 }, { L"바스타드 소드", IK_W, 7, 8, 17, 0 }, { L"대검", IK_W, 9, 11, 22, 0 },
    { L"룬 블레이드", IK_W, 12, 14, 28, 0 }, { L"악마의 낫", IK_W, 15, 18, 34, 0 },
    { L"누더기 갑옷", IK_A, 1, 0, 0, 2 }, { L"가죽 갑옷", IK_A, 2, 0, 0, 4 }, { L"사슬 갑옷", IK_A, 4, 0, 0, 8 },
    { L"판금 갑옷", IK_A, 7, 0, 0, 13 }, { L"고딕 판금", IK_A, 10, 0, 0, 19 }, { L"고대 갑옷", IK_A, 13, 0, 0, 26 },
    { L"두건", IK_H, 1, 0, 0, 1 }, { L"투구", IK_H, 3, 0, 0, 3 }, { L"해골 투구", IK_H, 6, 0, 0, 5 },
    { L"대투구", IK_H, 9, 0, 0, 8 }, { L"왕관", IK_H, 12, 0, 0, 12 },
    { L"반지", IK_R, 1, 0, 0, 0 },
    { L"생명 물약", IK_HP, 1, 0, 0, 0 }, { L"마나 물약", IK_MP, 1, 0, 0, 0 }, { L"회복 물약", IK_RJ, 1, 0, 0, 0 },
    { L"고대의 도끼", IK_W, 20, 24, 44, 0 }, { L"신성한 검", IK_W, 25, 30, 52, 0 }, { L"파멸의 대검", IK_W, 32, 38, 66, 0 }, { L"심연의 칼날", IK_W, 40, 46, 80, 0 },
    { L"어둠의 판금", IK_A, 17, 0, 0, 34 }, { L"용비늘 갑옷", IK_A, 24, 0, 0, 44 }, { L"신성한 갑주", IK_A, 32, 0, 0, 56 }, { L"심연의 갑주", IK_A, 40, 0, 0, 70 },
    { L"뼈 투구", IK_H, 17, 0, 0, 16 }, { L"악마의 뿔 투구", IK_H, 25, 0, 0, 22 }, { L"심연의 관", IK_H, 36, 0, 0, 30 },
    { L"귀환 두루마리", IK_TP, 1, 0, 0, 0 },
};
#define NBASE ((int)(sizeof(BASES) / sizeof(BASES[0])))

typedef struct { const wchar_t *n; unsigned char stat, suffix, tier, kinds; short lo, hi; } Affix;
#define KW (1 << IK_W)
#define KA (1 << IK_A)
#define KH (1 << IK_H)
#define KR (1 << IK_R)
static const Affix AFX[] = {
    { L"날카로운", ST_DMG, 0, 1, KW | KR, 1, 3 }, { L"잔혹한", ST_DMG, 0, 5, KW | KR, 3, 6 }, { L"무자비한", ST_DMG, 0, 10, KW | KR, 6, 11 },
    { L"튼튼한", ST_DEF, 0, 1, KA | KH | KR, 2, 4 }, { L"강철의", ST_DEF, 0, 5, KA | KH | KR, 4, 8 }, { L"신성한", ST_DEF, 0, 10, KA | KH | KR, 8, 15 },
    { L"푸른", ST_MP, 0, 1, KW | KA | KH | KR, 4, 9 }, { L"신비한", ST_MP, 0, 6, KW | KA | KH | KR, 10, 20 },
    { L"황소의", ST_STR, 1, 1, KW | KA | KH | KR, 1, 3 }, { L"거인의", ST_STR, 1, 7, KW | KA | KH | KR, 3, 7 },
    { L"여우의", ST_DEX, 1, 1, KW | KA | KH | KR, 1, 3 }, { L"매의", ST_DEX, 1, 7, KW | KA | KH | KR, 3, 7 },
    { L"곰의", ST_HP, 1, 1, KW | KA | KH | KR, 5, 12 }, { L"고래의", ST_HP, 1, 6, KW | KA | KH | KR, 12, 25 },
};
#define NAFX ((int)(sizeof(AFX) / sizeof(AFX[0])))

static const wchar_t *RN1[] = { L"피의", L"죽음의", L"어둠", L"그림자", L"저주받은", L"폭풍", L"영혼", L"해골", L"서리", L"불꽃" };
static const wchar_t *RN2[] = { L"송곳니", L"손아귀", L"비명", L"수호자", L"파괴자", L"껍질", L"흉터", L"숨결", L"맹세", L"심장" };

typedef struct { const wchar_t *n; unsigned char kind, base; short lo, hi, def; unsigned char st[3]; short sv[3]; unsigned char minf; } Uniq;
static const Uniq UNQ[] = {
    { L"피의 송곳니", IK_W, 2, 8, 18, 0, { ST_HP, ST_STR, ST_DMG }, { 15, 4, 2 }, 3 },
    { L"서리의 속삭임", IK_W, 5, 12, 24, 0, { ST_MP, ST_DEX, ST_DMG }, { 15, 5, 3 }, 6 },
    { L"망자의 수의", IK_A, 11, 0, 0, 22, { ST_HP, ST_DEF, ST_MP }, { 30, 5, 5 }, 7 },
    { L"잊혀진 왕의 관", IK_H, 18, 0, 0, 10, { ST_STR, ST_DEX, ST_MP }, { 5, 5, 10 }, 8 },
    { L"심연의 눈", IK_R, 19, 0, 0, 0, { ST_HP, ST_MP, ST_DMG }, { 20, 20, 5 }, 5 },
    { L"방랑자의 고리", IK_R, 19, 0, 0, 0, { ST_STR, ST_DEX, ST_DEF }, { 6, 6, 4 }, 3 },
    { L"학살자의 도끼", IK_W, 3, 14, 30, 0, { ST_STR, ST_DMG, ST_HP }, { 8, 6, 10 }, 10 },
};
#define NUNQ ((int)(sizeof(UNQ) / sizeof(UNQ[0])))

static int base_of_kind(int k) { int i; for (i = 0; i < NBASE; i++) if (BASES[i].kind == k) return i; return 0; }

static void item_name(const Item *it, wchar_t *o, int n)
{
    const Base *b = &BASES[it->base];
    o[0] = 0;
    if (it->rar == R_UNIQ) { wfmt(o, n, L"%ls", UNQ[it->uniq].n); return; }
    if (it->rar == R_RARE) { wfmt(o, n, L"%ls %ls", RN1[it->rn1], RN2[it->rn2]); return; }
    if (it->rar == R_MAGIC) {
        const wchar_t *pre = L"", *suf = L"";
        int i;
        for (i = 0; i < it->naff; i++) {
            if (AFX[it->aff[i]].suffix) suf = AFX[it->aff[i]].n; else pre = AFX[it->aff[i]].n;
        }
        wfmt(o, n, L"%ls%ls%ls%ls%ls", suf, *suf ? L" " : L"", pre, *pre ? L" " : L"", b->n);
        return;
    }
    if (it->pad && it->kind <= IK_R) wfmt(o, n, L"고급 %ls", b->n);
    else wfmt(o, n, L"%ls", b->n);
}
static u16 rar_col(const Item *it)
{
    if (it->kind >= IK_HP) return C_WHITE;
    switch (it->rar) {
    case R_MAGIC: return C_BLUE;
    case R_RARE: return C_YEL;
    case R_UNIQ: return C_GOLD;
    }
    return C_WHITE;
}

static void add_aff(Item *it, int ilvl)
{
    int tries;
    if (it->naff >= 4) return;
    for (tries = 0; tries < 40; tries++) {
        int a = rr(0, NAFX - 1), i, dup = 0;
        const Affix *f = &AFX[a];
        if (!(f->kinds & (1 << it->kind)) || f->tier > ilvl) continue;
        for (i = 0; i < it->naff; i++) if (AFX[it->aff[i]].stat == f->stat) dup = 1;
        if (dup) continue;
        if (it->rar == R_MAGIC) {
            for (i = 0; i < it->naff; i++) if (AFX[it->aff[i]].suffix == f->suffix) dup = 1;
            if (dup) continue;
        }
        it->aff[it->naff] = (unsigned char)a;
        it->av[it->naff] = (short)rr(f->lo, f->hi);
        it->naff++;
        return;
    }
}

static void make_potion(Item *it, int k)
{
    memset(it, 0, sizeof(*it));
    it->kind = (unsigned char)k;
    it->base = (unsigned char)base_of_kind(k);
}

static void gen_item(Item *it, int ilvl, int bonus)
{
    int k, cand[NBASE], nc = 0, i, roll;
    memset(it, 0, sizeof(*it));
    k = rr(IK_W, IK_R);
    if (k == IK_R && chance(50)) k = rr(IK_W, IK_H);
    roll = rr(0, 999) + bonus;
    if (roll >= 975 && ilvl >= 3) {
        int u, t;
        for (t = 0; t < 10; t++) {
            u = rr(0, NUNQ - 1);
            if (UNQ[u].minf <= ilvl + 1) {
                const Uniq *q = &UNQ[u];
                it->kind = q->kind; it->base = q->base; it->rar = R_UNIQ; it->uniq = (unsigned char)u;
                it->lo = q->lo; it->hi = q->hi; it->def = q->def;
                it->naff = 3;
                for (i = 0; i < 3; i++) { it->aff[i] = 0xFF; it->av[i] = q->sv[i]; }
                /* uniques store stat ids directly in aff with high bit */
                for (i = 0; i < 3; i++) it->aff[i] = (unsigned char)(0x80 | q->st[i]);
                return;
            }
        }
    }
    for (i = 0; i < NBASE; i++) if (BASES[i].kind == k && BASES[i].tier <= ilvl) cand[nc++] = i;
    if (nc == 0) cand[nc++] = base_of_kind(k);
    i = cand[nc > 3 ? rr(nc - 3, nc - 1) : rr(0, nc - 1)];
    it->kind = (unsigned char)k; it->base = (unsigned char)i;
    it->lo = BASES[i].lo; it->hi = BASES[i].hi; it->def = BASES[i].def;
    if (k == IK_A || k == IK_H) it->def = (short)(it->def + rr(0, it->def / 3));
    if (roll >= 880) it->rar = R_RARE;
    else if (roll >= 600 || k == IK_R) it->rar = R_MAGIC;
    else it->rar = R_NORM;
    if (it->rar == R_NORM && k != IK_R && chance(22)) {   /* superior quality */
        it->pad = 1;
        it->lo = (short)(it->lo * 6 / 5); it->hi = (short)(it->hi * 6 / 5 + 1);
        it->def = (short)(it->def * 5 / 4 + 1);
        if (k != IK_W) it->lo = it->hi = 0;
        if (k == IK_W) it->def = 0;
    }
    if (it->rar == R_MAGIC) {
        add_aff(it, ilvl);
        if (chance(45)) add_aff(it, ilvl);
    } else if (it->rar == R_RARE) {
        int n = rr(3, 4);
        while (n--) add_aff(it, ilvl);
        it->rn1 = (unsigned char)rr(0, 9); it->rn2 = (unsigned char)rr(0, 9);
    }
}

static int aff_stat(const Item *it, int i)
{
    if (it->aff[i] & 0x80) return it->aff[i] & 0x7F;
    return AFX[it->aff[i]].stat;
}

/* ================================================================ */
/*  save state                                                      */
/* ================================================================ */
#define MW 40
#define MH 40
#define INV_N 12
enum { F_NONE, F_STAIRS, F_CHEST, F_SHRINE, F_FOUNTAIN, F_MERCHANT, F_OPENCHEST, F_HEALER, F_ELDER, F_GEN, F_ARM, F_INN, F_WAYPOINT, F_PORTAL };
#define NAREA_MAX 32

typedef struct {
    int magic, ver;
    int lvl, exp, str, dex, vit, ene, pts, hp, mp, gold, kills;
    Item eq[4], inv[INV_N], belt[4];
    int floor, px, py, dir, steps, bossdone, regen, maxfloor;
    unsigned char map[MH][MW], feat[MH][MW], seen[MH][MW];
    unsigned rng;
    unsigned story, shopseed, wp;
    int tpfloor;
} SaveData;
static SaveData S;
static void camp_arrival(void);
static int hero_dirty = 1;        /* equipment changed: rebuild the hero's battle sprite */
#define SAVE_MAGIC 0x41425953
#define SAVE_VER 4

static const int DX[4] = { 0, 1, 0, -1 }, DY[4] = { -1, 0, 1, 0 };

/* ---- player derived stats ---- */
static int eqstat(int st)
{
    int s = 0, e, i;
    for (e = 0; e < 4; e++) {
        const Item *it = &S.eq[e];
        if (!it->kind) continue;
        for (i = 0; i < it->naff; i++) if (aff_stat(it, i) == st) s += it->av[i];
    }
    return s;
}
static int p_str(void) { return S.str + eqstat(ST_STR); }
static int p_dex(void) { return S.dex + eqstat(ST_DEX); }
static int p_maxhp(void) { return 20 + S.vit * 3 + S.lvl * 3 + eqstat(ST_HP); }
static int p_maxmp(void) { return S.ene * 2 + S.lvl + eqstat(ST_MP); }
static void p_dmg(int *lo, int *hi)
{
    int b = p_str() / 3 + eqstat(ST_DMG);
    if (S.eq[0].kind) { *lo = S.eq[0].lo + b; *hi = S.eq[0].hi + b; }
    else { *lo = 1 + b; *hi = 3 + b; }
}
static int p_def(void) { return S.eq[1].def + S.eq[2].def + eqstat(ST_DEF) + p_dex() / 5; }
static int exp_next(int l) { return l * l * 12 + l * 20; }
static void clamp_hpmp(void)
{
    if (S.hp > p_maxhp()) S.hp = p_maxhp();
    if (S.mp > p_maxmp()) S.mp = p_maxmp();
}

/* skills */
typedef struct { const wchar_t *n; int lvl, mp, target; } Skill; /* target: 0 single,1 all,2 self */
static const Skill SK[4] = {
    { L"화염구", 1, 4, 0 }, { L"치유", 2, 5, 2 }, { L"연쇄 번개", 5, 9, 1 }, { L"서리 신성", 9, 14, 1 },
};

/* ================================================================ */
/*  runtime state                                                   */
/* ================================================================ */
enum { M_TITLE, M_EXPLORE, M_BATTLE, M_INV, M_CHAR, M_MENU, M_DEAD, M_SHOP, M_PROMPT, M_STORY, M_TRADE, M_TALK, M_WARP };
static int mode = M_TITLE, quitflag = 0, dirty = 1;
static int showmap = 0, has_save = 0;
static int sel = 0;          /* generic cursor */
static int isel = 4;         /* inventory selection */
static int csel = 0;         /* char screen selection */
static int confirm = 0;
static wchar_t invmsg[64];
static int tickc = 0;

typedef struct { wchar_t t[52]; u16 c; } Line;
static Line elog[2];

static void addlog(u16 c, const wchar_t *f, ...)
{
    va_list a;
    elog[0] = elog[1];
    va_start(a, f);
    VSNW(elog[1].t, 52, f, a);
    va_end(a);
    elog[1].t[51] = 0;
    elog[1].c = c;
}

/* layout */
static int VY, VH, VCX, VCY, MY, PY;
static void layout(void)
{
    VY = 16; PY = GH - 120; MY = PY - 30; VH = MY - VY; VCX = GW / 2; VCY = VY + VH / 2;
}

/* buttons */
enum {
    B_NONE = 0, B_FWD, B_BACK, B_TL, B_TR, B_INV, B_MAP, B_CHAR, B_MENU, B_CANCEL, B_OKB,
    B_BELT = 20, B_CMD = 30, B_MON = 40, B_SLOT = 50, B_USE = 70, B_DROP, B_CLOSE,
    B_STAT = 80, B_LIST = 90, B_ANY = 120
};
typedef struct { short x, y, w, h, id; } Btn;
static Btn btns[80];
static int nbtn;
static void addbtn(int x, int y, int w, int h, int id)
{
    if (nbtn < 80) { btns[nbtn].x = (short)x; btns[nbtn].y = (short)y; btns[nbtn].w = (short)w; btns[nbtn].h = (short)h; btns[nbtn].id = (short)id; nbtn++; }
}
static int hitbtn(int x, int y)
{
    int i;
    for (i = nbtn - 1; i >= 0; i--)
        if (x >= btns[i].x && y >= btns[i].y && x < btns[i].x + btns[i].w && y < btns[i].y + btns[i].h) return btns[i].id;
    return B_NONE;
}
static void sbutton(int x, int y, int w, int h, const wchar_t *lab, int id, int en, int hl)
{
    fill(x, y, w, h, rgb(52, 44, 36));
    stone(x + 1, y + 1, w - 2, h - 2, en ? 60 : 40);
    hline(x, x + w - 1, y, rgb(120, 104, 76)); vline(x, y, y + h - 1, rgb(120, 104, 76));
    hline(x, x + w - 1, y + h - 1, rgb(16, 12, 10)); vline(x + w - 1, y, y + h - 1, rgb(16, 12, 10));
    if (hl) { rectl(x + 1, y + 1, w - 2, h - 2, rgb(230, 190, 90)); }
    txt(x + w / 2, y + (h - 12) / 2, lab, en ? (hl ? C_YEL : C_GOLD) : C_GRAY, T_CENTER | T_SHADOW);
    if (en) addbtn(x, y, w, h, id);
}

/* ================================================================ */
/*  dungeon generation                                              */
/* ================================================================ */
static void carve(int x, int y) { if (x > 0 && y > 0 && x < MW - 1 && y < MH - 1) S.map[y][x] = 0; }
static int solid(int x, int y) { if (x < 0 || y < 0 || x >= MW || y >= MH) return 1; return S.map[y][x]; }

/* ================================================================ */
/*  WORLD: 5 acts, 21 areas (camps, fields, caves, mazes, dungeons) */
/* ================================================================ */
enum { L_ROOMS, L_CAVE, L_MAZE, L_FIELD, L_CAMP };
enum { TX_ROCK, TX_BRICK, TX_SAND, TX_MOSS, TX_HELL, TX_TREE, TX_CLIFF, TX_PALI, TX_ICE, TX_DUNE, TX_JUNGLE, TX_SEWER, TX_RUNE };
enum { FT_SLAB, FT_DIRT, FT_GRASS, FT_SAND, FT_SNOW, FT_LAVA, FT_MUD };
typedef struct {
    const wchar_t *n;
    unsigned char act, floors, layout, wtex, ftex, sky, camp, boss;
    unsigned char wall[3], flr[3], top[3], top2[3];
    unsigned char pool[6];
} Area;
static const Area AREAS[] = {
    { L"새벽 야영지", 0, 1, L_CAMP, TX_PALI, FT_GRASS, 1, 1, 0, { 110, 80, 50 }, { 56, 72, 36 }, { 8, 10, 28 }, { 70, 48, 60 }, { 0, 0, 0, 0, 0, 0 } },
    { L"피의 들판", 0, 2, L_FIELD, TX_CLIFF, FT_GRASS, 1, 0, 0, { 96, 84, 70 }, { 56, 74, 34 }, { 8, 10, 24 }, { 64, 40, 44 }, { 23, 0, 10, 35, 28, 13 } },
    { L"어둠의 동굴", 0, 2, L_CAVE, TX_ROCK, FT_DIRT, 0, 0, 0, { 92, 76, 58 }, { 64, 52, 38 }, { 34, 28, 22 }, { 20, 16, 12 }, { 10, 35, 30, 0, 28, 1 } },
    { L"검은 숲", 0, 2, L_FIELD, TX_TREE, FT_GRASS, 1, 0, 0, { 44, 64, 32 }, { 42, 56, 28 }, { 4, 6, 12 }, { 30, 30, 44 }, { 23, 24, 10, 11, 29, 30 } },
    { L"무너진 수도원", 0, 3, L_ROOMS, TX_BRICK, FT_SLAB, 0, 0, 0, { 86, 86, 92 }, { 74, 68, 66 }, { 32, 32, 36 }, { 20, 20, 24 }, { 5, 41, 11, 7, 13, 2 } },
    { L"망자의 묘지", 0, 2, L_MAZE, TX_BRICK, FT_SLAB, 0, 0, 1, { 70, 70, 76 }, { 56, 54, 58 }, { 24, 24, 28 }, { 14, 14, 16 }, { 5, 6, 41, 0, 1, 2 } },
    { L"모래바람 야영지", 1, 1, L_CAMP, TX_PALI, FT_SAND, 1, 1, 0, { 130, 100, 60 }, { 160, 132, 86 }, { 50, 96, 190 }, { 230, 200, 150 }, { 0, 0, 0, 0, 0, 0 } },
    { L"타는 모래 사막", 1, 3, L_FIELD, TX_DUNE, FT_SAND, 1, 0, 0, { 160, 128, 84 }, { 172, 142, 94 }, { 46, 96, 196 }, { 236, 208, 160 }, { 38, 33, 34, 16, 25, 35 } },
    { L"모래 무덤", 1, 3, L_ROOMS, TX_SAND, FT_SLAB, 0, 0, 0, { 150, 122, 80 }, { 112, 94, 64 }, { 44, 36, 22 }, { 26, 20, 12 }, { 16, 5, 33, 41, 17, 34 } },
    { L"뱀의 신전", 1, 2, L_MAZE, TX_SAND, FT_SLAB, 0, 0, 1, { 118, 98, 70 }, { 92, 78, 54 }, { 34, 28, 18 }, { 20, 16, 10 }, { 40, 17, 33, 18, 14, 34 } },
    { L"정글 야영지", 2, 1, L_CAMP, TX_PALI, FT_MUD, 1, 1, 0, { 96, 76, 48 }, { 54, 58, 34 }, { 18, 36, 28 }, { 86, 116, 86 }, { 0, 0, 0, 0, 0, 0 } },
    { L"늪지 정글", 2, 3, L_FIELD, TX_JUNGLE, FT_MUD, 1, 0, 0, { 40, 74, 32 }, { 52, 58, 32 }, { 16, 34, 26 }, { 80, 110, 80 }, { 45, 12, 40, 36, 39, 31 } },
    { L"잊혀진 신전", 2, 3, L_ROOMS, TX_MOSS, FT_SLAB, 0, 0, 0, { 76, 88, 68 }, { 58, 64, 52 }, { 26, 30, 22 }, { 14, 18, 12 }, { 12, 9, 31, 21, 45, 41 } },
    { L"지하 하수도", 2, 2, L_MAZE, TX_SEWER, FT_MUD, 0, 0, 1, { 72, 82, 62 }, { 48, 58, 38 }, { 22, 26, 18 }, { 12, 14, 10 }, { 46, 45, 39, 28, 3, 15 } },
    { L"지옥문 요새", 3, 1, L_CAMP, TX_RUNE, FT_SLAB, 1, 1, 0, { 80, 64, 70 }, { 70, 60, 60 }, { 40, 0, 0 }, { 170, 56, 20 }, { 0, 0, 0, 0, 0, 0 } },
    { L"불타는 평원", 3, 3, L_FIELD, TX_HELL, FT_LAVA, 1, 0, 0, { 92, 46, 36 }, { 62, 30, 22 }, { 30, 0, 0 }, { 190, 64, 20 }, { 26, 19, 37, 42, 20, 4 } },
    { L"혼돈의 성역", 3, 3, L_ROOMS, TX_HELL, FT_LAVA, 0, 0, 1, { 90, 44, 36 }, { 64, 28, 20 }, { 40, 10, 6 }, { 20, 4, 2 }, { 19, 18, 42, 20, 47, 48 } },
    { L"산악 야영지", 4, 1, L_CAMP, TX_PALI, FT_SNOW, 1, 1, 0, { 110, 90, 70 }, { 196, 202, 214 }, { 56, 66, 90 }, { 170, 180, 200 }, { 0, 0, 0, 0, 0, 0 } },
    { L"얼어붙은 고원", 4, 3, L_FIELD, TX_CLIFF, FT_SNOW, 1, 0, 0, { 116, 120, 128 }, { 200, 206, 216 }, { 50, 60, 84 }, { 176, 186, 206 }, { 27, 22, 21, 43, 8, 50 } },
    { L"얼음 동굴", 4, 3, L_CAVE, TX_ICE, FT_SNOW, 0, 0, 0, { 120, 160, 200 }, { 150, 170, 190 }, { 30, 40, 60 }, { 16, 22, 36 }, { 43, 27, 32, 50, 22, 8 } },
    { L"세계석 성채", 4, 3, L_ROOMS, TX_RUNE, FT_SLAB, 0, 0, 1, { 80, 66, 100 }, { 60, 54, 72 }, { 26, 20, 34 }, { 14, 10, 20 }, { 44, 8, 20, 49, 21, 19 } },
};
#define NAREA ((int)(sizeof(AREAS) / sizeof(AREAS[0])))
static int CYCLE_LEN = 0;
static const wchar_t *DIFFN[3] = { L"보통", L"악몽", L"지옥" };
static const wchar_t *ACTN[5] = { L"제1막", L"제2막", L"제3막", L"제4막", L"제5막" };

/* floor -> area, index within area, difficulty cycle */
static int area_info(int f, int *idx, int *cyc)
{
    int i, a, ff;
    if (!CYCLE_LEN) for (i = 0; i < NAREA; i++) CYCLE_LEN += AREAS[i].floors;
    if (f < 1) f = 1;
    ff = (f - 1) % CYCLE_LEN;
    if (cyc) *cyc = (f - 1) / CYCLE_LEN;
    for (a = 0; a < NAREA; a++) {
        if (ff < AREAS[a].floors) { if (idx) *idx = ff; return a; }
        ff -= AREAS[a].floors;
    }
    if (idx) *idx = 0;
    return 0;
}
static int area_of(int f) { return area_info(f, 0, 0); }
#define CA (&AREAS[area_of(S.floor)])
static int is_boss_floor(int f) { int i, a = area_info(f, &i, 0); return AREAS[a].boss && i == AREAS[a].floors - 1; }
static float g_fog = 1.0f;

static void theme(int *wr, int *wg, int *wb, int *fr, int *fg, int *fb)
{
    const Area *a = CA;
    *wr = a->wall[0]; *wg = a->wall[1]; *wb = a->wall[2]; *fr = a->flr[0]; *fg = a->flr[1]; *fb = a->flr[2];
}

/* per-area wall texel; returns 0 normal, 1 self-lit, 2 transparent (see-through) */
static int texel(int tt, int ui, int vi, int sd, int wr, int wg, int wb, int *r, int *g, int *b)
{
    int v;
    switch (tt) {
    case TX_TREE: case TX_JUNGLE: {
        int top = 26 + (int)(hsh(ui / 14 + sd * 5, 7) & 63);
        int trunk = ((ui + sd * 37) % 90) < 20;
        if (vi < top) return 2;
        if (vi < 160 || (tt == TX_JUNGLE && !trunk)) {
            int n = (int)(hsh(ui >> 3, vi >> 3) & 31) - 16 + (int)(hsh(ui >> 1, vi >> 1) & 15) - 8;
            if ((hsh(ui >> 4, vi >> 4) & 3) == 0) n -= 22;
            if (tt == TX_JUNGLE && (hsh(ui >> 2, 3) & 7) == 0 && vi > top) { *r = 30; *g = 90 + n; *b = 30; return 0; } /* vines */
            *r = wr + n; *g = wg + n + 6; *b = wb + n;
            return 0;
        }
        if (trunk) {
            v = (int)(hsh(ui >> 1, vi >> 4) & 15) - 8 - ((ui + sd * 37) % 90 < 4 ? 20 : 0);
            *r = 70 + v; *g = 52 + v; *b = 36 + v;
        } else { v = (int)(hsh(ui >> 2, vi >> 2) & 15); *r = 12 + v; *g = 22 + v; *b = 10 + v / 2; }
        return 0;
    }
    case TX_CLIFF: {
        int top = 14 + (int)(hsh(ui / 10 + sd * 3, 1) & 31);
        if (vi < top) return 2;
        if (vi < top + 14) {
            const Area *a = CA;
            v = (int)(hsh(ui >> 1, vi >> 1) & 15) - 8;
            *r = a->flr[0] + v; *g = a->flr[1] + v; *b = a->flr[2] + v;
            return 0;
        }
        return texel(TX_ROCK, ui, vi, sd, wr, wg, wb, r, g, b);
    }
    case TX_PALI: {
        int lu = ui % 32, tip = 40 - (lu > 16 ? lu - 16 : 16 - lu) * 5 / 2;
        if (vi < tip) return 2;
        v = (int)(hsh(ui >> 1, vi >> 3) & 15) - 8;
        if (lu < 2 || lu > 29) v -= 40;
        if ((vi > 88 && vi < 96) || (vi > 196 && vi < 204)) { *r = 60 + v; *g = 50 + v; *b = 30; return 0; }
        *r = wr + v; *g = wg + v; *b = wb + v;
        return 0;
    }
    case TX_ICE: {
        int big = (int)(hsh(ui >> 5, vi >> 5) & 31) - 16, crack = (hsh(ui >> 1, vi >> 1) & 63) == 0 || (((ui + vi * 2 + sd * 11) & 63) == 0);
        v = big + (int)(hsh(ui >> 2, vi >> 2) & 7);
        if (((ui + vi) & 63) < 5) v += 40;
        if (crack) v += 60;
        *r = wr + v; *g = wg + v; *b = wb + v;
        return 0;
    }
    case TX_DUNE: {
        int st = vi / 22;
        v = (int)(hsh(st + sd, 2) & 15) - 8 + (int)(hsh(ui >> 2, vi >> 2) & 15) - 8;
        if (vi % 22 < 2) v -= 18;
        if ((hsh(ui >> 3, vi >> 3) & 31) == 0) v -= 25;
        *r = wr + v; *g = wg + v; *b = wb + v;
        return 0;
    }
    case TX_SEWER: {
        int rc = texel(TX_BRICK, ui, vi, sd, wr, wg, wb, r, g, b);
        unsigned h = hsh(ui / 6, sd);
        if ((h & 7) == 0 && vi < (int)((h >> 3) & 255)) { *r = 40; *g = 90 + (int)((h >> 11) & 31); *b = 30; }
        return rc;
    }
    case TX_RUNE: {
        int row = (vi * 5) >> 8, fv = (vi * 5) & 255, ub = ui * 2 + ((row & 1) ? 128 : 0), fu = ub & 255, col = ub >> 8;
        if (fv < 16 || fu < 10) { *r = wr / 3; *g = wg / 3; *b = wb / 3; return 0; }
        v = (int)(hsh(col + sd, row) & 15) - 8 + (int)(hsh(ui >> 2, vi >> 2) & 7) - 4;
        *r = wr + v; *g = wg + v; *b = wb + v;
        if ((hsh(col + sd * 3, row + 9) & 3) == 0 && fu > 70 && fu < 186 && fv > 60 && fv < 196) {
            int gx = (fu - 70) / 12, gy = (fv - 60) / 14;
            if ((hsh(gx + col * 13, gy + row * 7) & 3) == 0 && ((fu & 7) < 3 || (fv & 7) < 3)) { *r = 140; *g = 120; *b = 255; return 1; }
        }
        return 0;
    }
    }
    if (tt == TX_ROCK || tt == TX_HELL) {                      /* rough rock */
        int ry = vi / 36, off = (int)(hsh(ry, sd) & 31), bw = 28 + (int)(hsh(ry, sd + 1) & 31), cx = (ui + off) / bw, lu = (ui + off) % bw, lv = vi % 36;
        int edge = lu < 3 || lv < 3;
        v = (int)(hsh(cx + sd * 3, ry + sd) & 31) - 16 + (int)(hsh(ui >> 2, vi >> 2) & 15) - 8;
        if (lv > 27) v -= 10;
        if (lv < 9) v += 7;
        if (edge) v -= 34;
        *r = wr + v; *g = wg + v; *b = wb + v;
        if (tt == TX_HELL && edge && (hsh(ui >> 1, vi >> 1) & 7) < 4) {
            int f = (int)(hsh(ui >> 2, vi >> 3) & 63);
            *r = 255; *g = 90 + f; *b = 20;
            return 1;
        }
        return 0;
    }
    if (tt == TX_SAND) {                                 /* sandstone blocks */
        int row = (vi * 4) >> 8, fv = (vi * 4) & 255, ub = ui * 2 + ((row & 1) ? 128 : 0), fu = ub & 255, col = ub >> 8;
        v = (int)(hsh(ui >> 1, vi >> 1) & 15) - 8 + (int)(hsh(col + sd, row) & 15) - 8;
        if (fv < 12 || fu < 8) v -= 40;
        else if ((hsh(col + sd * 5, row + 3) & 3) == 0 && fu > 60 && fu < 196 && fv > 60 && fv < 196 &&
                 ((fu & 31) < 4 || (fv & 31) < 4)) v -= 26;
        *r = wr + v; *g = wg + v; *b = wb + v;
        return 0;
    }
    {                                              /* brick (catacomb / temple) */
        int row = (vi * 7) >> 8, fv = (vi * 7) & 255, ub = ui * 3 + ((row & 1) ? 128 : 0), fu = ub & 255;
        if (fv < 28 || fu < 14) { *r = wr / 3; *g = wg / 3; *b = wb / 3; }
        else {
            v = (int)(hsh((ub >> 8) + sd * 7, row + sd * 13) & 31) - 16 + (int)(hsh(ui >> 2, vi >> 2) & 7) - 4;
            if (fv > 224) v -= 12;
            if (fv < 48) v += 8;
            *r = wr + v; *g = wg + v; *b = wb + v;
        }
        if (tt == TX_MOSS && (int)(hsh(ui >> 2, vi >> 2) & 255) < vi * 2 / 3 - 30) { *r = *r * 2 / 3; *g += 14; *b = *b * 2 / 3; }
        return 0;
    }
}

/* ground / ceiling texel in world space (1/256 cell units); returns 1 if self-lit */
static int ftexel(int ft, int wx, int wy, int br, int bgc, int bb, int *r, int *g, int *b)
{
    int v = 0;
    switch (ft) {
    case FT_SLAB: {
        int fu = wx & 127, fv = wy & 127;
        v = (int)(hsh(wx >> 7, wy >> 7) & 15) - 8 + (int)(hsh(wx >> 3, wy >> 3) & 7) - 4;
        if (fu < 6 || fv < 6) v -= 30;
        break;
    }
    case FT_DIRT:
        v = (int)(hsh(wx >> 5, wy >> 5) & 15) - 8 + (int)(hsh(wx >> 2, wy >> 2) & 7) - 4;
        if ((hsh(wx >> 4, wy >> 4) & 63) == 0) v += 24;
        break;
    case FT_GRASS:
        v = (int)(hsh(wx >> 4, wy >> 4) & 15) - 8 + (int)(hsh(wx >> 2, wy >> 2) & 7) - 4;
        if ((hsh(wx >> 7, wy >> 7) & 7) == 0) { *r = br + 30 + v; *g = bgc + v - 6; *b = bb + v - 10; return 0; }
        if ((hsh(wx >> 2, wy >> 2) & 7) == 0) v += 16;
        break;
    case FT_SAND:
        v = (int)(hsh(wx >> 3, wy >> 3) & 7) - 4 + ((((wx + (wy >> 1)) >> 4) & 3) == 0 ? 10 : 0);
        break;
    case FT_SNOW:
        v = (int)(hsh(wx >> 4, wy >> 4) & 7) - 4;
        if ((hsh(wx >> 1, wy >> 1) & 255) == 0) v += 40;
        break;
    case FT_LAVA:
        v = (int)(hsh(wx >> 5, wy >> 5) & 15) - 8;
        if (((wx >> 2) & 31) < 2 && (hsh(wx >> 6, wy >> 4) & 3) == 0) { *r = 255; *g = 100 + (int)(hsh(wx >> 3, wy >> 3) & 63); *b = 20; return 1; }
        if (((wy >> 2) & 31) < 2 && (hsh(wx >> 4, wy >> 6) & 3) == 0) { *r = 240; *g = 70; *b = 10; return 1; }
        break;
    case FT_MUD:
        v = (int)(hsh(wx >> 4, wy >> 4) & 15) - 8;
        if ((hsh(wx >> 6, wy >> 6) & 7) < 2) { v = -20 + (int)(((wx + wy) >> 3) & 3) * 6; *r = br / 2 + v; *g = bgc / 2 + v + 10; *b = bb / 2 + v + 14; return 0; }
        break;
    }
    *r = br + v; *g = bgc + v; *b = bb + v;
    return 0;
}

/* ---- map generators ---- */
static short bq[MW * MH];
static short bd[MH][MW];
static int bfs(int sx, int sy, int *fx, int *fy)
{
    int h = 0, t = 0, n = 0, best = -1;
    memset(bd, 0xFF, sizeof(bd));
    bd[sy][sx] = 0; bq[t++] = (short)(sy * MW + sx);
    while (h < t) {
        int c = bq[h++], x = c % MW, y = c / MW, d;
        n++;
        if (bd[y][x] > best && !S.feat[y][x]) { best = bd[y][x]; if (fx) { *fx = x; *fy = y; } }
        for (d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (nx < 0 || ny < 0 || nx >= MW || ny >= MH || S.map[ny][nx] || bd[ny][nx] >= 0) continue;
            bd[ny][nx] = (short)(bd[y][x] + 1);
            bq[t++] = (short)(ny * MW + nx);
        }
    }
    return n;
}
static void connect_only(int sx, int sy)
{
    int x, y;
    bfs(sx, sy, 0, 0);
    for (y = 0; y < MH; y++) for (x = 0; x < MW; x++) if (!S.map[y][x] && bd[y][x] < 0) S.map[y][x] = 1;
}
static int floor_count(void) { int x, y, n = 0; for (y = 0; y < MH; y++) for (x = 0; x < MW; x++) n += !S.map[y][x]; return n; }
static void random_floor(int *x, int *y)
{
    int t;
    for (t = 0; t < 5000; t++) {
        *x = rr(1, MW - 2); *y = rr(1, MH - 2);
        if (!S.map[*y][*x]) return;
    }
}

static void gen_rooms(void)
{
    int rx[16], ry[16], rw[16], rh[16], nr = 0, t, i;
    for (t = 0; t < 900 && nr < 14; t++) {
        int w = rr(2, 6), h = rr(2, 6), x = rr(1, MW - w - 2), y = rr(1, MH - h - 2), ok = 1;
        for (i = 0; i < nr; i++)
            if (x <= rx[i] + rw[i] + 1 && rx[i] <= x + w + 1 && y <= ry[i] + rh[i] + 1 && ry[i] <= y + h + 1) ok = 0;
        if (!ok) continue;
        rx[nr] = x; ry[nr] = y; rw[nr] = w; rh[nr] = h; nr++;
        { int a, b; for (b = y; b < y + h; b++) for (a = x; a < x + w; a++) carve(a, b); }
    }
    for (i = 1; i < nr + 3; i++) {
        int a = (i < nr) ? i - 1 : rr(0, nr - 1), b = (i < nr) ? i : rr(0, nr - 1);
        int x1 = rx[a] + rw[a] / 2, y1 = ry[a] + rh[a] / 2, x2 = rx[b] + rw[b] / 2, y2 = ry[b] + rh[b] / 2, x, y;
        if (chance(50)) {
            for (x = x1; x != x2; x += (x2 > x1 ? 1 : -1)) carve(x, y1);
            for (y = y1; y != y2; y += (y2 > y1 ? 1 : -1)) carve(x2, y);
        } else {
            for (y = y1; y != y2; y += (y2 > y1 ? 1 : -1)) carve(x1, y);
            for (x = x1; x != x2; x += (x2 > x1 ? 1 : -1)) carve(x, y2);
        }
        carve(x2, y2);
    }
    S.px = rx[0] + rw[0] / 2; S.py = ry[0] + rh[0] / 2;
}
static void gen_cave(void)
{
    static unsigned char tmp[MH][MW];
    int x, y, it;
    for (y = 0; y < MH; y++) for (x = 0; x < MW; x++)
        S.map[y][x] = (x == 0 || y == 0 || x == MW - 1 || y == MH - 1) ? 1 : (unsigned char)chance(46);
    for (it = 0; it < 5; it++) {
        for (y = 1; y < MH - 1; y++) for (x = 1; x < MW - 1; x++) {
            int a, b, c = 0;
            for (b = -1; b <= 1; b++) for (a = -1; a <= 1; a++) c += S.map[y + b][x + a];
            tmp[y][x] = (unsigned char)(c >= 5 || (it < 2 && c <= 1));
        }
        for (y = 1; y < MH - 1; y++) for (x = 1; x < MW - 1; x++) S.map[y][x] = tmp[y][x];
    }
    /* start = cell of the largest region */
    {
        int best = 0, bx = MW / 2, by = MH / 2, t;
        for (t = 0; t < 12; t++) {
            int sx, sy, n;
            random_floor(&sx, &sy);
            n = bfs(sx, sy, 0, 0);
            if (n > best) { best = n; bx = sx; by = sy; }
        }
        S.px = bx; S.py = by;
    }
}
static void gen_maze(void)
{
    static short st[MW * MH];
    int sp = 0, x, y, i;
    for (y = 1; y < MH - 1; y += 2) for (x = 1; x < MW - 1; x += 2) S.map[y][x] = 2;   /* unvisited marker */
    S.map[1][1] = 0;
    st[sp++] = (short)(1 * MW + 1);
    while (sp) {
        int c = st[sp - 1], cx = c % MW, cy = c / MW, nb[4], nn = 0, d;
        for (d = 0; d < 4; d++) {
            int nx = cx + DX[d] * 2, ny = cy + DY[d] * 2;
            if (nx > 0 && ny > 0 && nx < MW - 1 && ny < MH - 1 && S.map[ny][nx] == 2) nb[nn++] = d;
        }
        if (!nn) { sp--; continue; }
        d = nb[rr(0, nn - 1)];
        S.map[cy + DY[d]][cx + DX[d]] = 0;
        S.map[cy + DY[d] * 2][cx + DX[d] * 2] = 0;
        st[sp++] = (short)((cy + DY[d] * 2) * MW + cx + DX[d] * 2);
    }
    for (y = 0; y < MH; y++) for (x = 0; x < MW; x++) if (S.map[y][x] == 2) S.map[y][x] = 1;
    for (i = 0; i < 70; i++) { x = rr(2, MW - 3); y = rr(2, MH - 3); if (S.map[y][x] && ((x & 1) != (y & 1))) S.map[y][x] = 0; }
    for (i = 0; i < 5; i++) {
        int rx = rr(2, MW - 6), ry = rr(2, MH - 6), a, b;
        for (b = 0; b < 3; b++) for (a = 0; a < 3; a++) carve(rx + a, ry + b);
    }
    S.px = 1; S.py = 1;
}
static void gen_field(void)
{
    int x, y, i;
    for (y = 1; y < MH - 1; y++) for (x = 1; x < MW - 1; x++) S.map[y][x] = 0;
    for (i = 0; i < 130; i++) {
        int cx = rr(2, MW - 3), cy = rr(2, MH - 3), r = rr(0, 2), a, b;
        for (b = -r; b <= r; b++) for (a = -r; a <= r; a++)
            if (a * a + b * b <= r * r + 1 && chance(85)) { int xx = cx + a, yy = cy + b; if (xx > 0 && yy > 0 && xx < MW - 1 && yy < MH - 1) S.map[yy][xx] = 1; }
    }
    for (i = 0; i < 7; i++) {
        int cx = rr(3, MW - 4), cy = rr(3, MH - 4), d = rr(0, 3), n = rr(6, 14);
        while (n--) {
            if (cx > 0 && cy > 0 && cx < MW - 1 && cy < MH - 1) S.map[cy][cx] = 1;
            if (chance(25)) d = (d + (chance(50) ? 1 : 3)) & 3;
            cx += DX[d]; cy += DY[d];
        }
    }
    random_floor(&S.px, &S.py);
}
static void gen_camp(void)
{
    int x, y, c = MW / 2, i;
    for (y = c - 6; y <= c + 6; y++) for (x = c - 6; x <= c + 6; x++) S.map[y][x] = 0;
    for (i = 0; i < 3; i++) { int a = c + rr(-4, 4), b = c + rr(-4, 4); if (abs(a - c) > 1 || abs(b - c) > 1) S.map[b][a] = 1; }
    S.px = c; S.py = c;
}

static void place_rand(int f, int mind)
{
    int t;
    for (t = 0; t < 400; t++) {
        int x, y;
        random_floor(&x, &y);
        if (!S.feat[y][x] && !(x == S.px && y == S.py) && bd[y][x] >= mind) { S.feat[y][x] = (unsigned char)f; return; }
    }
}

static void mark_seen(void);
static void gen_floor(void)
{
    const Area *A = CA;
    int t, d, fx = 0, fy = 0, i;
    g_fog = A->sky ? 0.62f : 1.0f;
    for (t = 0; t < 30; t++) {
        memset(S.map, 1, sizeof(S.map));
        memset(S.feat, 0, sizeof(S.feat));
        memset(S.seen, 0, sizeof(S.seen));
        switch (A->layout) {
        case L_CAVE: gen_cave(); break;
        case L_MAZE: gen_maze(); break;
        case L_FIELD: gen_field(); break;
        case L_CAMP: gen_camp(); break;
        default: gen_rooms(); break;
        }
        if (S.map[S.py][S.px]) continue;
        connect_only(S.px, S.py);
        if (floor_count() >= (A->layout == L_CAMP ? 40 : 120)) break;
    }
    bfs(S.px, S.py, &fx, &fy);
    S.wp |= 1u << area_of(S.floor);
    if (A->camp) {
        int c = MW / 2, x, y;
        for (y = c - 5; y <= c + 5; y++) for (x = c - 5; x <= c + 5; x++) S.map[y][x] = 0;
        S.px = c; S.py = c + 1;
        S.feat[c - 3][c] = F_ELDER;
        S.feat[c - 2][c - 4] = F_GEN;
        S.feat[c - 2][c + 4] = F_ARM;
        S.feat[c + 2][c - 4] = F_INN;
        S.feat[c + 2][c + 4] = F_WAYPOINT;
        S.feat[c + 5][c] = F_STAIRS;
        S.map[c + 5][c] = 0;
        if (S.tpfloor) S.feat[c + 3][c + 1] = F_PORTAL;
        S.shopseed = rnd();
    } else {
        int big = A->layout == L_FIELD || A->layout == L_CAVE;
        S.feat[fy][fx] = F_STAIRS;
        {
            int idx, d2;
            area_info(S.floor, &idx, 0);
            if (idx == 0) for (d2 = 0; d2 < 4; d2++) {
                int wx = S.px + DX[d2], wy = S.py + DY[d2];
                if (!solid(wx, wy) && !S.feat[wy][wx]) { S.feat[wy][wx] = F_WAYPOINT; break; }
            }
        }
        for (i = rr(2, 4) + (big ? 2 : 0); i > 0; i--) place_rand(F_CHEST, 3);
        if (chance(70)) place_rand(F_SHRINE, 4);
        if (chance(30) && big) place_rand(F_SHRINE, 4);
        if (chance(40)) place_rand(F_FOUNTAIN, 4);
        if (chance(35)) place_rand(F_MERCHANT, 5);
    }
    S.dir = 0;
    for (d = 0; d < 4; d++) if (!solid(S.px + DX[d], S.py + DY[d])) { S.dir = d; break; }
    if (A->camp) S.dir = 0;
    S.bossdone = !is_boss_floor(S.floor);
    S.steps = 0;
    mark_seen();
}

static void mark_seen(void)
{
    int k, l, r = (S.dir + 1) & 3;
    int km = CA->sky ? 7 : 4;
    for (k = 0; k <= km; k++) {
        int blocked = solid(S.px + DX[S.dir] * k, S.py + DY[S.dir] * k) && k > 0;
        for (l = -3; l <= 3; l++) {
            int x = S.px + DX[S.dir] * k + DX[r] * l, y = S.py + DY[S.dir] * k + DY[r] * l;
            if (x >= 0 && y >= 0 && x < MW && y < MH) S.seen[y][x] = 1;
        }
        if (blocked) break;
    }
    for (l = 0; l < 4; l++) {
        int x = S.px + DX[l], y = S.py + DY[l];
        if (x >= 0 && y >= 0 && x < MW && y < MH) S.seen[y][x] = 1;
    }
    S.seen[S.py][S.px] = 1;
}

/* ================================================================ */
/*  3D first-person view (Wizardry style, painter's algorithm)      */
/* ================================================================ */
static int iceilf(float f) { int i = (int)f; if ((float)i < f) i++; return i; }

static int shz(float z)
{
    int s = (int)(300.0f - z * g_fog * 58.0f);
    return clampi(s, 0, 256);
}

static void wallface(float l1, float z1, float l2, float z2, int side, int wseed)
{
    float hh = VH * 0.25f, s1, s2, u1 = 0, u2 = 1, iz1, iz2;
    int xa, xb, x, wr, wg, wb, fr, fg, fbb, tt = CA->wtex;
    float wtop = CA->sky ? 2.4f : 1.0f;
    theme(&wr, &wg, &wb, &fr, &fg, &fbb);
    s1 = VCX + 120.0f * l1 / z1;
    s2 = VCX + 120.0f * l2 / z2;
    if (s1 > s2) { float t = s1; s1 = s2; s2 = t; t = z1; z1 = z2; z2 = t; u1 = 1; u2 = 0; }
    if (s2 - s1 < 0.5f) return;
    xa = iceilf(s1 - 0.5f); xb = iceilf(s2 - 0.5f) - 1;
    if (xa < 0) xa = 0;
    if (xb > GW - 1) xb = GW - 1;
    iz1 = 1.0f / z1; iz2 = 1.0f / z2;
    for (x = xa; x <= xb; x++) {
        float t = (x + 0.5f - s1) / (s2 - s1), iz = iz1 + (iz2 - iz1) * t, z = 1.0f / iz;
        float u = ((1 - t) * u1 * iz1 + t * u2 * iz2) / iz;
        float h = hh * iz, top = VCY - h * wtop, ht = h * (1.0f + wtop);
        int sh = shz(z), y0, y1, y, vf, vstep, ui, warm;
        if (side) sh = sh * 3 / 4;
        y0 = iceilf(top - 0.5f); y1 = iceilf(VCY + h - 0.5f) - 1;
        if (y0 < VY) y0 = VY;
        if (y1 > VY + VH - 1) y1 = VY + VH - 1;
        if (y1 < y0) continue;
        vf = (int)(((y0 + 0.5f - top) / ht) * 65536.0f);
        vstep = (int)(65536.0f / ht);
        ui = clampi((int)(u * 256.0f), 0, 255);
        warm = z < 1.6f ? (int)((1.6f - z) * 18) : 0;
        for (y = y0; y <= y1; y++, vf += vstep) {
            int r, g, b, vi = clampi(vf >> 8, 0, 255);
            int rc = texel(tt, ui, vi, wseed, wr, wg, wb, &r, &g, &b);
            if (rc == 2) continue;
            if (rc) {
                int s2_ = 128 + sh / 2;
                FB[y * GW + x] = rgb(r * s2_ >> 8, g * s2_ >> 8, b * s2_ >> 8);
                continue;
            }
            r += warm; g += warm / 3;
            FB[y * GW + x] = rgb(r * sh >> 8, g * sh >> 8, b * sh >> 8);
        }
    }
}
static const wchar_t *NPCN[5][4];
static void draw_npc(int fx, int fy, int u, int pv, int sh, u16 robe, u16 trim, int prop)
{
    int w = u * 34 / 100, h = pv * 74 / 100, y;
    for (y = 0; y < h; y++) {
        int ww = w / 3 + (w * 2 / 3) * y / h;
        u16 c = shade(robe, sh * (200 + 56 * (h - y) / h) >> 8);
        hline(fx - ww / 2, fx + ww / 2, fy - h + y, c);
        if (y > h * 2 / 3 && y < h * 2 / 3 + 2) hline(fx - ww / 2, fx + ww / 2, fy - h + y, shade(trim, sh));
    }
    fcircle(fx, fy - h + h / 10, w / 5 + 1, shade(rgb(190, 150, 120), sh));
    fill(fx - w / 4, fy - h - h / 40, w / 2 + 1, h / 10 + 1, shade(robe, sh * 3 / 4));
    pset(fx - w / 12, fy - h + h / 10, rgb(20, 10, 10)); pset(fx + w / 12, fy - h + h / 10, rgb(20, 10, 10));
    if (prop == 1) { vline(fx + w / 2 + 2, fy - h - h / 6, fy, shade(rgb(120, 90, 50), sh)); fcircle(fx + w / 2 + 2, fy - h - h / 6, w / 9 + 1, rgb(255, 240, 170)); }
    if (prop == 2) { fill(fx + w / 2, fy - h / 5, w / 2, h / 6 + 1, shade(rgb(70, 70, 76), sh)); fill(fx + w / 2 + w / 8, fy - h / 5 + h / 6, w / 4, h / 5, shade(rgb(50, 50, 54), sh)); }
    if (prop == 3) { fcircle(fx - w / 2 - 3, fy - h / 3, w / 7 + 1, shade(rgb(140, 100, 60), sh)); fcircle(fx - w / 2 - 3, fy - h / 3 - w / 7, w / 12 + 1, rgb(120, 220, 120)); }
    if (prop == 4) { fill(fx + w / 2, fy - h / 2, w / 4 + 1, h / 3, shade(rgb(130, 90, 50), sh)); fcircle(fx + w / 2 + w / 8, fy - h / 2 - 2, w / 10 + 1, rgb(255, 200, 80)); }
}
static void draw_portal(int fx, int fy, int u, int pv, u16 inner, u16 rim)
{
    int w = u * 4 / 10, h = pv * 7 / 10, yy, xx;
    for (yy = -h / 2; yy <= h / 2; yy++) for (xx = -w / 2; xx <= w / 2; xx++) {
        int e = xx * xx * 256 / (w * w / 4 + 1) + yy * yy * 256 / (h * h / 4 + 1);
        if (e <= 256) pset(fx + xx, fy - h / 2 - 2 + yy, e > 200 ? rim : mix(inner, rgb(255, 255, 255), (int)(hsh(xx + tickc, yy) & 63)));
    }
}
static void draw_feature(int f, float z)
{
    float hh = VH * 0.25f;
    int fy = VCY + (int)(hh / z), u = (int)(120.0f / z), pv = (int)(2 * hh / z), sh = shz(z), fx = VCX;
    switch (f) {
    case F_CHEST: case F_OPENCHEST: {
        int w = u * 5 / 10, h = pv * 3 / 10, x = fx - w / 2, y = fy - h;
        fill(x, y, w, h, shade(rgb(110, 66, 28), sh));
        fill(x, y, w, h / 3, shade(f == F_CHEST ? rgb(90, 52, 20) : rgb(20, 10, 4), sh));
        rectl(x, y, w, h, shade(rgb(200, 160, 60), sh));
        hline(x, x + w - 1, y + h / 3, shade(rgb(200, 160, 60), sh));
        fill(fx - w / 12, y + h / 3 - 1, w / 6 + 1, h / 4 + 1, shade(rgb(230, 200, 80), sh));
        break;
    }
    case F_ELDER: case F_GEN: case F_ARM: case F_INN: {
        static const unsigned char RC[4][6] = { { 200, 196, 186, 200, 170, 90 }, { 60, 110, 60, 200, 180, 90 }, { 70, 64, 60, 160, 60, 40 }, { 150, 40, 32, 230, 210, 180 } };
        int r = f - F_ELDER;
        draw_npc(fx, fy, u, pv, sh, rgb(RC[r][0], RC[r][1], RC[r][2]), rgb(RC[r][3], RC[r][4], RC[r][5]), r == 0 ? 1 : r == 1 ? 3 : r == 2 ? 2 : 4);
        if (z < 1.5f && S.floor > 0) {
            int ar = AREAS[0].act; (void)ar;
            txt(fx, fy - pv * 74 / 100 - 16, NPCN[CA->act][r], C_GOLD, T_CENTER | T_SHADOW);
        }
        break;
    }
    case F_WAYPOINT: {
        int w = u * 7 / 10, h = pv / 7 + 2, yy, xx;
        for (yy = -h; yy <= 0; yy++) for (xx = -w / 2; xx <= w / 2; xx++) {
            int e = xx * xx * 256 / (w * w / 4 + 1) + (yy + h / 2) * (yy + h / 2) * 256 / (h * h / 4 + 1);
            if (e <= 256) pset(fx + xx, fy + yy, e > 190 ? shade(rgb(150, 140, 120), sh) : mix(shade(rgb(40, 60, 120), sh), rgb(120, 180, 255), (int)((hsh(xx / 2, yy + tickc / 3) & 63) + 40)));
        }
        if (z < 1.5f) txt(fx, fy - h - 16, L"길잡이석", rgb(140, 190, 255), T_CENTER | T_SHADOW);
        break;
    }
    case F_PORTAL:
        draw_portal(fx, fy, u, pv, rgb(160, 40, 40), rgb(255, 170, 120));
        if (z < 1.5f) txt(fx, fy - pv * 7 / 10 - 16, L"차원문", rgb(255, 170, 120), T_CENTER | T_SHADOW);
        break;
    case F_HEALER: {
        int w = u * 32 / 100, h = pv * 72 / 100, y;
        for (y = 0; y < h; y++) {
            int ww = w / 3 + (w * 2 / 3) * y / h;
            hline(fx - ww / 2, fx + ww / 2, fy - h + y, shade(rgb(170, 160, 140), sh));
        }
        fill(fx - w / 6, fy - h + h / 10, w / 3 + 1, h / 7 + 1, shade(rgb(200, 160, 130), sh));
        vline(fx + w / 2 + 2, fy - h - h / 8, fy, shade(rgb(120, 90, 50), sh));
        fcircle(fx + w / 2 + 2, fy - h - h / 8, w / 8 + 1, rgb(255, 240, 170));
        break;
    }
    case F_STAIRS:
        if (CA->sky) {     /* glowing portal in the open */
            int w = u * 4 / 10, h = pv * 7 / 10, yy, xx;
            for (yy = -h / 2; yy <= h / 2; yy++) for (xx = -w / 2; xx <= w / 2; xx++) {
                int e = xx * xx * 256 / (w * w / 4 + 1) + yy * yy * 256 / (h * h / 4 + 1);
                if (e <= 256) {
                    u16 c = e > 200 ? rgb(180, 220, 255) : rgb(30 + (int)(hsh(xx + tickc, yy) & 31), 70 + e / 4, 200);
                    pset(fx + xx, fy - h / 2 - 2 + yy, c);
                }
            }
            break;
        }
        /* fall through */
    case 100: {
        int w = u * 7 / 10, h = pv / 8 + 2, i;
        fill(fx - w / 2, fy - h, w, h, rgb(0, 0, 0));
        for (i = 1; i < 4; i++) hline(fx - w / 2 + i * w / 10, fx + w / 2 - i * w / 10, fy - h + i * h / 4, shade(rgb(120, 110, 90), sh));
        rectl(fx - w / 2, fy - h, w, h, shade(rgb(150, 130, 100), sh));
        break;
    }
    case F_SHRINE: {
        int w = u / 4, h = pv * 6 / 10;
        fill(fx - w / 2, fy - h, w, h, shade(rgb(120, 116, 110), sh));
        fill(fx - w / 2 - w / 4, fy - h, w + w / 2, h / 8 + 1, shade(rgb(150, 146, 140), sh));
        fill(fx - w / 2 - w / 4, fy - h / 10, w + w / 2, h / 10 + 1, shade(rgb(90, 86, 80), sh));
        fcircle(fx, fy - h - w / 2, w / 2 + 1, shade(rgb(120, 220, 255), clampi(sh + 60, 0, 256)));
        break;
    }
    case F_FOUNTAIN: {
        int w = u * 6 / 10, h = pv / 5;
        fill(fx - w / 2, fy - h, w, h, shade(rgb(110, 110, 120), sh));
        fill(fx - w / 2 + 2, fy - h, w - 4, h / 3 + 1, shade(rgb(60, 120, 230), sh));
        fill(fx - w / 12, fy - h - pv / 4, w / 6 + 1, pv / 4, shade(rgb(110, 110, 120), sh));
        fcircle(fx, fy - h - pv / 4, w / 10 + 1, shade(rgb(140, 190, 255), sh));
        break;
    }
    case F_MERCHANT: {
        int w = u * 35 / 100, h = pv * 7 / 10, y, x;
        for (y = 0; y < h; y++) {
            int ww = w / 3 + (w * 2 / 3) * y / h;
            hline(fx - ww / 2, fx + ww / 2, fy - h + y, shade(rgb(70, 46, 30), sh));
        }
        fill(fx - w / 6, fy - h + h / 10, w / 3 + 1, h / 7 + 1, rgb(0, 0, 0));
        x = w / 12 + 1;
        pset(fx - x, fy - h + h / 10 + h / 14, rgb(255, 220, 60));
        pset(fx + x, fy - h + h / 10 + h / 14, rgb(255, 220, 60));
        fcircle(fx + w / 2, fy - h / 2, w / 10 + 1, rgb(255, 210, 90));
        break;
    }
    }
}

/* Diablo-like light radius: darken toward the edges of the view */
static void vignette(void)
{
    int x, y, hv = VH / 2;
    for (y = VY; y < VY + VH; y++) {
        int dy = (y - VCY) * 256 / hv, dy2 = dy * dy;
        u16 *row = FB + y * GW;
        for (x = 0; x < GW; x++) {
            int dx = (x - VCX) * 256 / VCX, d2 = (dx * dx + dy2) >> 8, f;
            if (d2 <= 120) continue;
            f = 256 - (d2 - 120) * 200 / 392;
            if (f < 40) f = 40;
            row[x] = shade(row[x], f);
        }
    }
}

static void render_view(void)
{
    const Area *A = CA;
    int y, x, k, wr, wg, wb, fr, fg, fbb, r = (S.dir + 1) & 3, kmax = A->sky ? 7 : 4;
    float hh = VH * 0.25f;
    theme(&wr, &wg, &wb, &fr, &fg, &fbb);
    g_fog = A->sky ? 0.62f : 1.0f;
    /* sky or ceiling */
    for (y = VY; y < VCY; y++) {
        u16 *row = FB + y * GW;
        if (A->sky) {
            int t = (y - VY) * 256 / (VCY - VY + 1);
            int cr = A->top[0] + (A->top2[0] - A->top[0]) * t / 256, cg = A->top[1] + (A->top2[1] - A->top[1]) * t / 256,
                cb = A->top[2] + (A->top2[2] - A->top[2]) * t / 256;
            int night = A->top[0] + A->top[1] + A->top[2] < 90;
            for (x = 0; x < GW; x++) {
                int ox = (x + S.dir * 60 + S.px * 3) % 240;
                u16 c = rgb(cr, cg, cb);
                if (night && t < 160 && (hsh(ox, y) & 511) == 0) c = rgb(200, 200, 220);
                else if (!night && ((hsh(ox / 24, (y - VY) / 6) & 7) == 0)) c = mix(c, rgb(255, 255, 255), 50);
                row[x] = c;
            }
        } else {
            float d = VCY - (y + 0.5f), z = hh / d;
            int s = shz(z);
            float lstep = z / 120.0f, l0 = (0.5f - VCX) * lstep;
            float cx = S.px + 0.5f + DX[S.dir] * z, cy = S.py + 0.5f + DY[S.dir] * z;
            int wx = (int)((cx + DX[r] * l0) * 256), wyy = (int)((cy + DY[r] * l0) * 256);
            int sx = (int)(DX[r] * lstep * 256), sy = (int)(DY[r] * lstep * 256);
            for (x = 0; x < GW; x++, wx += sx, wyy += sy) {
                int cr, cg, cb;
                ftexel(A->layout == L_CAVE ? FT_DIRT : FT_SLAB, wx, wyy, A->top[0], A->top[1], A->top[2], &cr, &cg, &cb);
                row[x] = rgb(cr * s >> 8, cg * s >> 8, cb * s >> 8);
            }
        }
    }
    if (A->sky) {
        const int night = A->top[0] + A->top[1] + A->top[2] < 90;
        if (night) {   /* moon */
            int mx = 200 - ((S.dir * 60) % 240), my = VY + 18;
            if (mx < -20) mx += 240;
            fcircle(mx, my, 9, rgb(220, 214, 190));
            fcircle(mx + 3, my - 2, 2, rgb(190, 184, 160));
            fcircle(mx - 3, my + 3, 2, rgb(196, 190, 166));
        }
    }
    /* floor (perspective-correct texture casting) */
    for (y = VCY; y < VY + VH; y++) {
        float d = y + 0.5f - VCY, z = hh / d;
        int s = shz(z);
        float lstep = z / 120.0f, l0 = (0.5f - VCX) * lstep;
        float cx = S.px + 0.5f + DX[S.dir] * z, cy = S.py + 0.5f + DY[S.dir] * z;
        int wx = (int)((cx + DX[r] * l0) * 256), wyy = (int)((cy + DY[r] * l0) * 256);
        int sx = (int)(DX[r] * lstep * 256), sy = (int)(DY[r] * lstep * 256);
        u16 *row = FB + y * GW;
        int warm = z < 1.6f ? (int)((1.6f - z) * 16) : 0;
        for (x = 0; x < GW; x++, wx += sx, wyy += sy) {
            int cr, cg, cb;
            if (ftexel(A->ftex, wx, wyy, fr, fg, fbb, &cr, &cg, &cb)) {
                int s2 = 128 + s / 2;
                row[x] = rgb(cr * s2 >> 8, cg * s2 >> 8, cb * s2 >> 8);
            } else row[x] = rgb((cr + warm) * s >> 8, (cg + warm / 3) * s >> 8, cb * s >> 8);
        }
    }
    for (k = kmax; k >= 0; k--) {
        float zn = k - 0.5f, zf = k + 0.5f;
        int L;
        if (zn < 0.05f) zn = 0.05f;
        for (L = k + 2; L >= 0; L--) {
            int sgn;
            for (sgn = -1; sgn <= 1; sgn += 2) {
                int l = L * sgn;
                int wx = S.px + DX[S.dir] * k + DX[r] * l, wy = S.py + DY[S.dir] * k + DY[r] * l;
                int sd = (wx * 31 + wy * 17) & 63;
                if (L == 0 && sgn > 0) break;
                if (!solid(wx, wy)) continue;
                if (k > 0) wallface(l - 0.5f, zn, l + 0.5f, zn, 0, sd);
                if (l < 0) wallface(l + 0.5f, zn, l + 0.5f, zf, 1, sd + 1);
                if (l > 0) wallface(l - 0.5f, zn, l - 0.5f, zf, 1, sd + 2);
            }
        }
        if (k > 0) {
            int wx = S.px + DX[S.dir] * k, wy = S.py + DY[S.dir] * k;
            if (!solid(wx, wy) && S.feat[wy][wx]) draw_feature(S.feat[wy][wx], (float)k);
        }
    }
    vignette();
}

/* ================================================================ */
/*  automap (Diablo style line map overlaid on the view)            */
/* ================================================================ */
static void render_automap(void)
{
    int cs = 6, ox, oy, x, y;
    u16 wc = rgb(190, 160, 100);
    darken(0, VY, GW, VH, 80);
    ox = VCX - S.px * cs - cs / 2;
    oy = VCY - S.py * cs - cs / 2;
    clipY0 = VY; clipY1 = VY + VH - 1;
    for (y = 0; y < MH; y++) for (x = 0; x < MW; x++) {
        int sx = ox + x * cs, sy = oy + y * cs;
        if (!S.seen[y][x]) continue;
        if (S.map[y][x]) {
            if (!solid(x, y - 1) && S.seen[y - 1][x]) line(sx, sy, sx + cs, sy, wc);
            if (y + 1 < MH && !solid(x, y + 1) && S.seen[y + 1][x]) line(sx, sy + cs, sx + cs, sy + cs, wc);
            if (!solid(x - 1, y) && S.seen[y][x - 1]) line(sx, sy, sx, sy + cs, wc);
            if (x + 1 < MW && !solid(x + 1, y) && S.seen[y][x + 1]) line(sx + cs, sy, sx + cs, sy + cs, wc);
        } else {
            u16 fc = 0;
            switch (S.feat[y][x]) {
            case F_STAIRS: fc = rgb(240, 240, 120); break;
            case F_CHEST: fc = rgb(230, 170, 40); break;
            case F_SHRINE: fc = rgb(100, 220, 255); break;
            case F_FOUNTAIN: fc = rgb(80, 120, 255); break;
            case F_MERCHANT: fc = rgb(255, 255, 255); break;
            case F_HEALER: fc = rgb(120, 255, 120); break;
            case F_ELDER: case F_GEN: case F_ARM: case F_INN: fc = rgb(255, 255, 255); break;
            case F_WAYPOINT: case F_PORTAL: fc = rgb(100, 160, 255); break;
            }
            if (fc) { int i, j; for (j = 2; j < cs - 1; j++) for (i = 2; i < cs - 1; i++) pset(sx + i, sy + j, fc); }
        }
    }
    {
        int cx = ox + S.px * cs + cs / 2, cy = oy + S.py * cs + cs / 2, i;
        u16 pc = rgb(255, 80, 60);
        for (i = 0; i <= 3; i++) {
            int a = DX[S.dir], b = DY[S.dir], j;
            for (j = -(3 - i); j <= 3 - i; j++) pset(cx + a * (i - 1) + (-b) * j, cy + b * (i - 1) + a * j, pc);
        }
    }
    clipY0 = 0; clipY1 = 9999;
    txt(GW - 4, VY + 2, L"지도", C_GOLD, T_RIGHT | T_SHADOW);
}

/* ================================================================ */
/*  item icons                                                      */
/* ================================================================ */
static void potion_icon(int x, int y, int sz, int kind)
{
    u16 c = kind == IK_HP ? rgb(220, 30, 30) : kind == IK_MP ? rgb(40, 70, 230) : rgb(170, 50, 200);
    int r = sz * 3 / 10, cx = x + sz / 2, cy = y + sz - r - sz / 10;
    fill(cx - sz / 10, y + sz / 8, sz / 5 + 1, sz / 3, rgb(170, 170, 180));
    fill(cx - sz / 8, y + sz / 10, sz / 4 + 1, 2, rgb(120, 90, 60));
    fcircle(cx, cy, r + 1, rgb(170, 170, 180));
    fcircle(cx, cy, r, c);
    pset(cx - r / 2, cy - r / 2, rgb(255, 255, 255));
    pset(cx - r / 2 + 1, cy - r / 2, rgb(255, 230, 230));
}

static void item_icon(const Item *it, int x, int y, int sz)
{
    u16 m = rgb(180, 180, 190), d = rgb(90, 90, 100), cx = (u16)(x + sz / 2);
    int i;
    if (!it->kind) return;
    if (it->rar == R_MAGIC) tint(x, y, sz, sz, rgb(30, 30, 120), 50);
    else if (it->rar == R_RARE) tint(x, y, sz, sz, rgb(110, 110, 20), 45);
    else if (it->rar == R_UNIQ) tint(x, y, sz, sz, rgb(120, 90, 30), 60);
    switch (it->kind) {
    case IK_W:
        for (i = 0; i < sz * 6 / 10; i++) {
            pset(x + sz / 5 + i, y + sz * 4 / 5 - i, m);
            pset(x + sz / 5 + i + 1, y + sz * 4 / 5 - i, d);
        }
        line(x + sz / 6, y + sz * 6 / 10, x + sz * 4 / 10, y + sz * 9 / 10, rgb(200, 170, 80));
        fill(x + sz / 10, y + sz * 8 / 10, 3, 3, rgb(120, 80, 40));
        break;
    case IK_A:
        fill(x + sz / 4, y + sz / 5, sz / 2, sz * 6 / 10, m);
        fill(x + sz / 8, y + sz / 5, sz / 8 + 1, sz / 4, d);
        fill(x + sz * 3 / 4, y + sz / 5, sz / 8 + 1, sz / 4, d);
        fill(cx - 1, y + sz / 5, 2, sz * 6 / 10, d);
        hline(x + sz / 4, x + sz * 3 / 4, y + sz / 2, d);
        break;
    case IK_H:
        fcircle(cx, y + sz / 2, sz / 3, m);
        fill(x + sz / 6, y + sz / 2, sz * 2 / 3 + 1, sz / 3, m);
        fill(x + sz / 3, y + sz / 2, sz / 3 + 1, sz / 8 + 1, rgb(20, 20, 20));
        vline(cx, y + sz * 6 / 10, y + sz * 8 / 10, d);
        break;
    case IK_R:
        for (i = 0; i < 360; i += 10) {
            static const signed char CS[36] = { 10, 10, 9, 9, 8, 6, 5, 3, 2, 0, -2, -3, -5, -6, -8, -9, -9, -10, -10, -10, -9, -9, -8, -6, -5, -3, -2, 0, 2, 3, 5, 6, 8, 9, 9, 10 };
            int a = i / 10, ca = CS[a], sa = CS[(a + 27) % 36];
            pset(cx + ca * sz / 40, y + sz * 6 / 10 + sa * sz / 40, rgb(220, 190, 80));
            pset(cx + ca * sz / 44, y + sz * 6 / 10 + sa * sz / 44, rgb(160, 130, 50));
        }
        fcircle(cx, y + sz * 6 / 10 - sz / 4, sz / 9 + 1, rgb(255, 60, 60));
        break;
    case IK_TP:
        fill(x + sz / 5, y + sz / 4, sz * 3 / 5, sz / 2, rgb(220, 200, 150));
        fill(x + sz / 6, y + sz / 4 - 1, sz * 2 / 3 + 1, 3, rgb(170, 140, 90));
        fill(x + sz / 6, y + sz * 3 / 4 - 2, sz * 2 / 3 + 1, 3, rgb(170, 140, 90));
        fcircle(x + sz / 2, y + sz * 2 / 3, sz / 8 + 1, rgb(190, 30, 20));
        break;
    default:
        potion_icon(x, y, sz, it->kind);
    }
}

/* ================================================================ */
/*  monsters                                                        */
/* ================================================================ */
static const char *SPR_SKEL[] = {
    "........cccccc..........", ".......caaaaaab.........", "......caaakaaaab........", "......aakkkaakkkb.......",
    "......akkekaakekb.......", "......aakkkaakkkb.......", ".......aaaakkaab........", "........awkwkwab........",
    "........kwkwkwk.........", ".........abbab..........", "....aa..aaaaaaaa..aa....", "...a..a.akaakaak.a..a...",
    "...a...aakaakaakaa..a...", "..a....akaakaaka....a...", "..a.....kaakaaka.....a..", "..a.....aakaaka......a..",
    ".rr......aaaa........a..", ".rr......akka.......aaa.", ".rrd....aaaaaa......a.a.", ".rrf...aa....aa.........",
    ".rd...aa......aa........", ".r....a........a........", ".f...aa........aa.......", ".d...a..........a.......",
    "....aa..........aa......", "....bb..........bb......", 0 };
static const char *SPR_FALLEN[] = {
    "..k..................k..", "..kb................bk..", "...ab..aaaaaaaa....ba...", "....aaaaaaaaaaaaaaaa....",
    "....aakeeaaaaaaeekaa....", "....aaakkaaaaaakkaaa....", "....aawkwkwkwkwkwaa.....", "....aakfkfkfkfkfkaa.....",
    ".....awkwkwkwkwkwa......", "......aaaaaaaaaa........", "..r.....aabbaa..........", ".rrr..aaaaaaaaaaa.......",
    ".rrr.aa.abbaabba.aa.....", "..r.aa..aaaaaaaa..aa....", "..ra...aabbaabba...a....", "..r....aaaaaaaa....ad...",
    "..r....abaaaaba.....f...", "..r.....aa..aa..........", "..r....aa....aa.........", ".......a......a.........",
    "......kk......kk........", 0 };
static const char *SPR_BAT[] = {
    "a............................a", "ab..........................ba", "aab........................baa",
    "aaab.......a....a........baaa", "aaaab......aa..aa.......baaaa", "aaa.ab.....aaaaaa......ba.aaa",
    "aa..aab...aaaaaaaa....baa..aa", "aa...aab.aakeaaekaa..baa...aa", "a.....aabaaaaaaaaaabaa.....a",
    "a......aaaawkkkkwaaaa......a", "a.......aaawfffwaaa........a", "........baaawffwaaab.........",
    ".........baa.wf.aab..........", "..........ba..f..ab..........", "..............d...............",
    "..............f...............", 0 };
static const char *SPR_SPIDER[] = {
    "..a......................a..", "...a....................a...", "....a.....aaaaaaaa.....a....",
    ".a...a..aaaaaaaaaaaa..a...a.", "..a...aaaaacccccaaaaaa...a..", "...a.aaaaacwkcckwcaaaaa.a...",
    "aa..aaaaaacwwcccwwcaaaaa..aa", "..aaaaaaaaaccwccwcaaaaaaaa..", "....aaaaaaaacccccaaaaaaa....",
    ".aaa...aaaaaaaaaaaaaaa..aaa.", "a...aaa.abeeaeeaeeba.aaa...a", "...a...aaaaeaaaaeaaaa...a...",
    "..a...a.aawfaaaafwaa.a...a..", ".a...a....kwf..fwk....a...a.", "a...a......f....f......a...a",
    "...a.......d....d.......a...", "..a.....................a...", 0 };
static const char *SPR_ZOMBIE[] = {
    "......bbbbbb..........", ".....baaaaaab.........", ".....akkaaakkb........", ".....akeaaakeb........",
    ".....aaaaaaaab........", ".....aakkkkab.........", "......awkwkwb.........", ".......kfkfk..........",
    "..aaarrrrrrrrraa......", ".aa.rrgwgwgwgrr.aaa...", "aa..rrwkwkwkwrr...aa..", "a...rrgwgwgwgrr....a..",
    "a...rrrgfgfgrrr....aa.", "f....rrgggggrr......a.", "d....rrrfgrrrr......k.", ".....bbbfgbbbb........",
    "......aaf.aaa.........", "......aad.aa..........", ".....aa...aa..........", ".....a.....a..........",
    "....aa.....aa.........", "....bb.....bb.........", 0 };
static const char *SPR_GHOST[] = {
    ".......cccccccc.......", ".....ccaaaaaaaacc.....", "....caaaaaaaaaaaac....", "...caakkkaaaakkkaac...",
    "...caakkeaaaaekkaac...", "...caakkkaaaakkkaac...", "...caaaaaakkaaaaaac...", "...caaaaakkkkaaaaac...",
    "...caaaaakkkkaaaaac...", "..caaaaaakkkkaaaaaac..", "..caaaaaaakkaaaaaaac..", ".caaaaaaaaaaaaaaaaaab.",
    "caaa.aaaaaaaaaaaa.aaab", "aab..baaaaaaaaaab..baa", "aa....baaaaaaaab....ba", "a......aaa.aaab......b",
    ".......ba..b.ab.......", "........a...a.b.......", ".......b....a..b......", 0 };
static const char *SPR_DEMON[] = {
    ".cc......................cc.", "..ca....................ac..", "...ca..................ac...",
    "....ca....bbbbbbbb....ac....", "b....aaaaaaaaaaaaaaaaaa....b", "ab...aakeeaaaaaaaaeekaa...ba",
    "aab..aaakkaaaaaaaakkaaa..baa", "aaab..aawkwkwkwkwkwkaa..baaa", "aaaab.aakffffffffffkaa.baaaa",
    "aaaaabaawkwkwkwkwkwaabaaaaaa", "aaaa.aaaaaaaaaaaaaaaaaa.aaaa", "aaa..aarrrrrrrrrrrrrraa..aaa",
    "aa..aaarrbrrrbbrrrbrraaa..aa", "a..aa.arrrrrrrrrrrrrra.aa..a", "...a..aarrbrrbbrrbrraa..a...",
    "..ww..aaarrrrrrrrrraaa..ww..", "..w....aaarrrbbrrraaa....w..", "........aaaa....aaaa........",
    ".......aaaa......aaaa.......", "......aaa..........aaa......", ".....aaa............aaa.....",
    "....kkk..............kkk....", 0 };
static const char *SPR_GOLEM[] = {
    ".......bbbbbbbbbb.......", "......baaaaaaaaaab......", "......aaeaaaaaaeaa......", "......aaaafaafaaaa......",
    "......aakkkkkkkkaa......", "...bbbbaawkwkwkaabbbb...", "..baaaabbbbbbbbbbaaaab..", ".aaaaaaaaaafaaaaaaaaaaa.",
    ".aaaa.aaaaaffaaaaa.aaaa.", ".aaaa.aaacaaafaaaa.aaaa.", ".aaaa.aafaaaaaaaca.aaaa.", ".aaaa.aaafaaaacaaa.aaaa.",
    "bbbbb.aaaafaaaaaaa.bbbbb", "aaaaa.bbbbbbbbbbbb.aaaaa", "akaka..aaaaa.aaaaa.akaka", ".......aaaaa.aaaaa......",
    ".......aafaa.aaaaa......", "......baaaaa.aaaaab.....", "......bbbbbb.bbbbbb.....", 0 };

static const char *SPR_BEAST[] = {
    "...b..................b...", "...ab................ba...", "...aab..............baa...", "....aaabbbbbbbbbbbbaaa....",
    "....aaaaaaaaaaaaaaaaaa....", "...aaakeaaaaaaaaaaekaaa...", "...aaaakaaaaaaaaaakaaaa...", "..baaaaaaaaccccaaaaaaaab..",
    "..aaaaaaacckkkkccaaaaaaa..", ".baaaaaawkwkwkwkwkwaaaaab.", ".aaaaaaakfkfkfkfkfkaaaaaa.", ".aaaaaaaawkwkwkwkwaaaaaaa.",
    "aaaaaaaaaaafffaaaaaaaaaaaa", "aaabaaaaaaaadaaaaaaaabaaaa", "aab.aaaaaaaaaaaaaaaaa.baa.", "ab..aaaa.aaaaaaa.aaaa..ba.",
    "....aaa...aaaaa...aaa.....", "....aaa....aaa....aaa.....", "....aaa...........aaa.....", "...aaaa..........aaaa.....",
    "...kk.k..........k.kk.....", 0 };
static const char *SPR_MUMMY[] = {
    ".......bbbbbbb........", "......bcacacacb.......", ".....bacacacacab......", ".....akkacacakkb......",
    ".....akeacacaekb......", ".....acacacacacb......", "......akkkkkkab.......", "......acacacacb.......",
    "..cacacacacacacacac...", ".ca..acacacacaca..ca..", "ca...cacacacacac...ca.", "a....acacdcacaca....a.",
    "c....cacacacacac....c.", "a.....acacacaca.....a.", "r.....cacacacac.....b.", "......acaca.caca......",
    "......cacac.acac......", "......acaca.caca......", ".....rcacac.acacr.....", ".....r.cac...cac.r....",
    "......bbbb...bbbb.....", 0 };
static const char *SPR_WORM[] = {
    ".......bbbbbbbbbb.......", ".....bbaaaaaaaaaabb.....", "....baaawwwwwwwwaaab....", "...baawwkkkkkkkkwwaab...",
    "...bawkkwwwwwwwwkkwab...", "...bawkwkfkfkfkfwkwab...", "...bawkwfkkkkkkfwkwab...", "...bawkwkfkfkfkfwkwab...",
    "...bawkkwwwwwwwwkkwab...", "...baawwkkkkkkkkwwaab...", "....baaawwwwwwwwaaab....", ".....bacaaaaaaaacab.....",
    ".....baaaaaaaaaaaab.....", "......bacaaaaaacab......", "......baaaaaaaaaab......", ".......bacaaaacab.......",
    ".......baaaaaaaab.......", "......bbaacaaaaabb......", "....bbbaaaaaaaaaabbb....", "..bbbbbbbbbbbbbbbbbbbb..", 0 };
static const char *SPR_HORROR[] = {
    ".........bbbbbbb..........", "......bbbaaaaaaabbb.......", "....bbaaagaaakeaaaabb.....", "...baakeaaaggaaaaagaab....",
    "..baaaaaaagggaakeaaaaab...", ".baagaakeaaaaaaaaaaagaab..", ".baaaaaaaaaawkwkwaaaaaab..", ".bagaaaaaaawkfffkwaaakeab.",
    "baaakeaaaawfkkkkfwaaaaaab.", "baaaaaaaaawkfffkwaagaaaab.", "baagaaaaaaawkwkwaaaaaaaab.", ".baaaaagaaaaaaaaakeaaagab.",
    ".baakeaaaaaagaaaaaaaaaab..", "..baaaaaaagaaaaagaaaaab...", "...bbaaaaaaaaaaaaaaabb....", "..ba.bbaaaaaaaaaaabb.ab...",
    ".ba..a.bbbaaaaabbb.a..ab..", "ba..a..a..bbbbb..a..a..ab.", "a..a..a...a...a...a..a..a.", "..d..a...a.....a...a..d...",
    "......d..........d........", 0 };

enum { SP_SKEL, SP_FALLEN, SP_BAT, SP_SPIDER, SP_ZOMBIE, SP_GHOST, SP_DEMON, SP_GOLEM, SP_BEAST, SP_MUMMY, SP_WORM, SP_HORROR };
static const char **SPRITES[] = { SPR_SKEL, SPR_FALLEN, SPR_BAT, SPR_SPIDER, SPR_ZOMBIE, SPR_GHOST, SPR_DEMON, SPR_GOLEM, SPR_BEAST, SPR_MUMMY, SPR_WORM, SPR_HORROR };
enum { SPC_NONE, SPC_FIRE, SPC_DRAIN, SPC_BOSS };

typedef struct {
    const wchar_t *n; int minf; float hpk, atkk, defk; int agi, spr, spec;
    unsigned char col[4][3];
    int art;
} MonDef;
static const MonDef MD[] = {
    { L"썩어가는 시체", 0, 1.6f, 1.3f, 0.5f, 4, SP_ZOMBIE, 0, { { 120, 150, 100 }, { 60, 75, 50 }, { 180, 210, 160 }, { 90, 70, 60 } }, 0 },
    { L"굶주린 구울", 0, 1.3f, 1.5f, 0.6f, 12, SP_ZOMBIE, 0, { { 150, 148, 138 }, { 75, 74, 69 }, { 210, 208, 198 }, { 90, 70, 60 } }, 1 },
    { L"머리 없는 시체", 0, 1.7f, 1.6f, 0.6f, 5, SP_ZOMBIE, 0, { { 130, 118, 98 }, { 65, 59, 49 }, { 190, 178, 158 }, { 90, 70, 60 } }, 2 },
    { L"부풀어 오른 시체", 0, 2.4f, 1.2f, 0.4f, 3, SP_ZOMBIE, 0, { { 120, 132, 88 }, { 60, 66, 44 }, { 180, 192, 148 }, { 90, 70, 60 } }, 3 },
    { L"불타는 망자", 0, 1.6f, 1.6f, 0.8f, 6, SP_ZOMBIE, SPC_FIRE, { { 60, 40, 30 }, { 30, 20, 15 }, { 120, 100, 90 }, { 90, 70, 60 } }, 4 },
    { L"해골 전사", 0, 1.0f, 1.0f, 1.0f, 6, SP_SKEL, 0, { { 220, 214, 190 }, { 110, 107, 95 }, { 255, 255, 250 }, { 90, 70, 60 } }, 5 },
    { L"해골 창병", 0, 1.0f, 1.2f, 1.0f, 8, SP_SKEL, 0, { { 220, 214, 190 }, { 110, 107, 95 }, { 255, 255, 250 }, { 90, 70, 60 } }, 6 },
    { L"해골 기사", 0, 1.7f, 1.6f, 2.0f, 6, SP_SKEL, 0, { { 140, 130, 120 }, { 70, 65, 60 }, { 200, 190, 180 }, { 90, 70, 60 } }, 7 },
    { L"고대 해골", 0, 2.0f, 1.9f, 2.2f, 8, SP_SKEL, 0, { { 176, 198, 214 }, { 88, 99, 107 }, { 236, 255, 255 }, { 90, 70, 60 } }, 8 },
    { L"신전 수호 해골", 0, 1.6f, 1.6f, 2.0f, 8, SP_SKEL, 0, { { 120, 170, 130 }, { 60, 85, 65 }, { 180, 230, 190 }, { 90, 70, 60 } }, 9 },
    { L"타락자", 0, 0.7f, 0.8f, 0.3f, 10, SP_FALLEN, 0, { { 150, 42, 32 }, { 75, 21, 16 }, { 210, 102, 92 }, { 90, 70, 60 } }, 10 },
    { L"타락한 주술사", 0, 0.8f, 0.9f, 0.5f, 9, SP_FALLEN, SPC_FIRE, { { 110, 40, 110 }, { 55, 20, 55 }, { 170, 100, 170 }, { 90, 70, 60 } }, 11 },
    { L"늪지 주술사", 0, 1.0f, 1.1f, 0.6f, 10, SP_FALLEN, SPC_FIRE, { { 64, 110, 70 }, { 32, 55, 35 }, { 124, 170, 130 }, { 90, 70, 60 } }, 12 },
    { L"피의 광신도", 0, 1.1f, 1.3f, 0.6f, 10, SP_ZOMBIE, 0, { { 80, 20, 24 }, { 40, 10, 12 }, { 140, 80, 84 }, { 90, 70, 60 } }, 13 },
    { L"뱀의 사제", 0, 1.3f, 1.5f, 0.8f, 11, SP_ZOMBIE, 0, { { 40, 70, 50 }, { 20, 35, 25 }, { 100, 130, 110 }, { 90, 70, 60 } }, 14 },
    { L"피범벅 백정", 0, 2.8f, 2.2f, 1.0f, 4, SP_ZOMBIE, 0, { { 176, 132, 116 }, { 88, 66, 58 }, { 236, 192, 176 }, { 90, 70, 60 } }, 15 },
    { L"미라", 0, 1.8f, 1.4f, 1.2f, 5, SP_ZOMBIE, SPC_DRAIN, { { 170, 150, 110 }, { 85, 75, 55 }, { 230, 210, 170 }, { 90, 70, 60 } }, 16 },
    { L"저주받은 미라 군주", 0, 2.2f, 1.6f, 1.6f, 7, SP_ZOMBIE, SPC_DRAIN, { { 120, 100, 60 }, { 60, 50, 30 }, { 180, 160, 120 }, { 90, 70, 60 } }, 17 },
    { L"그림자 악마", 0, 1.8f, 1.9f, 1.3f, 12, SP_DEMON, 0, { { 52, 42, 62 }, { 26, 21, 31 }, { 112, 102, 122 }, { 90, 70, 60 } }, 18 },
    { L"불꽃 악마", 0, 2.0f, 2.0f, 1.4f, 10, SP_DEMON, SPC_FIRE, { { 140, 40, 20 }, { 70, 20, 10 }, { 200, 100, 80 }, { 90, 70, 60 } }, 19 },
    { L"지옥 기사", 0, 2.4f, 2.3f, 2.4f, 8, SP_DEMON, 0, { { 70, 40, 40 }, { 35, 20, 20 }, { 130, 100, 100 }, { 90, 70, 60 } }, 20 },
    { L"가고일", 0, 1.7f, 1.6f, 2.2f, 11, SP_DEMON, 0, { { 96, 94, 100 }, { 48, 47, 50 }, { 156, 154, 160 }, { 90, 70, 60 } }, 21 },
    { L"설원의 야수", 0, 2.6f, 2.1f, 1.2f, 8, SP_GOLEM, 0, { { 205, 205, 212 }, { 102, 102, 106 }, { 255, 255, 255 }, { 90, 70, 60 } }, 22 },
    { L"굶주린 들개", 0, 0.9f, 1.1f, 0.4f, 13, SP_BEAST, 0, { { 104, 86, 66 }, { 52, 43, 33 }, { 164, 146, 126 }, { 90, 70, 60 } }, 23 },
    { L"피에 젖은 늑대", 0, 1.2f, 1.4f, 0.6f, 15, SP_BEAST, 0, { { 110, 52, 40 }, { 55, 26, 20 }, { 170, 112, 100 }, { 90, 70, 60 } }, 24 },
    { L"송장 하이에나", 0, 1.0f, 1.3f, 0.5f, 14, SP_BEAST, 0, { { 150, 120, 72 }, { 75, 60, 36 }, { 210, 180, 132 }, { 90, 70, 60 } }, 25 },
    { L"지옥 사냥개", 0, 1.5f, 1.8f, 1.0f, 16, SP_BEAST, SPC_FIRE, { { 140, 40, 20 }, { 70, 20, 10 }, { 200, 100, 80 }, { 90, 70, 60 } }, 26 },
    { L"서리 늑대", 0, 1.6f, 1.8f, 1.0f, 16, SP_BEAST, 0, { { 210, 216, 226 }, { 105, 108, 113 }, { 255, 255, 255 }, { 90, 70, 60 } }, 27 },
    { L"시체 쥐", 0, 0.6f, 0.8f, 0.3f, 12, SP_BEAST, 0, { { 92, 82, 72 }, { 46, 41, 36 }, { 152, 142, 132 }, { 90, 70, 60 } }, 28 },
    { L"가시 멧돼지", 0, 1.6f, 1.5f, 1.4f, 9, SP_BEAST, 0, { { 80, 62, 52 }, { 40, 31, 26 }, { 140, 122, 112 }, { 90, 70, 60 } }, 29 },
    { L"시체 거미", 0, 1.0f, 1.3f, 0.9f, 12, SP_SPIDER, 0, { { 72, 52, 82 }, { 36, 26, 41 }, { 132, 112, 142 }, { 90, 70, 60 } }, 30 },
    { L"맹독 거미", 0, 1.2f, 1.5f, 1.0f, 13, SP_SPIDER, 0, { { 60, 116, 52 }, { 30, 58, 26 }, { 120, 176, 112 }, { 90, 70, 60 } }, 31 },
    { L"서리 거미", 0, 1.3f, 1.5f, 1.1f, 13, SP_SPIDER, 0, { { 170, 200, 222 }, { 85, 100, 111 }, { 230, 255, 255 }, { 90, 70, 60 } }, 32 },
    { L"독침 풍뎅이", 0, 1.2f, 1.3f, 2.0f, 9, SP_SPIDER, 0, { { 40, 84, 96 }, { 20, 42, 48 }, { 100, 144, 156 }, { 90, 70, 60 } }, 33 },
    { L"거대 전갈", 0, 1.5f, 1.7f, 1.8f, 10, SP_SPIDER, 0, { { 124, 92, 52 }, { 62, 46, 26 }, { 184, 152, 112 }, { 90, 70, 60 } }, 34 },
    { L"피에 굶주린 박쥐", 0, 0.6f, 0.8f, 0.2f, 16, SP_BAT, 0, { { 100, 78, 62 }, { 50, 39, 31 }, { 160, 138, 122 }, { 90, 70, 60 } }, 35 },
    { L"정글 흡혈박쥐", 0, 0.8f, 1.0f, 0.3f, 16, SP_BAT, 0, { { 62, 82, 52 }, { 31, 41, 26 }, { 122, 142, 112 }, { 90, 70, 60 } }, 36 },
    { L"지옥 박쥐", 0, 1.0f, 1.3f, 0.5f, 17, SP_BAT, SPC_FIRE, { { 170, 40, 30 }, { 85, 20, 15 }, { 230, 100, 90 }, { 90, 70, 60 } }, 37 },
    { L"모래 벌레", 0, 2.2f, 1.8f, 1.0f, 7, SP_WORM, 0, { { 170, 132, 92 }, { 85, 66, 46 }, { 230, 192, 152 }, { 90, 70, 60 } }, 38 },
    { L"피 거머리", 0, 1.6f, 1.4f, 0.6f, 8, SP_WORM, SPC_DRAIN, { { 110, 40, 50 }, { 55, 20, 25 }, { 170, 100, 110 }, { 90, 70, 60 } }, 39 },
    { L"늪 독사", 0, 1.4f, 1.6f, 0.8f, 14, SP_WORM, 0, { { 62, 92, 42 }, { 31, 46, 21 }, { 122, 152, 102 }, { 90, 70, 60 } }, 40 },
    { L"비명 지르는 원혼", 0, 1.2f, 1.2f, 0.8f, 12, SP_GHOST, SPC_DRAIN, { { 150, 190, 220 }, { 75, 95, 110 }, { 210, 250, 255 }, { 90, 70, 60 } }, 41 },
    { L"고문당한 영혼", 0, 1.5f, 1.6f, 1.0f, 13, SP_GHOST, SPC_DRAIN, { { 214, 112, 100 }, { 107, 56, 50 }, { 255, 172, 160 }, { 90, 70, 60 } }, 42 },
    { L"얼음 망령", 0, 1.6f, 1.6f, 1.1f, 14, SP_GHOST, SPC_DRAIN, { { 140, 212, 240 }, { 70, 106, 120 }, { 200, 255, 255 }, { 90, 70, 60 } }, 43 },
    { L"망령 군주", 0, 2.0f, 2.0f, 1.4f, 13, SP_GHOST, SPC_DRAIN, { { 126, 84, 176 }, { 63, 42, 88 }, { 186, 144, 236 }, { 90, 70, 60 } }, 44 },
    { L"늪의 괴물", 0, 2.4f, 1.6f, 1.0f, 5, SP_HORROR, 0, { { 80, 102, 60 }, { 40, 51, 30 }, { 140, 162, 120 }, { 90, 70, 60 } }, 45 },
    { L"하수도 살덩이", 0, 2.8f, 1.8f, 1.0f, 4, SP_HORROR, 0, { { 160, 92, 86 }, { 80, 46, 43 }, { 220, 152, 146 }, { 90, 70, 60 } }, 46 },
    { L"공포의 군주", 0, 2.6f, 2.2f, 1.4f, 8, SP_HORROR, SPC_FIRE, { { 150, 30, 20 }, { 75, 15, 10 }, { 210, 90, 80 }, { 90, 70, 60 } }, 47 },
    { L"피의 골렘", 0, 3.0f, 2.0f, 1.6f, 5, SP_GOLEM, 0, { { 140, 30, 20 }, { 70, 15, 10 }, { 200, 90, 80 }, { 90, 70, 60 } }, 48 },
    { L"석상 골렘", 0, 3.2f, 2.0f, 3.0f, 4, SP_GOLEM, 0, { { 112, 108, 102 }, { 56, 54, 51 }, { 172, 168, 162 }, { 90, 70, 60 } }, 49 },
    { L"얼음 골렘", 0, 3.2f, 2.1f, 2.6f, 5, SP_GOLEM, 0, { { 150, 200, 235 }, { 75, 100, 117 }, { 210, 255, 255 }, { 90, 70, 60 } }, 50 },
};
#define NMD ((int)(sizeof(MD) / sizeof(MD[0])))
static const MonDef BOSS[] = {
    { L"피의 군주 바르가스", 0, 18.0f, 3.0f, 3.0f, 10, SP_DEMON, SPC_BOSS, { { 140, 26, 20 }, { 70, 13, 10 }, { 200, 86, 80 }, { 90, 70, 60 } }, 51 },
    { L"모래의 여왕 세케트", 0, 22.0f, 3.2f, 3.5f, 14, SP_SPIDER, SPC_BOSS, { { 196, 156, 72 }, { 98, 78, 36 }, { 255, 216, 132 }, { 90, 70, 60 } }, 52 },
    { L"뼈의 왕 모르데크", 0, 26.0f, 3.4f, 4.0f, 10, SP_SKEL, SPC_BOSS, { { 230, 200, 110 }, { 115, 100, 55 }, { 255, 255, 170 }, { 90, 70, 60 } }, 53 },
    { L"공포의 화신", 0, 30.0f, 3.6f, 4.0f, 12, SP_HORROR, SPC_BOSS, { { 150, 30, 30 }, { 75, 15, 15 }, { 210, 90, 90 }, { 90, 70, 60 } }, 54 },
    { L"심연의 파수꾼", 0, 34.0f, 3.8f, 5.0f, 10, SP_GOLEM, SPC_BOSS, { { 72, 52, 104 }, { 36, 26, 52 }, { 132, 112, 164 }, { 90, 70, 60 } }, 55 },
};

typedef struct {
    wchar_t name[28];
    int hp, maxhp, atk, def, agi, exp, gold, spec, spr;
    int alive, flash, frozen, champ, boss, dying;
    int sx, sy, sw, sh, scale, slot, uniq, appear, anim, at;
    int art, fw, fh, fax, fay, zoom, tintk, nfr;
    u16 pal[4], tint;
    wchar_t mods[32];
} Mon;

enum { BP_INTRO, BP_CMD, BP_MAGIC, BP_POTION, BP_TARGET, BP_EXEC, BP_RESULT, BP_DEAD };
enum { ACT_ATTACK, ACT_SKILL, ACT_POTION, ACT_DEFEND, ACT_FLEE };
typedef struct { int v, x, y, t; u16 c; } Pop;
typedef struct {
    int role, art, hp, maxhp, mp, maxmp, atk, def;
    int anim, at, tgt, flash;
    int fw, fh, fax, fay, nf, hx, hy, zoom;
} PMember;
static struct {
    Mon m[3];
    int n, phase, cmd, msel, psel, tsel, act, arg, tgt;
    int order[8], no, oi, wait, defend, boss, fled;
    PMember pm[4];
    int np, pan, windup, fx, fxt, fxsrc, fxtgt;
    int aact[4], atgt[4];
    Line ln[3];
    Line res[12];
    int nres, resi;
    int shake, redflash, focus;
    Pop pop[3];
    struct { short x, y, vx, vy, t; } bl[32];
    int bgok;
} B;
static u16 bgbuf[GWMAX * 480];

static void sprite_size(int spr, int *w, int *h)
{
    const char **s = SPRITES[spr];
    int i, mw = 0;
    for (i = 0; s[i]; i++) { int l = (int)strlen(s[i]); if (l > mw) mw = l; }
    *w = mw; *h = i;
}

/* ---- pre-rendered look: pixel-art upscaling (Scale2x/3x) + auto volume shading ---- */
#define SPMAX 120
static char spch[2][SPMAX * SPMAX];
static u16 spc[3][SPMAX * SPMAX];
static unsigned char spa[3][SPMAX * SPMAX];

static char gc(const char *g, int w, int h, int x, int y) { return (x < 0 || y < 0 || x >= w || y >= h) ? '.' : g[y * w + x]; }

static void scale2x(const char *src, int w, int h, char *dst)
{
    int x, y;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        char B_ = gc(src, w, h, x, y - 1), D = gc(src, w, h, x - 1, y), E = gc(src, w, h, x, y), F = gc(src, w, h, x + 1, y), H = gc(src, w, h, x, y + 1);
        int o = (y * 2) * w * 2 + x * 2;
        dst[o] = (D == B_ && B_ != F && D != H) ? D : E;
        dst[o + 1] = (B_ == F && B_ != D && F != H) ? F : E;
        dst[o + w * 2] = (D == H && D != B_ && H != F) ? D : E;
        dst[o + w * 2 + 1] = (H == F && D != H && B_ != F) ? F : E;
    }
}
static void scale3x(const char *src, int w, int h, char *dst)
{
    int x, y, W = w * 3;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        char A = gc(src, w, h, x - 1, y - 1), B_ = gc(src, w, h, x, y - 1), C = gc(src, w, h, x + 1, y - 1);
        char D = gc(src, w, h, x - 1, y), E = gc(src, w, h, x, y), F = gc(src, w, h, x + 1, y);
        char G = gc(src, w, h, x - 1, y + 1), H = gc(src, w, h, x, y + 1), I = gc(src, w, h, x + 1, y + 1);
        char *o = dst + (y * 3) * W + x * 3;
        int db = D == B_ && D != H && B_ != F, bf = B_ == F && B_ != D && F != H, dh = D == H && D != B_ && H != F, hf = H == F && D != H && B_ != F;
        o[0] = db ? D : E;
        o[1] = ((db && E != C) || (bf && E != A)) ? B_ : E;
        o[2] = bf ? F : E;
        o[W] = ((db && E != G) || (dh && E != A)) ? D : E;
        o[W + 1] = E;
        o[W + 2] = ((bf && E != I) || (hf && E != C)) ? F : E;
        o[2 * W] = dh ? D : E;
        o[2 * W + 1] = ((dh && E != I) || (hf && E != G)) ? H : E;
        o[2 * W + 2] = hf ? F : E;
    }
}

static void build_sprite(Mon *m)
{
    const char **s = SPRITES[m->spr];
    int w, h, x, y, W, Hh, slot = m->slot;
    static unsigned char dist[SPMAX * SPMAX];
    char *g0 = spch[0], *g1 = spch[1], *g;
    sprite_size(m->spr, &w, &h);
    memset(g0, '.', w * h);
    for (y = 0; y < h; y++) for (x = 0; s[y][x]; x++) g0[y * w + x] = s[y][x];
    if (m->scale == 4) { scale2x(g0, w, h, g1); scale2x(g1, w * 2, h * 2, g0); g = g0; }
    else if (m->scale == 3) { scale3x(g0, w, h, g1); g = g1; }
    else { scale2x(g0, w, h, g1); g = g1; }
    W = w * m->scale; Hh = h * m->scale;
    /* distance to edge (chamfer) */
    for (y = 0; y < Hh; y++) for (x = 0; x < W; x++) dist[y * W + x] = g[y * W + x] == '.' ? 0 : 12;
    for (y = 0; y < Hh; y++) for (x = 0; x < W; x++) {
        unsigned char *d = &dist[y * W + x];
        if (!*d) continue;
        if (x == 0 || y == 0) *d = 1;
        else { int a = dist[y * W + x - 1] + 1, b = dist[(y - 1) * W + x] + 1; if (a < *d) *d = (unsigned char)a; if (b < *d) *d = (unsigned char)b; }
    }
    for (y = Hh - 1; y >= 0; y--) for (x = W - 1; x >= 0; x--) {
        unsigned char *d = &dist[y * W + x];
        if (!*d) continue;
        if (x == W - 1 || y == Hh - 1) *d = 1;
        else { int a = dist[y * W + x + 1] + 1, b = dist[(y + 1) * W + x] + 1; if (a < *d) *d = (unsigned char)a; if (b < *d) *d = (unsigned char)b; }
    }
    memset(spa[slot], 0, W * Hh);
    for (y = 0; y < Hh; y++) for (x = 0; x < W; x++) {
        char ch = g[y * W + x];
        int i = y * W + x, gx, gy, lit, dd;
        u16 c;
        if (ch == '.') continue;
        switch (ch) {
        case 'a': c = m->pal[0]; break;
        case 'b': c = m->pal[1]; break;
        case 'c': c = m->pal[2]; break;
        case 'r': c = m->pal[3]; break;
        case 'e': c = m->boss || m->uniq ? rgb(255, 230, 60) : rgb(255, 60, 20); spc[slot][i] = c; spa[slot][i] = 3; continue;
        case 'k': c = rgb(14, 8, 8); break;
        case 'd': c = rgb(96, 6, 6); break;
        case 'f': c = rgb(176, 14, 10); break;
        case 'g': c = rgb(150, 82, 76); break;
        default: c = rgb(214, 204, 176); break;
        }
        dd = dist[i];
        gx = (x + 1 < W ? dist[i + 1] : 0) - (x > 0 ? dist[i - 1] : 0);
        gy = (y + 1 < Hh ? dist[i + W] : 0) - (y > 0 ? dist[i - W] : 0);
        lit = 196 + (gx + gy) * 34 + (dd > 5 ? 30 : dd * 6) + (int)(hsh(x, y) & 15) - 8;
        if (dd == 1) lit -= 40;
        /* grimy desaturation (blood keeps its colour) */
        if (ch != 'd' && ch != 'f') {
            int r = CR(c), gg = CG(c), b = CB(c), l = (r * 3 + gg * 5 + b * 2) / 10;
            c = rgb((r * 3 + l * 2) / 5, (gg * 3 + l * 2) / 5, (b * 3 + l * 2) / 5);
        }
        spc[slot][i] = shade(c, clampi(lit, 50, 300) > 256 ? 256 : clampi(lit, 50, 256));
        if (lit > 256) spc[slot][i] = mix(spc[slot][i], rgb(255, 245, 220), (lit - 256) / 2);
        spa[slot][i] = 2;
    }
    /* dark outline + eye glow halo */
    for (y = 0; y < Hh; y++) for (x = 0; x < W; x++) {
        int i = y * W + x, a, b;
        if (spa[slot][i] == 3) {
            for (b = -2; b <= 2; b++) for (a = -2; a <= 2; a++) {
                int xx = x + a, yy = y + b;
                if (xx >= 0 && yy >= 0 && xx < W && yy < Hh && spa[slot][yy * W + xx] < 2) { spa[slot][yy * W + xx] = 4; spc[slot][yy * W + xx] = spc[slot][i]; }
            }
            continue;
        }
        if (spa[slot][i]) continue;
        for (b = -1; b <= 1; b++) for (a = -1; a <= 1; a++) {
            int xx = x + a, yy = y + b;
            if (xx >= 0 && yy >= 0 && xx < W && yy < Hh && spa[slot][yy * W + xx] >= 2 && spa[slot][yy * W + xx] != 4) spa[slot][i] = 1;
        }
    }
}

/* ---- monster animation: 24 frames (idle 8, attack 8, hurt 4, death 4) ---- */
#define NFR 24
enum { MT_HUM, MT_WING, MT_DEMON, MT_LEGS, MT_FLOAT, MT_HEAVY, MT_BLOB, MT_BEAST, MT_WORM };
static u16 *FCOL[8];
static unsigned char *FALP[8];
static int FCAP[8];   /* 0-2 monsters, 3-6 party, 7 hero portrait */
static int BTOP, BBOT;
static int sclipT = 0, sclipB = 9999;
static void gs_layout(void) { BTOP = 0; BBOT = GH - 120; }
static int frame_alloc_n(int slot, int w, int h, int nf)
{
    int n = nf * w * h;
    if (n > FCAP[slot]) {
        free(FCOL[slot]); free(FALP[slot]);
        FCOL[slot] = (u16 *)malloc(n * 2);
        FALP[slot] = (unsigned char *)malloc(n);
        FCAP[slot] = (FCOL[slot] && FALP[slot]) ? n : 0;
        if (!FCAP[slot]) return 0;
    }
    memset(FALP[slot], 0, n);
    return 1;
}
static int frame_alloc(int slot, int w, int h) { return frame_alloc_n(slot, w, h, NFR); }

static float fsn(float t)   /* sin(2*pi*t) approximation */
{
    float x, y;
    t -= (float)(int)t;
    if (t < 0) t += 1.0f;
    x = t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f;
    y = 4.0f * x * (1.0f - x);
    y = y * (0.775f + 0.225f * y);
    return t < 0.5f ? y : -y;
}
static float fclamp(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float fab(float v) { return v < 0 ? -v : v; }

static int motion_of(int spr)
{
    switch (spr) {
    case SP_BAT: return MT_WING;
    case SP_DEMON: return MT_DEMON;
    case SP_SPIDER: return MT_LEGS;
    case SP_GHOST: return MT_FLOAT;
    case SP_GOLEM: return MT_HEAVY;
    case SP_HORROR: return MT_BLOB;
    case SP_BEAST: return MT_BEAST;
    case SP_WORM: return MT_WORM;
    }
    return MT_HUM;
}

static void gen_frames(Mon *m)
{
    int W = m->sw, H = m->sh, slot = m->slot, mt = motion_of(m->spr), fi, x, y;
    if (!frame_alloc(slot, W, H)) return;
    float A = (float)m->scale, cx = W * 0.5f;
    for (fi = 0; fi < NFR; fi++) {
        u16 *dc = FCOL[slot] + fi * W * H;
        unsigned char *da = FALP[slot] + fi * W * H;
        float p = (fi & 7) / 8.0f, q = (fi - 8) / 7.0f, hu = (fi - 16) / 3.0f, de = (fi - 19) / 4.0f;
        for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
            float u = (x - cx) / cx, v = (float)y / H, au = fab(u);
            float sx = (float)x, sy = (float)y, dx = 0, dy = 0, s;
            int ix, iy;
            if (fi < 8) {                                   /* idle */
                switch (mt) {
                case MT_HUM:
                    dx = A * 0.8f * fsn(p) * (1 - v);
                    if (v < 0.3f) dy = A * 0.6f * fsn(p + 0.25f);
                    if (au > 0.4f && v > 0.3f && v < 0.85f) dy += A * 0.9f * fsn(p + (u > 0 ? 0 : 0.5f));
                    break;
                case MT_WING:
                    if (au > 0.22f) dy = A * 3.0f * fsn(p) * (au - 0.22f) / 0.78f;
                    dy += A * 0.7f * fsn(p + 0.25f);
                    break;
                case MT_DEMON:
                    if (au > 0.55f) dy = A * 1.6f * fsn(p) * (au - 0.55f) / 0.45f;
                    dx = A * 0.5f * fsn(p) * (1 - v);
                    if (v < 0.4f) dy += A * 0.5f * fsn(p + 0.25f);
                    break;
                case MT_LEGS:
                    if (au > 0.35f || v > 0.6f) dy = A * 0.9f * fsn(2 * p + x * 0.07f);
                    dy += A * 0.4f * fsn(p);
                    break;
                case MT_FLOAT:
                    dx = A * 1.2f * fsn(p + v * 1.5f);
                    dy = A * 1.2f * fsn(p);
                    break;
                case MT_HEAVY:
                    s = 0.035f * fsn(p);
                    sy = H - (H - y) / (1 - s); sx = cx + (x - cx) / (1 + s);
                    break;
                case MT_BLOB:
                    s = 0.045f * fsn(p);
                    sy = H - (H - y) / (1 + s); sx = cx + (x - cx) / (1 + s);
                    if (v > 0.72f) dx = A * 1.6f * fsn(p * 2 + x * 0.12f) * (v - 0.72f) * 3.5f;
                    break;
                case MT_BEAST:
                    if (v < 0.55f) dy = A * 0.7f * fsn(p);
                    if (v > 0.75f && au > 0.3f) dx = A * 0.4f * fsn(p + 0.5f);
                    break;
                case MT_WORM: {
                    float k = (1 - v) * (1 - v);
                    dx = A * 2.2f * fsn(p) * k;
                    if (v < 0.55f) { s = 0.05f * fsn(p * 2); sx = cx + (x - cx) / (1 + s); sy = H * 0.28f + (y - H * 0.28f) / (1 + s); }
                    break;
                }
                }
            } else if (fi < 16) {                           /* attack */
                float ao = q < 0.5f ? -A * 3.0f * (q / 0.5f) : -A * 3.0f + A * 7.0f * ((q - 0.5f) / 0.5f);
                switch (mt) {
                case MT_HUM: case MT_DEMON:
                    if (au > 0.3f && v > 0.15f && v < 0.9f) dy = ao * fclamp((au - 0.3f) * 2.5f, 0, 1);
                    if (v < 0.35f) dy += A * 1.2f * fsn(q * 0.5f);
                    if (mt == MT_DEMON && au > 0.55f) dy += -A * 3.0f * fsn(q);
                    break;
                case MT_WING:
                    if (au > 0.22f) dy = -A * 4.0f * (1 - q) * (au - 0.22f) + A * 3.0f * q * (au - 0.22f);
                    break;
                case MT_LEGS:
                    if (v < 0.6f) dy = -A * 3.0f * fsn(q * 0.5f);
                    if (v > 0.5f && au > 0.3f) dy -= A * 3.5f * fsn(q * 0.5f);
                    break;
                case MT_FLOAT:
                    s = 0.25f * fsn(q * 0.5f);
                    sy = H * 0.5f + (y - H * 0.5f) / (1 + s);
                    dx = A * 2.0f * fsn(q * 2 + v);
                    break;
                case MT_HEAVY:
                    if (au > 0.55f) dy = q < 0.6f ? -A * 3.0f * q / 0.6f : -A * 3.0f + A * 7.0f * (q - 0.6f) / 0.4f;
                    break;
                case MT_BLOB:
                    s = 0.12f * fsn(q * 0.5f);
                    sy = H - (H - y) / (1 + s); sx = cx + (x - cx) / (1 + s);
                    if (v > 0.6f) dx = A * 3.0f * fsn(q + x * 0.1f);
                    break;
                case MT_BEAST:
                    if (v < 0.6f) dy = A * 2.5f * fsn(q) * (1 - v);
                    break;
                case MT_WORM:
                    if (v < 0.6f) { s = 0.16f * fsn(q * 0.5f); sx = cx + (x - cx) / (1 + s); sy = H * 0.28f + (y - H * 0.28f) / (1 + s); }
                    dy = -A * 2.0f * fsn(q * 0.5f) * (1 - v);
                    break;
                }
            } else if (fi < 20) {                           /* hurt: recoil */
                dx = -A * 2.5f * (1 - hu) * (1 - v);
                if (v < 0.3f) dy = -A * (1 - hu);
            } else {                                        /* death: collapse */
                float k = 0.24f * de;
                sy = H - (H - y) / (1 - k);
                dx = A * 3.0f * de * (1 - v);
                if (mt == MT_FLOAT) dx += A * 3.0f * de * fsn(v * 2 + de);
            }
            ix = (int)(sx - dx + 0.5f); iy = (int)(sy - dy + 0.5f);
            if (ix < 0 || iy < 0 || ix >= W || iy >= H) { da[y * W + x] = 0; continue; }
            { static const unsigned char AM[5] = { 0, 1, 255, 255, 140 }; da[y * W + x] = AM[spa[slot][iy * W + ix]]; }
            dc[y * W + x] = spc[slot][iy * W + ix];
        }
    }
}

static int mon_frame(const Mon *m)
{
    if (m->dying || !m->alive) return 20 + clampi((10 - m->dying) / 3, 0, 3);
    if (m->anim == 1) return 8 + clampi(m->at, 0, 7);
    if (m->anim == 2) return 16 + clampi(m->at / 2, 0, 3);
    return ((tickc >> 1) + m->slot * 3) & 7;
}

static void draw_sprite(const Mon *m)
{
    int x, y, white = m->flash > 0 && (m->flash & 2), fi = mon_frame(m);
    int W = m->art >= 0 && m->fw ? m->fw : m->sw, Hh = m->art >= 0 && m->fw ? m->fh : m->sh;
    const u16 *pc;
    const unsigned char *pa;
    int zoom = m->zoom ? m->zoom : 256, dw, dh, ox, oy, gx, gy, ax, ay;
    if (!FCAP[m->slot]) return;
    pc = FCOL[m->slot] + fi * W * Hh;
    pa = FALP[m->slot] + fi * W * Hh;
    if (m->anim == 1 && m->alive) zoom = zoom * (256 + (int)(60 * fsn(clampi(m->at, 0, 8) / 16.0f))) >> 8;
    if (zoom < 16) zoom = 16;
    ax = m->fw ? m->fax : W / 2; ay = m->fw ? m->fay : Hh;
    gx = m->sx + m->sw / 2; gy = m->sy + m->sh;          /* ground anchor on screen */
    if (m->fw) { gx = m->sx + m->fax * m->zoom / 256; gy = m->sy + m->fay * m->zoom / 256; }
    dw = W * zoom >> 8; dh = Hh * zoom >> 8;
    ox = gx - ax * zoom / 256; oy = gy - ay * zoom / 256;
    /* ground shadow */
    if (!m->dying) {
        int rw = (m->fw ? m->sw / 3 : dw / 2);
        for (y = -3; y <= 3; y++) for (x = -rw; x <= rw; x++) {
            int sx = gx + x, sy = gy + y - 1;
            if ((unsigned)sx < GW && sy >= sclipT && sy < sclipB && x * x * 9 / (rw * rw + 1) + y * y < 9)
                FB[sy * GW + sx] = shade(FB[sy * GW + sx], 110);
        }
    }
    for (y = 0; y < dh; y++) {
        int sy = oy + y, syy = y * 256 / zoom;
        const u16 *rc; const unsigned char *ra;
        if (sy < sclipT || sy >= sclipB || syy >= Hh) continue;
        rc = pc + syy * W; ra = pa + syy * W;
        for (x = 0; x < dw; x++) {
            int sx = ox + x, sxx = x * 256 / zoom, a;
            u16 *d, c;
            if ((unsigned)sx >= GW || sxx >= W) continue;
            a = ra[sxx];
            if (!a) continue;
            d = &FB[sy * GW + sx];
            c = rc[sxx];
            if (a == 1) { if (!m->dying && !m->appear) *d = shade(*d, 110); continue; }
            if (m->tintk) c = mix(c, m->tint, m->tintk);
            if (white) c = mix(c, rgb(255, 255, 255), 200);
            else if (m->frozen) c = mix(c, rgb(120, 200, 255), 120);
            if (m->appear) a = a * (14 - m->appear) / 14;
            else if (m->dying) a = a * clampi(m->dying * 26, 0, 256) / 256;
            if (m->spr == SP_GHOST && !m->fw) a = a * 160 / 256;
            *d = a >= 250 ? c : mix(*d, c, a);
        }
    }
}

/* ---- pre-rendered monster art from abyss_mon.dat ----
   v2 art is stored at 2x resolution; at load time every frame is area-filtered
   down (or up) to exactly the size it will be shown at, so it stays crisp.      */
static void *mdat;
static int mcount, mver;
static unsigned moff[80];
static unsigned char *mscratch;
static int mscap;
static void mdat_open(void)
{
    unsigned char h[8];
    mdat = plat_fopen(L"abyss_mon.dat");
    if (!mdat) return;
    if (plat_fread(mdat, h, 8) != 8 || memcmp(h, "AMON", 4)) { plat_fclose(mdat); mdat = 0; return; }
    mver = h[4] | (h[5] << 8);
    mcount = h[6] | (h[7] << 8);
    if (mcount > 80) mcount = 80;
    {
        unsigned char t[320];
        int i;
        plat_fread(mdat, t, mcount * 4);
        for (i = 0; i < mcount; i++) moff[i] = t[i * 4] | (t[i * 4 + 1] << 8) | (t[i * 4 + 2] << 16) | ((unsigned)t[i * 4 + 3] << 24);
    }
}

/* premultiplied source frame (decode / composite target) */
static u16 *SPR, *SPG, *SPB, *SZ;
static unsigned char *SA;
static int scap;
static int src_alloc(int n)
{
    if (n > scap) {
        free(SPR); free(SPG); free(SPB); free(SZ); free(SA);
        SPR = (u16 *)malloc(n * 2); SPG = (u16 *)malloc(n * 2); SPB = (u16 *)malloc(n * 2); SZ = (u16 *)malloc(n * 2);
        SA = (unsigned char *)malloc(n);
        scap = (SPR && SPG && SPB && SZ && SA) ? n : 0;
        if (!scap) return 0;
    }
    memset(SA, 0, n);
    return 1;
}
static int read_block(void *f, unsigned n)
{
    if ((int)n > mscap) { free(mscratch); mscratch = (unsigned char *)malloc(n); mscap = mscratch ? (int)n : 0; if (!mscratch) return 0; }
    return plat_fread(f, mscratch, (int)n) == (int)n;
}
/* RLE rows -> premultiplied source buffers. wide = u16 x, depth = per-pixel u16 depth (hero layers).
   With depth the layer is merged into what is already there (front/back by depth). */
static void decode_rle(const unsigned char *p, int W, int Hh, int wide, int depth)
{
    int y;
    for (y = 0; y < Hh; y++) {
        int ns = *p++, sg;
        for (sg = 0; sg < ns; sg++) {
            int x0, len, k;
            const unsigned char *pc, *pa, *pz;
            if (wide) { x0 = p[0] | p[1] << 8; len = p[2]; p += 3; } else { x0 = p[0]; len = p[1]; p += 2; }
            pc = p; pa = p + len * 2; pz = pa + len;
            p = pa + len + (depth ? len * 2 : 0);
            for (k = 0; k < len; k++) {
                int o = y * W + x0 + k, c = pc[k * 2] | pc[k * 2 + 1] << 8, a = pa[k];
                int r = (c >> 11) * a, g = ((c >> 5) & 63) * a, b = (c & 31) * a;
                if (!depth || !SA[o] || (a == 255 && pz[k * 2] + (pz[k * 2 + 1] << 8) <= SZ[o])) {
                    SPR[o] = (u16)r; SPG[o] = (u16)g; SPB[o] = (u16)b; SA[o] = (unsigned char)a;
                    if (depth) SZ[o] = (u16)(pz[k * 2] | pz[k * 2 + 1] << 8);
                } else {
                    int z = pz[k * 2] | pz[k * 2 + 1] << 8, A = SA[o];
                    if (z <= SZ[o]) {            /* new fragment in front */
                        int ia = 255 - a;
                        SPR[o] = (u16)(r + SPR[o] * ia / 255); SPG[o] = (u16)(g + SPG[o] * ia / 255); SPB[o] = (u16)(b + SPB[o] * ia / 255);
                        SA[o] = (unsigned char)(a + A * ia / 255); SZ[o] = (u16)z;
                    } else {                     /* behind: only shows through what is not covered yet */
                        int ia = 255 - A;
                        SPR[o] = (u16)(SPR[o] + r * ia / 255); SPG[o] = (u16)(SPG[o] + g * ia / 255); SPB[o] = (u16)(SPB[o] + b * ia / 255);
                        SA[o] = (unsigned char)(A + a * ia / 255);
                    }
                }
            }
        }
    }
}

/* separable area filter: premultiplied source (SW x SH) -> 565 colour + alpha (DW x DH) */
static int *RT; static int rtcap;
static short *WX0, *WXN; static unsigned short *WXW; static int wxcap;
static int make_weights(int sn, int dn, short *j0, short *cnt, unsigned short *w, int maxk)
{
    int i, s = (int)(((long long)sn << 16) / dn);
    for (i = 0; i < dn; i++) {
        int a = (int)(((long long)i * sn << 16) / dn), b = a + s, j = a >> 16, k = 0, tot = 0;
        j0[i] = (short)j;
        while (j < sn && (j << 16) < b && k < maxk) {
            int lo = a > (j << 16) ? a : (j << 16), hi = b < ((j + 1) << 16) ? b : ((j + 1) << 16);
            int ww = (int)(((long long)(hi - lo) << 12) / s);
            w[i * maxk + k] = (unsigned short)ww; tot += ww; k++; j++;
        }
        if (k && tot != 4096) w[i * maxk + k - 1] = (unsigned short)(w[i * maxk + k - 1] + 4096 - tot);
        cnt[i] = (short)k;
    }
    return 1;
}
static int resample(int SW, int SH, int DW, int DH, u16 *dc, unsigned char *da)
{
    int x, y, kx = SW / DW + 2, ky = SH / DH + 2, n = DW * SH * 4, mx = DW > DH ? DW : DH, mk = kx > ky ? kx : ky;
    short *Y0, *YN; unsigned short *YW;
    if (n > rtcap) { free(RT); RT = (int *)malloc(n * sizeof(int)); rtcap = RT ? n : 0; if (!RT) return 0; }
    if (DW > 1024 || DH > 1024) return 0;
    mx = 1024;
    if (!WX0) { WX0 = (short *)malloc(2048 * sizeof(short)); WXN = (short *)malloc(2048 * sizeof(short)); if (!WX0 || !WXN) return 0; }
    if (mx * mk * 2 > wxcap) {
        free(WXW);
        WXW = (unsigned short *)malloc(mx * mk * 2 * sizeof(short));
        wxcap = WXW ? mx * mk * 2 : 0;
        if (!wxcap) return 0;
    }
    make_weights(SW, DW, WX0, WXN, WXW, kx);
    Y0 = WX0 + mx; YN = WXN + mx; YW = WXW + mx * mk;
    make_weights(SH, DH, Y0, YN, YW, ky);
    for (y = 0; y < SH; y++) {
        int *o = RT + y * DW * 4;
        for (x = 0; x < DW; x++, o += 4) {
            int j = WX0[x], k, A = 0, R = 0, G = 0, Bb = 0;
            for (k = 0; k < WXN[x]; k++, j++) {
                int si = y * SW + j, w = WXW[x * kx + k], a = SA[si];
                if (!a) continue;
                A += a * w; R += SPR[si] * w; G += SPG[si] * w; Bb += SPB[si] * w;
            }
            o[0] = A >> 8; o[1] = R >> 8; o[2] = G >> 8; o[3] = Bb >> 8;
        }
    }
    for (y = 0; y < DH; y++) for (x = 0; x < DW; x++) {
        int j = Y0[y], k, A = 0, R = 0, G = 0, Bb = 0, di = y * DW + x;
        for (k = 0; k < YN[y]; k++, j++) {
            const int *o = RT + (j * DW + x) * 4;
            int w = YW[y * ky + k];
            if (!o[0]) continue;
            A += o[0] * w; R += o[1] * w; G += o[2] * w; Bb += o[3] * w;
        }
        {
            int a = A >> 16;
            if (a < 4) { da[di] = 0; continue; }
            {
                /* R,G,B were premultiplied by the same alpha factor A carries: one reciprocal per pixel */
                int a12 = A >> 12, inv, rr_, gg, bb;
                if (a12 < 1) a12 = 1;
                inv = (1 << 24) / a12;
                rr_ = (int)(((long long)(R >> 12) * inv) >> 24); gg = (int)(((long long)(G >> 12) * inv) >> 24); bb = (int)(((long long)(Bb >> 12) * inv) >> 24);
                if (rr_ > 31) rr_ = 31; if (gg > 63) gg = 63; if (bb > 31) bb = 31;
                dc[di] = (u16)(rr_ << 11 | gg << 5 | bb);
                da[di] = (unsigned char)(a > 255 ? 255 : a < 2 ? 2 : a);
            }
        }
    }
    return 1;
}

/* load art #art into frame slot `slot`, scaled so that one logical pixel = zoom/256 screen pixels */
static int load_art_z(int art, int slot, int maxnf, int zoom, int *ow, int *oh, int *oax, int *oay, int *onf)
{
    unsigned char h[12];
    int W, Hh, nf, f, sc, DW, DH, wide = mver >= 2;
    if (!mdat || art < 0 || art >= mcount) return 0;
    plat_fseek(mdat, (long)moff[art]);
    if (plat_fread(mdat, h, wide ? 12 : 10) != (wide ? 12 : 10)) return 0;
    W = h[0] | h[1] << 8; Hh = h[2] | h[3] << 8; nf = h[8] | h[9] << 8;
    sc = wide ? (h[10] | h[11] << 8) : 256;
    if (sc < 64) sc = 256;
    if (nf > maxnf) nf = maxnf;
    DW = W * zoom / sc; DH = Hh * zoom / sc;
    if (DW < 4) DW = 4; if (DH < 4) DH = 4;
    if (!frame_alloc_n(slot, DW, DH, nf)) return 0;
    if (!src_alloc(W * Hh)) return 0;
    *ow = DW; *oh = DH; *onf = nf;
    *oax = (h[4] | h[5] << 8) * zoom / sc; *oay = (h[6] | h[7] << 8) * zoom / sc;
    for (f = 0; f < nf; f++) {
        unsigned char sz[4];
        unsigned n;
        if (plat_fread(mdat, sz, 4) != 4) return 0;
        n = sz[0] | sz[1] << 8 | sz[2] << 16 | (unsigned)sz[3] << 24;
        if (!read_block(mdat, n)) return 0;
        if (f) memset(SA, 0, W * Hh);
        decode_rle(mscratch, W, Hh, wide, 0);
        if (!resample(W, Hh, DW, DH, FCOL[slot] + f * DW * DH, FALP[slot] + f * DW * DH)) return 0;
    }
    return 1;
}
/* monster art at its display zoom; afterwards the sprite is drawn at 1:1 (m->zoom = 256) */
static int load_art_mon(Mon *m, int zoom)
{
    if (!load_art_z(m->art, m->slot, NFR, zoom, &m->fw, &m->fh, &m->fax, &m->fay, &m->nfr)) return 0;
    m->zoom = 256;
    return 1;
}
/* logical (on-screen at zoom 256) size of an art entry, without loading it */
static int art_size(int art, int *w, int *h)
{
    unsigned char hd[12];
    int wide = mver >= 2, sc;
    if (!mdat || art < 0 || art >= mcount) return 0;
    plat_fseek(mdat, (long)moff[art]);
    if (plat_fread(mdat, hd, wide ? 12 : 10) != (wide ? 12 : 10)) return 0;
    sc = wide ? (hd[10] | hd[11] << 8) : 256;
    if (sc < 64) sc = 256;
    *w = (hd[0] | hd[1] << 8) * 256 / sc; *h = (hd[2] | hd[3] << 8) * 256 / sc;
    return 1;
}

/* ---- the wanderer: equipment layers from abyss_hero.dat ---- */
static void *hdat;
static int hW, hH, hAX, hAY, hNF, hSC, hN, hgrp[8];
static unsigned hoff[64];
static void hdat_open(void)
{
    unsigned char h[20], g[9];
    int i, ng;
    hdat = plat_fopen(L"abyss_hero.dat");
    if (!hdat) return;
    if (plat_fread(hdat, h, 20) != 20 || memcmp(h, "AHRO", 4)) { plat_fclose(hdat); hdat = 0; return; }
    hN = h[6] | h[7] << 8; hW = h[8] | h[9] << 8; hH = h[10] | h[11] << 8; hAX = h[12] | h[13] << 8; hAY = h[14] | h[15] << 8;
    hNF = h[16] | h[17] << 8; hSC = h[18] | h[19] << 8;
    if (hN > 64) hN = 64;
    plat_fread(hdat, g, 1); ng = g[0] > 8 ? 8 : g[0];
    plat_fread(hdat, g + 1, ng);
    for (i = 0; i < ng; i++) hgrp[i] = g[1 + i];
    {
        unsigned char t[256];
        plat_fread(hdat, t, hN * 4);
        for (i = 0; i < hN; i++) hoff[i] = t[i * 4] | (t[i * 4 + 1] << 8) | (t[i * 4 + 2] << 16) | ((unsigned)t[i * 4 + 3] << 24);
    }
}
enum { HG_BODY, HG_HAIR, HG_ARMOR, HG_HELM, HG_WEAPON, HG_SHIELD };
static int hero_layer(int grp, int v)
{
    int i, o = 0;
    if (v < 0 || v >= hgrp[grp]) return -1;
    for (i = 0; i < grp; i++) o += hgrp[i];
    return o + v;
}
/* item base -> look variant (order = art/hero.py lists) */
static int hero_look(int slot)
{
    static const signed char WV[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    const Item *it = &S.eq[slot];
    int b = it->base;
    if (!it->kind) return -1;
    if (slot == 0) { if (b <= 7) return WV[b]; if (b >= 23 && b <= 26) return 8 + b - 23; return 2; }
    if (slot == 2) { if (b >= 8 && b <= 13) return b - 8; if (b >= 27 && b <= 30) return 6 + b - 27; return 3; }
    if (slot == 1) { if (b >= 14 && b <= 18) return b - 14; if (b >= 31 && b <= 33) return 5 + b - 31; return 1; }
    return -1;
}
static int hero_keys[8] = { -1, -1, -1, -1, -1, -1, -1, -1 }, hero_dirty_mask;
static int hero_build(int slot, int zoom, int *ow, int *oh, int *oax, int *oay, int *onf)
{
    int lay[8], nl = 0, f, k, w = hero_look(0), a = hero_look(2), hm = hero_look(1), sh = -1, key;
    int DW, DH;
    if (!hdat) return 0;
    if (hero_dirty) { hero_dirty = 0; hero_dirty_mask = 0xFF; }
    if (w >= 0 && (w <= 3 || w == 6 || w == 9 || w == 11) && a >= 1) sh = a <= 2 ? 0 : a <= 4 ? 1 : 2;
    key = ((((w + 1) * 16 + a + 1) * 16 + hm + 1) * 4 + sh + 1) * 1024 + zoom;
    DW = hW * zoom / hSC; DH = hH * zoom / hSC;
    *ow = DW; *oh = DH; *oax = hAX * zoom / hSC; *oay = hAY * zoom / hSC; *onf = hNF;
    if (key == hero_keys[slot] && !(hero_dirty_mask & (1 << slot)) && FCAP[slot] >= DW * DH * hNF) return 1;
    lay[nl++] = hero_layer(HG_BODY, 0);
    if (hm < 0 || hm == 4 || hm == 7) lay[nl++] = hero_layer(HG_HAIR, 0);
    if (a >= 0) lay[nl++] = hero_layer(HG_ARMOR, a);
    if (hm >= 0) lay[nl++] = hero_layer(HG_HELM, hm);
    if (w >= 0) lay[nl++] = hero_layer(HG_WEAPON, w);
    if (sh >= 0) lay[nl++] = hero_layer(HG_SHIELD, sh);
    hero_keys[slot] = -1;
    if (!frame_alloc_n(slot, DW, DH, hNF) || !src_alloc(hW * hH)) return 0;
    {
        /* each layer's frames are consecutive: remember where the next frame of every layer starts */
        unsigned pos[8];
        for (k = 0; k < nl; k++) pos[k] = lay[k] >= 0 ? hoff[lay[k]] : 0;
        for (f = 0; f < hNF; f++) {
            memset(SA, 0, hW * hH);
            for (k = 0; k < nl; k++) {
                unsigned char sz[4];
                unsigned n;
                if (lay[k] < 0) continue;
                plat_fseek(hdat, (long)pos[k]);
                if (plat_fread(hdat, sz, 4) != 4) return 0;
                n = sz[0] | sz[1] << 8 | sz[2] << 16 | (unsigned)sz[3] << 24;
                if (!read_block(hdat, n)) return 0;
                pos[k] += 4 + n;
                decode_rle(mscratch, hW, hH, 1, 1);
            }
            if (!resample(hW, hH, DW, DH, FCOL[slot] + f * DW * DH, FALP[slot] + f * DW * DH)) return 0;
        }
    }
    hero_keys[slot] = key; hero_dirty_mask &= ~(1 << slot);
    return 1;
}

static void mon_init(Mon *m, const MonDef *d, int champ, int boss)
{
    float mult = 1.0f + 0.16f * (S.floor - 1);
    float hk = d->hpk * (champ ? 2.0f : 1.0f), ak = d->atkk * (champ ? 1.3f : 1.0f);
    int i;
    memset(m, 0, sizeof(*m));
    if (champ) wfmt(m->name, 28, L"광포한 %ls", d->n);
    else wfmt(m->name, 28, L"%ls", d->n);
    m->maxhp = m->hp = (int)(14 * hk * mult);
    m->atk = (int)(5 * ak * mult);
    m->def = (int)(2 * d->defk * mult);
    m->agi = d->agi + S.floor / 3;
    m->exp = (int)(8 * mult * (d->hpk + d->atkk * 1.5f) / 2.5f * (champ ? 2.5f : 1.0f));
    m->gold = (int)(5 * mult * (d->hpk + d->atkk) / 2);
    m->spec = d->spec; m->spr = d->spr; m->alive = 1; m->champ = champ; m->boss = boss;
    m->art = d->art;
    for (i = 0; i < 4; i++) m->pal[i] = rgb(d->col[i][0], d->col[i][1], d->col[i][2]);
    if (champ) { m->pal[0] = mix(m->pal[0], rgb(80, 80, 255), 90); m->tint = rgb(90, 90, 255); m->tintk = 60; }
    m->scale = boss ? 4 : 2;
}

static int alive_count(void) { int i, c = 0; for (i = 0; i < B.n; i++) c += B.m[i].alive; return c; }

static void bline(int i, u16 c, const wchar_t *f, ...)
{
    va_list a;
    va_start(a, f);
    VSNW(B.ln[i].t, 52, f, a);
    va_end(a);
    B.ln[i].t[51] = 0;
    B.ln[i].c = c;
}
static void bclear(void) { B.ln[0].t[0] = B.ln[1].t[0] = B.ln[2].t[0] = 0; }
static void bres(u16 c, const wchar_t *f, ...)
{
    va_list a;
    if (B.nres >= 12) return;
    va_start(a, f);
    VSNW(B.res[B.nres].t, 52, f, a);
    va_end(a);
    B.res[B.nres].t[51] = 0;
    B.res[B.nres].c = c;
    B.nres++;
}

static void layout_mons(void)
{
    int i, totw = 0, maxh = 0, zoom, availh, ground;
    gs_layout();
    ground = BTOP + (BBOT - 42 - BTOP) * 64 / 100 + 6;
    availh = ground - BTOP - 24;
    int useart = 1, lw[3], lh[3];
    for (i = 0; i < B.n; i++) {
        Mon *m = &B.m[i];
        m->slot = i; m->appear = 14; m->fw = 0;
        if (!art_size(m->art, &lw[i], &lh[i])) useart = 0;
    }
    if (useart) {
        for (i = 0; i < B.n; i++) { totw += lw[i] * 3 / 4; if (lh[i] > maxh) maxh = lh[i]; }
        zoom = B.n == 1 ? 300 : 256;
        if (maxh * zoom / 256 > availh) zoom = availh * 256 / maxh;
        if (totw * zoom / 256 > GW - 4) zoom = (GW - 4) * 256 / totw;
        for (i = 0; i < B.n; i++) if (!load_art_mon(&B.m[i], zoom)) useart = 0;
    }
    if (useart) {
        for (i = 0; i < B.n; i++) {
            Mon *m = &B.m[i];
            int cx = GW * (i + 1) / (B.n + 1);
            m->sw = m->fw; m->sh = m->fh;
            m->sx = cx - m->fax;
            m->sy = ground - m->fay;
        }
        return;
    }
    for (i = 0; i < B.n; i++) {
        Mon *m = &B.m[i];
        int w, h;
        m->fw = 0; m->art = -1; m->zoom = 256;
        if (!m->boss) m->scale = B.n == 1 ? 3 : 2;
        sprite_size(m->spr, &w, &h);
        m->sw = w * m->scale; m->sh = h * m->scale;
        m->sx = GW * (i + 1) / (B.n + 1) - m->sw / 2;
        m->sy = BTOP + (BBOT - 42 - BTOP) * 64 / 100 + 6 - m->sh;
        if (m->sy < BTOP + 22) m->sy = BTOP + 22;
        build_sprite(m);
        gen_frames(m);
    }
}

/* Diablo-style unique monster with random name and modifiers */
static const wchar_t *UN1[] = { L"피", L"뼈", L"재", L"썩은살", L"검은", L"독", L"굶주림", L"그림자", L"불꽃", L"서리" };
static const wchar_t *UN2[] = { L"파쇄자", L"갈퀴손", L"송곳니", L"도살꾼", L"울음꾼", L"가죽벗기개", L"해골먹이", L"심장포식자" };
static void make_unique(Mon *m)
{
    static const wchar_t *MODN[5] = { L"매우 강함", L"매우 빠름", L"화염 강화", L"돌 피부", L"마나 흡수" };
    int a = rr(0, 4), b = rr(0, 4), k;
    if (b == a) b = -1;
    wfmt(m->name, 28, L"%ls %ls", UN1[rr(0, 9)], UN2[rr(0, 7)]);
    m->uniq = 1; m->champ = 0;
    m->maxhp = m->hp = m->hp * 3;
    m->exp *= 4; m->gold *= 3;
    m->pal[0] = mix(m->pal[0], rgb(rr(60, 255), rr(40, 200), rr(30, 160)), 110);
    m->pal[2] = mix(m->pal[2], rgb(255, 230, 160), 60);
    m->tint = rgb(rr(120, 255), rr(80, 220), rr(40, 160)); m->tintk = 70;
    for (k = 0; k < 2; k++) {
        int md = k ? b : a;
        if (md < 0) continue;
        if (md == 0) m->atk = m->atk * 3 / 2;
        else if (md == 1) m->agi += 10;
        else if (md == 2) m->spec = SPC_FIRE;
        else if (md == 3) m->def = m->def * 2 + 4;
        else m->spec = SPC_DRAIN;
    }
    if (b >= 0) wfmt(m->mods, 32, L"%ls, %ls", MODN[a], MODN[b]);
    else wfmt(m->mods, 32, L"%ls", MODN[a]);
}

/* ================================================================ */
/*  Golden-Sun style battle presentation                            */
/*  (hero + event allies seen from behind, dash attacks, camera     */
/*   sway, spell effects, top status window, icon command bar)     */
/* ================================================================ */
#define ART_HERO 56

/* blue gradient window with light border (GS style) */
static void render_panel(void);
static void belt_slots(int active);
static int first_alive(void);
static void gswin(int x, int y, int w, int h)
{
    int j;
    for (j = 0; j < h; j++) {
        u16 c = mix(rgb(52, 70, 150), rgb(12, 18, 64), j * 256 / (h ? h : 1));
        if (y + j >= 0 && y + j < GH) hline(x + 1, x + w - 2, y + j, c);
    }
    rectl(x, y, w, h, rgb(10, 10, 30));
    rectl(x + 1, y + 1, w - 2, h - 2, rgb(206, 214, 240));
    hline(x + 2, x + w - 3, y + 2, rgb(120, 140, 200));
    pset(x + 1, y + 1, rgb(10, 10, 30)); pset(x + w - 2, y + 1, rgb(10, 10, 30));
    pset(x + 1, y + h - 2, rgb(10, 10, 30)); pset(x + w - 2, y + h - 2, rgb(10, 10, 30));
}

/* generic frame blit for party members (alpha blended, anchored at feet) */
static void blit_frame(int slot, int fi, int W, int H, int ax, int ay, int gx, int gy, int zoom, int amul, int white, u16 tintc, int tk)
{
    int x, y, dw, dh, ox, oy;
    const u16 *pc;
    const unsigned char *pa;
    if (!FCAP[slot] || zoom < 16) return;
    pc = FCOL[slot] + fi * W * H; pa = FALP[slot] + fi * W * H;
    dw = W * zoom >> 8; dh = H * zoom >> 8;
    ox = gx - ax * zoom / 256; oy = gy - ay * zoom / 256;
    for (y = 0; y < dh; y++) {
        int sy = oy + y, syy = y * 256 / zoom;
        if (sy < sclipT || sy >= sclipB || syy >= H) continue;
        for (x = 0; x < dw; x++) {
            int sx = ox + x, sxx = x * 256 / zoom, a;
            u16 c, *d;
            if ((unsigned)sx >= GW || sxx >= W) continue;
            a = pa[syy * W + sxx];
            if (a < 3) continue;
            c = pc[syy * W + sxx];
            if (tk) c = mix(c, tintc, tk);
            if (white) c = mix(c, rgb(255, 255, 255), 190);
            a = a * amul >> 8;
            d = &FB[sy * GW + sx];
            *d = a >= 250 ? c : mix(*d, c, a);
        }
    }
}

/* ---- party ---- */
enum { PR_HERO, PR_PRIEST, PR_ROGUE, PR_BARB };
static const wchar_t *ALLYN[4] = { L"방랑자", L"수도사 엘레나", L"추적자 카심", L"야만용사 고룸" };
static const wchar_t *ALLYS[4] = { L"방랑자", L"엘레나", L"카심", L"고룸" };

static int pm_alive(int i) { return i == 0 ? S.hp > 0 : B.pm[i].hp > 0; }
static int pm_hp(int i) { return i == 0 ? S.hp : B.pm[i].hp; }
static int pm_maxhp(int i) { return i == 0 ? p_maxhp() : B.pm[i].maxhp; }

static int load_pm_art(int i)
{
    PMember *p = &B.pm[i];
    int z = p->zoom;
    if (i == 0 && hero_build(3, z, &p->fw, &p->fh, &p->fax, &p->fay, &p->nf)) { p->zoom = 256; return 1; }
    hero_keys[3 + i] = -1;              /* the slot is about to be overwritten */
    if (!load_art_z(p->art, 3 + i, 32, z, &p->fw, &p->fh, &p->fax, &p->fay, &p->nf)) return 0;
    p->zoom = 256;
    return 1;
}

static void add_ally(int role)
{
    PMember *p;
    float mult = 1.0f + 0.16f * (S.floor - 1);
    if (B.np >= 4) return;
    p = &B.pm[B.np++];
    memset(p, 0, sizeof(*p));
    p->role = role; p->art = ART_HERO + role;
    p->maxhp = p->hp = (int)((role == PR_BARB ? 80 : role == PR_PRIEST ? 50 : 60) * mult);
    p->atk = (int)((role == PR_BARB ? 10 : role == PR_ROGUE ? 8 : 6) * mult);
    p->def = (int)(3 * mult);
    p->mp = p->maxmp = (int)(40 * mult);
}

static void party_layout(void)
{
    int i, n = B.np, z = n == 1 ? 300 : n == 2 ? 260 : 220;
    for (i = 0; i < n; i++) {
        PMember *p = &B.pm[i];
        p->zoom = z;
        p->hy = BBOT + (n == 1 ? 14 : 8);
        if (n == 1) p->hx = GW * 40 / 100;
        else p->hx = GW * (i * 2 + 1) / (n * 2) + (i == 0 ? 0 : 0);
    }
    if (n > 1) {   /* hero in the middle-left, allies around */
        static const int ORD[4][4] = { { 0 }, { 0, 1 }, { 1, 0, 2 }, { 1, 0, 2, 3 } };
        int k;
        for (k = 0; k < n; k++) B.pm[ORD[n - 1][k]].hx = GW * (k * 2 + 1) / (n * 2);
    }
}

static void party_init(void)
{
    B.np = 1;
    memset(&B.pm[0], 0, sizeof(PMember));
    B.pm[0].art = ART_HERO; B.pm[0].role = PR_HERO;
    if (B.boss) {
        static const int EV[5][3] = { { PR_PRIEST, 0, 0 }, { PR_ROGUE, 0, 0 }, { PR_BARB, 0, 0 }, { PR_PRIEST, PR_ROGUE, 0 }, { PR_PRIEST, PR_ROGUE, PR_BARB } };
        int k, a = CA->act;
        for (k = 0; k < 3; k++) if (EV[a][k]) add_ally(EV[a][k]);
    }
    party_layout();
    {
        int i;
        for (i = 0; i < B.np; i++) if (!load_pm_art(i)) B.pm[i].fw = 0;
    }
}

/* frame for a party member */
static int pm_frame(PMember *p, int i)
{
    if (!pm_alive(i)) return 20 + clampi(p->at / 3, 0, 3);
    switch (p->anim) {
    case 1: return 8 + clampi(p->at - 5, 0, 7);
    case 2: return 16 + clampi(p->at / 2, 0, 3);
    case 4: return p->nf >= 32 ? 24 + clampi(p->at, 0, 7) : clampi(p->at, 0, 7);
    }
    return ((tickc >> 1) + i * 3) & 7;
}

/* where a party member currently stands (dash towards the target while attacking) */
static void pm_pos(int i, int *gx, int *gy, int *zoom)
{
    PMember *p = &B.pm[i];
    int s = 0;
    *gx = p->hx - B.pan / 2; *gy = p->hy; *zoom = p->zoom;
    if (p->anim == 1) {
        int t = p->at;
        s = t < 6 ? t * 256 / 6 : t < 14 ? 256 : t < 20 ? 256 - (t - 14) * 256 / 6 : 0;
        if (s > 0 && p->tgt >= 0 && p->tgt < B.n) {
            Mon *m = &B.m[p->tgt];
            int tx = m->sx + (m->fw ? m->fax * m->zoom / 256 : m->sw / 2) + B.pan * 7 / 10;
            int ty = m->sy + (m->fw ? m->fay * m->zoom / 256 : m->sh) + 10;
            *gx += (tx - *gx) * s * 85 / 100 / 256;
            *gy += (ty - *gy) * s * 85 / 100 / 256;
            *zoom = *zoom - (*zoom * 40 / 100) * s / 256;
        }
    }
}

/* ---- spell / hit effects ---- */
enum { FX_NONE, FX_FIRE, FX_BOLT, FX_FROST, FX_HEAL, FX_HOLY, FX_POTION, FX_WHIRL };
static void fx_start(int type, int src, int tgt)
{
    B.fx = type; B.fxt = 0; B.fxsrc = src; B.fxtgt = tgt;
}
static void mon_center(int i, int *x, int *y)
{
    Mon *m = &B.m[i];
    *x = m->sx + m->sw / 2 + B.pan * 7 / 10;
    *y = m->sy + m->sh / 2;
}
static void glow(int cx, int cy, int r, u16 c, int k)
{
    int x, y;
    for (y = -r; y <= r; y++) for (x = -r; x <= r; x++) {
        int d = x * x + y * y, px = cx + x, py = cy + y;
        if (d > r * r || (unsigned)px >= GW || py < BTOP || py >= BBOT) continue;
        FB[py * GW + px] = mix(FB[py * GW + px], c, k * (r * r - d) / (r * r));
    }
}
static void render_fx(void)
{
    int t = B.fxt, i, sx, sy, tx, ty, z;
    if (!B.fx) return;
    pm_pos(B.fxsrc >= 0 ? B.fxsrc : 0, &sx, &sy, &z);
    sy -= B.pm[0].fh * z / 256 * 7 / 10;
    switch (B.fx) {
    case FX_FIRE:
        if (B.fxtgt < 0 || B.fxtgt >= B.n) break;
        mon_center(B.fxtgt, &tx, &ty);
        if (t < 8) {
            int x = sx + (tx - sx) * t / 8, y = sy + (ty - sy) * t / 8 - (t * (8 - t));
            for (i = 0; i < 5; i++) glow(x - (tx - sx) * i / 40, y - (ty - sy) * i / 40, 7 - i, rgb(255, 120 + i * 20, 20), 220 - i * 30);
            glow(x, y, 3, rgb(255, 255, 200), 255);
        } else if (t < 18) {
            int r = (t - 8) * 3 + 6;
            glow(tx, ty, r, rgb(255, 90, 10), 200 - (t - 8) * 15);
            glow(tx, ty, r / 2, rgb(255, 220, 120), 230 - (t - 8) * 20);
        }
        break;
    case FX_BOLT:
        if (t < 13) for (i = 0; i < B.n; i++) if (B.m[i].alive || B.m[i].dying) {
            int x, y, k, px, py;
            mon_center(i, &tx, &ty);
            px = tx + (int)(hsh(i, t) % 20) - 10; py = BTOP;
            for (k = 1; k <= 8; k++) {
                x = px + (tx - px) * k / 8 + ((k < 8) ? (int)(hsh(k + i * 9, t / 2) % 16) - 8 : 0);
                y = py + (ty - py) * k / 8;
                line(px, py, x, y, rgb(230, 240, 255)); line(px + 1, py, x + 1, y, rgb(140, 170, 255));
                px = x; py = y;
            }
            glow(tx, ty, 10, rgb(180, 200, 255), 160);
        }
        if (t == 2 || t == 3) tint(0, BTOP, GW, BBOT - BTOP, rgb(255, 255, 255), 90);
        break;
    case FX_FROST:
        if (t < 16) {
            tint(0, BTOP, GW, BBOT - BTOP, rgb(120, 180, 255), 40);
            for (i = 0; i < 24; i++) {
                int x = (int)(hsh(i, 3) % GW), y = BTOP + (int)((hsh(i, 5) % 40)) + t * 9 - 30, k;
                for (k = 0; k < 6; k++) { pset(x + k / 3, y + k, rgb(220, 240, 255)); pset(x - k / 3, y + k, rgb(160, 210, 255)); }
            }
        }
        break;
    case FX_HEAL: case FX_POTION: {
        int tg = B.fxtgt >= 0 ? B.fxtgt : 0, gx, gy, gz;
        u16 c = B.fx == FX_HEAL ? rgb(120, 255, 140) : rgb(255, 120, 120);
        pm_pos(tg, &gx, &gy, &gz);
        if (t < 16) for (i = 0; i < 14; i++) {
            int x = gx + (int)(hsh(i, 7) % 50) - 25, y = gy - 10 - (int)(hsh(i, 9) % 60) - t * 3;
            glow(x, y, 3, c, 220 - t * 12);
        }
        break;
    }
    case FX_HOLY:
        if (B.fxtgt < 0 || B.fxtgt >= B.n) break;
        mon_center(B.fxtgt, &tx, &ty);
        if (t < 14) {
            int w = 14 - (t > 8 ? (t - 8) * 2 : 0), y;
            for (y = BTOP; y < ty + 20 && y < BBOT; y++) {
                int x;
                for (x = tx - w; x <= tx + w; x++) if ((unsigned)x < GW)
                    FB[y * GW + x] = mix(FB[y * GW + x], rgb(255, 240, 160), 180 - abs(x - tx) * 180 / (w + 1));
            }
        }
        break;
    case FX_WHIRL:
        if (t < 12) for (i = 0; i < B.n; i++) if (B.m[i].alive || B.m[i].dying) {
            int k;
            mon_center(i, &tx, &ty);
            for (k = 0; k < 10; k++) {
                int a = k * 36 + t * 40, r = 18;
                static const signed char CS[10] = { 10, 8, 3, -3, -8, -10, -8, -3, 3, 8 };
                int ci = (a / 36) % 10, si = (ci + 7) % 10;
                glow(tx + CS[ci] * r / 10, ty + CS[si] * r / 20, 3, rgb(255, 230, 200), 200);
            }
        }
        break;
    }
}

/* ---- battle UI ---- */
static void draw_cmd_icon(int k, int x, int y, int sz, int sel)
{
    int c = x + sz / 2, m = y + sz / 2, i;
    gswin(x, y, sz, sz);
    if (sel) { rectl(x, y, sz, sz, rgb(255, 220, 60)); rectl(x + 1, y + 1, sz - 2, sz - 2, rgb(255, 240, 140)); }
    switch (k) {
    case 0:   /* sword */
        for (i = 0; i < sz * 6 / 10; i++) { pset(x + sz / 5 + i, y + sz * 4 / 5 - i, rgb(230, 230, 240)); pset(x + sz / 5 + i + 1, y + sz * 4 / 5 - i, rgb(140, 140, 160)); }
        line(x + sz / 6, y + sz * 6 / 10, x + sz * 4 / 10, y + sz * 9 / 10, rgb(220, 180, 70));
        break;
    case 1:   /* magic star */
        glow(c, m, sz / 3, rgb(120, 180, 255), 255);
        glow(c, m, sz / 6, rgb(255, 255, 255), 255);
        for (i = 0; i < 4; i++) { line(c, m, c + (i & 1 ? sz / 3 : -sz / 3) * (i < 2), m + (i & 1 ? 0 : 0), rgb(200, 230, 255)); }
        line(c - sz / 3, m, c + sz / 3, m, rgb(220, 240, 255)); line(c, m - sz / 3, c, m + sz / 3, rgb(220, 240, 255));
        break;
    case 2: potion_icon(x + 3, y + 3, sz - 6, IK_HP); break;
    case 3:   /* shield */
        fcircle(c, m, sz / 3, rgb(150, 110, 60));
        fcircle(c, m, sz / 3 - 2, rgb(110, 80, 44));
        fcircle(c, m, sz / 9, rgb(200, 200, 210));
        break;
    case 4:   /* flee arrow */
        for (i = 0; i < sz / 2; i++) hline(c - sz / 4 - i / 2, c - sz / 4 + sz / 2 - i, m - sz / 4 + i, 0);
        line(x + sz / 5, m, x + sz * 4 / 5, m, rgb(255, 255, 255));
        line(x + sz * 4 / 5, m, x + sz * 3 / 5, m - sz / 5, rgb(255, 255, 255));
        line(x + sz * 4 / 5, m, x + sz * 3 / 5, m + sz / 5, rgb(255, 255, 255));
        line(x + sz / 5, m + 1, x + sz * 4 / 5, m + 1, rgb(200, 200, 200));
        break;
    }
}

static void count_potions(int *hp, int *mp)
{
    int i;
    *hp = *mp = 0;
    for (i = 0; i < 4; i++) { if (S.belt[i].kind == IK_HP || S.belt[i].kind == IK_RJ) (*hp)++; if (S.belt[i].kind == IK_MP) (*mp)++; }
    for (i = 0; i < INV_N; i++) { if (S.inv[i].kind == IK_HP || S.inv[i].kind == IK_RJ) (*hp)++; if (S.inv[i].kind == IK_MP) (*mp)++; }
}

static void bar(int x, int y, int w, int cur, int mx, u16 c)
{
    int f = mx > 0 ? w * clampi(cur, 0, mx) / mx : 0;
    fill(x, y, w, 3, rgb(10, 10, 30));
    fill(x, y, f, 3, c);
}

static void render_status(void)
{
    int i;
    for (i = 1; i < B.np; i++) {
        int x = 3, y = 22 + (i - 1) * 30, w = 58;
        u16 nc = pm_alive(i) ? C_GOLD : C_RED;
        fill(x, y, w, 26, rgb(10, 8, 6));
        goldframe(x, y, w, 26);
        txt(x + 5, y + 3, ALLYS[B.pm[i].role], nc, T_SHADOW);
        bar(x + 5, y + 17, w - 10, pm_hp(i), pm_maxhp(i), rgb(200, 30, 20));
        bar(x + 5, y + 21, w - 10, B.pm[i].mp, B.pm[i].maxmp, rgb(50, 80, 220));
    }
}
static const wchar_t *CMDS[5] = { L"공격", L"마법", L"도구", L"방어", L"도망" };
static const int CMDX[5] = { 73, 105, 137, 73, 121 }, CMDY[5] = { 36, 36, 36, 66, 66 }, CMDW[5] = { 30, 30, 30, 46, 46 };
static void d2win(int x, int y, int w, int h)
{
    darken(x, y, w, h, 60);
    goldframe(x, y, w, h);
}
static void render_cmdbar(void)
{
    int i, choosing = B.phase == BP_CMD || B.phase == BP_MAGIC || B.phase == BP_POTION || B.phase == BP_TARGET;
    /* battle messages: dark strip just above the panel, like the explore log */
    if (B.phase != BP_MAGIC && B.phase != BP_POTION) {
        int y0 = PY - 42;
        darken(0, y0, GW, 42, 70);
        hline(0, GW - 1, y0, rgb(90, 66, 30));
        for (i = 0; i < 3; i++) if (B.ln[i].t[0]) txt(5, y0 + 2 + i * 13, B.ln[i].t, B.ln[i].c, T_SHADOW);
        if ((B.phase == BP_RESULT || B.phase == BP_DEAD || B.phase == BP_INTRO) && (tickc & 8)) txt(GW - 12, PY - 14, L"▼", C_GOLD, T_SHADOW);
    }
    if (B.phase == BP_MAGIC) {
        int n = 4, h = n * 16 + 10, wy = PY - h - 2, wx = GW - 152;
        d2win(wx, wy, 150, h);
        for (i = 0; i < n; i++) {
            u16 c = S.lvl >= SK[i].lvl && S.mp >= SK[i].mp ? C_WHITE : C_GRAY;
            if (i == B.msel) fill(wx + 3, wy + 5 + i * 16, 144, 15, rgb(60, 46, 26));
            if (S.lvl >= SK[i].lvl) {
                txt(wx + 8, wy + 6 + i * 16, SK[i].n, c, T_SHADOW);
                txtf(wx + 142, wy + 6 + i * 16, C_BLUE, T_RIGHT | T_SHADOW, L"마나 %d", SK[i].mp);
            } else txtf(wx + 8, wy + 6 + i * 16, C_GRAY, 0, L"??? (레벨 %d)", SK[i].lvl);
            addbtn(wx, wy + 5 + i * 16, 150, 16, B_LIST + i);
        }
    } else if (B.phase == BP_POTION) {
        int h = 4 * 16 + 10, wy = PY - h - 2, wx = GW - 152;
        d2win(wx, wy, 150, h);
        for (i = 0; i < 4; i++) {
            wchar_t nm[40];
            if (i == B.psel) fill(wx + 3, wy + 5 + i * 16, 144, 15, rgb(60, 46, 26));
            if (S.belt[i].kind) {
                potion_icon(wx + 6, wy + 6 + i * 16, 13, S.belt[i].kind);
                item_name(&S.belt[i], nm, 40);
                txt(wx + 24, wy + 6 + i * 16, nm, C_WHITE, T_SHADOW);
                addbtn(wx, wy + 5 + i * 16, 150, 16, B_BELT + i);
            } else txt(wx + 24, wy + 6 + i * 16, L"(비어 있음)", C_GRAY, 0);
        }
    } else if (B.phase == BP_TARGET) {
        d2win(GW / 2 - 60, PY - 60, 120, 18);
        txt(GW / 2, PY - 57, L"대상을 고르세요", C_GOLD, T_CENTER | T_SHADOW);
    }
    /* panel: belt is usable (drink = item turn), command stones in the centre */
    belt_slots(B.phase == BP_CMD || B.phase == BP_POTION);
    for (i = 0; i < 5; i++) {
        static const wchar_t *LB[5] = { L"공격", L"마법", L"도구", L"방어", L"도망" };
        sbutton(XO + CMDX[i], PY + CMDY[i], CMDW[i], 28, LB[i], B_CMD + i, choosing, choosing && i == B.cmd);
    }
    sbutton(4, PY + 96, 64, 20, L"취소", B_CANCEL, B.phase == BP_MAGIC || B.phase == BP_TARGET || B.phase == BP_POTION, 0);
    sbutton(GW - 68, PY + 96, 64, 20, choosing ? L"결정" : L"계속", B_OKB, 1, 0);
    txtf(GW / 2, PY + 99, C_GOLD, T_CENTER | T_SHADOW, L"레벨 %d", S.lvl);
    if (!choosing) addbtn(0, PY - 42, GW, 42, B_OKB);
}
static void render_battle(void)
{
    int i, bo = B.pan * 35 / 100;
    gs_layout();
    if (!B.bgok) {
        int sVY = VY, sVH = VH, sVCY = VCY;
        VY = BTOP; VH = BBOT - BTOP; VCY = VY + VH * 36 / 100;
        render_view();
        darken(0, VY, GW, VH, 120);
        tint(0, VY, GW, VH, rgb(40, 0, 0), 30);
        memcpy(bgbuf, FB + BTOP * GW, GW * (BBOT - BTOP) * 2);
        VY = sVY; VH = sVH; VCY = sVCY;
        B.bgok = 1;
    }
    /* camera pan: shift the backdrop (edges clamped) */
    for (i = 0; i < BBOT - BTOP; i++) {
        u16 *dst = FB + (BTOP + i) * GW, *src = bgbuf + i * GW;
        if (bo >= 0) { memcpy(dst + bo, src, (GW - bo) * 2); { int k; for (k = 0; k < bo; k++) dst[k] = src[0]; } }
        else { memcpy(dst, src - bo, (GW + bo) * 2); { int k; for (k = GW + bo; k < GW; k++) dst[k] = src[GW - 1]; } }
    }
    if (B.redflash > 0) tint(0, BTOP, GW, BBOT - BTOP, rgb(200, 0, 0), 60);
    sclipT = BTOP; sclipB = BBOT;
    /* enemies */
    for (i = 0; i < B.n; i++) {
        Mon *m = &B.m[i];
        int off = B.pan * 7 / 10;
        m->sx += off;
        if (m->alive || m->dying) draw_sprite(m);
        if (m->alive) addbtn(m->sx, m->sy, m->sw, m->sh, B_MON + i);
        if (m->alive && B.phase == BP_TARGET && i == B.tsel) {
            u16 nc = (m->boss || m->uniq) ? C_GOLD : m->champ ? C_BLUE : rgb(255, 255, 255);
            int cx = m->sx + m->sw / 2, ty = m->sy + 2;
            if (B.phase == BP_TARGET && i == B.tsel) {
                txt(cx, ty - 14, L"▼", (tickc & 4) ? C_YEL : rgb(255, 255, 255), T_CENTER | T_SHADOW);
                (void)nc;
            }

        }
        m->sx -= off;
    }
    for (i = 0; i < 32; i++) if (B.bl[i].t > 0) {
        int x = B.bl[i].x / 16 + B.pan * 7 / 10, y = B.bl[i].y / 16;
        u16 c = (i & 1) ? rgb(200, 20, 14) : rgb(120, 6, 4);
        if (y >= BTOP && y < BBOT) { int a, b; for (b = 0; b < 3; b++) for (a = 0; a < 3; a++) if (a + b < 4) pset(x + a, y + b, c); }
    }
    render_fx();
    /* party, seen from behind */
    for (i = B.np - 1; i >= 0; i--) {
        PMember *p = &B.pm[i];
        int gx, gy, z;
        pm_pos(i, &gx, &gy, &z);
        if (p->fw) {
            /* shadow */
            int x, y, rw = p->fw * z / 256 / 4;
            for (y = -3; y <= 3; y++) for (x = -rw; x <= rw; x++) {
                int px = gx + x, py = gy + y - 4;
                if ((unsigned)px < GW && py >= BTOP && py < BBOT && x * x * 9 / (rw * rw + 1) + y * y < 9) FB[py * GW + px] = shade(FB[py * GW + px], 120);
            }
            blit_frame(3 + i, pm_frame(p, i), p->fw, p->fh, p->fax, p->fay, gx, gy, z, 256, p->flash > 0 && (p->flash & 2), 0, 0);
        } else {
            /* fallback silhouette */
            int h = 70 * z / 256;
            fill(gx - 12, gy - h, 24, h, rgb(40, 30, 30));
            fcircle(gx, gy - h - 8, 9, rgb(60, 44, 36));
        }
    }
    for (i = 0; i < 3; i++) if (B.pop[i].t > 0) {
        wchar_t b[12];
        wfmt(b, 12, L"%d", B.pop[i].v);
        txt(B.pop[i].x + B.pan * 7 / 10, B.pop[i].y - (14 - B.pop[i].t), b, B.pop[i].c, T_CENTER | T_SHADOW | T_BIG);
    }
    sclipT = 0; sclipB = 9999;
    if (B.shake > 0) {
        int s = (B.shake & 2) ? 3 : -3, y;
        for (y = BTOP; y < BBOT; y++) {
            u16 *row = FB + y * GW;
            if (s > 0) { memmove(row + s, row, (GW - s) * 2); memset(row, 0, s * 2); }
            else { memmove(row, row - s, (GW + s) * 2); memset(row + GW + s, 0, -s * 2); }
        }
    }
    {
        int a = alive_count(), f = B.phase == BP_TARGET ? B.tsel : B.focus;
        if (f < 0 || f >= B.n || !B.m[f].alive) f = first_alive();
        if (a == 0) txt(GW / 2, 4, L"승리!", C_YEL, T_CENTER | T_SHADOW);
        else {
            Mon *m = &B.m[f];
            int bx = 40, bw = 160, w = (bw - 2) * m->hp / (m->maxhp ? m->maxhp : 1);
            u16 nc = (m->boss || m->uniq) ? C_GOLD : m->champ ? C_BLUE : C_WHITE;
            fill(bx, 3, bw, 15, rgb(8, 6, 6));
            fill(bx + 1, 4, w, 13, rgb(120, 10, 8));
            fill(bx + 1, 4, w, 2, rgb(170, 30, 20));
            rectl(bx, 3, bw, 15, rgb(90, 80, 70));
            txt(GW / 2, 4, m->name, nc, T_CENTER | T_SHADOW);
            if (m->uniq || m->champ || m->boss) txt(GW / 2, 19, m->uniq ? m->mods : m->boss ? L"보스" : L"챔피언", m->uniq ? C_BLUE : C_GRAY, T_CENTER | T_SHADOW);
            if (a > 1) txtf(GW - 4, 4, C_GRAY, T_RIGHT | T_SHADOW, L"x%d", a);
        }
    }
    render_status();
    render_panel();
    render_cmdbar();
}

static void start_battle(int boss)
{
    int i;
    memset(&B, 0, sizeof(B));
    B.boss = boss;
    if (boss) {
        B.n = 1;
        mon_init(&B.m[0], &BOSS[CA->act], 0, 1);
        bline(0, C_GOLD, L"%ls%ls", B.m[0].name, JI(B.m[0].name));
        bline(1, C_WHITE, L"길을 막아섰다!");
    } else {
        int cand[6], nc = 0, maxn = S.floor < 3 ? 2 : 3;
        for (i = 0; i < 6; i++) cand[nc++] = CA->pool[i];
        B.n = rr(1, maxn);
        {
            int t0 = cand[rr(0, nc - 1)];
            for (i = 0; i < B.n; i++) {
                int t = chance(60) ? t0 : cand[rr(0, nc - 1)];
                mon_init(&B.m[i], &MD[t], chance(S.floor >= 3 ? 10 : 4), 0);
            }
        }
        if (S.floor >= 2 && chance(7)) {
            make_unique(&B.m[0]);
            if (B.n > 1) {
                bline(0, C_GOLD, L"%ls%ls", B.m[0].name, JI(B.m[0].name));
                bline(1, C_WHITE, L"졸개들을 이끌고 나타났다!");
            } else bline(0, C_GOLD, L"%ls%ls 나타났다!", B.m[0].name, JI(B.m[0].name));
        } else if (B.n == 1) {
            bline(0, B.m[0].champ ? C_BLUE : C_WHITE, L"%ls%ls 나타났다!", B.m[0].name, JI(B.m[0].name));
        } else {
            int same = 1;
            for (i = 1; i < B.n; i++) if (wcscmp(B.m[i].name, B.m[0].name)) same = 0;
            if (same) bline(0, C_WHITE, L"%ls %d마리가 나타났다!", B.m[0].name, B.n);
            else bline(0, C_WHITE, L"괴물 무리가 나타났다!");
        }
    }
    layout_mons();
    party_init();
    if (B.np > 1) {
        int k;
        for (k = 1; k < B.np && k < 3; k++) bline(k == 1 ? 2 : 1, rgb(140, 220, 255), L"%ls%ls 함께 싸운다!", ALLYN[B.pm[k].role], JI(ALLYN[B.pm[k].role]));
    }
    B.phase = BP_INTRO;
    B.wait = 30;
    mode = M_BATTLE;
    snd_play(SX_ENC);
    sel = 0;
    dirty = 1;
}

/* ---- inventory helpers ---- */
static int give_item(const Item *it)
{
    int i;
    if (IS_POT(it->kind)) for (i = 0; i < 4; i++) if (!S.belt[i].kind) { S.belt[i] = *it; return 1; }
    for (i = 0; i < INV_N; i++) if (!S.inv[i].kind) { S.inv[i] = *it; return 1; }
    return 0;
}
static void refill_belt(void)
{
    int b, i;
    for (b = 0; b < 4; b++) if (!S.belt[b].kind)
        for (i = 0; i < INV_N; i++) if (IS_POT(S.inv[i].kind)) { S.belt[b] = S.inv[i]; memset(&S.inv[i], 0, sizeof(Item)); break; }
}
/* drink potion; returns heal text into out */
static void drink(Item *it, wchar_t *out, int n)
{
    int mh = p_maxhp(), mm = p_maxmp(), h = 0, m = 0;
    if (it->kind == IK_HP) h = mh * 45 / 100 + 10;
    else if (it->kind == IK_MP) m = mm * 55 / 100 + 6;
    else { h = mh; m = mm; }
    if (h > mh - S.hp) h = mh - S.hp;
    if (m > mm - S.mp) m = mm - S.mp;
    S.hp += h; S.mp += m;
    if (it->kind == IK_HP) wfmt(out, n, L"생명력이 %d 회복되었다.", h);
    else if (it->kind == IK_MP) wfmt(out, n, L"마나가 %d 회복되었다.", m);
    else wfmt(out, n, L"생명력과 마나가 모두 회복되었다!");
    memset(it, 0, sizeof(Item));
    refill_belt();
}

/* ---- battle actions ---- */
static void popup(int slot, int v, int x, int y, u16 c)
{
    B.pop[slot].v = v; B.pop[slot].x = x; B.pop[slot].y = y; B.pop[slot].t = 14; B.pop[slot].c = c;
}

static int first_alive(void) { int i; for (i = 0; i < B.n; i++) if (B.m[i].alive) return i; return 0; }

static void kill_mon(Mon *m)
{
    m->alive = 0; m->dying = 10; S.kills++;
    snd_play(SX_MDIE);
}

static void dmg_mon(int i, int d, int slot)
{
    Mon *m = &B.m[i];
    B.focus = i;
    {
        int k, n = 0;
        for (k = 0; k < 32 && n < 16; k++) if (B.bl[k].t <= 0) {
            B.bl[k].x = (short)((m->sx + m->sw / 2 + rr(-6, 6)) * 16); B.bl[k].y = (short)((m->sy + m->sh / 3 + rr(-6, 6)) * 16);
            B.bl[k].vx = (short)rr(-40, 40); B.bl[k].vy = (short)rr(-50, -10); B.bl[k].t = (short)rr(8, 14); n++;
        }
    }
    m->hp -= d;
    m->flash = 8;
    popup(slot, d, m->sx + m->sw / 2, m->sy + m->sh / 3, C_WHITE);
    if (m->hp <= 0) { m->hp = 0; kill_mon(m); }
    else { m->anim = 2; m->at = 0; }
}

static void player_act(void)
{
    const wchar_t *P = L"방랑자";
    bclear();
    if (B.act == ACT_ATTACK) {
        int t = B.tgt, lo, hi, d, crit, hit;
        Mon *m;
        if (!B.m[t].alive) t = first_alive();
        m = &B.m[t];
        p_dmg(&lo, &hi);
        hit = clampi(80 + (p_dex() - m->agi) * 2, 50, 97);
        bline(0, C_WHITE, L"%ls의 공격!", P);
        if (!chance(hit)) { snd_play(SX_MISS); bline(1, C_WHITE, L"그러나 %ls%ls 재빠르게 피했다!", m->name, JI(m->name)); return; }
        crit = chance(5 + p_dex() / 8);
        snd_play(crit ? SX_CRIT : SX_HIT);
        d = rr(lo, hi);
        if (crit) d *= 2;
        d -= m->def / 2;
        if (d < 1) d = 1;
        if (crit) bline(1, C_YEL, L"회심의 일격! %ls에게 %d의 피해!", m->name, d);
        else bline(1, C_WHITE, L"%ls에게 %d의 피해!", m->name, d);
        dmg_mon(t, d, 0);
        if (!m->alive) bline(2, C_WHITE, L"%ls%ls 쓰러뜨렸다!", m->name, JUL(m->name));
    } else if (B.act == ACT_SKILL) {
        const Skill *k = &SK[B.arg];
        int i, kills = 0;
        S.mp -= k->mp;
        snd_play(B.arg == 0 ? SX_FIRE : B.arg == 1 ? SX_HEAL : B.arg == 2 ? SX_BOLT : SX_FROST);
        bline(0, C_BLUE, L"%ls%ls %ls%ls 외웠다!", P, JUN(P), k->n, JUL(k->n));
        if (B.arg == 0) {
            int t = B.tgt, d;
            if (!B.m[t].alive) t = first_alive();
            d = 8 + S.ene * 6 / 10 + S.lvl * 2 + rr(0, 4);
            bline(1, C_WHITE, L"불길이 %ls%ls 휘감았다! %d의 피해!", B.m[t].name, JUL(B.m[t].name), d);
            dmg_mon(t, d, 0);
            if (!B.m[t].alive) bline(2, C_WHITE, L"%ls%ls 쓰러뜨렸다!", B.m[t].name, JUL(B.m[t].name));
        } else if (B.arg == 1) {
            int h = 15 + S.ene + S.lvl * 3;
            if (h > p_maxhp() - S.hp) h = p_maxhp() - S.hp;
            S.hp += h;
            bline(1, C_GREEN, L"생명력이 %d 회복되었다!", h);
        } else {
            int base = B.arg == 2 ? 6 + S.ene / 2 + S.lvl * 2 : 10 + S.ene * 6 / 10 + S.lvl * 2;
            bline(1, C_WHITE, B.arg == 2 ? L"번개가 적들을 휩쓸었다!" : L"얼음 폭풍이 몰아쳤다!");
            for (i = 0; i < B.n; i++) if (B.m[i].alive) {
                dmg_mon(i, base + rr(0, 4), i);
                if (!B.m[i].alive) kills++;
                else if (B.arg == 3 && chance(60)) B.m[i].frozen = 2;
            }
            if (kills) bline(2, C_WHITE, L"%d마리를 쓰러뜨렸다!", kills);
            else if (B.arg == 3) bline(2, rgb(140, 200, 255), L"적들이 얼어붙었다!");
        }
    } else if (B.act == ACT_POTION) {
        wchar_t buf[52], nm[40];
        item_name(&S.belt[B.arg], nm, 40);
        drink(&S.belt[B.arg], buf, 52);
        snd_play(SX_DRINK);
        bline(0, C_WHITE, L"%ls%ls %ls%ls 마셨다.", P, JUN(P), nm, JUL(nm));
        bline(1, C_GREEN, L"%ls", buf);
    } else if (B.act == ACT_DEFEND) {
        bline(0, C_WHITE, L"%ls%ls 몸을 지키고 있다.", P, JUN(P));
    } else if (B.act == ACT_FLEE) {
        int i, ag = 0, c;
        bline(0, C_WHITE, L"%ls%ls 도망쳤다!", P, JUN(P));
        if (B.boss) { bline(1, C_RED, L"그러나 도망칠 수 없다!"); return; }
        for (i = 0; i < B.n; i++) if (B.m[i].alive && B.m[i].agi > ag) ag = B.m[i].agi;
        c = clampi(55 + (p_dex() - ag) * 3, 25, 90);
        if (chance(c)) B.fled = 1;
        else bline(1, C_WHITE, L"그러나 돌아갈 길이 막혀 있었다!");
    }
}

static void mon_act(int i)
{
    Mon *m = &B.m[i];
    const wchar_t *P = L"방랑자";
    int d, hit, vic = 0;
    bclear();
    m->anim = 1; m->at = 0;
    if (!m->frozen) snd_play_v(SX_GROWL, 200);
    if (B.np > 1 && chance(50)) {
        int k, c = 0, list[4];
        for (k = 1; k < B.np; k++) if (B.pm[k].hp > 0) list[c++] = k;
        if (c) vic = list[rr(0, c - 1)];
    }
    if (vic) {
        PMember *a = &B.pm[vic];
        const wchar_t *an = ALLYN[a->role];
        if (m->frozen) { bline(0, rgb(140, 200, 255), L"%ls%ls 얼어붙어 움직이지 못한다!", m->name, JUN(m->name)); m->frozen--; return; }
        bline(0, C_WHITE, L"%ls의 공격!", m->name);
        d = m->atk * rr(85, 115) / 100 - a->def / 2;
        if (d < 1) d = 1;
        a->hp -= d; a->anim = 2; a->at = 0; a->flash = 8;
        snd_play(SX_HIT);
        if (a->hp <= 0) { a->hp = 0; a->anim = 3; a->at = 0; bline(1, C_RED, L"%ls%ls 쓰러졌다!", an, JI(an)); }
        else bline(1, C_RED, L"%ls%ls %d의 피해를 입었다!", an, JUN(an), d);
        B.shake = 6;
        return;
    }
    if (m->frozen) {
        bline(0, rgb(140, 200, 255), L"%ls%ls 얼어붙어 움직이지 못한다!", m->name, JUN(m->name));
        m->frozen--;
        return;
    }
    if (m->spec == SPC_DRAIN && chance(30) && S.mp > 0) {
        int dr = clampi(4 + S.floor, 1, S.mp);
        S.mp -= dr;
        bline(0, C_WHITE, L"%ls%ls 차가운 손을 뻗었다!", m->name, JI(m->name));
        bline(1, C_BLUE, L"%ls의 마나를 %d 빨아들였다!", P, dr);
        return;
    }
    if (m->spec == SPC_FIRE && chance(30)) {
        d = m->atk * 12 / 10 + rr(0, 3) - p_def() / 4;
        snd_play(SX_FIRE);
        bline(0, C_WHITE, L"%ls%ls 화염을 내뿜었다!", m->name, JI(m->name));
    } else if (m->spec == SPC_BOSS && chance(25)) {
        d = m->atk * 17 / 10 - p_def() / 2;
        bline(0, C_RED, L"%ls의 무시무시한 일격!", m->name);
    } else {
        bline(0, C_WHITE, L"%ls의 공격!", m->name);
        hit = clampi(85 + m->agi - p_dex() / 2, 55, 95);
        if (!chance(hit)) { snd_play(SX_MISS); bline(1, C_WHITE, L"%ls%ls 재빨리 몸을 피했다!", P, JUN(P)); return; }
        d = m->atk * rr(85, 115) / 100 - p_def() / 2;
    }
    if (d < 1) d = 1;
    if (B.defend) { d /= 2; if (d < 1) d = 1; }
    S.hp -= d;
    if (S.hp < 0) S.hp = 0;
    snd_play(SX_HIT); snd_play(S.hp > 0 ? SX_HURT : SX_DEATH);
    bline(1, C_RED, L"%ls%ls %d의 피해를 입었다!", P, JUN(P), d);
    B.shake = 8; B.redflash = 4;
    B.pm[0].anim = S.hp > 0 ? 2 : 3; B.pm[0].at = 0; B.pm[0].flash = 8;
}

static void level_check(void)
{
    while (S.exp >= exp_next(S.lvl)) {
        int k;
        S.exp -= exp_next(S.lvl);
        S.lvl++;
        S.pts += 5;
        S.hp = p_maxhp(); S.mp = p_maxmp();
        snd_play(SX_LVUP);
        bres(C_YEL, L"레벨이 올랐다! 레벨 %d", S.lvl);
        bres(C_YEL, L"능력치 포인트 5를 얻었다.");
        for (k = 0; k < 4; k++) if (SK[k].lvl == S.lvl) bres(C_BLUE, L"새 마법을 익혔다: %ls", SK[k].n);
    }
}

static void loot_line(const Item *it)
{
    wchar_t nm[40];
    item_name(it, nm, 40);
    if (give_item(it)) bres(rar_col(it), L"%ls%ls 얻었다!", nm, JUL(nm));
    else bres(C_GRAY, L"가방이 가득 차 %ls%ls 두고 왔다.", nm, JUL(nm));
}

static void victory(void)
{
    int i, ex = 0, gd = 0;
    B.nres = 0; B.resi = 0;
    for (i = 0; i < B.n; i++) { ex += B.m[i].exp; gd += B.m[i].gold * rr(70, 130) / 100; }
    bres(C_WHITE, B.boss ? L"강적을 물리쳤다!" : L"괴물들을 물리쳤다!");
    bres(C_WHITE, L"경험치 %d, 금화 %d 획득", ex, gd);
    S.exp += ex; S.gold += gd;
    snd_play(SX_COINS);
    for (i = 0; i < B.n; i++) {
        Mon *m = &B.m[i];
        int c = (m->boss || m->uniq) ? 100 : m->champ ? 60 : 18;
        if (chance(c)) {
            Item it;
            gen_item(&it, S.floor + (m->boss ? 2 : m->uniq ? 1 : 0), m->boss ? 400 : m->uniq ? 250 : m->champ ? 150 : 0);
            loot_line(&it);
        } else if (chance(18)) {
            Item it;
            make_potion(&it, chance(60) ? IK_HP : chance(85) ? IK_MP : IK_RJ);
            loot_line(&it);
        }
    }
    if (B.boss) { S.bossdone = 1; bres(C_GOLD, L"아래로 향하는 길이 열렸다."); }
    level_check();
    bclear();
    for (i = 0; i < 3 && i < B.nres; i++) B.ln[i] = B.res[i];
    B.resi = 3;
    B.phase = BP_RESULT;
}

static void story_play(int id, int then);
static int story_seen(int id);
static void end_battle(void)
{
    mode = M_EXPLORE;
    showmap = 0;
    dirty = 1;
    if (B.boss && S.bossdone) {
        int a = CA->act;
        if (a == 4) story_play(14, 0);
        else if (!story_seen(a * 3 + 2)) story_play(a * 3 + 2, 0);
    }
}

static void begin_round(void)
{
    int i, pfirst = 1;
    B.no = 0;
    for (i = 0; i < B.n; i++) if (B.m[i].alive && B.m[i].agi > p_dex() + 5 && chance(30)) pfirst = 0;
    if (pfirst) B.order[B.no++] = -1;
    for (i = 1; i < B.np; i++) if (B.pm[i].hp > 0) B.order[B.no++] = -1 - i;
    for (i = 0; i < B.n; i++) if (B.m[i].alive) B.order[B.no++] = i;
    if (!pfirst) B.order[B.no++] = -1;
    B.defend = (B.act == ACT_DEFEND);
    B.oi = 0; B.wait = 0;
    B.phase = BP_EXEC;
}

static int first_alive(void);
/* start the visible action of a party member; the effect lands when the windup ends */
static void party_windup(int pi)
{
    PMember *p = &B.pm[pi];
    int act, tgt;
    if (pi == 0) { act = B.act; tgt = B.tgt; if (!B.m[tgt].alive) tgt = first_alive(); B.tgt = tgt; }
    else {
        /* simple AI */
        int k, low = 0, lr = 256;
        tgt = first_alive();
        for (k = 0; k < B.n; k++) if (B.m[k].alive && B.m[k].hp < B.m[tgt].hp) tgt = k;
        for (k = 0; k < B.np; k++) if (pm_alive(k)) { int r = pm_hp(k) * 256 / (pm_maxhp(k) ? pm_maxhp(k) : 1); if (r < lr) { lr = r; low = k; } }
        if (p->role == PR_PRIEST && lr < 150 && p->mp >= 6) act = 10 + low;           /* heal */
        else if (p->role == PR_PRIEST) act = 30;                                         /* holy bolt */
        else if (p->role == PR_BARB && chance(25)) act = 31;                             /* whirlwind */
        else act = ACT_ATTACK;
        B.aact[pi] = act; B.atgt[pi] = tgt;
    }
    B.windup = 1;
    p->tgt = tgt; p->at = 0;
    if (act == ACT_ATTACK) { p->anim = 1; B.wait = 7; snd_play_v(SX_SWING, 180); }
    else if (act == ACT_DEFEND || act == ACT_FLEE) { p->anim = 0; B.wait = 0; }
    else {
        p->anim = 4; B.wait = 8;
        if (pi == 0 && act == ACT_SKILL) fx_start(B.arg == 0 ? FX_FIRE : B.arg == 1 ? FX_HEAL : B.arg == 2 ? FX_BOLT : FX_FROST, 0, B.arg == 1 ? 0 : tgt);
        else if (pi == 0 && act == ACT_POTION) fx_start(FX_POTION, 0, 0);
        else if (act >= 10 && act < 14) fx_start(FX_HEAL, pi, act - 10);
        else if (act == 30) fx_start(FX_HOLY, pi, tgt);
        else if (act == 31) { p->anim = 1; fx_start(FX_WHIRL, pi, tgt); }
    }
}

static void dmg_mon(int i, int d, int slot);
static void ally_act(int pi)
{
    PMember *p = &B.pm[pi];
    const wchar_t *an = ALLYN[p->role];
    int act = B.aact[pi], t = B.atgt[pi], d;
    bclear();
    if (!B.m[t].alive) t = first_alive();
    if (act >= 10 && act < 14) {
        int k = act - 10, h = p->maxmp / 2 + 10 + S.lvl * 2;
        p->mp -= 6;
        if (k == 0) { h = clampi(h, 0, p_maxhp() - S.hp); S.hp += h; }
        else { PMember *q = &B.pm[k]; h = clampi(h, 0, q->maxhp - q->hp); q->hp += h; }
        snd_play(SX_HEAL);
        bline(0, C_WHITE, L"%ls%ls 치유의 기도를 올렸다!", an, JI(an));
        bline(1, C_GREEN, L"%ls의 생명력이 %d 회복되었다!", ALLYS[B.pm[k].role], h);
        return;
    }
    if (act == 31) {
        int k, kills = 0;
        snd_play(SX_SWING); snd_play(SX_HIT);
        bline(0, C_WHITE, L"%ls의 회전베기!", an);
        for (k = 0; k < B.n; k++) if (B.m[k].alive) { dmg_mon(k, clampi(p->atk * rr(70, 100) / 100 - B.m[k].def / 3, 1, 9999), k); if (!B.m[k].alive) kills++; }
        if (kills) bline(1, C_WHITE, L"%d마리를 쓰러뜨렸다!", kills);
        return;
    }
    if (act == 30) {
        d = p->atk * rr(100, 140) / 100;
        snd_play(SX_SHRINE);
        bline(0, C_WHITE, L"%ls%ls 성스러운 빛을 내렸다!", an, JI(an));
    } else {
        d = p->atk * rr(85, 120) / 100 - B.m[t].def / 2;
        if (p->role == PR_BARB) d = d * 14 / 10;
        snd_play(SX_HIT);
        bline(0, C_WHITE, L"%ls의 공격!", an);
    }
    if (d < 1) d = 1;
    bline(1, C_WHITE, L"%ls에게 %d의 피해!", B.m[t].name, d);
    dmg_mon(t, d, 0);
    if (p->role == PR_ROGUE && B.m[t].alive && chance(30)) {
        int d2 = clampi(p->atk * rr(60, 90) / 100 - B.m[t].def / 2, 1, 9999);
        dmg_mon(t, d2, 1);
        bline(2, C_YEL, L"연속 공격! %d의 추가 피해!", d2);
    }
    if (!B.m[t].alive && !B.ln[2].t[0]) bline(2, C_WHITE, L"%ls%ls 쓰러뜨렸다!", B.m[t].name, JUL(B.m[t].name));
}

static void battle_tick(void)
{
    if (B.phase == BP_INTRO) {
        if (B.wait > 0) B.wait--;
        if (B.wait == 0) { B.phase = BP_CMD; B.cmd = 0; }
        return;
    }
    if (B.phase != BP_EXEC) return;
    if (B.wait > 0) { B.wait--; return; }
    if (B.fled) { addlog(C_WHITE, L"무사히 도망쳤다."); end_battle(); return; }
    if (S.hp <= 0) {
        bclear();
        bline(0, C_RED, L"방랑자는 쓰러졌다...");
        B.phase = BP_DEAD;
        plat_delsave();
        has_save = 0;
        return;
    }
    if (alive_count() == 0) { victory(); return; }
    if (B.oi >= B.no) { B.phase = BP_CMD; B.defend = 0; return; }
    {
        int a = B.order[B.oi];
        if (a < 0) {
            int pi = -a - 1;
            if (pi > 0 && B.pm[pi].hp <= 0) { B.oi++; return; }
            if (!B.windup) { party_windup(pi); return; }
            B.windup = 0; B.oi++;
            if (pi == 0) player_act(); else ally_act(pi);
            B.wait = 16;
            return;
        }
        B.oi++;
        if (B.m[a].alive) mon_act(a);
        else return;
        B.wait = 15;
    }
}

/* ================================================================ */
/*  explore actions                                                 */
/* ================================================================ */
static void new_game(void)
{
    memset(&S, 0, sizeof(S));
    S.magic = SAVE_MAGIC; S.ver = SAVE_VER;
    S.lvl = 1; S.str = 10; S.dex = 10; S.vit = 10; S.ene = 10;
    S.floor = 1; S.maxfloor = 1;
    S.eq[0].kind = IK_W; S.eq[0].base = 0; S.eq[0].lo = 2; S.eq[0].hi = 5;
    S.eq[2].kind = IK_A; S.eq[2].base = 8; S.eq[2].def = 2;
    make_potion(&S.belt[0], IK_HP); make_potion(&S.belt[1], IK_HP); make_potion(&S.belt[2], IK_MP);
    S.hp = p_maxhp(); S.mp = p_maxmp();
    S.shopseed = rnd();
    gen_floor();
    memset(elog, 0, sizeof(elog));
    addlog(C_GOLD, L"제1막 - 새벽 야영지");
    addlog(C_WHITE, L"마을 사람들과 이야기를 나눠 보자.");
    mode = M_EXPLORE; showmap = 0;
    camp_arrival();
}

static void save_game(void) { S.rng = rs; plat_save(&S, sizeof(S)); has_save = 1; }

static void descend(void)
{
    int cyc0, cyc1;
    area_info(S.floor, 0, &cyc0);
    snd_play(SX_PORTAL);
    S.floor++;
    if (S.floor > S.maxfloor) S.maxfloor = S.floor;
    area_info(S.floor, 0, &cyc1);
    gen_floor();
    memset(elog, 0, sizeof(elog));
    if (cyc1 != cyc0) addlog(C_RED, L"난이도 상승: %ls", DIFFN[cyc1 > 2 ? 2 : cyc1]);
    if (area_of(S.floor) != area_of(S.floor - 1)) {
        if (CA->camp) addlog(C_GOLD, L"%ls - %ls", ACTN[CA->act], CA->n);
        else addlog(C_GOLD, L"새로운 지역: %ls", CA->n);
    } else addlog(C_GOLD, L"%ls, 더 깊은 곳으로...", CA->n);
    if (is_boss_floor(S.floor)) addlog(C_RED, L"강대한 기운이 느껴진다...");
    save_game();
    camp_arrival();
}

static void item_details(const Item *it, int x, int y);
static void list_window(int x, int y, int w, const wchar_t **items, const int *en, int n, int cur, int idbase, int lh);
/* ================================================================ */
/*  STORY, TOWNS (NPCs, trade, inn), WAYPOINTS, TOWN PORTAL         */
/* ================================================================ */
typedef struct { const wchar_t *who; const wchar_t *txt; } Page;
#define NARR 0

static const Page SC0[] = {
    { L"순례단장 카엘", L"깨어났군, 방랑자.\n사흘 전, 언덕 위 수도원에서\n붉은 빛이 솟아올랐다." },
    { L"순례단장 카엘", L"그날 밤부터 죽은 자들이\n무덤을 박차고 걷기 시작했지.\n순례단도 절반을 잃었다." },
    { L"순례단장 카엘", L"수도원 지하에는\n'심연의 봉인'이 잠들어 있었다.\n누군가 그것을 깨뜨린 거다." },
    { L"순례단장 카엘", L"자네 손등의 그 표식...\n봉인을 지키던 자들의 것과 같군." },
    { L"순례단장 카엘", L"부탁하네. 들판을 지나\n수도원으로 가 주게.\n여관과 상인들도 둘러보고 가게." },
    { 0, 0 } };
static const Page SC1[] = {
    { L"피의 군주 바르가스", L"봉인의 표식을 지닌 자여...\n네 피로 문을 활짝 열어주마!" },
    { L"???", L"기다려요!" },
    { L"수도사 엘레나", L"수도원의 마지막 수도사,\n엘레나예요. 저 악마가\n형제들을 모두 죽였어요." },
    { L"수도사 엘레나", L"혼자서는 무리예요.\n빛의 이름으로, 함께 싸우겠어요!" },
    { 0, 0 } };
static const Page SC2[] = {
    { L"수도사 엘레나", L"바르가스는 문지기에 불과했어요.\n그가 지키던 이것은...\n봉인의 파편!" },
    { L"수도사 엘레나", L"파편은 모두 다섯 개.\n하나는 남쪽 사막의 무덤에\n묻혀 있다고 전해져요." },
    { L"수도사 엘레나", L"저는 형제들을 묻어 줘야 해요.\n빛이 당신과 함께하길." },
    { NARR, L"봉인의 파편을 손에 넣었다. (1/5)\n길을 따라 남쪽 사막으로 향하자." },
    { 0, 0 } };
static const Page SC3[] = {
    { L"상단주 하심", L"모래바람 야영지에 온 걸\n환영하오, 이방인." },
    { L"상단주 하심", L"사막의 무덤에서\n'모래의 여왕'이 깨어났소.\n캐러밴이 셋이나 사라졌지." },
    { L"상단주 하심", L"여왕은 반짝이는 돌 하나를\n품고 잠들어 있었다더군.\n당신이 찾는 것 아니오?" },
    { 0, 0 } };
static const Page SC4[] = {
    { L"모래의 여왕 세케트", L"산 자의 냄새...\n내 아이들이 굶주렸다.\n너는 좋은 둥지가 되겠구나." },
    { L"추적자 카심", L"그 여왕에게 동생을 잃었소.\n추적자 카심, 내 칼도 보태지." },
    { 0, 0 } };
static const Page SC5[] = {
    { L"추적자 카심", L"두 번째 파편이군.\n이상하오... 파편끼리\n서로를 부르는 것 같소." },
    { L"추적자 카심", L"동쪽 정글에서\n같은 울림이 느껴지오.\n복수는 끝났소. 행운을 비오." },
    { NARR, L"봉인의 파편을 손에 넣었다. (2/5)\n동쪽 정글로 향하자." },
    { 0, 0 } };
static const Page SC6[] = {
    { L"선교사 오웬", L"이 정글 깊은 곳에\n옛 왕국의 신전이 있습니다." },
    { L"선교사 오웬", L"신전 아래 하수도는\n뼈의 왕 모르데크의 무덤.\n그가 군대를 일으키고 있어요." },
    { L"선교사 오웬", L"부디 조심하십시오.\n그곳의 물은 피처럼 붉습니다." },
    { 0, 0 } };
static const Page SC7[] = {
    { L"뼈의 왕 모르데크", L"천 년의 왕좌 앞에\n감히 고개를 드는가.\n네 뼈도 내 군대에 더해주마." },
    { L"야만용사 고룸", L"하하! 뼈다귀 왕이라니!\n이 야만용사 고룸의 도끼가\n마침 심심하던 참이다!" },
    { 0, 0 } };
static const Page SC8[] = {
    { L"야만용사 고룸", L"세 번째 파편이다!\n그런데 이놈, 점점 뜨거워지는군.\n마치 무언가가 부르는 것처럼." },
    { L"야만용사 고룸", L"지옥의 문이 열렸다는\n소문이 돌고 있다.\n가 보라고, 방랑자!" },
    { NARR, L"봉인의 파편을 손에 넣었다. (3/5)\n지옥문 요새로 향하자." },
    { 0, 0 } };
static const Page SC9[] = {
    { L"타락한 천사 이시엘", L"나는 한때 봉인을 지키던\n천사였다. 그리고 실패했지." },
    { L"타락한 천사 이시엘", L"그대의 표식은 수호자의 증표.\n그대가 마지막 수호자다." },
    { L"타락한 천사 이시엘", L"공포의 화신이 파편을 노린다.\n다섯이 모두 모이면\n심연이 세상을 삼킬 것이다." },
    { 0, 0 } };
static const Page SC10[] = {
    { L"공포의 화신", L"수호자의 핏줄...\n마침내 제 발로 왔구나.\n네 공포를 먹고 나는 완성된다!" },
    { L"수도사 엘레나", L"늦지 않았네요, 방랑자!" },
    { L"추적자 카심", L"빚은 갚는 법이지.\n이번엔 우리가 함께요." },
    { 0, 0 } };
static const Page SC11[] = {
    { L"타락한 천사 이시엘", L"네 번째 파편...\n마지막 하나는 북쪽 산맥,\n세계석 성채에 있다." },
    { L"타락한 천사 이시엘", L"그곳을 지키는 것은\n심연의 파수꾼.\n수호자들이 만든 수호상이었지." },
    { NARR, L"봉인의 파편을 손에 넣었다. (4/5)\n북쪽 산맥으로 향하자." },
    { 0, 0 } };
static const Page SC12[] = {
    { L"장로 바르크", L"세계석은 이 땅의 심장이오.\n그 심장이 검게 물들고 있소." },
    { L"장로 바르크", L"파수꾼은 원래 세계석을\n지키던 수호상이었소.\n심연이 그것을 삼켜 버렸지." },
    { 0, 0 } };
static const Page SC13[] = {
    { L"심연의 파수꾼", L"...수호자 확인.\n봉인 해제 절차... 거부.\n침입자를 제거한다." },
    { L"야만용사 고룸", L"다들 모였군! 가자!" },
    { L"수도사 엘레나", L"빛이 우리를 지켜 줄 거예요." },
    { 0, 0 } };
static const Page SC14[] = {
    { NARR, L"다섯 개의 파편이 하나가 되자\n손등의 표식이 타오르듯 빛났다." },
    { NARR, L"방랑자는 마침내 기억해 냈다.\n천 년 전, 첫 번째 봉인을\n만든 이가 바로 자신이었음을." },
    { L"수도사 엘레나", L"봉인이... 다시 닫혀요!" },
    { NARR, L"심연은 다시 잠들었다.\n그러나 심연은 결코\n완전히 잠들지 않는다..." },
    { NARR, L"- 악몽 난이도가 열렸다 -\n더 깊은 어둠이 기다린다." },
    { 0, 0 } };
static const Page *SCENES[15] = { SC0, SC1, SC2, SC3, SC4, SC5, SC6, SC7, SC8, SC9, SC10, SC11, SC12, SC13, SC14 };

static const wchar_t *NPCN[5][4] = {
    { L"순례단장 카엘", L"약초상 미라", L"대장장이 브론", L"여관 주인 토마" },
    { L"상단주 하심", L"향료상 라일라", L"무기상 자파르", L"여관 주인 아미르" },
    { L"선교사 오웬", L"주술사 마쿠", L"용병대장 렌", L"여관 주인 니나" },
    { L"타락한 천사 이시엘", L"망각의 상인", L"지옥 대장장이 하드락", L"쉼터지기 오린" },
    { L"장로 바르크", L"약제사 일바", L"룬 대장장이 토르겐", L"산장 주인 프레야" },
};
static const wchar_t *RUMOR[5][3] = {
    { L"수도원 묘지에는 머리 없는\n시체가 걸어 다닌다더군.", L"약초상 미라의 물약은\n비싸지만 효과는 확실하지.", L"검은 숲의 늑대들은\n피 냄새를 맡으면 미쳐 버려." },
    { L"사막의 모래 벌레는\n땅이 울리면 솟아오른다오.", L"무덤의 미라에게 맞으면\n마나가 빠져나간다더군.", L"왕의 무덤엔 금이 넘친다지만\n돌아온 자는 없지." },
    { L"하수도 살덩이는 무엇이든\n먹어 치운다오. 사람도.", L"늪지 주술사는 불을\n다룬다니 조심하시오.", L"신전의 해골들은 아직도\n왕을 지키고 있대요." },
    { L"이곳에선 잠들면\n악몽이 현실이 된다오.", L"지옥 사냥개는 불을 뿜으니\n회복 물약을 챙기시오.", L"공포의 화신은 여러 개의\n머리로 비명을 지른다지." },
    { L"얼음 망령은 마나를\n얼려 빼앗아 간다오.", L"세계석 성채의 룬은\n아직 희미하게 빛나고 있소.", L"눈보라 속에서 야수가\n울부짖는 소리를 들었소." },
};
static const wchar_t *GREET[5][4] = {
    { L"", L"\"상처엔 약초가 최고지.\"", L"\"좋은 강철은 목숨을 구하지.\"", L"\"어서 오게, 따뜻한 수프가 있네.\"" },
    { L"", L"\"사막을 건너려면 물약이 필요하오.\"", L"\"자파르의 칼은 모래도 벤다오.\"", L"\"오아시스의 물은 공짜라오.\"" },
    { L"", L"\"정글의 독엔 이 약이 듣지.\"", L"\"용병은 장비가 생명이오.\"", L"\"모기장 친 침대가 있어요.\"" },
    { L"", L"\"...값은 기억으로 치르겠나?\"", L"\"지옥불로 벼린 칼이다.\"", L"\"여기가 마지막 쉼터요.\"" },
    { L"", L"\"추위엔 이 물약이 좋아요.\"", L"\"룬이 새겨진 무기를 보시오.\"", L"\"벽난로 곁으로 오시오.\"" },
};

/* ---- story scene player ---- */
static int scn = -1, spage = 0, spend = 0;      /* spend: 1 = boss fight after the scene */
static int story_seen(int id) { return (S.story >> id) & 1; }
static void story_play(int id, int then)
{
    if (id < 0 || id > 14) return;
    S.story |= 1u << id;
    scn = id; spage = 0; spend = then;
    mode = M_STORY;
    dirty = 1;
}
static void story_next(void)
{
    spage++;
    if (!SCENES[scn][spage].txt) {
        int then = spend, id = scn;
        scn = -1;
        mode = M_EXPLORE;
        if (then == 1) start_battle(1);
        if (id == 14) addlog(C_RED, L"악몽 난이도가 열렸다.");
        save_game();
    }
    dirty = 1;
}
static void camp_arrival(void)
{
    if (CA->camp && !story_seen(CA->act * 3)) story_play(CA->act * 3, 0);
}
static int shards(void) { int a, n = 0; for (a = 0; a < 5; a++) if (story_seen(a * 3 + 2)) n++; return n; }

/* draw a page of dialogue (D2 style dark box with gold frame) */
static void render_story(void)
{
    const Page *pg;
    const wchar_t *p;
    int y, h = 86, y0 = PY - h - 2;
    if (scn < 0) return;
    pg = &SCENES[scn][spage];
    darken(0, VY, GW, PY - VY, 150);
    fill(4, y0, GW - 8, h, rgb(6, 4, 4));
    goldframe(4, y0, GW - 8, h);
    if (pg->who) txt(12, y0 + 6, pg->who, C_GOLD, T_SHADOW);
    y = y0 + (pg->who ? 24 : 14);
    for (p = pg->txt; *p;) {
        wchar_t ln[64];
        int n = 0;
        while (*p && *p != L'\n' && n < 63) ln[n++] = *p++;
        ln[n] = 0;
        if (*p == L'\n') p++;
        txt(12, y, ln, pg->who ? C_WHITE : rgb(220, 200, 160), T_SHADOW);
        y += 15;
    }
    if (tickc & 8) txt(GW - 16, y0 + h - 16, L"▼", C_GOLD, 0);
    addbtn(0, 0, GW, GH, B_ANY);
}

/* ---- waypoints / travel ---- */
static int area_first_floor(int a, int cyc)
{
    int i, f = 1;
    area_info(1, 0, 0);
    for (i = 0; i < a; i++) f += AREAS[i].floors;
    return cyc * CYCLE_LEN + f;
}
static int cur_cycle(void) { int c; area_info(S.floor, 0, &c); return c; }
static void travel_to_floor(int f, const wchar_t *how)
{
    snd_play(SX_PORTAL);
    S.floor = f;
    if (S.floor > S.maxfloor) S.maxfloor = S.floor;
    gen_floor();
    memset(elog, 0, sizeof(elog));
    addlog(rgb(140, 190, 255), L"%ls", how);
    if (CA->camp) addlog(C_GOLD, L"%ls - %ls", ACTN[CA->act], CA->n);
    else addlog(C_GOLD, L"%ls", CA->n);
    mode = M_EXPLORE;
    save_game();
    camp_arrival();
}
static int wlist[NAREA_MAX], wn, wsel, wscroll;
static void warp_open(void)
{
    int a;
    wn = 0;
    for (a = 0; a < NAREA; a++) if ((S.wp >> a) & 1) wlist[wn++] = a;
    wsel = 0; wscroll = 0;
    for (a = 0; a < wn; a++) if (wlist[a] == area_of(S.floor)) wsel = a;
    if (wsel > 5) wscroll = wsel - 5;
    mode = M_WARP;
}
static void warp_go(int k)
{
    int a;
    if (k < 0 || k >= wn) return;
    a = wlist[k];
    if (a == area_of(S.floor) && CA->camp) { mode = M_EXPLORE; return; }
    travel_to_floor(area_first_floor(a, cur_cycle()), L"길잡이석의 빛이 몸을 감쌌다...");
}
static void render_warp(void)
{
    int i, rows = 7, x = 12, y = VY + 8, w = GW - 24, h = rows * 18 + 26;
    fill(x, y, w, h, rgb(6, 4, 4));
    goldframe(x, y, w, h);
    txt(GW / 2, y + 5, L"길잡이석 - 이동할 곳", C_GOLD, T_CENTER | T_SHADOW);
    for (i = 0; i < rows && wscroll + i < wn; i++) {
        int k = wscroll + i, a = wlist[k], ry = y + 22 + i * 18;
        if (k == wsel) fill(x + 3, ry - 1, w - 6, 17, rgb(60, 46, 26));
        txtf(x + 8, ry + 1, AREAS[a].camp ? C_GOLD : C_WHITE, T_SHADOW, L"%ls  %ls", ACTN[AREAS[a].act], AREAS[a].n);
        addbtn(x, ry - 1, w, 17, B_LIST + i);
    }
    if (wscroll > 0) txt(GW - 20, y + 20, L"▲", C_GOLD, 0);
    if (wscroll + rows < wn) txt(GW - 20, y + h - 14, L"▼", C_GOLD, 0);
}

/* ---- town portal ---- */
static int camp_area_of_act(int act) { int a; for (a = 0; a < NAREA; a++) if (AREAS[a].camp && AREAS[a].act == act) return a; return 0; }
static int use_portal_scroll(void)
{
    if (CA->camp) { wfmt(invmsg, 64, L"이곳은 이미 마을이다."); return 0; }
    S.tpfloor = S.floor;
    travel_to_floor(area_first_floor(camp_area_of_act(CA->act), cur_cycle()), L"귀환 두루마리를 펼쳤다. 차원문이 열린다...");
    return 1;
}

/* ---- item values & trading ---- */
static int item_value(const Item *it)
{
    int v, i;
    switch (it->kind) {
    case IK_HP: case IK_MP: return 25 + S.floor * 3;
    case IK_RJ: return 90 + S.floor * 6;
    case IK_TP: return 30 + S.floor * 2;
    case IK_NONE: return 0;
    }
    v = 10 + BASES[it->base].tier * 14 + (it->lo + it->hi) * 3 + it->def * 5;
    for (i = 0; i < it->naff; i++) v += it->av[i] * (aff_stat(it, i) == ST_HP || aff_stat(it, i) == ST_MP ? 3 : 9);
    if (it->rar == R_MAGIC) v = v * 16 / 10;
    else if (it->rar == R_RARE) v = v * 3;
    else if (it->rar == R_UNIQ) v = v * 6;
    if (it->pad) v = v * 13 / 10;
    return v;
}
enum { TN_GEN, TN_ARM, TN_WANDER };
static Item stock[10];
static int nstock, tnpc = -1, ttab, tsel, tscroll, stock_floor = -1, stock_npc = -1;
static wchar_t tmsg[64];
static int tsell[INV_N], ntsell;
static void build_sell(void) { int i; ntsell = 0; for (i = 0; i < INV_N; i++) if (S.inv[i].kind) tsell[ntsell++] = i; }
static void open_trade(int who)
{
    tnpc = who; ttab = 0; tsel = 0; tscroll = 0; tmsg[0] = 0;
    if (stock_floor != S.floor || stock_npc != who) {
        unsigned save = rs;
        int i;
        rs = (S.shopseed ^ (unsigned)(S.floor * 2654435761u) ^ (unsigned)(who * 7919)) | 1;
        nstock = 0;
        if (who == TN_ARM) {
            for (i = 0; i < 8; i++) { gen_item(&stock[nstock], S.floor + 2, 250); if (stock[nstock].kind == IK_R) gen_item(&stock[nstock], S.floor + 2, 250); nstock++; }
        } else {
            make_potion(&stock[nstock++], IK_HP);
            make_potion(&stock[nstock++], IK_MP);
            make_potion(&stock[nstock++], IK_RJ);
            if (who == TN_GEN) {
                memset(&stock[nstock], 0, sizeof(Item)); stock[nstock].kind = IK_TP; stock[nstock].base = (unsigned char)base_of_kind(IK_TP); nstock++;
                for (i = 0; i < 2; i++) { int t; for (t = 0; t < 30; t++) { gen_item(&stock[nstock], S.floor + 1, 200); if (stock[nstock].kind == IK_R) break; } nstock++; }
            }
        }
        rs = save;
        stock_floor = S.floor; stock_npc = who;
    }
    build_sell();
    mode = M_TRADE;
}
static int trade_count(void) { return ttab == 0 ? nstock : ntsell; }
static Item *trade_item(int k)
{
    if (ttab == 0) return k < nstock ? &stock[k] : 0;
    return k < ntsell ? &S.inv[tsell[k]] : 0;
}
static void trade_act(void)
{
    Item *it = trade_item(tsel);
    wchar_t nm[48];
    if (!it || !it->kind) return;
    item_name(it, nm, 48);
    if (ttab == 0) {
        int p = item_value(it);
        Item copy = *it;
        if (S.gold < p) { wfmt(tmsg, 64, L"금화가 부족하다."); return; }
        if (!give_item(&copy)) { wfmt(tmsg, 64, L"가방이 가득 찼다."); return; }
        S.gold -= p;
        snd_play(SX_TRADE);
        wfmt(tmsg, 64, L"%ls%ls 샀다. (-%d)", nm, JUL(nm), p);
        if (!IS_POT(it->kind) && it->kind != IK_TP) memset(it, 0, sizeof(Item));
        build_sell();
    } else {
        int p = item_value(it) / 4 + 1;
        S.gold += p;
        snd_play(SX_TRADE);
        wfmt(tmsg, 64, L"%ls%ls 팔았다. (+%d)", nm, JUL(nm), p);
        memset(it, 0, sizeof(Item));
        build_sell();
        if (tsel >= ntsell && tsel > 0) tsel--;
    }
}
static void trade_close(void)
{
    mode = M_EXPLORE;
    if (tnpc == TN_WANDER) { addlog(C_GRAY, L"상인이 어둠 속으로 사라졌다."); S.feat[S.py][S.px] = 0; }
}
static const wchar_t *trade_name(void)
{
    if (tnpc == TN_WANDER) return L"떠돌이 상인";
    return NPCN[CA->act][tnpc == TN_GEN ? 1 : 2];
}
static void render_trade(void)
{
    int i, y0 = VY, h = PY - VY, rows, ly = y0 + 52, dy = PY - 58, n = trade_count();
    stone(0, y0, GW, h, 34);
    goldframe(0, y0, GW, h);
    txt(6, y0 + 4, trade_name(), C_GOLD, T_SHADOW);
    txtf(GW - 6, y0 + 4, C_YEL, T_RIGHT | T_SHADOW, L"금화 %d", S.gold);
    sbutton(6, y0 + 19, 56, 17, L"구매", B_STAT, 1, ttab == 0);
    sbutton(64, y0 + 19, 56, 17, L"판매", B_STAT + 1, 1, ttab == 1);
    if (tmsg[0]) txt(6, y0 + 37, tmsg, C_YEL, T_SHADOW);
    else if (tnpc != TN_WANDER) txt(6, y0 + 37, GREET[CA->act][tnpc == TN_GEN ? 1 : 2], C_GRAY, 0);
    else txt(6, y0 + 37, L"\"살아남고 싶다면 사 가게나.\"", C_GRAY, 0);
    rows = (dy - ly) / 15;
    if (tsel < tscroll) tscroll = tsel;
    if (tsel >= tscroll + rows) tscroll = tsel - rows + 1;
    for (i = 0; i < rows && tscroll + i < n; i++) {
        int k = tscroll + i, ry = ly + i * 15;
        Item *it = trade_item(k);
        wchar_t nm[48];
        if (!it || !it->kind) { txt(10, ry + 1, L"(팔림)", C_GRAY, 0); continue; }
        if (k == tsel) fill(3, ry, GW - 6, 15, rgb(60, 46, 26));
        fill(6, ry + 1, 13, 13, rgb(14, 10, 8));
        item_icon(it, 6, ry + 1, 13);
        item_name(it, nm, 48);
        txt(24, ry + 1, nm, rar_col(it), T_SHADOW);
        txtf(GW - 8, ry + 1, C_YEL, T_RIGHT | T_SHADOW, L"%d", ttab == 0 ? item_value(it) : item_value(it) / 4 + 1);
        addbtn(3, ry, GW - 6, 15, B_SLOT + i);
    }
    if (!n) txt(GW / 2, ly + 20, ttab ? L"팔 물건이 없다." : L"물건이 다 팔렸다.", C_GRAY, T_CENTER);
    fill(4, dy, GW - 8, PY - dy - 4, rgb(0, 0, 0));
    rectl(4, dy, GW - 8, PY - dy - 4, rgb(50, 46, 40));
    {
        Item *it = trade_item(tsel);
        if (it && it->kind) item_details(it, 8, dy + 2);
    }
}
static void panel_trade(void)
{
    Item *it = trade_item(tsel);
    belt_slots(0);
    sbutton(XO + 73, PY + 36, 94, 28, ttab == 0 ? L"구매" : L"판매", B_USE, it && it->kind, 0);
    sbutton(XO + 73, PY + 66, 94, 28, L"닫기", B_CLOSE, 1, 0);
}

/* ---- inn / storyteller conversation ---- */
static int talk_who;                 /* 0 storyteller, 3 inn */
static wchar_t talkmsg[2][48];
static void talk_open(int who)
{
    talk_who = who; sel = 0;
    talkmsg[0][0] = talkmsg[1][0] = 0;
    if (who == 3) {
        const wchar_t *g = GREET[CA->act][3];
        wfmt(talkmsg[0], 48, L"%ls", g);
    } else wfmt(talkmsg[0], 48, L"\"무엇이 궁금한가, 방랑자?\"");
    mode = M_TALK;
}
static int talk_len(void) { return talk_who == 3 ? 3 : 2; }
static void talk_choose(int i)
{
    if (talk_who == 3) {
        if (i == 0) {
            S.hp = p_maxhp(); S.mp = p_maxmp();
            save_game();
            wfmt(talkmsg[0], 48, L"푹 쉬었다. 생명력과 마나가");
            wfmt(talkmsg[1], 48, L"모두 회복되었다. (저장됨)");
        } else if (i == 1) {
            const wchar_t *r = RUMOR[CA->act][rr(0, 2)], *p = r;
            int n = 0, l = 0;
            talkmsg[0][0] = talkmsg[1][0] = 0;
            while (*p && l < 2) {
                if (*p == L'\n') { talkmsg[l][n] = 0; l++; n = 0; p++; continue; }
                if (n < 47) talkmsg[l][n++] = *p;
                p++;
            }
            if (l < 2) talkmsg[l][n] = 0;
        } else mode = M_EXPLORE;
    } else {
        if (i == 0) {
            int a = CA->act;
            mode = M_EXPLORE;
            if (story_seen(a * 3 + 2) && a < 4) story_play(a * 3 + 2, 0);
            else story_play(a * 3, 0);
        } else mode = M_EXPLORE;
    }
}
static void render_talk(void)
{
    static const wchar_t *IN[3] = { L"휴식하기 (회복 + 저장)", L"소문 듣기", L"떠나기" };
    static const wchar_t *EL[2] = { L"이야기 듣기", L"떠나기" };
    int y0 = VY + 6;
    fill(8, y0, GW - 16, 52, rgb(6, 4, 4));
    goldframe(8, y0, GW - 16, 52);
    txt(16, y0 + 5, NPCN[CA->act][talk_who], C_GOLD, T_SHADOW);
    if (talkmsg[0][0]) txt(16, y0 + 21, talkmsg[0], C_WHITE, T_SHADOW);
    if (talkmsg[1][0]) txt(16, y0 + 35, talkmsg[1], C_WHITE, T_SHADOW);
    list_window(GW / 2 - 90, y0 + 60, 180, talk_who == 3 ? IN : EL, 0, talk_len(), sel, B_LIST, 20);
}

/* ---- NPC interaction (bumping into them) ---- */
static int is_npc(int f) { return f >= F_ELDER && f <= F_INN; }
static void npc_interact(int f)
{
    if (f == F_ELDER) talk_open(0);
    else if (f == F_INN) talk_open(3);
    else if (f == F_GEN) open_trade(TN_GEN);
    else if (f == F_ARM) open_trade(TN_ARM);
    dirty = 1;
}

static void on_feature(void)
{
    int f = S.feat[S.py][S.px];
    switch (f) {
    case F_CHEST: {
        int g = rr(4, 12) * S.floor;
        Item it;
        S.feat[S.py][S.px] = F_OPENCHEST;
        S.gold += g;
        snd_play(SX_CHEST); snd_play_v(SX_COINS, 180);
        addlog(C_GOLD, L"상자를 열었다. 금화 %d 획득.", g);
        if (chance(55)) gen_item(&it, S.floor, 60);
        else make_potion(&it, chance(65) ? IK_HP : chance(80) ? IK_MP : IK_RJ);
        {
            wchar_t nm[40];
            item_name(&it, nm, 40);
            if (give_item(&it)) addlog(rar_col(&it), L"%ls%ls 얻었다!", nm, JUL(nm));
            else addlog(C_GRAY, L"가방이 가득 찼다.");
        }
        break;
    }
    case F_SHRINE: {
        int r = rr(0, 4);
        S.feat[S.py][S.px] = 0;
        snd_play(SX_SHRINE);
        if (r == 0) { S.hp = p_maxhp(); S.mp = p_maxmp(); addlog(C_BLUE, L"축복의 성소: 모든 상처가 아물었다."); }
        else if (r == 1) { S.str++; addlog(C_BLUE, L"힘의 성소: 힘이 1 올랐다."); }
        else if (r == 2) { S.ene++; addlog(C_BLUE, L"비전의 성소: 에너지가 1 올랐다."); }
        else if (r == 3) { int g = 20 * S.floor; S.gold += g; addlog(C_BLUE, L"황금 성소: 금화 %d를 얻었다.", g); }
        else { S.hp -= S.hp / 4; addlog(C_RED, L"저주받은 성소: 생명력을 빼앗겼다!"); }
        addlog(C_GRAY, L"성소의 빛이 사라졌다.");
        break;
    }
    case F_FOUNTAIN:
        snd_play(SX_DRINK); snd_play_v(SX_HEAL, 160);
        S.hp = p_maxhp(); S.mp = p_maxmp();
        S.feat[S.py][S.px] = 0;
        addlog(rgb(120, 170, 255), L"맑은 샘물을 마셨다. 기운이 넘친다!");
        break;
    case F_MERCHANT:
        open_trade(TN_WANDER);
        break;
    case F_WAYPOINT:
        addlog(rgb(140, 190, 255), L"길잡이석이 빛난다.");
        snd_play(SX_PORTAL);
        warp_open();
        break;
    case F_PORTAL:
        if (S.tpfloor) {
            int f = S.tpfloor;
            S.tpfloor = 0;
            travel_to_floor(f, L"차원문을 통해 돌아왔다.");
        }
        break;
    case F_HEALER:
        snd_play(SX_HEAL);
        S.hp = p_maxhp(); S.mp = p_maxmp();
        addlog(rgb(255, 230, 160), L"치유사: \"빛이 그대를 지키기를.\"");
        addlog(C_GREEN, L"생명력과 마나가 모두 회복되었다.");
        break;
    case F_STAIRS:
        if (!S.bossdone) {
            int id = CA->act * 3 + 1;
            if (!story_seen(id)) story_play(id, 1);
            else start_battle(1);
            return;
        }
        mode = M_PROMPT; sel = 0;
        break;
    default:
        return;
    }
}

static void step(int dir)
{
    int nx = S.px + DX[dir], ny = S.py + DY[dir];
    if (solid(nx, ny)) { snd_play(SX_BUMP); addlog(C_GRAY, L"단단한 벽이 가로막고 있다."); return; }
    if (is_npc(S.feat[ny][nx])) { npc_interact(S.feat[ny][nx]); return; }
    S.px = nx; S.py = ny;
    snd_play_v(SX_STEP, 150);
    mark_seen();
    S.regen++;
    if ((S.regen & 1) == 0 && S.mp < p_maxmp()) S.mp++;
    if (S.regen % 5 == 0 && S.hp < p_maxhp()) S.hp++;
    if (S.feat[ny][nx] && S.feat[ny][nx] != F_OPENCHEST) { on_feature(); return; }
    if (CA->camp) return;
    S.steps++;
    if (S.steps > 3 && chance(clampi((S.steps - 3) * 3, 0, CA->layout == L_FIELD ? 26 : 22))) { S.steps = 0; start_battle(0); }
}

static void turn(int d) { S.dir = (S.dir + d) & 3; mark_seen(); }

/* ================================================================ */
/*  rendering: frame parts                                          */
/* ================================================================ */
static void orb(int cx, int cy, int r, int cur, int mx, int red)
{
    int x, y, lvl = mx > 0 ? cy + r - (2 * r * cur + mx - 1) / mx : cy + r;
    u16 dark = rgb(18, 12, 12);
    /* sculpted frame */
    fcircle(cx, cy, r + 4, rgb(16, 12, 10));
    fcircle(cx, cy, r + 3, rgb(110, 100, 86));
    fcircle(cx, cy, r + 2, rgb(46, 40, 34));
    for (y = -r; y <= r; y++) for (x = -r; x <= r; x++) {
        int dd = x * x + y * y, py = cy + y;
        if (dd > r * r) continue;
        if (py >= lvl) {
            int br = 256 - dd * 140 / (r * r);
            int sw = (int)(hsh((x + tickc / 4) / 3, (y - tickc / 3) / 3) & 31);
            int base = red ? 0 : 1;
            u16 c = base == 0 ? rgb(170 + sw, 14 + sw / 3, 10) : rgb(20 + sw / 3, 30 + sw / 2, 180 + sw);
            if (py == lvl) c = mix(c, rgb(255, 255, 255), 90);
            FB[py * GW + cx + x] = shade(c, br);
        } else {
            FB[py * GW + cx + x] = mix(dark, rgb(60, 60, 70), (r * r - dd) * 40 / (r * r));
        }
    }
    /* glass highlight */
    for (y = 0; y < r / 3; y++) for (x = 0; x < r / 2 - y; x++) {
        int px = cx - r / 2 + x + y / 2, py = cy - r / 2 - r / 6 + y;
        FB[py * GW + px] = mix(FB[py * GW + px], rgb(255, 255, 255), 60);
    }
}

static void render_topbar(void)
{
    stone(0, 0, GW, VY, 46);
    hline(0, GW - 1, VY - 1, rgb(150, 120, 64));
    {
        int idx, cyc;
        area_info(S.floor, &idx, &cyc);
        if (CA->camp) txtf(4, 2, C_GOLD, T_SHADOW, L"%ls", ACTN[CA->act]);
        else if (cyc) txtf(4, 2, C_RED, T_SHADOW, L"%ls %d층", DIFFN[cyc > 2 ? 2 : cyc], S.floor);
        else txtf(4, 2, C_GOLD, T_SHADOW, L"%d층", S.floor);
    }
    txtf(GW - 4, 2, C_YEL, T_RIGHT | T_SHADOW, L"금화 %d", S.gold);
    txt(GW / 2 + 6, 2, CA->n, rgb(200, 190, 170), T_CENTER | T_SHADOW);
}

static void render_msgs(void)
{
    int i;
    fill(0, MY, GW, 30, rgb(10, 8, 6));
    hline(0, GW - 1, MY, rgb(90, 66, 30));
    for (i = 0; i < 2; i++) if (elog[i].t[0]) txt(4, MY + 2 + i * 14, elog[i].t, i == 0 ? shade(elog[i].c, 170) : elog[i].c, T_SHADOW);
}

static void belt_slots(int active)
{
    int i;
    for (i = 0; i < 4; i++) {
        int x = XO + 74 + i * 23, y = PY + 9;
        fill(x, y, 22, 22, rgb(14, 10, 8));
        rectl(x, y, 22, 22, rgb(110, 86, 44));
        if (S.belt[i].kind) potion_icon(x + 2, y + 1, 19, S.belt[i].kind);
        if (active && S.belt[i].kind) addbtn(x, y, 22, 22, B_BELT + i);
    }
}

/* carved stone statue frame behind an orb (gargoyle wings / horns) */
static void orb_cradle(int cx, int cy, int right)
{
    int y, x, s = right ? 1 : -1;
    for (y = -34; y <= 36; y++) {
        int half = 34 + (y > 10 ? (y - 10) / 3 : 0);
        for (x = -half; x <= half; x++) {
            int px = cx + x, py = cy + y;
            unsigned h = hsh(px, py);
            int v = 34 + (int)(h & 7) + (x * s > 0 ? 8 : 0) - (y > 26 ? 10 : 0);
            if ((unsigned)px < GW && py < GH) FB[py * GW + px] = rgb(v + 2, v, v - 2);
        }
    }
    /* horn on the outer side */
    for (y = 0; y < 18; y++) {
        int x0 = cx - s * (30 + y / 2), w = 6 - y / 3;
        for (x = 0; x < w; x++) pset(x0 - s * x, cy - 30 - y + x / 2, rgb(72 - y, 62 - y, 50 - y));
    }
    /* claw at the bottom */
    for (x = -3; x <= 3; x++) { vline(cx + x * 7, cy + 30, cy + 36 - (x < 0 ? -x : x), rgb(60, 54, 46)); }
}

static void render_panel(void)
{
    int mh = p_maxhp(), mm = p_maxmp();
    stone(0, PY, GW, 120, 46);
    hline(0, GW - 1, PY, rgb(10, 8, 6));
    hline(0, GW - 1, PY + 1, rgb(120, 104, 80));
    hline(0, GW - 1, PY + 2, rgb(40, 34, 28));
    {
        int ol = 36 + XO / 2, orr = GW - 36 - XO / 2;     /* orbs sit between the centre block and the edges */
        orb_cradle(ol, PY + 48, 0);
        orb_cradle(orr, PY + 48, 1);
        orb(ol, PY + 48, 28, S.hp, mh, 1);
        orb(orr, PY + 48, 28, S.mp, mm, 0);
        txtf(ol, PY + 80, rgb(255, 160, 150), T_CENTER | T_SHADOW, L"%d/%d", S.hp, mh);
        txtf(orr, PY + 80, rgb(170, 180, 255), T_CENTER | T_SHADOW, L"%d/%d", S.mp, mm);
    }
    /* exp bar */
    {
        int w = 94 * S.exp / exp_next(S.lvl);
        fill(XO + 73, PY + 3, 94, 4, rgb(16, 12, 8));
        fill(XO + 73, PY + 3, w, 4, rgb(200, 170, 70));
    }
}

static void panel_explore(void)
{
    belt_slots(1);
    sbutton(XO + 73, PY + 36, 30, 28, L"◀", B_TL, 1, 0);
    sbutton(XO + 105, PY + 36, 30, 28, L"▲", B_FWD, 1, 0);
    sbutton(XO + 137, PY + 36, 30, 28, L"▶", B_TR, 1, 0);
    sbutton(XO + 73, PY + 66, 30, 28, L"가방", B_INV, 1, 0);
    sbutton(XO + 105, PY + 66, 30, 28, L"▼", B_BACK, 1, 0);
    sbutton(XO + 137, PY + 66, 30, 28, L"지도", B_MAP, 1, showmap);
    sbutton(4, PY + 96, 64, 20, S.pts ? L"캐릭터 +" : L"캐릭터", B_CHAR, 1, S.pts > 0 && (tickc & 8));
    sbutton(GW - 68, PY + 96, 64, 20, L"메뉴", B_MENU, 1, 0);
    txtf(GW / 2, PY + 99, C_GOLD, T_CENTER | T_SHADOW, L"레벨 %d", S.lvl);
}

static void list_window(int x, int y, int w, const wchar_t **items, const int *en, int n, int cur, int idbase, int lh)
{
    int i;
    dqwin(x, y, w, n * lh + 10);
    for (i = 0; i < n; i++) {
        u16 c = (en && !en[i]) ? C_GRAY : rgb(255, 255, 255);
        if (i == cur) txt(x + 7, y + 5 + i * lh + (lh - 12) / 2, L"▶", rgb(255, 255, 255), 0);
        txt(x + 20, y + 5 + i * lh + (lh - 12) / 2, items[i], c, 0);
        addbtn(x, y + 5 + i * lh, w, lh, idbase + i);
    }
}


/* ---- inventory screen ---- */
static const wchar_t *EQN[4] = { L"무기", L"투구", L"갑옷", L"반지" };
static const int EQMAP[4] = { 0, 1, 2, 3 }; /* display order -> eq index: 0 weapon,1 helm,2 armor,3 ring */

static int eq_slot_for(int kind) { return kind == IK_W ? 0 : kind == IK_H ? 1 : kind == IK_A ? 2 : kind == IK_R ? 3 : -1; }

static Item *isel_item(void) { return isel < 4 ? &S.eq[EQMAP[isel]] : &S.inv[isel - 4]; }

/* Diablo II style tooltip: centred lines, name in rarity colour, affixes in blue */
static void item_details(const Item *it, int x, int y)
{
    wchar_t nm[48], ln[64], st[32];
    int i, cx = GW / 2;
    (void)x;
    if (!it->kind) { txt(cx, y, L"(비어 있음)", C_GRAY, T_CENTER); return; }
    item_name(it, nm, 48);
    txt(cx, y, nm, rar_col(it), T_CENTER | T_SHADOW);
    y += 12;
    if (it->kind == IK_W) wfmt(st, 32, L"공격력: %d-%d", it->lo, it->hi);
    else if (it->kind == IK_A || it->kind == IK_H) wfmt(st, 32, L"방어력: %d", it->def);
    else if (it->kind == IK_R) st[0] = 0;
    else if (it->kind == IK_HP) wfmt(st, 32, L"생명력 45%% 회복");
    else if (it->kind == IK_MP) wfmt(st, 32, L"마나 55%% 회복");
    else if (it->kind == IK_TP) wfmt(st, 32, L"사용: 마을로 차원문을 연다");
    else wfmt(st, 32, L"생명력·마나 완전 회복");
    if (it->rar == R_RARE || it->rar == R_UNIQ) {
        wfmt(ln, 64, L"%ls", BASES[it->base].n);
        txt(cx, y, ln, rar_col(it), T_CENTER);
        y += 12;
        if (st[0]) { txt(cx, y, st, C_WHITE, T_CENTER); y += 12; }
    } else if (st[0]) { txt(cx, y, st, C_WHITE, T_CENTER); y += 12; }
    for (i = 0; i < it->naff; i += 2) {
        wchar_t a[32], b[32];
        wfmt(a, 32, L"+%d %ls", it->av[i], STN[aff_stat(it, i)]);
        if (i + 1 < it->naff) wfmt(b, 32, L",  +%d %ls", it->av[i + 1], STN[aff_stat(it, i + 1)]);
        else b[0] = 0;
        wfmt(ln, 64, L"%ls%ls", a, b);
        txt(cx, y, ln, rgb(110, 110, 255), T_CENTER);
        y += 12;
    }
}
static void render_inv(void)
{
    int i, y0 = VY, h = PY - VY;
    stone(0, y0, GW, h, 34);
    goldframe(0, y0, GW, h);
    for (i = 0; i < 4; i++) {
        int x = XO + 16 + i * 56, y = y0 + 16;
        txt(x + 18, y0 + 3, EQN[i], C_GOLD, T_CENTER | T_SHADOW);
        fill(x, y, 36, 36, rgb(14, 10, 8));
        rectl(x, y, 36, 36, isel == i ? rgb(255, 230, 120) : rgb(90, 84, 72));
        item_icon(&S.eq[EQMAP[i]], x + 3, y + 3, 30);
        addbtn(x, y, 36, 36, B_SLOT + i);
    }
    for (i = 0; i < INV_N; i++) {
        int x = XO + 12 + (i % 6) * 36, y = y0 + 58 + (i / 6) * 36;
        fill(x, y, 34, 34, rgb(14, 10, 8));
        fill(x, y, 34, 34, rgb(10, 10, 14));
        rectl(x, y, 34, 34, isel == i + 4 ? rgb(255, 230, 120) : rgb(62, 62, 72));
        item_icon(&S.inv[i], x + 2, y + 2, 30);
        addbtn(x, y, 34, 34, B_SLOT + 4 + i);
    }
    fill(4, y0 + 131, GW - 8, h - 135, rgb(0, 0, 0));
    rectl(4, y0 + 131, GW - 8, h - 135, rgb(50, 46, 40));
    item_details(isel_item(), 8, y0 + 133);
    if (invmsg[0]) txt(GW / 2, PY - 15, invmsg, C_YEL, T_CENTER | T_SHADOW);
}

static void panel_inv(void)
{
    Item *it = isel_item();
    const wchar_t *lab = L"사용";
    belt_slots(0);
    if (isel < 4) lab = L"해제";
    else if (it->kind >= IK_W && it->kind <= IK_R) lab = L"장착";
    sbutton(XO + 73, PY + 36, 46, 28, lab, B_USE, it->kind != 0, 0);
    sbutton(XO + 121, PY + 36, 46, 28, L"버리기", B_DROP, it->kind != 0 && isel >= 4, 0);
    sbutton(XO + 73, PY + 66, 94, 28, L"닫기", B_CLOSE, 1, 0);
}

static int use_portal_scroll(void);
static void inv_use(void)
{
    Item *it = isel_item();
    wchar_t nm[48];
    if (!it->kind) return;
    item_name(it, nm, 48);
    if (isel < 4) {
        int i;
        for (i = 0; i < INV_N; i++) if (!S.inv[i].kind) {
            S.inv[i] = *it; memset(it, 0, sizeof(Item));
            snd_play(SX_PICKUP); hero_dirty = 1;
            wfmt(invmsg, 64, L"%ls%ls 벗었다.", nm, JUL(nm));
            clamp_hpmp();
            return;
        }
        wfmt(invmsg, 64, L"가방에 빈 자리가 없다.");
        return;
    }
    if (it->kind == IK_TP) {
        Item keep = *it;
        memset(it, 0, sizeof(Item));
        if (!use_portal_scroll()) *it = keep;
        return;
    }
    if (IS_POT(it->kind)) {
        wchar_t b[52];
        drink(it, b, 52);
        snd_play(SX_DRINK);
        wfmt(invmsg, 64, L"%ls", b);
        return;
    }
    {
        int s = eq_slot_for(it->kind);
        Item tmp = S.eq[s];
        S.eq[s] = *it;
        *it = tmp;
        snd_play(SX_PICKUP); hero_dirty = 1;
        wfmt(invmsg, 64, L"%ls%ls 장착했다.", nm, JUL(nm));
        clamp_hpmp();
    }
}

/* ---- character screen ---- */
static int shards(void);
static void render_char(void)
{
    int y0 = VY, h = PY - VY, lo, hi, i, y;
    static const wchar_t *SN[4] = { L"힘", L"민첩", L"활력", L"에너지" };
    int *sv[4];
    sv[0] = &S.str; sv[1] = &S.dex; sv[2] = &S.vit; sv[3] = &S.ene;
    stone(0, y0, GW, h, 34);
    goldframe(0, y0, GW, h);
    {   /* the wanderer as currently equipped */
        int pw, ph, pax, pay, pnf;
        if (hero_build(7, 150, &pw, &ph, &pax, &pay, &pnf)) {
            int gx = GW - 32 - XO / 2, gy = y0 + 128;
            for (y = -2; y <= 2; y++) for (i = -16; i <= 16; i++)
                if (i * i * 4 / 256 + y * y < 4) FB[(gy + y) * GW + gx + i] = shade(FB[(gy + y) * GW + gx + i], 120);
            blit_frame(7, (tickc >> 1) & 7, pw, ph, pax, pay, gx, gy, 256, 256, 0, 0, 0);
        }
    }
    txtf(10, y0 + 5, C_GOLD, T_SHADOW, L"방랑자");
    txtf(GW - 10, y0 + 5, C_GOLD, T_RIGHT | T_SHADOW, L"레벨 %d", S.lvl);
    txtf(GW - 10, y0 + 19, rgb(200, 120, 255), T_RIGHT | T_SHADOW, L"봉인의 파편 %d/5", shards());
    txtf(10, y0 + 19, C_WHITE, 0, L"경험치 %d / %d", S.exp, exp_next(S.lvl));
    for (i = 0; i < 4; i++) {
        int bonus = i == 0 ? p_str() - S.str : i == 1 ? p_dex() - S.dex : 0;
        y = y0 + 36 + i * 17;
        fill(8, y - 1, 140, 16, csel == i ? rgb(60, 46, 26) : rgb(18, 14, 10));
        txt(14, y + 1, SN[i], C_GOLD, 0);
        if (bonus) txtf(140, y + 1, C_BLUE, T_RIGHT, L"%d", *sv[i] + bonus);
        else txtf(140, y + 1, C_WHITE, T_RIGHT, L"%d", *sv[i]);
        if (S.pts > 0) sbutton(154, y - 1, 26, 16, L"+", B_STAT + i, 1, csel == i);
        addbtn(8, y - 1, 140, 16, B_STAT + 10 + i);
    }
    y = y0 + 36 + 4 * 17;
    if (S.pts > 0) txtf(14, y + 1, C_YEL, 0, L"남은 포인트: %d", S.pts);
    p_dmg(&lo, &hi);
    y += 16;
    txtf(14, y, C_WHITE, 0, L"공격력 %d-%d    방어력 %d", lo, hi, p_def());
    y += 14;
    txtf(14, y, rgb(255, 150, 140), 0, L"생명력 %d/%d", S.hp, p_maxhp());
    txtf(130, y, rgb(160, 170, 255), 0, L"마나 %d/%d", S.mp, p_maxmp());
    y += 15;
    for (i = 0; i < 4 && y + 12 < PY; i++) {
        int lx = 14 + (i & 1) * 112;
        if (S.lvl >= SK[i].lvl) txtf(lx, y, C_BLUE, 0, L"%ls (마나 %d)", SK[i].n, SK[i].mp);
        else txtf(lx, y, C_GRAY, 0, L"??? (Lv %d)", SK[i].lvl);
        if (i & 1) y += 13;
    }
}

static void panel_closeonly(void)
{
    belt_slots(0);
    sbutton(XO + 73, PY + 66, 94, 28, L"닫기", B_CLOSE, 1, 0);
}

/* ---- title / dead / menus ---- */
#define FW_ 80
#define FH_ 36
static unsigned char fire[FW_ * FH_];
static void fire_step(void)
{
    int x, y;
    for (x = 0; x < FW_; x++) fire[(FH_ - 1) * FW_ + x] = (unsigned char)(rnd() % 4 ? 30 : 14);
    for (y = 0; y < FH_ - 1; y++) for (x = 0; x < FW_; x++) {
        int src = (y + 1) * FW_ + x, r = (int)(rnd() & 3), v = fire[src] - (int)(rnd() % 3);
        int dx = x - r + 1;
        if (dx < 0) dx = 0;
        if (dx >= FW_) dx = FW_ - 1;
        fire[y * FW_ + dx] = (unsigned char)(v < 0 ? 0 : v);
    }
}
static u16 fire_col(int v)
{
    if (v <= 0) return 0;
    if (v < 12) return rgb(v * 12, 0, 0);
    if (v < 24) return rgb(150 + (v - 12) * 8, (v - 12) * 10, 0);
    return rgb(255, 120 + (v - 24) * 10, (v - 24) * 12);
}

static u16 tbuf[GWMAX * 480];
static void render_title(void)
{
    int y, x, ty = GH / 5;
    static int tbg = 0;
    if (!tbg) {
        render_view();
        darken(0, VY, GW, VH, 90);
        memcpy(tbuf, FB + VY * GW, GW * VH * 2);
        tbg = 1;
    } else memcpy(FB + VY * GW, tbuf, GW * VH * 2);
    for (y = 0; y < GH; y++) if (y < VY || y >= VY + VH) hline(0, GW - 1, y, rgb(0, 0, 0));
    /* fire behind the logo (3x scaled) */
    for (y = 0; y < FH_; y++) for (x = 0; x < FW_; x++) {
        int v = fire[y * FW_ + x];
        if (v > 2) {
            u16 c = fire_col(v);
            int a, b;
            for (b = 0; b < 2; b++) for (a = 0; a < 3; a++) {
                int px = XO + x * 3 + a, py = ty - 46 + y * 2 + b;
                if (py >= 0 && py < GH) FB[py * GW + px] = mix(FB[py * GW + px], c, clampi(v * 9, 0, 255));
            }
        }
    }
    darken(0, ty - 2, GW, 28, 150);
    { int a, b; for (b = -1; b <= 1; b++) for (a = -1; a <= 1; a++) if (a || b) txt(GW / 2 + a * 2, ty + b * 2, L"심연의 미궁", rgb(20, 0, 0), T_CENTER | T_BIG); }
    txt(GW / 2, ty, L"심연의 미궁", rgb(210, 180, 110), T_CENTER | T_BIG);
    txt(GW / 2, ty + 28, L"ABYSS LABYRINTH", rgb(170, 140, 90), T_CENTER | T_SHADOW);
    {
        const wchar_t *it[3] = { L"새로운 모험", L"이어하기", L"종료" };
        int en[3] = { 1, has_save, 1 };
        list_window(GW / 2 - 64, GH / 2 + 20, 128, it, en, 3, sel, B_LIST, 22);
    }
    txt(GW / 2, GH - 16, L"WM6.1 SCH-M480", C_GRAY, T_CENTER);
}
static void render_dead(void)
{
    fill(0, 0, GW, GH, rgb(0, 0, 0));
    txt(GW / 2, GH / 3, L"당신은 죽었습니다", rgb(200, 20, 20), T_CENTER | T_BIG);
    txtf(GW / 2, GH / 3 + 40, C_WHITE, T_CENTER, L"도달한 층: 지하 %d층", S.maxfloor);
    txtf(GW / 2, GH / 3 + 56, C_WHITE, T_CENTER, L"레벨 %d  /  처치한 적 %d", S.lvl, S.kills);
    txtf(GW / 2, GH / 3 + 72, C_YEL, T_CENTER, L"모은 금화 %d", S.gold);
    txt(GW / 2, GH - 40, L"화면을 누르면 처음으로", C_GRAY, T_CENTER);
    addbtn(0, 0, GW, GH, B_ANY);
}

static void render_menu(void)
{
    {   /* key guide for the physical keyboard */
        int y0 = PY - 42;
        fill(0, y0, GW, 42, rgb(8, 6, 4));
        hline(0, GW - 1, y0, rgb(90, 66, 30));
        txt(GW / 2, y0 + 2, L"소프트키: 왼쪽 캐릭터 · 오른쪽 메뉴", C_GOLD, T_CENTER | T_SHADOW);
        txt(GW / 2, y0 + 15, L"I 가방  C 캐릭터  M 지도  1~4(QWER) 물약", C_WHITE, T_CENTER | T_SHADOW);
        txt(GW / 2, y0 + 28, L"전투: A공격 S마법 D도구 F방어 G도망", C_WHITE, T_CENTER | T_SHADOW);
    }
    const wchar_t *it[5] = { L"모험 계속", snd_music_on ? L"음악: 켜짐" : L"음악: 꺼짐", snd_sfx_on ? L"효과음: 켜짐" : L"효과음: 꺼짐",
                             L"저장하고 종료", confirm ? L"정말 포기? (한번 더)" : L"포기하고 처음으로" };
    list_window(GW / 2 - 90, VY + 20, 180, it, 0, 5, sel, B_LIST, 22);
}

static int price(int k) { return k == IK_HP ? 25 + S.floor * 3 : k == IK_MP ? 25 + S.floor * 3 : 90 + S.floor * 6; }
static void render_shop(void)
{
    wchar_t lb[3][32];
    const wchar_t *it[4];
    wfmt(lb[0], 32, L"생명 물약   %d", price(IK_HP));
    wfmt(lb[1], 32, L"마나 물약   %d", price(IK_MP));
    wfmt(lb[2], 32, L"회복 물약   %d", price(IK_RJ));
    it[0] = lb[0]; it[1] = lb[1]; it[2] = lb[2]; it[3] = L"떠나기";
    dqwin(10, VY + 6, GW - 20, 40);
    txt(20, VY + 12, L"떠돌이 상인:", C_GOLD, 0);
    txt(20, VY + 26, L"\"살아남고 싶다면 사 가게나.\"", rgb(255, 255, 255), 0);
    list_window(GW / 2 - 90, VY + 52, 180, it, 0, 4, sel, B_LIST, 20);
}

static void render_prompt(void)
{
    const wchar_t *it[2] = { L"예", L"아니오" };
    int na = area_of(S.floor + 1);
    dqwin(10, VY + 14, GW - 20, 40);
    if (na != area_of(S.floor)) txtf(GW / 2, VY + 21, C_GOLD, T_CENTER, L"다음 지역: %ls", AREAS[na].n);
    else txtf(GW / 2, VY + 21, C_GOLD, T_CENTER, L"%ls 깊은 곳", AREAS[na].n);
    if (AREAS[na].camp && na != area_of(S.floor)) txtf(GW / 2, VY + 21, C_GOLD, T_CENTER, L"%ls: %ls", ACTN[AREAS[na].act], AREAS[na].n);
    txt(GW / 2, VY + 35, L"이동하시겠습니까?", rgb(255, 255, 255), T_CENTER);
    list_window(GW / 2 - 40, VY + 56, 80, it, 0, 2, sel, B_LIST, 20);
}

void game_render(void)
{
    nbtn = 0;
    if (mode == M_TITLE) { render_title(); dirty = 0; return; }
    if (mode == M_DEAD) { render_dead(); dirty = 0; return; }
    if (mode == M_BATTLE) { render_battle(); dirty = 0; return; }
    render_topbar();
    render_panel();
    switch (mode) {
    case M_INV:
        render_inv();
        panel_inv();
        break;
    case M_CHAR:
        render_char();
        panel_closeonly();
        break;
    case M_TRADE:
        render_trade();
        panel_trade();
        break;
    default:
        render_view();
        if (showmap) render_automap();
        if (mode != M_STORY) render_msgs();
        if (mode == M_MENU) { render_menu(); belt_slots(0); }
        else if (mode == M_SHOP) { render_shop(); belt_slots(0); }
        else if (mode == M_PROMPT) { render_prompt(); belt_slots(0); }
        else if (mode == M_STORY) { render_story(); belt_slots(0); }
        else if (mode == M_TALK) { render_talk(); belt_slots(0); sbutton(XO + 73, PY + 66, 94, 28, L"떠나기", B_CLOSE, 1, 0); }
        else if (mode == M_WARP) { render_warp(); belt_slots(0); sbutton(XO + 73, PY + 66, 94, 28, L"닫기", B_CLOSE, 1, 0); }
        else panel_explore();
        break;
    }
    dirty = 0;
}

/* ================================================================ */
/*  input                                                           */
/* ================================================================ */
static void battle_cmd(int c)
{
    if (c == 0) {
        B.act = ACT_ATTACK;
        if (alive_count() > 1) { B.phase = BP_TARGET; B.tsel = first_alive(); }
        else { B.tgt = first_alive(); begin_round(); }
    } else if (c == 1) { B.phase = BP_MAGIC; B.msel = 0; }
    else if (c == 2) {
        int i;
        for (i = 0; i < 4; i++) if (S.belt[i].kind) break;
        if (i == 4) { bclear(); bline(0, C_GRAY, L"허리띠에 물약이 없다!"); return; }
        B.phase = BP_POTION; B.psel = i;
    } else if (c == 3) { B.act = ACT_DEFEND; begin_round(); }
    else { B.act = ACT_FLEE; begin_round(); }
}
static void battle_skill(int s)
{
    if (S.lvl < SK[s].lvl) return;
    if (S.mp < SK[s].mp) { bclear(); bline(0, C_GRAY, L"마나가 부족하다!"); return; }
    B.act = ACT_SKILL; B.arg = s;
    if (SK[s].target == 0 && alive_count() > 1) { B.phase = BP_TARGET; B.tsel = first_alive(); }
    else { B.tgt = first_alive(); begin_round(); }
}
static void battle_potion(int s)
{
    if (!S.belt[s].kind) return;
    B.act = ACT_POTION; B.arg = s; begin_round();
}
static void battle_advance(void)
{
    if (B.phase == BP_INTRO) { B.phase = BP_CMD; B.cmd = 0; }
    else if (B.phase == BP_EXEC) B.wait = 0;
    else if (B.phase == BP_RESULT) {
        if (B.resi >= B.nres) { end_battle(); return; }
        bclear();
        { int i; for (i = 0; i < 3 && B.resi < B.nres; i++) B.ln[i] = B.res[B.resi++]; }
    } else if (B.phase == BP_DEAD) { mode = M_DEAD; }
}
static void battle_cancel(void)
{
    if (B.phase == BP_MAGIC || B.phase == BP_POTION || B.phase == BP_TARGET) B.phase = BP_CMD;
}
static int next_alive(int from, int d)
{
    int i, t = from;
    for (i = 0; i < B.n; i++) { t = (t + d + B.n) % B.n; if (B.m[t].alive) return t; }
    return from;
}

static void stat_add(int i)
{
    if (S.pts <= 0) return;
    S.pts--;
    if (i == 0) S.str++;
    else if (i == 1) S.dex++;
    else if (i == 2) { S.vit++; S.hp += 3; }
    else { S.ene++; S.mp += 2; }
}

static int music_last = -99;
static void music_update(void)
{
    int w;
    if (mode == M_TITLE) w = MU_TITLE;
    else if (mode == M_DEAD || (mode == M_BATTLE && B.phase == BP_DEAD)) w = MU_DIE;
    else if (mode == M_BATTLE) w = B.phase == BP_RESULT ? MU_WIN : B.boss ? MU_BOSS : MU_BATTLE;
    else w = CA->camp ? MU_TOWN : MU_ACT1 + CA->act;
    if (!snd_music_on) w = -1;
    if (w != music_last) { music_last = w; snd_music(w); }
}

static void list_choose(int i)
{
    if (mode == M_TITLE) {
        if (i == 0) { new_game(); save_game(); }
        else if (i == 1 && has_save) {
            if (plat_load(&S, sizeof(S)) && S.magic == SAVE_MAGIC && S.ver == SAVE_VER) {
                rs = S.rng ? S.rng : rs;
                memset(elog, 0, sizeof(elog));
                addlog(C_GOLD, L"%ls에서 모험을 이어간다.", CA->n);
                mode = M_EXPLORE;
            } else { has_save = 0; }
        } else if (i == 2) quitflag = 1;
    } else if (mode == M_MENU) {
        if (i == 0) mode = M_EXPLORE;
        else if (i == 1) { snd_music_on = !snd_music_on; music_last = -99; }
        else if (i == 2) snd_sfx_on = !snd_sfx_on;
        else if (i == 3) { save_game(); quitflag = 1; }
        else if (i == 4) {
            if (!confirm) { confirm = 1; return; }
            plat_delsave(); has_save = 0; mode = M_TITLE; sel = 0;
        }
        confirm = 0;
    } else if (mode == M_SHOP) {
        if (i == 3) {
            mode = M_EXPLORE;
            if (!CA->camp) { addlog(C_GRAY, L"상인이 어둠 속으로 사라졌다."); S.feat[S.py][S.px] = 0; }
            return;
        }
        {
            int k = i == 0 ? IK_HP : i == 1 ? IK_MP : IK_RJ, p = price(k);
            Item it;
            if (S.gold < p) { addlog(C_GRAY, L"금화가 부족하다."); return; }
            make_potion(&it, k);
            if (!give_item(&it)) { addlog(C_GRAY, L"가방이 가득 찼다."); return; }
            S.gold -= p;
            addlog(C_WHITE, L"%ls%ls 샀다.", BASES[it.base].n, JUL(BASES[it.base].n));
        }
    } else if (mode == M_TALK) {
        talk_choose(i);
    } else if (mode == M_PROMPT) {
        if (i == 0) descend();
        else addlog(C_GRAY, L"아직은 이 층에 머물기로 했다.");
        mode = M_EXPLORE;
    } else if (mode == M_BATTLE) {
        if (B.phase == BP_MAGIC) battle_skill(i);
    }
}

static int list_len(void)
{
    if (mode == M_TITLE) return 3;
    if (mode == M_MENU) return 5;
    if (mode == M_SHOP) return 4;
    if (mode == M_PROMPT) return 2;
    if (mode == M_TALK) return talk_len();
    return 0;
}

static void use_belt(int s)
{
    wchar_t b[52], nm[40];
    if (!S.belt[s].kind) return;
    item_name(&S.belt[s], nm, 40);
    drink(&S.belt[s], b, 52); snd_play(SX_DRINK);
    addlog(C_WHITE, L"%ls%ls 마셨다.", nm, JUL(nm));
    addlog(C_GREEN, L"%ls", b);
}

static void do_button(int id)
{
    if (id == B_NONE) return;
    dirty = 1;
    if (mode == M_DEAD) { mode = M_TITLE; sel = 0; return; }
    if (mode == M_STORY) { story_next(); return; }
    if (mode == M_WARP) {
        if (id == B_CLOSE) mode = M_EXPLORE;
        else if (id >= B_LIST && id < B_LIST + 10) { wsel = wscroll + id - B_LIST; warp_go(wsel); }
        return;
    }
    if (mode == M_TALK && id == B_CLOSE) { mode = M_EXPLORE; return; }
    if (mode == M_TRADE) {
        if (id == B_CLOSE) trade_close();
        else if (id == B_STAT || id == B_STAT + 1) { ttab = id - B_STAT; tsel = 0; tscroll = 0; tmsg[0] = 0; build_sell(); }
        else if (id == B_USE) trade_act();
        else if (id >= B_SLOT && id < B_SLOT + 16) { int k = tscroll + id - B_SLOT; if (k == tsel) trade_act(); else { tsel = k; tmsg[0] = 0; } }
        return;
    }
    if (id >= B_LIST && id < B_LIST + 10) {
        if (mode == M_TITLE || mode == M_MENU || mode == M_SHOP || mode == M_PROMPT || mode == M_TALK) { sel = id - B_LIST; list_choose(sel); return; }
        if (mode == M_BATTLE) { B.msel = id - B_LIST; battle_skill(B.msel); return; }
    }
    if (mode == M_EXPLORE) {
        switch (id) {
        case B_FWD: step(S.dir); break;
        case B_BACK: step((S.dir + 2) & 3); break;
        case B_TL: turn(3); break;
        case B_TR: turn(1); break;
        case B_INV: mode = M_INV; invmsg[0] = 0; break;
        case B_MAP: showmap = !showmap; break;
        case B_CHAR: mode = M_CHAR; csel = 0; break;
        case B_MENU: mode = M_MENU; sel = 0; confirm = 0; break;
        default:
            if (id >= B_BELT && id < B_BELT + 4) use_belt(id - B_BELT);
        }
        return;
    }
    if (mode == M_BATTLE) {
        if (id == B_OKB) {
            if (B.phase == BP_CMD) battle_cmd(B.cmd);
            else if (B.phase == BP_MAGIC) battle_skill(B.msel);
            else if (B.phase == BP_POTION) battle_potion(B.psel);
            else if (B.phase == BP_TARGET) { B.tgt = B.tsel; begin_round(); }
            else battle_advance();
        } else if (id == B_CANCEL) battle_cancel();
        else if (id >= B_CMD && id < B_CMD + 5 && (B.phase == BP_CMD || B.phase == BP_MAGIC || B.phase == BP_POTION || B.phase == BP_TARGET)) {
            B.phase = BP_CMD;
            B.cmd = id - B_CMD;
            battle_cmd(B.cmd);
        }
        else if (id >= B_BELT && id < B_BELT + 4 && (B.phase == BP_POTION || B.phase == BP_CMD)) battle_potion(id - B_BELT);
        else if (id >= B_MON && id < B_MON + 3) {
            if (B.phase == BP_TARGET) { B.tgt = id - B_MON; begin_round(); }
            else if (B.phase == BP_CMD) { B.act = ACT_ATTACK; B.tgt = id - B_MON; begin_round(); }
            else battle_advance();
        }
        return;
    }
    if (mode == M_INV) {
        if (id == B_CLOSE) mode = M_EXPLORE;
        else if (id == B_USE) inv_use();
        else if (id == B_DROP) {
            Item *it = isel_item();
            wchar_t nm[48];
            item_name(it, nm, 48);
            wfmt(invmsg, 64, L"%ls%ls 버렸다.", nm, JUL(nm));
            memset(it, 0, sizeof(Item));
        } else if (id >= B_SLOT && id < B_SLOT + 16) {
            int s = id - B_SLOT;
            if (s == isel && isel_item()->kind) inv_use();
            else { isel = s; invmsg[0] = 0; }
        }
        return;
    }
    if (mode == M_CHAR) {
        if (id == B_CLOSE) mode = M_EXPLORE;
        else if (id >= B_STAT && id < B_STAT + 4) { csel = id - B_STAT; stat_add(csel); }
        else if (id >= B_STAT + 10 && id < B_STAT + 14) csel = id - B_STAT - 10;
        return;
    }
}

void game_tap(int x, int y)
{
    int id = hitbtn(x, y);
    if (id != B_NONE) snd_play_v(SX_CLICK, 120);
    if (mode == M_STORY) { story_next(); return; }
    if (mode == M_BATTLE && id == B_NONE && y < PY &&
        (B.phase == BP_INTRO || B.phase == BP_EXEC || B.phase == BP_RESULT || B.phase == BP_DEAD)) {
        battle_advance();
        dirty = 1;
        return;
    }
    do_button(id);
}

/* physical shortcut keys: returns 1 if handled, otherwise k is turned into a basic key (or 0 = ignore) */
static int hotkey(int *pk)
{
    int k = *pk, b = k >= K_BELT1 && k <= K_BELT4 ? k - K_BELT1 : -1, c = k >= K_CMD1 && k <= K_CMD5 ? k - K_CMD1 : -1;
    if (mode == M_DEAD || mode == M_STORY) { *pk = K_OK; return 0; }
    if (mode == M_EXPLORE) {
        if (k == K_SOFTL || k == K_CHAR) { snd_play_v(SX_CLICK, 120); do_button(B_CHAR); }
        else if (k == K_SOFTR) { snd_play_v(SX_CLICK, 120); do_button(B_MENU); }
        else if (k == K_INV) { snd_play_v(SX_CLICK, 120); do_button(B_INV); }
        else if (k == K_MAP) do_button(B_MAP);
        else if (b >= 0) do_button(B_BELT + b);
        return 1;
    }
    if (mode == M_BATTLE) {
        if (k == K_SOFTL) { if (B.phase == BP_MAGIC || B.phase == BP_POTION || B.phase == BP_TARGET) do_button(B_CANCEL); }
        else if (k == K_SOFTR) do_button(B_OKB);
        else if (c >= 0) { if (B.phase == BP_CMD || B.phase == BP_MAGIC || B.phase == BP_POTION || B.phase == BP_TARGET) do_button(B_CMD + c); }
        else if (b >= 0) { if (S.belt[b].kind) do_button(B_BELT + b); }
        else return 1;
        snd_play_v(SX_CLICK, 120);
        return 1;
    }
    if (mode == M_INV) {
        if (k == K_CHAR) { mode = M_CHAR; csel = 0; }
        else if (k == K_INV || k == K_SOFTL || k == K_SOFTR) mode = M_EXPLORE;
        else if (b >= 0) { use_belt(b); }
        return 1;
    }
    if (mode == M_CHAR) {
        if (k == K_INV) { mode = M_INV; invmsg[0] = 0; }
        else if (k == K_CHAR || k == K_SOFTL || k == K_SOFTR) mode = M_EXPLORE;
        return 1;
    }
    if (mode == M_TITLE) { if (k == K_SOFTR) *pk = K_OK; else return 1; return 0; }
    if (mode == M_MENU) { if (k == K_SOFTL || k == K_SOFTR) { mode = M_EXPLORE; } return 1; }
    /* trade / talk / warp / shop / prompt */
    if (k == K_SOFTL) { *pk = K_BACK; return 0; }
    if (k == K_SOFTR) { *pk = K_OK; return 0; }
    return 1;
}

void game_key(int k)
{
    dirty = 1;
    if (k >= K_SOFTL && hotkey(&k)) return;
    if (mode != M_EXPLORE && mode != M_BATTLE) snd_play_v(SX_CLICK, 120);
    else if (mode == M_BATTLE && (k == K_OK || k == K_BACK)) snd_play_v(SX_CLICK, 120);
    if (mode == M_DEAD) { mode = M_TITLE; sel = 0; return; }
    if (mode == M_STORY) { story_next(); return; }
    if (mode == M_WARP) {
        if (k == K_UP && wsel > 0) { wsel--; if (wsel < wscroll) wscroll = wsel; }
        else if (k == K_DOWN && wsel < wn - 1) { wsel++; if (wsel >= wscroll + 7) wscroll = wsel - 6; }
        else if (k == K_OK) warp_go(wsel);
        else if (k == K_BACK || k == K_MENU) mode = M_EXPLORE;
        return;
    }
    if (mode == M_TRADE) {
        int n = trade_count();
        if (k == K_UP && tsel > 0) tsel--;
        else if (k == K_DOWN && tsel < n - 1) tsel++;
        else if (k == K_LEFT || k == K_RIGHT) { ttab ^= 1; tsel = 0; tscroll = 0; tmsg[0] = 0; build_sell(); }
        else if (k == K_OK) trade_act();
        else if (k == K_BACK || k == K_MENU) trade_close();
        return;
    }
    if (mode == M_TALK) {
        int n = talk_len();
        if (k == K_UP) sel = (sel + n - 1) % n;
        else if (k == K_DOWN) sel = (sel + 1) % n;
        else if (k == K_OK) talk_choose(sel);
        else if (k == K_BACK || k == K_MENU) mode = M_EXPLORE;
        return;
    }
    if (mode == M_TITLE || mode == M_MENU || mode == M_SHOP || mode == M_PROMPT) {
        int n = list_len();
        if (k == K_UP) sel = (sel + n - 1) % n;
        else if (k == K_DOWN) sel = (sel + 1) % n;
        else if (k == K_OK) list_choose(sel);
        else if (k == K_BACK || k == K_MENU) {
            if (mode == M_MENU) mode = M_EXPLORE;
            else if (mode == M_SHOP) list_choose(3);
            else if (mode == M_PROMPT) list_choose(1);
        }
        return;
    }
    if (mode == M_EXPLORE) {
        switch (k) {
        case K_UP: step(S.dir); break;
        case K_DOWN: step((S.dir + 2) & 3); break;
        case K_LEFT: turn(3); break;
        case K_RIGHT: turn(1); break;
        case K_OK: {
            int f = S.feat[S.py][S.px], fx = S.px + DX[S.dir], fy = S.py + DY[S.dir];
            if (fx >= 0 && fy >= 0 && fx < MW && fy < MH && is_npc(S.feat[fy][fx])) { npc_interact(S.feat[fy][fx]); break; }
            if (f == F_WAYPOINT) { warp_open(); break; }
            if (f == F_STAIRS || f == F_MERCHANT) on_feature();
            else showmap = !showmap;
            break;
        }
        case K_MENU: mode = M_MENU; sel = 0; confirm = 0; break;
        case K_BACK: mode = M_INV; invmsg[0] = 0; break;
        }
        return;
    }
    if (mode == M_BATTLE) {
        switch (B.phase) {
        case BP_CMD:
            if (k == K_LEFT) B.cmd = (B.cmd + 4) % 5;
            else if (k == K_RIGHT) B.cmd = (B.cmd + 1) % 5;
            else if (k == K_UP) B.cmd = B.cmd >= 3 ? (B.cmd == 3 ? 0 : 2) : B.cmd;
            else if (k == K_DOWN) B.cmd = B.cmd < 3 ? (B.cmd == 0 ? 3 : 4) : B.cmd;
            else if (k == K_OK) battle_cmd(B.cmd);
            break;
        case BP_MAGIC:
            if (k == K_UP) B.msel = (B.msel + 3) % 4;
            else if (k == K_DOWN) B.msel = (B.msel + 1) % 4;
            else if (k == K_OK) battle_skill(B.msel);
            else if (k == K_BACK) battle_cancel();
            break;
        case BP_POTION:
            if (k == K_LEFT || k == K_UP) { int i; for (i = 0; i < 4; i++) { B.psel = (B.psel + 3) % 4; if (S.belt[B.psel].kind) break; } }
            else if (k == K_RIGHT || k == K_DOWN) { int i; for (i = 0; i < 4; i++) { B.psel = (B.psel + 1) % 4; if (S.belt[B.psel].kind) break; } }
            else if (k == K_OK) battle_potion(B.psel);
            else if (k == K_BACK) battle_cancel();
            break;
        case BP_TARGET:
            if (k == K_LEFT || k == K_UP) B.tsel = next_alive(B.tsel, -1);
            else if (k == K_RIGHT || k == K_DOWN) B.tsel = next_alive(B.tsel, 1);
            else if (k == K_OK) { B.tgt = B.tsel; begin_round(); }
            else if (k == K_BACK) battle_cancel();
            break;
        default:
            if (k == K_OK || k == K_BACK) battle_advance();
        }
        return;
    }
    if (mode == M_INV) {
        if (k == K_BACK || k == K_MENU) { mode = M_EXPLORE; return; }
        if (k == K_OK) { inv_use(); return; }
        if (isel < 4) {
            if (k == K_LEFT) isel = (isel + 3) % 4;
            else if (k == K_RIGHT) isel = (isel + 1) % 4;
            else if (k == K_DOWN) isel = 4 + isel * 6 / 4;
        } else {
            int g = isel - 4, cx = g % 6, cy = g / 6;
            if (k == K_LEFT) cx = (cx + 5) % 6;
            else if (k == K_RIGHT) cx = (cx + 1) % 6;
            else if (k == K_UP) { if (cy == 0) { isel = cx * 4 / 6; invmsg[0] = 0; return; } cy--; }
            else if (k == K_DOWN) cy = cy < 1 ? cy + 1 : cy;
            isel = 4 + cy * 6 + cx;
        }
        invmsg[0] = 0;
        return;
    }
    if (mode == M_CHAR) {
        if (k == K_UP) csel = (csel + 3) % 4;
        else if (k == K_DOWN) csel = (csel + 1) % 4;
        else if (k == K_OK || k == K_RIGHT) stat_add(csel);
        else if (k == K_BACK || k == K_MENU) mode = M_EXPLORE;
    }
}

int game_tick(void)
{
    int i, r = 0;
    tickc++;
    music_update();
    if (mode == M_BATTLE) {
        int ph = B.phase, oi = B.oi;
        battle_tick();
        if (ph != B.phase || oi != B.oi || mode != M_BATTLE) r = 1;
        if (B.shake > 0) { B.shake--; r = 1; }
        if (B.redflash > 0) { B.redflash--; r = 1; }
        for (i = 0; i < 32; i++) if (B.bl[i].t > 0) {
            B.bl[i].t--; B.bl[i].x += B.bl[i].vx; B.bl[i].y += B.bl[i].vy; B.bl[i].vy += 12; r = 1;
        }
        for (i = 0; i < 3; i++) {
            if (B.pop[i].t > 0) { B.pop[i].t--; r = 1; }
            if (B.m[i].flash > 0) { B.m[i].flash--; r = 1; }
            if (B.m[i].dying > 0) { B.m[i].dying--; r = 1; }
            if (B.m[i].appear > 0) { B.m[i].appear--; r = 1; }
            if (B.m[i].anim) { B.m[i].at++; if (B.m[i].at >= (B.m[i].anim == 1 ? 9 : 8)) B.m[i].anim = 0; }
        }
        r = 1; /* animated monsters */
        for (i = 0; i < B.np; i++) {
            PMember *pm = &B.pm[i];
            if (pm->flash > 0) pm->flash--;
            if (pm->anim || !pm_alive(i)) {
                pm->at++;
                if (pm->anim == 1 && pm->at >= 21) pm->anim = 0;
                if (pm->anim == 2 && pm->at >= 8) pm->anim = 0;
                if (pm->anim == 4 && pm->at >= 14) pm->anim = 0;
                if (pm->at > 40) pm->at = 40;
            }
        }
        if (B.fx) { B.fxt++; if (B.fxt > 20) B.fx = 0; }
        {
            int target = (int)(10 * fsn(tickc / 120.0f)), k;
            for (k = 0; k < B.np; k++) if (B.pm[k].anim == 1 && B.pm[k].tgt >= 0 && B.pm[k].tgt < B.n) {
                Mon *mm = &B.m[B.pm[k].tgt];
                target = (GW / 2 - (mm->sx + mm->sw / 2)) / 2;
            }
            B.pan += (target - B.pan) / 4 + (target > B.pan ? 1 : target < B.pan ? -1 : 0);
        }
    } else if (mode == M_TITLE) {
        fire_step();
        r = 1;
    } else if (mode == M_CHAR) {
        if (!(tickc & 1)) r = 1;        /* idle animation of the equipped portrait */
    } else if (mode == M_DEAD) {
        if ((tickc & 7) == 0) r = 1;
    } else if (mode == M_EXPLORE || mode == M_CHAR) {
        if ((tickc & 7) == 0 && S.pts > 0) r = 1;
    }
    return r || dirty;
}

int game_wants_quit(void) { return quitflag; }

void game_suspend(void)
{
    if (mode != M_TITLE && mode != M_DEAD && S.hp > 0 && S.magic == SAVE_MAGIC) save_game();
}

void game_init(int h, u16 *fb, unsigned seed)
{
    GH = h; FB = fb;
    mdat_open();
    hdat_open();
    rs = seed ? seed : 12345;
    layout();
    {
        SaveData tmp;
        has_save = plat_load(&tmp, sizeof(tmp)) && tmp.magic == SAVE_MAGIC && tmp.ver == SAVE_VER && tmp.hp > 0;
    }
    /* demo floor for the title screen */
    memset(&S, 0, sizeof(S));
    S.floor = 1;
    gen_floor();
    mode = M_TITLE;
    sel = has_save ? 1 : 0;
    (void)GOLD_DUMMY;
}

#ifdef HARNESS
int harness_mode(void) { return mode; }
void harness_equip(int w, int a, int h)
{
    S.eq[0].kind = w >= 0 ? IK_W : 0; S.eq[0].base = (unsigned char)(w >= 0 ? w : 0);
    S.eq[2].kind = a >= 0 ? IK_A : 0; S.eq[2].base = (unsigned char)(a >= 0 ? a : 0);
    S.eq[1].kind = h >= 0 ? IK_H : 0; S.eq[1].base = (unsigned char)(h >= 0 ? h : 0);
    hero_dirty = 1;
}
void harness_charscreen(void) { mode = M_CHAR; }
int harness_bphase(void) { return B.phase; }
void harness_cheat(void)
{
    int i;
    S.lvl = 9; S.pts = 5; S.gold += 500;
    for (i = 0; i < 7; i++) { Item it; gen_item(&it, 8, i * 150); give_item(&it); }
    S.hp = p_maxhp(); S.mp = p_maxmp();
    mode = M_EXPLORE;
}
void harness_boss(void) { S.floor = 5; start_battle(1); }
void harness_magic(void) { B.phase = BP_MAGIC; B.msel = 2; }
void harness_area(int f) { int d, k, best = -1, bd = 0; S.floor = f; gen_floor(); mode = M_EXPLORE; showmap = 0;
  for (d = 0; d < 4; d++) { for (k = 1; k < 8 && !solid(S.px + DX[d] * k, S.py + DY[d] * k); k++); if (k > best) { best = k; bd = d; } }
  S.dir = bd; mark_seen(); }
void harness_gallery(int a, int b, int c) { int t[3] = { a, b, c }, i, n = c < 0 ? (b < 0 ? 1 : 2) : 3; start_battle(0); B.n = n;
  for (i = 0; i < n; i++) mon_init(&B.m[i], &MD[t[i]], 0, 0); layout_mons(); B.phase = BP_CMD; B.bgok = 0; }
void harness_bossk(int k) { static const int bf[5] = { 12, 21, 30, 37, 47 }; S.floor = bf[k]; gen_floor(); start_battle(1); }
void harness_anim(int a, int at) { B.m[0].anim = a; B.m[0].at = at; B.m[0].appear = 0; }
void harness_act(int act, int arg) { B.act = act; B.arg = arg; B.tgt = first_alive(); begin_round(); }
void harness_lvl(int l) { S.lvl = l; S.ene = 30; S.dex = 60; S.vit = 40; S.hp = p_maxhp(); S.mp = p_maxmp(); }
void harness_town(void (*snap)(const char *))
{
    int i;
    new_game();
    snap("t_story0");
    story_next(); story_next(); story_next();
    snap("t_story3");
    while (mode == M_STORY) story_next();
    snap("t_camp");
    S.dir = 0; step(0); step(0);
    snap("t_elder");
    S.gold = 3000;
    for (i = 0; i < 3; i++) { Item it; gen_item(&it, 6, i * 300); give_item(&it); }
    open_trade(TN_ARM); snap("t_arm_buy");
    tsel = 2; trade_act(); snap("t_arm_bought");
    ttab = 1; build_sell(); tsel = 0; snap("t_arm_sell");
    trade_close();
    open_trade(TN_GEN); snap("t_gen");
    trade_close();
    talk_open(3); talk_choose(1); snap("t_inn_rumor");
    mode = M_EXPLORE;
    S.wp = 0x1FFFFF; warp_open(); wsel = 9; wscroll = 3; snap("t_warp");
    warp_go(9); while (mode == M_STORY) story_next();
    snap("t_after_warp");
    { Item tp; memset(&tp, 0, sizeof tp); tp.kind = IK_TP; tp.base = (unsigned char)base_of_kind(IK_TP); give_item(&tp); }
    S.floor = 15; gen_floor(); mode = M_EXPLORE;
    for (i = 0; i < INV_N; i++) if (S.inv[i].kind == IK_TP) { isel = 4 + i; mode = M_INV; snap("t_inv_tp"); inv_use(); break; }
    while (mode == M_STORY) story_next();
    S.dir = 2; S.px = MW / 2 + 1; S.py = MH / 2 + 1; mark_seen();
    snap("t_portal_camp");
    S.floor = 12; gen_floor(); mode = M_EXPLORE;
    story_play(1, 1); story_next(); story_next(); snap("t_boss_story");
    while (mode == M_STORY) story_next();
    for (i = 0; i < 40; i++) game_tick();
    snap("t_boss_battle");
    S.floor = 13; gen_floor(); camp_arrival(); snap("t_act2_intro");
}
void harness_unique(void) { int i; S.floor = 7; for (i = 0; i < 200; i++) { start_battle(0); if (B.m[0].uniq && B.n > 1) break; } }
#endif
