/* one game session: load package, boot the VM, run */
#include "app.h"
#include "jvm.h"
#include "pkg.h"
#include "host.h"
#include "pal.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>

const char *pkg_prop_first(Pkg *p, int i, const char **val);
extern const u8 rt_jar[];
extern const u32 rt_jar_len;

static Pkg pkg;
static Zip *rtzip;
static int running;

static const u8 *zip_src(void *ctx, const char *name, int *len)
{
    char fn[300];
    u32 l;
    u8 *d;
    snprintf(fn, sizeof fn, "%s.class", name);
    d = zip_extract((Zip *)ctx, fn, &l);
    if (d) *len = (int)l;
    return d;
}
static const u8 *game_src(void *ctx, const char *name, int *len)
{
    (void)ctx;
    return pkg.jar ? zip_src(pkg.jar, name, len) : NULL;
}
static u8 *res_hook(const char *name, u32 *len) { return pkg_resource(&pkg, name, len); }

static int find_main(void)
{
    Class *jlet = cls_find("org/kwis/msp/lcdui/Jlet"), *midlet = cls_find("javax/microedition/midlet/MIDlet");
    int i;
    if (pkg.mainclass[0]) {
        Class *c = cls_find(pkg.mainclass);
        if (c) return 1;
        vm_log("main class %s not found, searching", pkg.mainclass);
    }
    for (i = 0; i < pkg.jar->n; i++) {
        char n[300];
        size_t l;
        Class *c;
        snprintf(n, sizeof n, "%s", pkg.jar->ents[i].name);
        l = strlen(n);
        if (l < 7 || strcmp(n + l - 6, ".class") || strchr(n, '$')) continue;
        n[l - 6] = 0;
        c = cls_find(n);
        if (c && !(c->acc & ACC_ABSTRACT) && ((jlet && cls_is_sub(c, jlet) && c != jlet) || (midlet && cls_is_sub(c, midlet) && c != midlet))) {
            snprintf(pkg.mainclass, sizeof pkg.mainclass, "%s", n);
            vm_log("found main class %s", n);
            return 1;
        }
    }
    return 0;
}

int app_start(const char *path, int carrier, int scr_w, int scr_h, const char *savebase, char *err, int errmax)
{
    int i;
    Slot arg;
    Class *mc, *jlet;
    char tmp[64];
    if (!pkg_open(&pkg, path)) { snprintf(err, errmax, "게임 파일을 열 수 없습니다"); return 0; }
    if (pkg.kind == PKG_KTF_NATIVE || pkg.kind == PKG_LGT_NATIVE || pkg.kind == PKG_CLET) {
        snprintf(err, errmax, "이 게임은 %s 형식(ARM 기계어)이라 아직 지원하지 않습니다.", pkg_kind_text(pkg.kind));
        return 0;
    }
    if (carrier < 0) carrier = pkg.carrier_guess;
    host.carrier = carrier;
    if (scr_w <= 0) { scr_w = pkg.scr_w ? pkg.scr_w : 240; scr_h = pkg.scr_h ? pkg.scr_h : 320; }
    host.screen = surf_alloc(scr_w, scr_h, 0);
    if (!host.screen) { snprintf(err, errmax, "메모리 부족"); return 0; }
    memset(host.screen->px, 0, (size_t)scr_w * scr_h * 2);
    surf_set_screen(host.screen);
    snprintf(host.savedir, sizeof host.savedir, "%s/%s", savebase, pkg.id);
    pal_mkdir(savebase);
    pal_mkdir(host.savedir);
    rtzip = zip_open_mem(rt_jar, rt_jar_len, 0);
    cls_add_source(zip_src, rtzip);
    cls_add_source(game_src, NULL);
    vm_resource_hook = res_hook;
    if (!vm_boot(12u << 20)) { snprintf(err, errmax, "VM 시작 실패"); return 0; }
    for (i = 0; i < pkg.nprops; i++) {
        const char *v, *k = pkg_prop_first(&pkg, i, &v);
        char kk[256];
        snprintf(kk, sizeof kk, "app.%s", k);
        vm_set_prop(kk, v);
    }
    vm_set_prop("microedition.platform", "SCH-M480");
    vm_set_prop("microedition.configuration", "CLDC-1.1");
    vm_set_prop("microedition.profiles", "MIDP-2.0");
    vm_set_prop("microedition.locale", "ko-KR");
    vm_set_prop("microedition.encoding", "KSC5601");
    vm_set_prop("m.CARRIER", carrier == CARRIER_SKT ? "SKT" : carrier == CARRIER_KTF ? "KTF" : "LGT");
    vm_set_prop("CARRIER", carrier == CARRIER_SKT ? "SKT" : carrier == CARRIER_KTF ? "KTF" : "LGT");
    snprintf(tmp, sizeof tmp, "%d", scr_w); vm_set_prop("m.LCD_WIDTH", tmp);
    snprintf(tmp, sizeof tmp, "%d", scr_h); vm_set_prop("m.LCD_HEIGHT", tmp);
    if (!find_main()) { snprintf(err, errmax, "시작 클래스(Jlet/MIDlet)를 찾지 못했습니다"); return 0; }
    mc = cls_find(pkg.mainclass);
    jlet = cls_find("org/kwis/msp/lcdui/Jlet");
    vm_set_prop("m480.main", pkg.mainclass);
    vm_set_prop("m480.type", (jlet && mc && cls_is_sub(mc, jlet)) ? "jlet" : "midlet");
    vm_log("start %s [%s] carrier=%d screen=%dx%d main=%s", pkg.name, pkg.path, carrier, scr_w, scr_h, pkg.mainclass);
    arg.l = NULL;
    if (!vm_call_static("m480/Boot", "main", "([Ljava/lang/String;)V", &arg, 1)) { snprintf(err, errmax, "부트 실패"); return 0; }
    running = 1;
    return 1;
}

int app_step(int budget_ms)
{
    Thread *t;
    int alive = 0;
    if (!running) return 0;
    vm_run(budget_ms);
    for (t = vm.threads; t; t = t->next) if (t->state != TS_DEAD) alive++;
    if (!alive || vm.quit || host.quit_requested) { running = 0; snd_stop_all(); }
    return running;
}

int app_idle_ms(void)
{
    u32 w;
    if (vm_runnable()) return 0;
    w = vm_next_wake();
    if (w == 0xFFFFFFFFu) return 100;
    {
        s32 d = (s32)(w - vm_ticks());
        return d < 0 ? 0 : d > 100 ? 100 : d;
    }
}

const char *app_name(void) { return pkg.name; }
const char *app_id(void) { return pkg.id; }
int app_running(void) { return running; }
void app_request_quit(void) { ev_push(EV_QUIT, 0, 0, 0); }
