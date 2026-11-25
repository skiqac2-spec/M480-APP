/* Which game file belongs to which system (and so to which emu_<id>.exe), plus zip support. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "host.h"

static const struct { const char *id; const wch *tag; const char *exts; } reg[] = {
    { "ps1",  u"PS1",          ".cue.img.iso.pbp.mdf.ccd.m3u.cbn.psx.psexe." },
    { "gba",  u"GBA",          ".gba.agb." },
    { "nes",  u"패미컴",       ".nes.fds.unf.unif." },
    { "snes", u"슈퍼패미컴",   ".smc.sfc.swc.fig." },
    { "gb",   u"게임보이",     ".gb.gbc.dmg.sgb." },
    { "md",   u"메가드라이브", ".md.gen.smd.sms.gg.sg." },
};
#define NREG ((int)(sizeof(reg) / sizeof(reg[0])))
static signed char avail[NREG];          /* 0 unknown, 1 yes, -1 no */

int sysreg_count(void) { return NREG; }
const char *sysreg_id(int i) { return reg[i].id; }
const wch *sysreg_tag(int i) { return reg[i].tag; }

int sysreg_self(void)
{
    int i;
    for (i = 0; i < NREG; i++)
        if (!strcmp(reg[i].id, sys.id)) return i;
    return -1;
}

int sysreg_available(int i)
{
    char path[HPATH];
    if (i < 0 || i >= NREG) return 0;
    if (!avail[i]) {
        if (i == sysreg_self()) avail[i] = 1;
        else {
            snprintf(path, sizeof(path), "%s%cemu_%s.exe", plat_app_dir(), PSEP, reg[i].id);
            avail[i] = plat_file_exists(path) ? 1 : -1;
        }
    }
    return avail[i] > 0;
}

static void lower_ext(const char *name, char *out, int max)   /* ".gba." style, lower case */
{
    const char *e = strrchr(name, '.');
    int n = 0;
    out[0] = 0;
    if (!e) return;
    while (e[n] && n < max - 2) { char c = e[n]; out[n++] = (c >= 'A' && c <= 'Z') ? c + 32 : c; }
    out[n++] = '.'; out[n] = 0;
}

static int by_ext(const char *name)
{
    char ext[16];
    int i;
    lower_ext(name, ext, sizeof(ext));
    if (!ext[0] || !ext[2]) return -1;
    for (i = 0; i < NREG; i++)
        if (strstr(reg[i].exts, ext)) return i;
    return -1;
}

static int find_id(const char *id)
{
    int i;
    for (i = 0; i < NREG; i++) if (!strcmp(reg[i].id, id)) return i;
    return -1;
}

/* .bin can be a PlayStation data track or a Mega Drive cartridge; .exe a PS-X program */
static int sniff(const char *dir, const char *name, const char *ext)
{
    char path[HPATH * 2];
    unsigned char hd[0x110];
    FILE *f;
    size_t n;
    int r = -1;

    snprintf(path, sizeof(path), "%s%c%s", dir, PSEP, name);
    f = fopen(path, "rb");
    if (!f) return -1;
    n = fread(hd, 1, sizeof(hd), f);
    if (!strcmp(ext, ".exe.")) {
        if (n >= 8 && !memcmp(hd, "PS-X EXE", 8)) r = find_id("ps1");
    } else if (n == sizeof(hd)) {
        static const unsigned char sync[12] = { 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0 };
        if (!memcmp(hd + 0x100, "SEGA", 4) || !memcmp(hd + 0x101, "SEGA", 4)) r = find_id("md");
        else if (!memcmp(hd, sync, 12)) r = find_id("ps1");
        else {
            long size;
            fseek(f, 0, SEEK_END); size = ftell(f);
            r = find_id(size > 16L * 1024 * 1024 ? "ps1" : "md");
        }
    }
    fclose(f);
    return r;
}

int sysreg_classify(const char *dir, const char *name)
{
    char ext[16];
    int i;
    lower_ext(name, ext, sizeof(ext));
    if (!ext[0]) return -1;
    if (!strcmp(ext, ".zip.")) return -2;
    if (!strcmp(ext, ".bin.") || !strcmp(ext, ".exe.")) i = sniff(dir, name, ext);
    else i = by_ext(name);
    return (i >= 0 && sysreg_available(i)) ? i : -1;
}

/* ---- zip: find and extract the first ROM ---------------------------------------- */
static unsigned rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned long rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24); }

struct zent { char name[HPATH]; unsigned long csize, usize, lho; unsigned method; int sys; };

static int zip_find(FILE *f, struct zent *ze)
{
    unsigned char *buf, hd[46];
    long size, tail, i, cd_off = -1;
    unsigned n, k, count = 0;

    fseek(f, 0, SEEK_END); size = ftell(f);
    tail = size < 66000 ? size : 66000;
    buf = malloc((size_t)tail);
    if (!buf) return -1;
    fseek(f, size - tail, SEEK_SET);
    if (fread(buf, 1, (size_t)tail, f) != (size_t)tail) { free(buf); return -1; }
    for (i = tail - 22; i >= 0; i--)
        if (buf[i] == 'P' && buf[i + 1] == 'K' && buf[i + 2] == 5 && buf[i + 3] == 6) {
            count = rd16(buf + i + 10); cd_off = (long)rd32(buf + i + 16); break;
        }
    free(buf);
    if (cd_off < 0) return -1;
    fseek(f, cd_off, SEEK_SET);
    for (k = 0; k < count; k++) {
        unsigned nl, el, cl;
        if (fread(hd, 1, 46, f) != 46 || rd32(hd) != 0x02014b50UL) return -1;
        nl = rd16(hd + 28); el = rd16(hd + 30); cl = rd16(hd + 32);
        n = nl < HPATH - 1 ? nl : HPATH - 1;
        if (fread(ze->name, 1, n, f) != n) return -1;
        ze->name[n] = 0;
        fseek(f, (long)(nl - n + el + cl), SEEK_CUR);
        ze->method = rd16(hd + 10); ze->csize = rd32(hd + 20); ze->usize = rd32(hd + 24); ze->lho = rd32(hd + 42);
        ze->sys = by_ext(ze->name);
        if (ze->sys < 0) {
            char ext[16];
            lower_ext(ze->name, ext, sizeof(ext));
            if (!strcmp(ext, ".bin.")) ze->sys = find_id("md");       /* zipped .bin: cartridge dump */
        }
        if (ze->sys >= 0 && ze->sys != find_id("ps1") && ze->usize > 0 && sysreg_available(ze->sys))
            return 0;
    }
    return -1;
}

/* returns the system index of the first ROM inside, without extracting */
static int zip_peek(const char *zip_path)
{
    struct zent ze;
    FILE *f = fopen(zip_path, "rb");
    int r = -1;
    if (!f) return -1;
    if (zip_find(f, &ze) == 0) r = ze.sys;
    fclose(f);
    return r;
}

int zip_extract_rom(const char *zip_path, char *out_path)
{
    struct zent ze;
    unsigned char lh[30], *in = NULL, *out = NULL;
    FILE *f, *o = NULL;
    const char *base;
    z_stream z;
    unsigned long left, total = 0;
    int ret = -1, zr = Z_OK, zinit = 0;
    char *p;

    if (!out_path) return zip_peek(zip_path);
    f = fopen(zip_path, "rb");
    if (!f) return -1;
    if (zip_find(f, &ze) != 0) goto done;
    base = ze.name;
    for (p = ze.name; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    snprintf(out_path, HPATH, "%s%ctemp", plat_app_dir(), PSEP);
    plat_mkdir(out_path);
    snprintf(out_path, HPATH, "%s%ctemp%c%s", plat_app_dir(), PSEP, PSEP, base);

    o = fopen(out_path, "rb");                      /* already extracted earlier? */
    if (o) {
        long have;
        fseek(o, 0, SEEK_END); have = ftell(o); fclose(o); o = NULL;
        if ((unsigned long)have == ze.usize) { ret = ze.sys; goto done; }
    }
    {   /* keep only one unpacked ROM around */
        char dir[HPATH], victim[HPATH * 2];
        struct dirent_h *ents = NULL;
        int n, i;
        snprintf(dir, sizeof(dir), "%s%ctemp", plat_app_dir(), PSEP);
        n = plat_list_dir(dir, &ents);
        for (i = 0; i < n; i++)
            if (!ents[i].is_dir) {
                snprintf(victim, sizeof(victim), "%s%c%s", dir, PSEP, ents[i].name);
                plat_remove(victim);
            }
        free(ents);
    }
    fseek(f, (long)ze.lho, SEEK_SET);
    if (fread(lh, 1, 30, f) != 30 || rd32(lh) != 0x04034b50UL) goto done;
    fseek(f, (long)(rd16(lh + 26) + rd16(lh + 28)), SEEK_CUR);
    in = malloc(32768); out = malloc(65536);
    o = fopen(out_path, "wb");
    if (!in || !out || !o) goto done;
    left = ze.csize;
    if (ze.method == 0) {
        while (left) {
            size_t n = left > 32768 ? 32768 : (size_t)left;
            if (fread(in, 1, n, f) != n || fwrite(in, 1, n, o) != n) goto done;
            left -= n; total += n;
        }
    } else if (ze.method == 8) {
        memset(&z, 0, sizeof(z));
        if (inflateInit2(&z, -15) != Z_OK) goto done;
        zinit = 1;
        while (zr != Z_STREAM_END) {
            if (z.avail_in == 0) {
                size_t n = left > 32768 ? 32768 : (size_t)left;
                if (n == 0 || fread(in, 1, n, f) != n) goto done;
                left -= n; z.next_in = in; z.avail_in = (uInt)n;
            }
            z.next_out = out; z.avail_out = 65536;
            zr = inflate(&z, Z_NO_FLUSH);
            if (zr != Z_OK && zr != Z_STREAM_END) goto done;
            if (fwrite(out, 1, 65536 - z.avail_out, o) != 65536 - z.avail_out) goto done;
            total += 65536 - z.avail_out;
        }
    } else goto done;
    if (total == ze.usize) ret = ze.sys;
done:
    if (zinit) inflateEnd(&z);
    if (o) { if (fclose(o) != 0) ret = -1; if (ret < 0) plat_remove(out_path); }
    fclose(f);
    free(in); free(out);
    return ret;
}
