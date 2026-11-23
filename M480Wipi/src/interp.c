/* bytecode interpreter, green threads, monitors, exceptions */
#include "jvm.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

VM vm;
static Thread *rr;

#define STACK_SLOTS (24 * 1024)
#define MAX_DEPTH 768

/* quick opcodes (unused by the JVM spec) */
enum {
    Q_GETFIELD1 = 0xCB, Q_GETFIELD2, Q_PUTFIELD1, Q_PUTFIELD2,
    Q_GETSTATIC1, Q_GETSTATIC2, Q_PUTSTATIC1, Q_PUTSTATIC2,
    Q_INVOKEVIRTUAL, Q_INVOKESPECIAL, Q_INVOKESTATIC, Q_NEW, Q_LDC_W, Q_LDC
};

Thread *thread_new(Object *jthread)
{
    Thread *t = (Thread *)calloc(1, sizeof(Thread));
    Thread **pp;
    t->stack = (Slot *)calloc(STACK_SLOTS, sizeof(Slot));
    t->stack_end = t->stack + STACK_SLOTS;
    t->frames = (Frame *)calloc(MAX_DEPTH, sizeof(Frame));
    t->maxdepth = MAX_DEPTH;
    t->id = ++vm.next_tid;
    t->jthread = jthread;
    t->state = TS_RUNNABLE;
    for (pp = &vm.threads; *pp; pp = &(*pp)->next) {}
    *pp = t;
    return t;
}

static void thread_free_dead(void)
{
    Thread **pp = &vm.threads, *t;
    while ((t = *pp) != NULL) {
        if (t->state == TS_DEAD && t != vm.cur) {
            *pp = t->next;
            if (rr == t) rr = NULL;
            free(t->stack); free(t->frames); free(t);
        } else pp = &t->next;
    }
}

/* push a frame whose arguments are copied from args (or already in place when args == NULL
 * and the method takes none) */
int interp_push(Thread *t, Method *m, Slot *args)
{
    Frame *f, *prev = t->depth ? &t->frames[t->depth - 1] : NULL;
    Slot *locals;
    if (t->depth >= t->maxdepth) { throw_new(t, "java/lang/StackOverflowError", NULL); return 0; }
    locals = prev ? prev->sp : t->stack;
    if (locals + m->max_locals + m->max_stack + 4 > t->stack_end) { throw_new(t, "java/lang/StackOverflowError", NULL); return 0; }
    if (args && m->nargs) memmove(locals, args, m->nargs * sizeof(Slot));
    f = &t->frames[t->depth++];
    memset(f, 0, sizeof(*f));
    f->m = m;
    f->pc = m->code;
    f->locals = locals;
    f->base = locals + m->max_locals;
    f->sp = f->base;
    if (prev) prev->sp = locals;     /* nothing of caller above this point */
    return 1;
}

int thread_start_method(Thread *t, Method *m, Slot *args, int nargs)
{
    (void)nargs;
    if (!interp_push(t, m, args)) return 0;
    t->frames[t->depth - 1].boundary = 1;
    if (m->acc & ACC_SYNCHRONIZED) {}
    return 1;
}

/* ---------------- exceptions ---------------- */
static char last_trace[512];

static void record_trace(Thread *t, Object *ex)
{
    int i, n = 0;
    char *p = last_trace;
    p += snprintf(p, 120, "%s", ex && ex->cls ? ex->cls->name : "?");
    for (i = t->depth - 1; i >= 0 && n < 6; i--, n++) {
        Frame *f = &t->frames[i];
        if (p - last_trace > (int)sizeof(last_trace) - 100) break;
        p += snprintf(p, 100, "\n  at %s.%s @%d", f->m->cls->name, f->m->name, f->m->code ? (int)(f->pc - f->m->code) : -1);
    }
}

int throw_new(Thread *t, const char *cname, const char *msg)
{
    Class *c = cls_find(cname);
    Object *o;
    if (!c) { vm_log("throw: class %s missing", cname); c = C_Throwable; }
    o = obj_new(NULL, c);
    if (!o) { vm_log("throw %s: out of memory", cname); o = NULL; }
    if (o) {
        troot(o);
        if (msg) OBJ_FIELDS(o)[F_Throwable_msg].l = str_new_utf8(NULL, msg);
    }
    t->exception = o;
    if (o) record_trace(t, o);
    return NR_EXC;
}
int throw_npe(Thread *t) { return throw_new(t, "java/lang/NullPointerException", NULL); }

void vm_print_stack(Thread *t)
{
    int i;
    for (i = t->depth - 1; i >= 0; i--) {
        Frame *f = &t->frames[i];
        vm_log("  at %s.%s%s pc=%d", f->m->cls->name, f->m->name, f->m->desc, f->m->code ? (int)(f->pc - f->m->code) : -1);
    }
}

/* ---------------- monitors ---------------- */
#define LOCK_OWNER(o) ((o)->lock & 0xFFFF)
#define LOCK_COUNT(o) ((o)->lock >> 16)

int monitor_enter(Thread *t, Object *o)
{
    if (o->lock == 0) { o->lock = (u32)t->id | (1u << 16); return 1; }
    if (LOCK_OWNER(o) == (u32)t->id) { o->lock += 1u << 16; return 1; }
    t->blocked_on = o;
    t->state = TS_BLOCKED;
    return 0;
}

int monitor_exit(Thread *t, Object *o)
{
    if (LOCK_OWNER(o) != (u32)t->id) return 0;
    o->lock -= 1u << 16;
    if (LOCK_COUNT(o) == 0) o->lock = 0;
    return 1;
}

/* Object.wait: first call releases and waits; re-executed call re-acquires */
int monitor_wait(Thread *t, Object *o, s64 ms)
{
    if (t->ret_kind == 1 && t->wait_obj == o) {
        if (o->lock == 0) {
            o->lock = (u32)t->id | (t->wait_lockcount << 16);
            t->ret_kind = 0; t->wait_obj = NULL;
            if (t->interrupted) { t->interrupted = 0; return throw_new(t, "java/lang/InterruptedException", NULL); }
            return NR_OK;
        }
        t->blocked_on = o;
        t->state = TS_BLOCKED;
        return NR_RETRY;
    }
    if (LOCK_OWNER(o) != (u32)t->id) return throw_new(t, "java/lang/IllegalMonitorStateException", NULL);
    t->wait_lockcount = LOCK_COUNT(o);
    o->lock = 0;
    t->wait_obj = o;
    t->ret_kind = 1;
    t->state = TS_WAITING;
    t->wake = ms > 0 ? vm.now + (u32)(ms > 0x3FFFFFFF ? 0x3FFFFFFF : ms) : 0;
    return NR_RETRY;
}

int monitor_notify(Thread *t, Object *o, int all)
{
    Thread *w;
    if (LOCK_OWNER(o) != (u32)t->id) return throw_new(t, "java/lang/IllegalMonitorStateException", NULL);
    for (w = vm.threads; w; w = w->next) {
        if (w->state == TS_WAITING && w->wait_obj == o) {
            w->state = TS_RUNNABLE;
            if (!all) break;
        }
    }
    return NR_OK;
}

void vm_wake_event(void)
{
    Thread *w;
    for (w = vm.threads; w; w = w->next)
        if (w->state == TS_EVENTWAIT) w->state = TS_RUNNABLE;
}

static void update_states(void)
{
    Thread *w;
    u32 now = vm.now;
    for (w = vm.threads; w; w = w->next) {
        switch (w->state) {
        case TS_SLEEPING:
            if ((s32)(now - w->wake) >= 0) w->state = TS_RUNNABLE;
            break;
        case TS_WAITING:
        case TS_EVENTWAIT:
            if (w->wake && (s32)(now - w->wake) >= 0) w->state = TS_RUNNABLE;
            if (w->interrupted && w->state == TS_WAITING) w->state = TS_RUNNABLE;
            break;
        case TS_BLOCKED:
            if (!w->blocked_on || w->blocked_on->lock == 0) { w->state = TS_RUNNABLE; w->blocked_on = NULL; }
            break;
        }
    }
}

int vm_runnable(void)
{
    Thread *w;
    update_states();
    for (w = vm.threads; w; w = w->next) if (w->state == TS_RUNNABLE) return 1;
    return 0;
}

u32 vm_next_wake(void)
{
    Thread *w;
    u32 best = 0xFFFFFFFFu;
    for (w = vm.threads; w; w = w->next) {
        if ((w->state == TS_SLEEPING || ((w->state == TS_WAITING || w->state == TS_EVENTWAIT) && w->wake)) && w->wake - vm.now < best - vm.now)
            best = w->wake;
    }
    return best;
}

/* ---------------- helpers ---------------- */
static s32 f2i(float f)
{
    if (f != f) return 0;
    if (f >= 2147483647.0f) return 0x7FFFFFFF;
    if (f <= -2147483648.0f) return (s32)0x80000000;
    return (s32)f;
}
static s32 d2i(double f)
{
    if (f != f) return 0;
    if (f >= 2147483647.0) return 0x7FFFFFFF;
    if (f <= -2147483648.0) return (s32)0x80000000;
    return (s32)f;
}
static s64 f2l(double f)
{
    if (f != f) return 0;
    if (f >= 9223372036854775807.0) return (s64)0x7FFFFFFFFFFFFFFFLL;
    if (f <= -9223372036854775808.0) return (s64)0x8000000000000000ULL;
    return (s64)f;
}

static Class *resolve_class(Thread *t, Class *c, int idx)
{
    CP *e = &c->cp[idx];
    if (!(e->flags & CPF_RESOLVED)) {
        const char *n = c->cp[e->a].v.utf8;
        Class *k = cls_find_sym(n);
        if (!k) { throw_new(t, "java/lang/NoClassDefFoundError", n); return NULL; }
        e->v.cls = k;
        e->flags |= CPF_RESOLVED;
    }
    return e->v.cls;
}

static Field *resolve_field(Thread *t, Class *c, int idx)
{
    CP *e = &c->cp[idx];
    if (!(e->flags & CPF_RESOLVED)) {
        Class *k = resolve_class(t, c, e->a);
        CP *nt;
        Field *f;
        if (!k) return NULL;
        nt = &c->cp[e->b];
        f = cls_field(k, c->cp[nt->a].v.utf8, c->cp[nt->b].v.utf8);
        if (!f) {
            vm_log("no field %s.%s", k->name, c->cp[nt->a].v.utf8);
            throw_new(t, "java/lang/NoSuchFieldError", c->cp[nt->a].v.utf8);
            return NULL;
        }
        e->v.field = f;
        e->flags |= CPF_RESOLVED;
    }
    return e->v.field;
}

static Method *resolve_method(Thread *t, Class *c, int idx, int is_static)
{
    CP *e = &c->cp[idx];
    if (!(e->flags & CPF_RESOLVED)) {
        Class *k = resolve_class(t, c, e->a);
        CP *nt;
        Method *m;
        const char *n, *d;
        if (!k) return NULL;
        nt = &c->cp[e->b];
        n = c->cp[nt->a].v.utf8; d = c->cp[nt->b].v.utf8;
        m = cls_method(k, n, d);
        if (!m) m = cls_imethod(k, n, d);
        if (!m && (k->acc & ACC_INTERFACE)) m = cls_method(C_Object, n, d);
        if (!m) m = cls_stub_method(k, n, d, is_static);
        if (!m) {
            vm_log("no method %s.%s%s", k->name, n, d);
            throw_new(t, "java/lang/NoSuchMethodError", n);
            return NULL;
        }
        e->v.method = m;
        e->flags |= CPF_RESOLVED;
    }
    return e->v.method;
}

static Object *resolve_string(Thread *t, Class *c, int idx)
{
    CP *e = &c->cp[idx];
    if (!(e->flags & CPF_RESOLVED)) {
        Object *s = str_new_utf8(t, c->cp[e->a].v.utf8);
        if (!s) return NULL;
        s = str_intern(t, s);
        e->v.str = s;
        e->flags |= CPF_RESOLVED;
    }
    return e->v.str;
}

static Array *multi_new(Thread *t, Class *ac, s32 *dims, int nd)
{
    Array *a = arr_new(t, ac, dims[0]);
    s32 i;
    if (!a || nd == 1 || ac->elemtype != 'L' || !ac->elem->is_array) return a;
    troot(&a->o);
    for (i = 0; i < dims[0]; i++) {
        Array *sub = multi_new(t, ac->elem, dims + 1, nd - 1);
        if (!sub) return NULL;
        ARR_L(a)[i] = &sub->o;
    }
    return a;
}

/* ---------------- interpreter ---------------- */
#define U2(p) ((u16)(((p)[0] << 8) | (p)[1]))
#define S2(p) ((s16)U2(p))
#define S4(p) ((s32)(((u32)(p)[0] << 24) | ((u32)(p)[1] << 16) | ((u32)(p)[2] << 8) | (p)[3]))

#define SAVE() do { fp->pc = pc; fp->sp = sp; } while (0)
#define LOAD() do { fp = &t->frames[t->depth - 1]; m = fp->m; pc = fp->pc; sp = fp->sp; locals = fp->locals; cls = m->cls; } while (0)
#define THROW(name) do { SAVE(); throw_new(t, name, NULL); goto exception; } while (0)
#define NPE() THROW("java/lang/NullPointerException")
#define CHECKNULL(o) do { if (!(o)) NPE(); } while (0)
#define ACHECK(a, i) do { if (!(a)) NPE(); if ((u32)(i) >= (u32)(a)->length) { SAVE(); throw_new(t, "java/lang/ArrayIndexOutOfBoundsException", NULL); goto exception; } } while (0)
#define NEXT(n) do { pc += (n); goto next; } while (0)

/* run one thread for up to 'budget' operations; returns when it blocks, yields or dies */
static void run_thread(Thread *t, int budget)
{
    Frame *fp;
    Method *m;
    Class *cls;
    u8 *pc;
    Slot *sp, *locals;
    Method *callee;
    int r;

    vm.cur = t;
    if (t->depth == 0) { t->state = TS_DEAD; return; }
    LOAD();
    if (t->exception) goto exception;

next:
    switch (*pc) {
    case 0x00: NEXT(1);
    case 0x01: sp->l = NULL; sp++; NEXT(1);
    case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08:
        sp->i = (s32)*pc - 3; sp++; NEXT(1);
    case 0x09: case 0x0a: slot_setj(sp, *pc - 9); sp += 2; NEXT(1);
    case 0x0b: case 0x0c: case 0x0d: sp->f = (float)(*pc - 0x0b); sp++; NEXT(1);
    case 0x0e: case 0x0f: slot_setd(sp, (double)(*pc - 0x0e)); sp += 2; NEXT(1);
    case 0x10: sp->i = (s8)pc[1]; sp++; NEXT(2);
    case 0x11: sp->i = S2(pc + 1); sp++; NEXT(3);
    case 0x12: case 0x13: case Q_LDC: case Q_LDC_W: {
        int wide = (*pc == 0x13 || *pc == Q_LDC_W);
        int idx = wide ? U2(pc + 1) : pc[1];
        CP *e = &cls->cp[idx];
        if (e->tag == 3 || e->tag == 4) sp->i = e->v.i;
        else if (e->tag == 8) { SAVE(); sp->l = resolve_string(t, cls, idx); if (!sp->l) goto exception; }
        else if (e->tag == 7) {
            Class *k;
            SAVE();
            k = resolve_class(t, cls, idx);
            if (!k) goto exception;
            sp->l = cls_mirror(k);
        } else sp->i = 0;
        sp++;
        NEXT(wide ? 3 : 2);
    }
    case 0x14: { CP *e = &cls->cp[U2(pc + 1)]; slot_setj(sp, e->v.j); sp += 2; NEXT(3); }
    case 0x15: case 0x17: case 0x19: *sp++ = locals[pc[1]]; NEXT(2);
    case 0x16: case 0x18: sp[0] = locals[pc[1]]; sp[1] = locals[pc[1] + 1]; sp += 2; NEXT(2);
    case 0x1a: case 0x1b: case 0x1c: case 0x1d: *sp++ = locals[*pc - 0x1a]; NEXT(1);
    case 0x1e: case 0x1f: case 0x20: case 0x21: { int i = *pc - 0x1e; sp[0] = locals[i]; sp[1] = locals[i + 1]; sp += 2; NEXT(1); }
    case 0x22: case 0x23: case 0x24: case 0x25: *sp++ = locals[*pc - 0x22]; NEXT(1);
    case 0x26: case 0x27: case 0x28: case 0x29: { int i = *pc - 0x26; sp[0] = locals[i]; sp[1] = locals[i + 1]; sp += 2; NEXT(1); }
    case 0x2a: case 0x2b: case 0x2c: case 0x2d: *sp++ = locals[*pc - 0x2a]; NEXT(1);
    /* array loads */
    case 0x2e: case 0x30: { Array *a = (Array *)sp[-2].l; s32 i = sp[-1].i; ACHECK(a, i); sp -= 2; sp->i = ARR_I(a)[i]; sp++; NEXT(1); }
    case 0x32: { Array *a = (Array *)sp[-2].l; s32 i = sp[-1].i; ACHECK(a, i); sp -= 2; sp->l = ARR_L(a)[i]; sp++; NEXT(1); }
    case 0x2f: case 0x31: { Array *a = (Array *)sp[-2].l; s32 i = sp[-1].i; s64 v; ACHECK(a, i); memcpy(&v, (s64 *)ARR_DATA(a) + i, 8); sp -= 2; slot_setj(sp, v); sp += 2; NEXT(1); }
    case 0x33: { Array *a = (Array *)sp[-2].l; s32 i = sp[-1].i; ACHECK(a, i); sp -= 2; sp->i = ARR_B(a)[i]; sp++; NEXT(1); }
    case 0x34: { Array *a = (Array *)sp[-2].l; s32 i = sp[-1].i; ACHECK(a, i); sp -= 2; sp->i = ARR_C(a)[i]; sp++; NEXT(1); }
    case 0x35: { Array *a = (Array *)sp[-2].l; s32 i = sp[-1].i; ACHECK(a, i); sp -= 2; sp->i = ARR_S(a)[i]; sp++; NEXT(1); }
    /* stores */
    case 0x36: case 0x38: case 0x3a: locals[pc[1]] = *--sp; NEXT(2);
    case 0x37: case 0x39: sp -= 2; locals[pc[1]] = sp[0]; locals[pc[1] + 1] = sp[1]; NEXT(2);
    case 0x3b: case 0x3c: case 0x3d: case 0x3e: locals[*pc - 0x3b] = *--sp; NEXT(1);
    case 0x3f: case 0x40: case 0x41: case 0x42: { int i = *pc - 0x3f; sp -= 2; locals[i] = sp[0]; locals[i + 1] = sp[1]; NEXT(1); }
    case 0x43: case 0x44: case 0x45: case 0x46: locals[*pc - 0x43] = *--sp; NEXT(1);
    case 0x47: case 0x48: case 0x49: case 0x4a: { int i = *pc - 0x47; sp -= 2; locals[i] = sp[0]; locals[i + 1] = sp[1]; NEXT(1); }
    case 0x4b: case 0x4c: case 0x4d: case 0x4e: locals[*pc - 0x4b] = *--sp; NEXT(1);
    case 0x4f: case 0x51: { Array *a = (Array *)sp[-3].l; s32 i = sp[-2].i; ACHECK(a, i); ARR_I(a)[i] = sp[-1].i; sp -= 3; NEXT(1); }
    case 0x50: case 0x52: { Array *a = (Array *)sp[-4].l; s32 i = sp[-3].i; s64 v = slot_j(sp - 2); ACHECK(a, i); memcpy((s64 *)ARR_DATA(a) + i, &v, 8); sp -= 4; NEXT(1); }
    case 0x53: {
        Array *a = (Array *)sp[-3].l; s32 i = sp[-2].i; Object *v = sp[-1].l;
        ACHECK(a, i);
        if (v && a->o.cls->elem && !cls_is_sub(v->cls, a->o.cls->elem)) THROW("java/lang/ArrayStoreException");
        ARR_L(a)[i] = v; sp -= 3; NEXT(1);
    }
    case 0x54: { Array *a = (Array *)sp[-3].l; s32 i = sp[-2].i; ACHECK(a, i); ARR_B(a)[i] = (s8)sp[-1].i; sp -= 3; NEXT(1); }
    case 0x55: { Array *a = (Array *)sp[-3].l; s32 i = sp[-2].i; ACHECK(a, i); ARR_C(a)[i] = (u16)sp[-1].i; sp -= 3; NEXT(1); }
    case 0x56: { Array *a = (Array *)sp[-3].l; s32 i = sp[-2].i; ACHECK(a, i); ARR_S(a)[i] = (s16)sp[-1].i; sp -= 3; NEXT(1); }
    /* stack */
    case 0x57: sp--; NEXT(1);
    case 0x58: sp -= 2; NEXT(1);
    case 0x59: sp[0] = sp[-1]; sp++; NEXT(1);
    case 0x5a: { Slot a = sp[-1], b = sp[-2]; sp[-2] = a; sp[-1] = b; sp[0] = a; sp++; NEXT(1); }
    case 0x5b: { Slot a = sp[-1], b = sp[-2], c = sp[-3]; sp[-3] = a; sp[-2] = c; sp[-1] = b; sp[0] = a; sp++; NEXT(1); }
    case 0x5c: sp[0] = sp[-2]; sp[1] = sp[-1]; sp += 2; NEXT(1);
    case 0x5d: { Slot a = sp[-1], b = sp[-2], c = sp[-3]; sp[-3] = b; sp[-2] = a; sp[-1] = c; sp[0] = b; sp[1] = a; sp += 2; NEXT(1); }
    case 0x5e: { Slot a = sp[-1], b = sp[-2], c = sp[-3], d = sp[-4]; sp[-4] = b; sp[-3] = a; sp[-2] = d; sp[-1] = c; sp[0] = b; sp[1] = a; sp += 2; NEXT(1); }
    case 0x5f: { Slot a = sp[-1]; sp[-1] = sp[-2]; sp[-2] = a; NEXT(1); }
    /* int arithmetic */
    case 0x60: sp[-2].i = (s32)((u32)sp[-2].i + (u32)sp[-1].i); sp--; NEXT(1);
    case 0x64: sp[-2].i = (s32)((u32)sp[-2].i - (u32)sp[-1].i); sp--; NEXT(1);
    case 0x68: sp[-2].i = (s32)((u32)sp[-2].i * (u32)sp[-1].i); sp--; NEXT(1);
    case 0x6c: {
        s32 b = sp[-1].i, a = sp[-2].i;
        if (!b) THROW("java/lang/ArithmeticException");
        sp[-2].i = (b == -1) ? (s32)(0u - (u32)a) : a / b; sp--; NEXT(1);
    }
    case 0x70: {
        s32 b = sp[-1].i, a = sp[-2].i;
        if (!b) THROW("java/lang/ArithmeticException");
        sp[-2].i = (b == -1) ? 0 : a % b; sp--; NEXT(1);
    }
    case 0x74: sp[-1].i = (s32)(0u - (u32)sp[-1].i); NEXT(1);
    case 0x78: sp[-2].i = (s32)((u32)sp[-2].i << (sp[-1].i & 31)); sp--; NEXT(1);
    case 0x7a: sp[-2].i = sp[-2].i >> (sp[-1].i & 31); sp--; NEXT(1);
    case 0x7c: sp[-2].i = (s32)((u32)sp[-2].i >> (sp[-1].i & 31)); sp--; NEXT(1);
    case 0x7e: sp[-2].i &= sp[-1].i; sp--; NEXT(1);
    case 0x80: sp[-2].i |= sp[-1].i; sp--; NEXT(1);
    case 0x82: sp[-2].i ^= sp[-1].i; sp--; NEXT(1);
    /* long arithmetic */
    case 0x61: { s64 b = slot_j(sp - 2), a = slot_j(sp - 4); slot_setj(sp - 4, (s64)((u64)a + (u64)b)); sp -= 2; NEXT(1); }
    case 0x65: { s64 b = slot_j(sp - 2), a = slot_j(sp - 4); slot_setj(sp - 4, (s64)((u64)a - (u64)b)); sp -= 2; NEXT(1); }
    case 0x69: { s64 b = slot_j(sp - 2), a = slot_j(sp - 4); slot_setj(sp - 4, (s64)((u64)a * (u64)b)); sp -= 2; NEXT(1); }
    case 0x6d: { s64 b = slot_j(sp - 2), a = slot_j(sp - 4); if (!b) THROW("java/lang/ArithmeticException"); slot_setj(sp - 4, b == -1 ? (s64)(0 - (u64)a) : a / b); sp -= 2; NEXT(1); }
    case 0x71: { s64 b = slot_j(sp - 2), a = slot_j(sp - 4); if (!b) THROW("java/lang/ArithmeticException"); slot_setj(sp - 4, b == -1 ? 0 : a % b); sp -= 2; NEXT(1); }
    case 0x75: slot_setj(sp - 2, (s64)(0 - (u64)slot_j(sp - 2))); NEXT(1);
    case 0x79: { s32 s = sp[-1].i; sp--; slot_setj(sp - 2, (s64)((u64)slot_j(sp - 2) << (s & 63))); NEXT(1); }
    case 0x7b: { s32 s = sp[-1].i; sp--; slot_setj(sp - 2, slot_j(sp - 2) >> (s & 63)); NEXT(1); }
    case 0x7d: { s32 s = sp[-1].i; sp--; slot_setj(sp - 2, (s64)((u64)slot_j(sp - 2) >> (s & 63))); NEXT(1); }
    case 0x7f: { s64 b = slot_j(sp - 2), a = slot_j(sp - 4); slot_setj(sp - 4, a & b); sp -= 2; NEXT(1); }
    case 0x81: { s64 b = slot_j(sp - 2), a = slot_j(sp - 4); slot_setj(sp - 4, a | b); sp -= 2; NEXT(1); }
    case 0x83: { s64 b = slot_j(sp - 2), a = slot_j(sp - 4); slot_setj(sp - 4, a ^ b); sp -= 2; NEXT(1); }
    /* float / double */
    case 0x62: sp[-2].f += sp[-1].f; sp--; NEXT(1);
    case 0x66: sp[-2].f -= sp[-1].f; sp--; NEXT(1);
    case 0x6a: sp[-2].f *= sp[-1].f; sp--; NEXT(1);
    case 0x6e: sp[-2].f /= sp[-1].f; sp--; NEXT(1);
    case 0x72: sp[-2].f = (float)fmod(sp[-2].f, sp[-1].f); sp--; NEXT(1);
    case 0x76: sp[-1].f = -sp[-1].f; NEXT(1);
    case 0x63: { double b = slot_d(sp - 2), a = slot_d(sp - 4); slot_setd(sp - 4, a + b); sp -= 2; NEXT(1); }
    case 0x67: { double b = slot_d(sp - 2), a = slot_d(sp - 4); slot_setd(sp - 4, a - b); sp -= 2; NEXT(1); }
    case 0x6b: { double b = slot_d(sp - 2), a = slot_d(sp - 4); slot_setd(sp - 4, a * b); sp -= 2; NEXT(1); }
    case 0x6f: { double b = slot_d(sp - 2), a = slot_d(sp - 4); slot_setd(sp - 4, a / b); sp -= 2; NEXT(1); }
    case 0x73: { double b = slot_d(sp - 2), a = slot_d(sp - 4); slot_setd(sp - 4, fmod(a, b)); sp -= 2; NEXT(1); }
    case 0x77: slot_setd(sp - 2, -slot_d(sp - 2)); NEXT(1);
    case 0x84: locals[pc[1]].i = (s32)((u32)locals[pc[1]].i + (u32)(s8)pc[2]); NEXT(3);
    /* conversions */
    case 0x85: { s32 v = sp[-1].i; slot_setj(sp - 1, v); sp++; NEXT(1); }
    case 0x86: sp[-1].f = (float)sp[-1].i; NEXT(1);
    case 0x87: { s32 v = sp[-1].i; slot_setd(sp - 1, (double)v); sp++; NEXT(1); }
    case 0x88: { s64 v = slot_j(sp - 2); sp--; sp[-1].i = (s32)v; NEXT(1); }
    case 0x89: { s64 v = slot_j(sp - 2); sp--; sp[-1].f = (float)v; NEXT(1); }
    case 0x8a: { s64 v = slot_j(sp - 2); slot_setd(sp - 2, (double)v); NEXT(1); }
    case 0x8b: sp[-1].i = f2i(sp[-1].f); NEXT(1);
    case 0x8c: { float v = sp[-1].f; slot_setj(sp - 1, f2l(v)); sp++; NEXT(1); }
    case 0x8d: { float v = sp[-1].f; slot_setd(sp - 1, (double)v); sp++; NEXT(1); }
    case 0x8e: { double v = slot_d(sp - 2); sp--; sp[-1].i = d2i(v); NEXT(1); }
    case 0x8f: { double v = slot_d(sp - 2); slot_setj(sp - 2, f2l(v)); NEXT(1); }
    case 0x90: { double v = slot_d(sp - 2); sp--; sp[-1].f = (float)v; NEXT(1); }
    case 0x91: sp[-1].i = (s8)sp[-1].i; NEXT(1);
    case 0x92: sp[-1].i = (u16)sp[-1].i; NEXT(1);
    case 0x93: sp[-1].i = (s16)sp[-1].i; NEXT(1);
    /* compares */
    case 0x94: { s64 b = slot_j(sp - 2), a = slot_j(sp - 4); sp -= 3; sp[-1].i = a < b ? -1 : a > b; NEXT(1); }
    case 0x95: case 0x96: { float b = sp[-1].f, a = sp[-2].f; sp--; sp[-1].i = a < b ? -1 : a > b ? 1 : a == b ? 0 : (*pc == 0x95 ? -1 : 1); NEXT(1); }
    case 0x97: case 0x98: { double b = slot_d(sp - 2), a = slot_d(sp - 4); sp -= 3; sp[-1].i = a < b ? -1 : a > b ? 1 : a == b ? 0 : (*pc == 0x97 ? -1 : 1); NEXT(1); }
    /* branches */
#define BR(cond) do { if (cond) { s16 off = S2(pc + 1); if (off <= 0 && --budget <= 0) { pc += off; SAVE(); return; } pc += off; goto next; } NEXT(3); } while (0)
    case 0x99: sp--; BR(sp->i == 0);
    case 0x9a: sp--; BR(sp->i != 0);
    case 0x9b: sp--; BR(sp->i < 0);
    case 0x9c: sp--; BR(sp->i >= 0);
    case 0x9d: sp--; BR(sp->i > 0);
    case 0x9e: sp--; BR(sp->i <= 0);
    case 0x9f: sp -= 2; BR(sp[0].i == sp[1].i);
    case 0xa0: sp -= 2; BR(sp[0].i != sp[1].i);
    case 0xa1: sp -= 2; BR(sp[0].i < sp[1].i);
    case 0xa2: sp -= 2; BR(sp[0].i >= sp[1].i);
    case 0xa3: sp -= 2; BR(sp[0].i > sp[1].i);
    case 0xa4: sp -= 2; BR(sp[0].i <= sp[1].i);
    case 0xa5: sp -= 2; BR(sp[0].l == sp[1].l);
    case 0xa6: sp -= 2; BR(sp[0].l != sp[1].l);
    case 0xc6: sp--; BR(sp->l == NULL);
    case 0xc7: sp--; BR(sp->l != NULL);
    case 0xa7: BR(1);
    case 0xc8: { s32 off = S4(pc + 1); if (off <= 0 && --budget <= 0) { pc += off; SAVE(); return; } pc += off; goto next; }
    case 0xa8: sp->i = (s32)(pc + 3 - m->code); sp++; pc += S2(pc + 1); goto next;
    case 0xc9: sp->i = (s32)(pc + 5 - m->code); sp++; pc += S4(pc + 1); goto next;
    case 0xa9: pc = m->code + locals[pc[1]].i; goto next;
    case 0xaa: {
        u8 *p = m->code + ((pc - m->code + 4) & ~3);
        s32 def = S4(p), lo = S4(p + 4), hi = S4(p + 8), v = (--sp)->i;
        if (v < lo || v > hi) pc += def; else pc += S4(p + 12 + (v - lo) * 4);
        if (--budget <= 0) { SAVE(); return; }
        goto next;
    }
    case 0xab: {
        u8 *p = m->code + ((pc - m->code + 4) & ~3);
        s32 def = S4(p), n = S4(p + 4), v = (--sp)->i, lo = 0, hi = n - 1, off = def;
        while (lo <= hi) {
            s32 mid = (lo + hi) >> 1, k = S4(p + 8 + mid * 8);
            if (k == v) { off = S4(p + 12 + mid * 8); break; }
            if (k < v) lo = mid + 1; else hi = mid - 1;
        }
        pc += off;
        if (--budget <= 0) { SAVE(); return; }
        goto next;
    }
    /* returns */
    case 0xac: case 0xae: case 0xb0: case 0xad: case 0xaf: case 0xb1: {
        int nret = (*pc == 0xb1) ? 0 : (*pc == 0xad || *pc == 0xaf) ? 2 : 1;
        Slot rv[2];
        Slot *dst;
        if (nret) { rv[0] = sp[-nret]; if (nret == 2) rv[1] = sp[-1]; }
        if (fp->sync) monitor_exit(t, fp->sync);
        if (fp->clinit) fp->clinit->state = CS_INITED;
        dst = fp->locals;
        if (fp->boundary) {
            t->depth--;
            if (t->depth == 0) { t->state = TS_DEAD; return; }
            /* boundary inside a thread (not used) */
            t->state = TS_DEAD; return;
        }
        t->depth--;
        LOAD();
        sp = dst;
        if (nret) { sp[0] = rv[0]; if (nret == 2) sp[1] = rv[1]; sp += nret; }
        goto next;
    }
    /* fields */
    case 0xb2: case 0xb3: {
        int put = *pc == 0xb3;
        Field *f;
        Class *fc;
        SAVE();
        f = resolve_field(t, cls, U2(pc + 1));
        if (!f) goto exception;
        fc = f->cls;
        if (fc->state != CS_INITED) {
            r = cls_init(t, fc);
            if (r < 0) goto exception;
            if (r == 0) { LOAD(); goto next; }
        }
        if (f->desc[0] == 'J' || f->desc[0] == 'D') *pc = put ? Q_PUTSTATIC2 : Q_GETSTATIC2;
        else *pc = put ? Q_PUTSTATIC1 : Q_GETSTATIC1;
        goto next;
    }
    case Q_GETSTATIC1: { Field *f = cls->cp[U2(pc + 1)].v.field; *sp++ = f->cls->statics[f->off]; NEXT(3); }
    case Q_GETSTATIC2: { Field *f = cls->cp[U2(pc + 1)].v.field; sp[0] = f->cls->statics[f->off]; sp[1] = f->cls->statics[f->off + 1]; sp += 2; NEXT(3); }
    case Q_PUTSTATIC1: { Field *f = cls->cp[U2(pc + 1)].v.field; f->cls->statics[f->off] = *--sp; NEXT(3); }
    case Q_PUTSTATIC2: { Field *f = cls->cp[U2(pc + 1)].v.field; sp -= 2; f->cls->statics[f->off] = sp[0]; f->cls->statics[f->off + 1] = sp[1]; NEXT(3); }
    case 0xb4: case 0xb5: {
        Field *f;
        SAVE();
        f = resolve_field(t, cls, U2(pc + 1));
        if (!f) goto exception;
        if (f->desc[0] == 'J' || f->desc[0] == 'D') *pc = *pc == 0xb5 ? Q_PUTFIELD2 : Q_GETFIELD2;
        else *pc = *pc == 0xb5 ? Q_PUTFIELD1 : Q_GETFIELD1;
        goto next;
    }
    case Q_GETFIELD1: { Object *o = sp[-1].l; CHECKNULL(o); sp[-1] = OBJ_FIELDS(o)[cls->cp[U2(pc + 1)].v.field->off]; NEXT(3); }
    case Q_GETFIELD2: { Object *o = sp[-1].l; Slot *s; CHECKNULL(o); s = OBJ_FIELDS(o) + cls->cp[U2(pc + 1)].v.field->off; sp[-1] = s[0]; sp[0] = s[1]; sp++; NEXT(3); }
    case Q_PUTFIELD1: { Object *o = sp[-2].l; CHECKNULL(o); OBJ_FIELDS(o)[cls->cp[U2(pc + 1)].v.field->off] = sp[-1]; sp -= 2; NEXT(3); }
    case Q_PUTFIELD2: { Object *o = sp[-3].l; Slot *s; CHECKNULL(o); s = OBJ_FIELDS(o) + cls->cp[U2(pc + 1)].v.field->off; s[0] = sp[-2]; s[1] = sp[-1]; sp -= 3; NEXT(3); }
    /* invokes */
    case 0xb6: case 0xb7: case 0xb8: {
        Method *tm;
        SAVE();
        tm = resolve_method(t, cls, U2(pc + 1), *pc == 0xb8);
        if (!tm) goto exception;
        if (*pc == 0xb8) {
            if (tm->cls->state != CS_INITED) {
                r = cls_init(t, tm->cls);
                if (r < 0) goto exception;
                if (r == 0) { LOAD(); goto next; }
            }
            *pc = Q_INVOKESTATIC;
        } else if (*pc == 0xb7) *pc = Q_INVOKESPECIAL;
        else {
            if (tm->vindex >= 0) *pc = Q_INVOKEVIRTUAL;
            else if ((tm->acc & ACC_PRIVATE) || (tm->cls->acc & ACC_INTERFACE) == 0) *pc = Q_INVOKESPECIAL;
            else {
                /* method only declared in an interface: dynamic lookup each time */
                Object *recv = sp[-tm->nargs].l;
                CHECKNULL(recv);
                callee = cls_imethod(recv->cls, tm->name, tm->desc);
                if (!callee || (!callee->code && !callee->native)) THROW("java/lang/AbstractMethodError");
                fp->pc = pc + 3;
                goto invoke_go;
            }
        }
        goto next;
    }
    case Q_INVOKEVIRTUAL: {
        Object *recv;
        callee = cls->cp[U2(pc + 1)].v.method;
        recv = sp[-callee->nargs].l;
        CHECKNULL(recv);
        callee = recv->cls->vtable[callee->vindex];
        fp->pc = pc + 3;
        goto invoke_go;
    }
    case Q_INVOKESPECIAL:
        callee = cls->cp[U2(pc + 1)].v.method;
        if (!sp[-callee->nargs].l) NPE();
        fp->pc = pc + 3;
        goto invoke_go;
    case Q_INVOKESTATIC:
        callee = cls->cp[U2(pc + 1)].v.method;
        fp->pc = pc + 3;
        goto invoke_go;
    case 0xb9: {
        CP *e = &cls->cp[U2(pc + 1)];
        Method *rm;
        Object *recv;
        if (!(e->flags & CPF_RESOLVED)) {
            SAVE();
            if (!resolve_method(t, cls, U2(pc + 1), 0)) goto exception;
        }
        rm = e->v.method;
        recv = sp[-rm->nargs].l;
        CHECKNULL(recv);
        if (e->ic_cls == recv->cls) callee = e->ic_m;
        else {
            callee = cls_imethod(recv->cls, rm->name, rm->desc);
            if (!callee) THROW("java/lang/AbstractMethodError");
            e->ic_cls = recv->cls; e->ic_m = callee;
        }
        fp->pc = pc + 5;
        goto invoke_go;
    }
    case 0xba: THROW("java/lang/UnsupportedOperationException");
    /* objects */
    case 0xbb: {
        Class *k;
        SAVE();
        k = resolve_class(t, cls, U2(pc + 1));
        if (!k) goto exception;
        if (k->state != CS_INITED) {
            r = cls_init(t, k);
            if (r < 0) goto exception;
            if (r == 0) { LOAD(); goto next; }
        }
        if (k->acc & (ACC_ABSTRACT | ACC_INTERFACE)) THROW("java/lang/InstantiationError");
        *pc = Q_NEW;
        goto next;
    }
    case Q_NEW: {
        Object *o;
        SAVE();
        o = obj_new(t, cls->cp[U2(pc + 1)].v.cls);
        if (!o) goto exception;
        sp->l = o; sp++;
        NEXT(3);
    }
    case 0xbc: {
        static const char tc[] = "????ZCFDBSIJ";
        Array *a;
        int ty = pc[1];
        SAVE();
        a = arr_newp(t, (ty >= 4 && ty <= 11) ? tc[ty] : 'I', sp[-1].i);
        if (!a) goto exception;
        sp[-1].l = &a->o;
        NEXT(2);
    }
    case 0xbd: {
        Class *k, *ac;
        Array *a;
        SAVE();
        k = resolve_class(t, cls, U2(pc + 1));
        if (!k) goto exception;
        ac = cls_array(k, 'L');
        a = ac ? arr_new(t, ac, sp[-1].i) : NULL;
        if (!a) { if (!t->exception) throw_new(t, "java/lang/OutOfMemoryError", NULL); goto exception; }
        sp[-1].l = &a->o;
        NEXT(3);
    }
    case 0xbe: { Array *a = (Array *)sp[-1].l; CHECKNULL(a); sp[-1].i = a->length; NEXT(1); }
    case 0xbf: {
        Object *ex = sp[-1].l;
        SAVE();
        if (!ex) { throw_npe(t); goto exception; }
        t->exception = ex;
        record_trace(t, ex);
        goto exception;
    }
    case 0xc0: case 0xc1: {
        Object *o = sp[-1].l;
        Class *k;
        if (!o) { if (*pc == 0xc1) sp[-1].i = 0; NEXT(3); }
        SAVE();
        k = resolve_class(t, cls, U2(pc + 1));
        if (!k) goto exception;
        if (*pc == 0xc1) { sp[-1].i = cls_is_sub(o->cls, k); NEXT(3); }
        if (!cls_is_sub(o->cls, k)) {
            char msg[200];
            snprintf(msg, sizeof msg, "%s -> %s", o->cls->name, k->name);
            throw_new(t, "java/lang/ClassCastException", msg);
            goto exception;
        }
        NEXT(3);
    }
    case 0xc2: {
        Object *o = sp[-1].l;
        CHECKNULL(o);
        if (!monitor_enter(t, o)) { SAVE(); return; }
        sp--; NEXT(1);
    }
    case 0xc3: {
        Object *o = sp[-1].l;
        CHECKNULL(o);
        monitor_exit(t, o);
        sp--; NEXT(1);
    }
    case 0xc4: {
        int op = pc[1], idx = U2(pc + 2);
        switch (op) {
        case 0x15: case 0x17: case 0x19: *sp++ = locals[idx]; NEXT(4);
        case 0x16: case 0x18: sp[0] = locals[idx]; sp[1] = locals[idx + 1]; sp += 2; NEXT(4);
        case 0x36: case 0x38: case 0x3a: locals[idx] = *--sp; NEXT(4);
        case 0x37: case 0x39: sp -= 2; locals[idx] = sp[0]; locals[idx + 1] = sp[1]; NEXT(4);
        case 0x84: locals[idx].i = (s32)((u32)locals[idx].i + (u32)(s32)S2(pc + 4)); NEXT(6);
        case 0xa9: pc = m->code + locals[idx].i; goto next;
        }
        THROW("java/lang/VerifyError");
    }
    case 0xc5: {
        Class *k;
        int nd = pc[3], i;
        s32 dims[16];
        Array *a;
        SAVE();
        k = resolve_class(t, cls, U2(pc + 1));
        if (!k) goto exception;
        if (nd > 16) nd = 16;
        for (i = 0; i < nd; i++) {
            dims[i] = sp[-nd + i].i;
            if (dims[i] < 0) THROW("java/lang/NegativeArraySizeException");
        }
        a = multi_new(t, k, dims, nd);
        troot_reset();
        if (!a) goto exception;
        sp -= nd;
        sp->l = &a->o; sp++;
        NEXT(4);
    }
    default: {
        char msg[64];
        snprintf(msg, sizeof msg, "bad opcode 0x%02x", *pc);
        vm_log("%s in %s.%s", msg, m->cls->name, m->name);
        SAVE();
        throw_new(t, "java/lang/VerifyError", msg);
        goto exception;
    }
    }

invoke_go:
    /* fp->pc already holds the resume address; pc is the invoke instruction */
    if (callee->acc & ACC_NATIVE) {
        Slot *args = sp - callee->nargs;
        fp->sp = sp;
        cur_native = callee;
        r = callee->native(t, args);
        troot_reset();
        if (r == NR_OK || r == NR_YIELD) {
            sp = args;
            switch (callee->ret) {
            case 'V': break;
            case 'J': case 'D': sp += 2; break;
            default: sp++; break;
            }
            pc = fp->pc;
            if (r == NR_YIELD) { fp->sp = sp; return; }
            goto next;
        }
        if (r == NR_INVOKE) { callee = t->tail; sp = args + callee->nargs; goto invoke_go; }
        fp->pc = pc;
        if (r == NR_RETRY) { fp->sp = sp; return; }
        sp = args;
        fp->sp = sp;
        goto exception;
    }
    if ((callee->acc & ACC_ABSTRACT) || !callee->code) { fp->pc = pc; fp->sp = sp; throw_new(t, "java/lang/AbstractMethodError", callee->name); goto exception; }
    {
        Slot *args = sp - callee->nargs;
        Object *so = NULL;
        if (callee->acc & ACC_SYNCHRONIZED) {
            so = (callee->acc & ACC_STATIC) ? cls_mirror(callee->cls) : args[0].l;
            if (!monitor_enter(t, so)) { fp->pc = pc; fp->sp = sp; return; }
        }
        if (t->depth >= t->maxdepth || args + callee->max_locals + callee->max_stack + 4 > t->stack_end) {
            if (so) monitor_exit(t, so);
            fp->pc = pc; fp->sp = sp;
            throw_new(t, "java/lang/StackOverflowError", NULL);
            goto exception;
        }
        fp->sp = args;
        fp = &t->frames[t->depth++];
        fp->m = callee; fp->pc = callee->code; fp->locals = args;
        fp->base = args + callee->max_locals; fp->sp = fp->base;
        fp->sync = so; fp->clinit = NULL; fp->boundary = 0;
        m = callee; cls = callee->cls; pc = callee->code; locals = args; sp = fp->base;
        if (--budget <= 0) { SAVE(); return; }
        goto next;
    }

exception: {
        Object *ex = t->exception;
        int was_clinit;
        if (!ex) { vm_log("exception without object"); t->state = TS_DEAD; return; }
        for (;;) {
            Frame *f = &t->frames[t->depth - 1];
            Method *em = f->m;
            int i, found = -1;
            if (em->code) {
                u32 off = (u32)(f->pc - em->code);
                for (i = 0; i < em->nexc; i++) {
                    ExcEntry *x = &em->exc[i];
                    if (off >= x->start && off < x->end) {
                        if (x->type) {
                            CP *e = &em->cls->cp[x->type];
                            Class *k;
                            if (!(e->flags & CPF_RESOLVED)) {
                                Object *saved = t->exception;
                                k = resolve_class(t, em->cls, x->type);
                                t->exception = saved;
                                if (!k) continue;
                            } else k = e->v.cls;
                            if (!cls_is_sub(ex->cls, k)) continue;
                        }
                        found = x->handler;
                        break;
                    }
                }
            }
            if (found >= 0) {
                t->exception = NULL;
                f->pc = em->code + found;
                f->sp = f->base;
                f->sp->l = ex;
                f->sp++;
                LOAD();
                goto next;
            }
            if (f->sync) monitor_exit(t, f->sync);
            if (f->clinit) f->clinit->state = CS_ERROR;
            was_clinit = f->clinit != NULL;
            if (f->boundary || t->depth == 1) {
                char buf[200];
                Object *msg = OBJ_FIELDS(ex)[F_Throwable_msg].l;
                vm_log("uncaught %s: %s in thread %d", ex->cls->name, msg ? str_to_utf8(msg, buf, sizeof buf) : "", t->id);
                vm_log("%s", last_trace);
                t->depth = 0;
                t->exception = NULL;
                t->state = TS_DEAD;
                return;
            }
            t->depth--;
            /* the caller's pc points after its invoke; step back into it for range checks
               (a <clinit> caller still points at the instruction itself) */
            if (!was_clinit) t->frames[t->depth - 1].pc--;
        }
    }
}

/* ---------------- scheduler ---------------- */

static Thread *pick(void)
{
    Thread *t = rr ? rr->next : vm.threads;
    int n;
    for (n = 0; n < 1000; n++) {
        if (!t) t = vm.threads;
        if (!t) return NULL;
        if (t->state == TS_RUNNABLE) return t;
        if (t == rr) return NULL;
        t = t->next;
        if (!rr && !t) return NULL;
    }
    return NULL;
}

void vm_run(int budget_ms)
{
    u32 start = vm_ticks();
    vm.now = start;
    while (!vm.quit) {
        Thread *t;
        update_states();
        t = pick();
        if (!t) break;
        run_thread(t, 4000);
        vm.cur = NULL;
        rr = t;
        vm.now = vm_ticks();
        if ((s32)(vm.now - start) >= budget_ms) break;
    }
    thread_free_dead();
}

int vm_call_static(const char *cname, const char *name, const char *desc, Slot *args, int nargs)
{
    Class *c = cls_find(cname);
    Method *mm;
    Thread *t;
    if (!c) { vm_log("class %s not found", cname); return 0; }
    mm = cls_method(c, name, desc);
    if (!mm) { vm_log("method %s.%s%s not found", cname, name, desc); return 0; }
    t = thread_new(NULL);
    return thread_start_method(t, mm, args, nargs);
}
