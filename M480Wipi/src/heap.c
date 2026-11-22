/* Java heap: one arena, size-class free lists, non-moving mark & sweep.
 * Thread stacks are scanned conservatively (object-start bitmap); objects precisely. */
#include "jvm.h"
#include <stdlib.h>
#include <stdio.h>
#ifdef _WIN32_WCE
#include <windows.h>
#endif

static u8 *hbase, *htop, *hend;
static u32 *startbits;           /* 1 bit per 8 bytes */
#define NSMALL 64                /* free lists for 8..512 bytes */
typedef struct FreeBlk { u32 hdr; u32 pad; struct FreeBlk *next; } FreeBlk;
static FreeBlk *flist[NSMALL + 1];   /* [NSMALL] = large blocks */
static size_t freebytes;
static size_t since_gc;
static Object *tmproots[256];
static int ntmp;
void troot(Object *o) { if (ntmp < 256) tmproots[ntmp++] = o; }
void troot_reset(void) { ntmp = 0; }
static Object ***roots;
static int nroots, caproots;

#define BIT_IDX(p) (((u8 *)(p) - hbase) >> 3)
static inline void bset(void *p) { size_t i = BIT_IDX(p); startbits[i >> 5] |= 1u << (i & 31); }
static inline void bclr(void *p) { size_t i = BIT_IDX(p); startbits[i >> 5] &= ~(1u << (i & 31)); }
static inline int bget(void *p) { size_t i = BIT_IDX(p); return (startbits[i >> 5] >> (i & 31)) & 1; }

int heap_init(size_t bytes)
{
    size_t nb;
    bytes = (bytes + 4095) & ~(size_t)4095;
#ifdef _WIN32_WCE
    /* > 2MB reservations land in the large shared area, outside the 32MB process slot */
    hbase = (u8 *)VirtualAlloc(NULL, bytes, MEM_RESERVE, PAGE_NOACCESS);
    if (hbase && !VirtualAlloc(hbase, bytes, MEM_COMMIT, PAGE_READWRITE)) { VirtualFree(hbase, 0, MEM_RELEASE); hbase = NULL; }
#else
    hbase = (u8 *)malloc(bytes + 8);
    if (hbase) hbase = (u8 *)(((uintptr_t)hbase + 7) & ~(uintptr_t)7);
#endif
    if (!hbase) return 0;
    htop = hbase;
    hend = hbase + bytes;
    nb = (bytes / 8 + 31) / 32;
    startbits = (u32 *)calloc(nb, 4);
    freebytes = 0;
    return startbits != NULL;
}

size_t heap_total_bytes(void) { return (size_t)(hend - hbase); }
size_t heap_free_bytes(void) { return freebytes + (size_t)(hend - htop); }

int heap_is_obj(void *p)
{
    if ((u8 *)p < hbase || (u8 *)p >= htop || ((uintptr_t)p & 7)) return 0;
    return bget(p);
}

void gc_add_root(Object **p)
{
    if (nroots == caproots) {
        caproots = caproots ? caproots * 2 : 64;
        roots = (Object ***)realloc(roots, caproots * sizeof(*roots));
    }
    roots[nroots++] = p;
}
void gc_del_root(Object **p)
{
    int i;
    for (i = 0; i < nroots; i++) if (roots[i] == p) { roots[i] = roots[--nroots]; return; }
}

static void *raw_alloc(size_t *psz)
{
    FreeBlk *b, **pp;
    size_t sz = *psz;
    if (sz <= NSMALL * 8) {
        int k = (int)(sz >> 3);
        b = flist[k - 1];
        if (b) { flist[k - 1] = b->next; freebytes -= sz; return b; }
    }
    if ((size_t)(hend - htop) >= sz) { void *p = htop; htop += sz; return p; }
    /* first fit in large list, split remainder */
    for (pp = &flist[NSMALL]; (b = *pp) != NULL; pp = &b->next) {
        size_t bs = HDR_SIZE(b->hdr);
        if (bs >= sz) {
            *pp = b->next;
            freebytes -= bs;
            if (bs - sz >= 16) {
                FreeBlk *rest = (FreeBlk *)((u8 *)b + sz);
                size_t rs = bs - sz;
                rest->hdr = (u32)rs | HDR_FREE;
                if (rs <= NSMALL * 8) { rest->next = flist[(rs >> 3) - 1]; flist[(rs >> 3) - 1] = rest; }
                else { rest->next = flist[NSMALL]; flist[NSMALL] = rest; }
                freebytes += rs;
            } else sz = bs;
            *psz = sz;
            return b;
        }
    }
    /* small request: try bigger small lists */
    if (sz <= NSMALL * 8) {
        int k;
        for (k = (int)(sz >> 3) + 2; k <= NSMALL; k++) {
            b = flist[k - 1];
            if (b) {
                size_t bs = (size_t)k * 8, rs = bs - sz;
                flist[k - 1] = b->next;
                freebytes -= bs;
                if (rs >= 16) {
                    FreeBlk *rest = (FreeBlk *)((u8 *)b + sz);
                    rest->hdr = (u32)rs | HDR_FREE;
                    rest->next = flist[(rs >> 3) - 1]; flist[(rs >> 3) - 1] = rest;
                    freebytes += rs;
                } else sz = bs;
                *psz = sz;
                return b;
            }
        }
    }
    return NULL;
}

static Object *alloc_obj(Thread *t, size_t sz)
{
    Object *o;
    sz = (sz + 7) & ~(size_t)7;
    if (sz < 16) sz = 16;
    if (since_gc > heap_total_bytes() / 2) { gc_collect(); }
    o = (Object *)raw_alloc(&sz);
    if (!o) { gc_collect(); o = (Object *)raw_alloc(&sz); }
    if (!o) {
        vm_log("OutOfMemory: request %u, free %u", (unsigned)sz, (unsigned)heap_free_bytes());
        if (t) throw_new(t, "java/lang/OutOfMemoryError", NULL);
        return NULL;
    }
    memset(o, 0, sz);
    o->hdr = (u32)sz;
    bset(o);
    since_gc += sz;
    return o;
}

Object *obj_new(Thread *t, Class *c)
{
    Object *o = alloc_obj(t, sizeof(Object) + (size_t)c->inst_slots * sizeof(Slot));
    if (o) o->cls = c;
    return o;
}

Array *arr_new(Thread *t, Class *ac, s32 n)
{
    Array *a;
    if (n < 0) { if (t) throw_new(t, "java/lang/NegativeArraySizeException", NULL); return NULL; }
    if ((u32)n > 64u * 1024 * 1024 / (u32)ac->elemsize) { if (t) throw_new(t, "java/lang/OutOfMemoryError", NULL); return NULL; }
    a = (Array *)alloc_obj(t, ARR_HDR + (size_t)n * ac->elemsize);
    if (a) { a->o.cls = ac; a->length = n; }
    return a;
}

Array *arr_newp(Thread *t, char type, s32 n) { return arr_new(t, cls_prim_array(type), n); }

/* ---------------- mark ---------------- */
static Object **mstack;
static int msp, mcap;
static int moverflow;

static void mark(Object *o)
{
    if (!o || (o->hdr & HDR_MARK)) return;
    o->hdr |= HDR_MARK;
    if (msp == mcap) {
        int nc = mcap ? mcap * 2 : 4096;
        Object **ns = (Object **)realloc(mstack, nc * sizeof(Object *));
        if (!ns) { moverflow = 1; return; }
        mstack = ns; mcap = nc;
    }
    mstack[msp++] = o;
}

static void mark_conservative(Slot *from, Slot *to)
{
    for (; from < to; from++) {
        void *p = from->l;
        if (heap_is_obj(p)) mark((Object *)p);
    }
}

static void scan(Object *o)
{
    Class *c = o->cls;
    if (!c) return;
    if (c->is_array) {
        if (c->elemtype == 'L') {
            Array *a = (Array *)o;
            Object **e = ARR_L(a);
            s32 i;
            for (i = 0; i < a->length; i++) if (e[i]) mark(e[i]);
        }
    } else {
        Slot *f = OBJ_FIELDS(o);
        int i;
        for (i = 0; i < c->inst_slots; i++) if (c->refmap[i] && f[i].l) mark(f[i].l);
    }
}

static void drain(void)
{
    while (msp > 0) scan(mstack[--msp]);
}

/* ---------------- sweep ---------------- */
static void sweep(void)
{
    u8 *p = hbase, *run = NULL;
    size_t runsz = 0;
    int k;
    for (k = 0; k <= NSMALL; k++) flist[k] = NULL;
    freebytes = 0;
#define FLUSH_RUN() do { if (run) { FreeBlk *fb = (FreeBlk *)run; fb->hdr = (u32)runsz | HDR_FREE; \
        if (runsz <= NSMALL * 8) { fb->next = flist[(runsz >> 3) - 1]; flist[(runsz >> 3) - 1] = fb; } \
        else { fb->next = flist[NSMALL]; flist[NSMALL] = fb; } freebytes += runsz; run = NULL; runsz = 0; } } while (0)
    while (p < htop) {
        Object *o = (Object *)p;
        size_t sz = HDR_SIZE(o->hdr);
        if (sz == 0) { vm_log("heap corrupt at %p", (void *)p); break; }
        if (o->hdr & HDR_FREE) {
            if (!run) run = p;
            runsz += sz;
        } else if (o->hdr & HDR_MARK) {
            o->hdr &= ~HDR_MARK;
            FLUSH_RUN();
        } else {
            if (o->cls && o->cls->finalizer) o->cls->finalizer(o);
            bclr(o);
            o->hdr = (u32)sz | HDR_FREE;
            if (!run) run = p;
            runsz += sz;
        }
        p += sz;
    }
    if (run && run + runsz == htop) { htop = run; run = NULL; runsz = 0; }
    FLUSH_RUN();
}

void gc_collect(void)
{
    Thread *t;
    int i;
    moverflow = 0;
    msp = 0;
    for (t = vm.threads; t; t = t->next) {
        if (t->state == TS_DEAD) continue;
        if (t->jthread) mark(t->jthread);
        if (t->exception) mark(t->exception);
        if (t->wait_obj) mark(t->wait_obj);
        if (t->blocked_on) mark(t->blocked_on);
        if (t->depth > 0) {
            Frame *f = &t->frames[t->depth - 1];
            Slot *top = f->base + f->m->max_stack + 2;
            if (top > t->stack_end) top = t->stack_end;
            mark_conservative(t->stack, top);
        }
        for (i = 0; i < t->depth; i++) if (t->frames[i].sync) mark(t->frames[i].sync);
    }
    cls_gc_mark(mark);
    for (i = 0; i < nroots; i++) if (*roots[i]) mark(*roots[i]);
    for (i = 0; i < ntmp; i++) mark(tmproots[i]);
    { extern void str_intern_mark(void (*)(Object *)); str_intern_mark(mark); }
    drain();
    if (moverflow) {
        /* rescan heap for marked objects until stable */
        int again = 1;
        while (again) {
            u8 *p;
            again = 0;
            moverflow = 0;
            for (p = hbase; p < htop; p += HDR_SIZE(((Object *)p)->hdr)) {
                Object *o = (Object *)p;
                if ((o->hdr & HDR_MARK) && !(o->hdr & HDR_FREE)) { scan(o); drain(); }
            }
            if (moverflow) again = 1;
        }
    }
    sweep();
    since_gc = 0;
}
