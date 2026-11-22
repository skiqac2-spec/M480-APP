/* host test: run static main(String[]) of a class from rt dir + test dir */
#include "../src/jvm.h"
#include "../src/pal.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static const char *dirs[2];
static const u8 *dir_src(void *ctx, const char *name, int *len)
{
    char p[1024]; u32 l; u8 *d;
    snprintf(p, sizeof p, "%s/%s.class", (const char *)ctx, name);
    d = pal_load(p, &l);
    if (d) *len = (int)l;
    return d;
}
void natives_api(void) {}
int main(int argc, char **argv)
{
    Slot a;
    if (argc < 4) { fprintf(stderr, "vmtest rtdir testdir Class\n"); return 1; }
    cls_add_source(dir_src, argv[1]);
    cls_add_source(dir_src, argv[2]);
    if (!vm_boot(4 << 20)) return 1;
    a.l = NULL;
    if (!vm_call_static(argv[3], "main", "([Ljava/lang/String;)V", &a, 1)) return 1;
    while (vm.threads && !vm.quit) {
        vm_run(50);
        if (!vm_runnable()) {
            u32 w = vm_next_wake();
            if (w == 0xFFFFFFFFu) { int alive = 0; Thread *t; for (t = vm.threads; t; t = t->next) if (t->state != TS_DEAD) alive = 1; if (!alive) break; usleep(1000); continue; }
            { s32 d = (s32)(w - pal_ms()); if (d > 0) usleep(d * 1000); }
        }
    }
    return 0;
}
