/* java.lang natives + VM boot */
#include "jvm.h"
#include "pal.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

u8 *(*vm_resource_hook)(const char *name, u32 *len);

#define ARG_OBJ(i) (a[i].l)
#define RET_I(v) do { a[0].i = (v); return NR_OK; } while (0)
#define RET_L(v) do { a[0].l = (Object *)(v); return NR_OK; } while (0)
#define RET_J(v) do { slot_setj(&a[0], (v)); return NR_OK; } while (0)
#define RET_D(v) do { slot_setd(&a[0], (v)); return NR_OK; } while (0)

u32 vm_ticks(void) { return pal_ms(); }

void vm_log(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    pal_log(buf);
}

/* ---------------- properties ---------------- */
typedef struct Prop { char *k, *v; struct Prop *next; } Prop;
static Prop *props;
void vm_set_prop(const char *k, const char *v)
{
    Prop *p;
    for (p = props; p; p = p->next) if (!strcmp(p->k, k)) { free(p->v); p->v = strdup(v); return; }
    p = (Prop *)malloc(sizeof(Prop));
    p->k = strdup(k); p->v = strdup(v); p->next = props;
    props = p;
}
const char *vm_get_prop(const char *k)
{
    Prop *p;
    for (p = props; p; p = p->next) if (!strcmp(p->k, k)) return p->v;
    return NULL;
}

/* ---------------- java.lang.Object ---------------- */
static int n_getClass(Thread *t, Slot *a) { (void)t; RET_L(cls_mirror(a[0].l->cls)); }
static int n_hashCode(Thread *t, Slot *a) { (void)t; RET_I((s32)((uintptr_t)a[0].l >> 3)); }
static int n_clone(Thread *t, Slot *a)
{
    Object *o = a[0].l, *c;
    size_t sz;
    if (!o->cls->is_array && !cls_is_sub(o->cls, cls_find("java/lang/Cloneable")))
        return throw_new(t, "java/lang/CloneNotSupportedException", o->cls->name);
    if (o->cls->is_array) c = &arr_new(t, o->cls, ((Array *)o)->length)->o;
    else c = obj_new(t, o->cls);
    if (!c) return NR_EXC;
    o = a[0].l;
    sz = o->cls->is_array ? ARR_HDR + (size_t)((Array *)o)->length * o->cls->elemsize : sizeof(Object) + (size_t)o->cls->inst_slots * sizeof(Slot);
    memcpy((char *)c + sizeof(Object), (char *)o + sizeof(Object), sz - sizeof(Object));
    RET_L(c);
}
static int n_notify(Thread *t, Slot *a) { return monitor_notify(t, a[0].l, 0); }
static int n_notifyAll(Thread *t, Slot *a) { return monitor_notify(t, a[0].l, 1); }
static int n_wait(Thread *t, Slot *a) { return monitor_wait(t, a[0].l, slot_j(&a[1])); }

/* ---------------- java.lang.Class ---------------- */
static Class *mirror_cls(Object *m) { return (Class *)OBJ_FIELDS(m)[F_Class_vm].p; }

static int n_cls_getName(Thread *t, Slot *a)
{
    Class *c = mirror_cls(a[0].l);
    char buf[300];
    int i;
    snprintf(buf, sizeof buf, "%s", c->name);
    if (!c->is_array) for (i = 0; buf[i]; i++) if (buf[i] == '/') buf[i] = '.';
    RET_L(str_new_utf8(t, buf));
}
static int n_cls_find0(Thread *t, Slot *a)
{
    char buf[300];
    Class *c;
    str_to_utf8(a[0].l, buf, sizeof buf);
    c = cls_find(buf);
    RET_L(c ? cls_mirror(c) : NULL);
}
static int n_cls_init0(Thread *t, Slot *a)
{
    Class *c = mirror_cls(a[0].l);
    int r = cls_init(t, c);
    if (r < 0) return NR_EXC;
    if (r == 0) return NR_RETRY;
    return NR_OK;
}
static int n_cls_alloc0(Thread *t, Slot *a)
{
    Class *c = mirror_cls(a[0].l);
    if (c->acc & (ACC_ABSTRACT | ACC_INTERFACE)) RET_L(NULL);
    RET_L(obj_new(t, c));
}
static int n_cls_construct0(Thread *t, Slot *a)
{
    Object *o = a[0].l;
    Method *m = cls_method(o->cls, "<init>", "()V");
    if (!m || m->cls != o->cls) return throw_new(t, "java/lang/InstantiationException", o->cls->name);
    t->tail = m;
    return NR_INVOKE;
}
static int n_cls_isInstance(Thread *t, Slot *a) { (void)t; RET_I(a[1].l && cls_is_sub(a[1].l->cls, mirror_cls(a[0].l))); }
static int n_cls_isAssignableFrom(Thread *t, Slot *a)
{
    if (!a[1].l) return throw_npe(t);
    RET_I(cls_is_sub(mirror_cls(a[1].l), mirror_cls(a[0].l)));
}
static int n_cls_isInterface(Thread *t, Slot *a) { (void)t; RET_I((mirror_cls(a[0].l)->acc & ACC_INTERFACE) != 0); }
static int n_cls_isArray(Thread *t, Slot *a) { (void)t; RET_I(mirror_cls(a[0].l)->is_array); }
static int n_cls_getSuperclass(Thread *t, Slot *a)
{
    Class *c = mirror_cls(a[0].l);
    (void)t;
    RET_L(c->super && !(c->acc & ACC_INTERFACE) ? cls_mirror(c->super) : NULL);
}
static int n_cls_resource0(Thread *t, Slot *a)
{
    char buf[300];
    u32 len = 0;
    u8 *d;
    Array *b;
    str_to_utf8(a[0].l, buf, sizeof buf);
    d = vm_resource_hook ? vm_resource_hook(buf, &len) : NULL;
    if (!d) { vm_log("resource not found: %s", buf); RET_L(NULL); }
    b = arr_newp(t, 'B', (s32)len);
    if (!b) { free(d); return NR_EXC; }
    memcpy(ARR_B(b), d, len);
    free(d);
    RET_L(b);
}

/* ---------------- Throwable ---------------- */
static int n_trace0(Thread *t, Slot *a)
{
    char buf[600], *p = buf;
    int i, n = 0;
    buf[0] = 0;
    for (i = t->depth - 1; i >= 0 && n < 8; i--) {
        Frame *f = &t->frames[i];
        if (f->m->name[0] == '<' && f->m->cls->super && cls_is_sub(f->m->cls, C_Throwable)) continue;
        if (p - buf > 480) break;
        p += snprintf(p, 110, "%s  at %s.%s(%d)", n ? "\n" : "", f->m->cls->name, f->m->name, f->m->code ? (int)(f->pc - f->m->code) : -1);
        n++;
    }
    RET_L(str_new_utf8(t, buf));
}

/* ---------------- Thread ---------------- */
static int off_thr_name = -1, off_thr_alive = -1, off_thr_vmref = -1;
static void thr_offsets(void)
{
    if (off_thr_name >= 0) return;
    off_thr_name = cls_field_off("java/lang/Thread", "name");
    off_thr_alive = cls_field_off("java/lang/Thread", "alive");
    off_thr_vmref = cls_field_off("java/lang/Thread", "vmref");
}

static int n_currentThread(Thread *t, Slot *a)
{
    if (!t->jthread) {
        Object *o;
        thr_offsets();
        o = obj_new(t, C_Thread);
        if (!o) return NR_EXC;
        t->jthread = o;
        OBJ_FIELDS(o)[off_thr_alive].i = 1;
        OBJ_FIELDS(o)[off_thr_vmref].i = t->id;
        OBJ_FIELDS(o)[cls_field_off("java/lang/Thread", "priority")].i = 5;
        OBJ_FIELDS(o)[off_thr_name].l = str_new_utf8(t, t->id == 1 ? "main" : "vm");
    }
    RET_L(t->jthread);
}

static int n_sleep(Thread *t, Slot *a)
{
    s64 ms = slot_j(&a[0]);
    if (t->ret_kind == 2) {
        t->ret_kind = 0;
        if (t->interrupted) { t->interrupted = 0; return throw_new(t, "java/lang/InterruptedException", NULL); }
        return NR_OK;
    }
    if (ms < 0) return throw_new(t, "java/lang/IllegalArgumentException", NULL);
    if (ms == 0) return NR_YIELD;
    t->ret_kind = 2;
    t->wake = vm.now + (u32)(ms > 0x3FFFFFFF ? 0x3FFFFFFF : ms);
    t->state = TS_SLEEPING;
    return NR_RETRY;
}
static int n_yield(Thread *t, Slot *a) { (void)t; (void)a; return NR_YIELD; }

static int n_start0(Thread *t, Slot *a)
{
    Object *jt = a[0].l;
    Method *m = cls_method(jt->cls, "runWrapper", "()V");
    Thread *nt;
    Slot arg;
    (void)t;
    thr_offsets();
    if (!m) return throw_new(t, "java/lang/InternalError", "runWrapper");
    nt = thread_new(jt);
    arg.l = jt;
    if (!thread_start_method(nt, m, &arg, 1)) { nt->state = TS_DEAD; return NR_EXC; }
    OBJ_FIELDS(jt)[off_thr_vmref].i = nt->id;
    return NR_OK;
}

static Thread *find_thread(Object *jt)
{
    Thread *w;
    for (w = vm.threads; w; w = w->next) if (w->jthread == jt && w->state != TS_DEAD) return w;
    return NULL;
}
static int n_interrupt(Thread *t, Slot *a)
{
    Thread *w = find_thread(a[0].l);
    (void)t;
    if (w) {
        w->interrupted = 1;
        if (w->state == TS_SLEEPING) w->state = TS_RUNNABLE;
        if (w->state == TS_WAITING) w->state = TS_RUNNABLE;
    }
    return NR_OK;
}
static int n_isInterrupted(Thread *t, Slot *a) { Thread *w = find_thread(a[0].l); (void)t; RET_I(w ? w->interrupted : 0); }
static int n_clear0(Thread *t, Slot *a) { Thread *w = find_thread(a[0].l); (void)t; if (w) w->interrupted = 0; return NR_OK; }
static int n_activeCount(Thread *t, Slot *a)
{
    Thread *w;
    int n = 0;
    (void)t;
    for (w = vm.threads; w; w = w->next) if (w->state != TS_DEAD) n++;
    RET_I(n);
}

/* ---------------- Runtime / System ---------------- */
static int n_freeMemory(Thread *t, Slot *a) { (void)t; RET_J((s64)heap_free_bytes()); }
static int n_totalMemory(Thread *t, Slot *a) { (void)t; RET_J((s64)heap_total_bytes()); }
static int n_gc(Thread *t, Slot *a) { (void)t; (void)a; gc_collect(); return NR_OK; }

static int n_arraycopy(Thread *t, Slot *a)
{
    Array *s = (Array *)a[0].l, *d = (Array *)a[2].l;
    s32 sp = a[1].i, dp = a[3].i, n = a[4].i;
    Class *sc, *dc;
    if (!s || !d) return throw_npe(t);
    sc = s->o.cls; dc = d->o.cls;
    if (!sc->is_array || !dc->is_array) return throw_new(t, "java/lang/ArrayStoreException", NULL);
    if (sc->elemtype != dc->elemtype) return throw_new(t, "java/lang/ArrayStoreException", NULL);
    if (n < 0 || sp < 0 || dp < 0 || sp > s->length - n || dp > d->length - n)
        return throw_new(t, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    if (sc->elemtype == 'L' && sc != dc && !cls_is_sub(sc, dc)) {
        s32 i;
        Object **so = ARR_L(s) + sp, **dd = ARR_L(d) + dp;
        for (i = 0; i < n; i++) {
            if (so[i] && !cls_is_sub(so[i]->cls, dc->elem)) return throw_new(t, "java/lang/ArrayStoreException", NULL);
            dd[i] = so[i];
        }
        return NR_OK;
    }
    memmove((char *)ARR_DATA(d) + (size_t)dp * dc->elemsize, (char *)ARR_DATA(s) + (size_t)sp * sc->elemsize, (size_t)n * sc->elemsize);
    return NR_OK;
}
static int n_currentTimeMillis(Thread *t, Slot *a) { (void)t; RET_J(pal_time_ms()); }
static int n_identityHashCode(Thread *t, Slot *a) { (void)t; RET_I((s32)((uintptr_t)a[0].l >> 3)); }
static int n_getProperty(Thread *t, Slot *a)
{
    char buf[200];
    const char *v;
    if (!a[0].l) return throw_npe(t);
    str_to_utf8(a[0].l, buf, sizeof buf);
    v = vm_get_prop(buf);
    RET_L(v ? str_new_utf8(t, v) : NULL);
}
static int n_exit(Thread *t, Slot *a)
{
    vm_log("System.exit(%d)", a[0].i);
    vm.quit = 1;
    t->state = TS_DEAD;
    return NR_RETRY;
}

/* ---------------- Math / Float / Double ---------------- */
#define MATH1(nm, fn) static int nm(Thread *t, Slot *a) { (void)t; RET_D(fn(slot_d(&a[0]))); }
MATH1(n_sin, sin) MATH1(n_cos, cos) MATH1(n_tan, tan) MATH1(n_asin, asin) MATH1(n_acos, acos) MATH1(n_atan, atan)
MATH1(n_sqrt, sqrt) MATH1(n_ceil, ceil) MATH1(n_floor, floor) MATH1(n_exp, exp) MATH1(n_log, log)
static int n_atan2(Thread *t, Slot *a) { (void)t; RET_D(atan2(slot_d(&a[0]), slot_d(&a[2]))); }
static int n_pow(Thread *t, Slot *a) { (void)t; RET_D(pow(slot_d(&a[0]), slot_d(&a[2]))); }

static void fmt_double(char *buf, double d, int is_float)
{
    if (d != d) { strcpy(buf, "NaN"); return; }
    if (d > 1.7e308) { strcpy(buf, "Infinity"); return; }
    if (d < -1.7e308) { strcpy(buf, "-Infinity"); return; }
    if (d == 0) { strcpy(buf, (1 / d) < 0 ? "-0.0" : "0.0"); return; }
    {
        double ad = d < 0 ? -d : d;
        int prec = is_float ? 7 : 15;
        if (ad >= 1e-3 && ad < 1e7) {
            char *p;
            snprintf(buf, 64, "%.*f", prec - 1 - (int)floor(log10(ad)) > 0 ? prec - 1 - (int)floor(log10(ad)) : 1, d);
            p = buf + strlen(buf) - 1;
            while (p > buf && *p == '0' && p[-1] != '.') *p-- = 0;
            if (!strchr(buf, '.')) strcat(buf, ".0");
        } else {
            char m[64], *e, *p;
            int ex;
            snprintf(m, sizeof m, "%.*e", prec - 1, d);
            e = strchr(m, 'e');
            ex = atoi(e + 1);
            *e = 0;
            p = m + strlen(m) - 1;
            while (p > m && *p == '0' && p[-1] != '.') *p-- = 0;
            snprintf(buf, 64, "%sE%d", m, ex);
        }
    }
}
static int n_f_toString(Thread *t, Slot *a) { char b[64]; fmt_double(b, a[0].f, 1); RET_L(str_new_utf8(t, b)); }
static int n_d_toString(Thread *t, Slot *a) { char b[64]; fmt_double(b, slot_d(&a[0]), 0); RET_L(str_new_utf8(t, b)); }
static int parse_dbl(Thread *t, Object *s, double *out)
{
    char buf[80], *end;
    if (!s) { throw_npe(t); return 0; }
    str_to_utf8(s, buf, sizeof buf);
    {
        char *p = buf;
        while (*p == ' ') p++;
        *out = strtod(p, &end);
        while (*end == ' ' || *end == 'f' || *end == 'F' || *end == 'd' || *end == 'D') end++;
        if (end == p || *end) { throw_new(t, "java/lang/NumberFormatException", buf); return 0; }
    }
    return 1;
}
static int n_parseFloat(Thread *t, Slot *a) { double d; if (!parse_dbl(t, a[0].l, &d)) return NR_EXC; a[0].f = (float)d; return NR_OK; }
static int n_parseDouble(Thread *t, Slot *a) { double d; if (!parse_dbl(t, a[0].l, &d)) return NR_EXC; RET_D(d); }
static int n_floatToIntBits(Thread *t, Slot *a) { (void)t; return NR_OK; }   /* same bits in the slot */
static int n_intBitsToFloat(Thread *t, Slot *a) { (void)t; return NR_OK; }
static int n_doubleToLongBits(Thread *t, Slot *a) { (void)t; return NR_OK; }
static int n_longBitsToDouble(Thread *t, Slot *a) { (void)t; return NR_OK; }

/* ---------------- String ---------------- */
static int n_decode0(Thread *t, Slot *a)
{
    Array *b = (Array *)a[0].l, *c;
    s32 off = a[1].i, n = a[2].i, enc = a[3].i, k = 0, i;
    u16 *tmp;
    const u8 *p;
    if (!b) return throw_npe(t);
    p = (const u8 *)ARR_B(b) + off;
    tmp = (u16 *)malloc((size_t)n * 2 + 2);
    if (enc == 1) {
        for (i = 0; i < n;) {
            u8 x = p[i];
            if (x < 0x80) { tmp[k++] = x; i++; }
            else if ((x & 0xE0) == 0xC0 && i + 1 < n) { tmp[k++] = (u16)(((x & 0x1F) << 6) | (p[i + 1] & 0x3F)); i += 2; }
            else if ((x & 0xF0) == 0xE0 && i + 2 < n) { tmp[k++] = (u16)(((x & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F)); i += 3; }
            else { tmp[k++] = 0xFFFD; i++; }
        }
    } else if (enc == 2) { for (i = 0; i < n; i++) tmp[k++] = p[i]; }
    else if (enc == 3) { for (i = 0; i + 1 < n; i += 2) tmp[k++] = (u16)((p[i] << 8) | p[i + 1]); }
    else if (enc == 4) { for (i = 0; i + 1 < n; i += 2) tmp[k++] = (u16)((p[i + 1] << 8) | p[i]); }
    else k = ksc_to_u16(p, n, tmp);
    c = arr_newp(t, 'C', k);
    if (!c) { free(tmp); return NR_EXC; }
    memcpy(ARR_C(c), tmp, (size_t)k * 2);
    free(tmp);
    RET_L(c);
}
static int n_encode0(Thread *t, Slot *a)
{
    Array *v = (Array *)a[0].l, *b;
    s32 off = a[1].i, n = a[2].i, enc = a[3].i, k = 0, i;
    u8 *tmp = (u8 *)malloc((size_t)n * 3 + 1);
    const u16 *c = ARR_C(v) + off;
    if (enc == 1) {
        for (i = 0; i < n; i++) {
            u16 ch = c[i];
            if (ch < 0x80) tmp[k++] = (u8)ch;
            else if (ch < 0x800) { tmp[k++] = (u8)(0xC0 | (ch >> 6)); tmp[k++] = (u8)(0x80 | (ch & 0x3F)); }
            else { tmp[k++] = (u8)(0xE0 | (ch >> 12)); tmp[k++] = (u8)(0x80 | ((ch >> 6) & 0x3F)); tmp[k++] = (u8)(0x80 | (ch & 0x3F)); }
        }
    } else if (enc == 2) { for (i = 0; i < n; i++) tmp[k++] = c[i] < 256 ? (u8)c[i] : '?'; }
    else if (enc == 3) { for (i = 0; i < n; i++) { tmp[k++] = (u8)(c[i] >> 8); tmp[k++] = (u8)c[i]; } }
    else if (enc == 4) { for (i = 0; i < n; i++) { tmp[k++] = (u8)c[i]; tmp[k++] = (u8)(c[i] >> 8); } }
    else k = u16_to_ksc(c, n, tmp);
    b = arr_newp(t, 'B', k);
    if (!b) { free(tmp); return NR_EXC; }
    memcpy(ARR_B(b), tmp, k);
    free(tmp);
    RET_L(b);
}
static int n_intern(Thread *t, Slot *a) { RET_L(str_intern(t, a[0].l)); }

static int n_log0(Thread *t, Slot *a)
{
    Array *b = (Array *)a[1].l;
    s32 n = a[2].i;
    u16 *w;
    char *u;
    int k, i, o = 0;
    (void)t;
    if (!b || n <= 0) return NR_OK;
    w = (u16 *)malloc((size_t)n * 2 + 2);
    u = (char *)malloc((size_t)n * 3 + 8);
    k = ksc_to_u16((const u8 *)ARR_B(b), n, w);
    if (a[0].i) { strcpy(u, "! "); o = 2; }
    for (i = 0; i < k; i++) {
        u16 ch = w[i];
        if (ch < 0x80) u[o++] = (char)ch;
        else if (ch < 0x800) { u[o++] = (char)(0xC0 | (ch >> 6)); u[o++] = (char)(0x80 | (ch & 0x3F)); }
        else { u[o++] = (char)(0xE0 | (ch >> 12)); u[o++] = (char)(0x80 | ((ch >> 6) & 0x3F)); u[o++] = (char)(0x80 | (ch & 0x3F)); }
    }
    u[o] = 0;
    pal_log(u);
    free(w); free(u);
    return NR_OK;
}

void natives_lang(void)
{
#define R(c, n, d, f) native_register(c, n, d, f)
    R("java/lang/Object", "getClass", "()Ljava/lang/Class;", n_getClass);
    R("java/lang/Object", "hashCode", "()I", n_hashCode);
    R("java/lang/Object", "clone", "()Ljava/lang/Object;", n_clone);
    R("java/lang/Object", "notify", "()V", n_notify);
    R("java/lang/Object", "notifyAll", "()V", n_notifyAll);
    R("java/lang/Object", "wait", "(J)V", n_wait);
    R("java/lang/Class", "getName", "()Ljava/lang/String;", n_cls_getName);
    R("java/lang/Class", "find0", NULL, n_cls_find0);
    R("java/lang/Class", "init0", NULL, n_cls_init0);
    R("java/lang/Class", "alloc0", NULL, n_cls_alloc0);
    R("java/lang/Class", "construct0", NULL, n_cls_construct0);
    R("java/lang/Class", "isInstance", NULL, n_cls_isInstance);
    R("java/lang/Class", "isAssignableFrom", NULL, n_cls_isAssignableFrom);
    R("java/lang/Class", "isInterface", NULL, n_cls_isInterface);
    R("java/lang/Class", "isArray", NULL, n_cls_isArray);
    R("java/lang/Class", "getSuperclass", NULL, n_cls_getSuperclass);
    R("java/lang/Class", "resource0", NULL, n_cls_resource0);
    R("java/lang/Throwable", "trace0", NULL, n_trace0);
    R("java/lang/Thread", "currentThread", NULL, n_currentThread);
    R("java/lang/Thread", "sleep", NULL, n_sleep);
    R("java/lang/Thread", "yield", NULL, n_yield);
    R("java/lang/Thread", "start0", NULL, n_start0);
    R("java/lang/Thread", "interrupt", NULL, n_interrupt);
    R("java/lang/Thread", "isInterrupted", NULL, n_isInterrupted);
    R("java/lang/Thread", "clear0", NULL, n_clear0);
    R("java/lang/Thread", "activeCount", NULL, n_activeCount);
    R("java/lang/Runtime", "freeMemory", NULL, n_freeMemory);
    R("java/lang/Runtime", "totalMemory", NULL, n_totalMemory);
    R("java/lang/Runtime", "gc", NULL, n_gc);
    R("java/lang/System", "arraycopy", NULL, n_arraycopy);
    R("java/lang/System", "currentTimeMillis", NULL, n_currentTimeMillis);
    R("java/lang/System", "identityHashCode", NULL, n_identityHashCode);
    R("java/lang/System", "getProperty", "(Ljava/lang/String;)Ljava/lang/String;", n_getProperty);
    R("java/lang/System", "exit", NULL, n_exit);
    R("java/lang/Math", "sin", NULL, n_sin); R("java/lang/Math", "cos", NULL, n_cos);
    R("java/lang/Math", "tan", NULL, n_tan); R("java/lang/Math", "asin", NULL, n_asin);
    R("java/lang/Math", "acos", NULL, n_acos); R("java/lang/Math", "atan", NULL, n_atan);
    R("java/lang/Math", "atan2", NULL, n_atan2); R("java/lang/Math", "sqrt", NULL, n_sqrt);
    R("java/lang/Math", "ceil", NULL, n_ceil); R("java/lang/Math", "floor", NULL, n_floor);
    R("java/lang/Math", "exp", NULL, n_exp); R("java/lang/Math", "log", NULL, n_log);
    R("java/lang/Math", "pow", NULL, n_pow);
    R("java/lang/Float", "toString", "(F)Ljava/lang/String;", n_f_toString);
    R("java/lang/Float", "parseFloat", NULL, n_parseFloat);
    R("java/lang/Float", "floatToIntBits", NULL, n_floatToIntBits);
    R("java/lang/Float", "intBitsToFloat", NULL, n_intBitsToFloat);
    R("java/lang/Double", "toString", "(D)Ljava/lang/String;", n_d_toString);
    R("java/lang/Double", "parseDouble", NULL, n_parseDouble);
    R("java/lang/Double", "doubleToLongBits", NULL, n_doubleToLongBits);
    R("java/lang/Double", "longBitsToDouble", NULL, n_longBitsToDouble);
    R("java/lang/String", "decode0", NULL, n_decode0);
    R("java/lang/String", "encode0", NULL, n_encode0);
    R("java/lang/String", "intern", NULL, n_intern);
    R("m480/LogStream", "log0", NULL, n_log0);
#undef R
}

int vm_boot(size_t heap_bytes)
{
    if (!heap_init(heap_bytes)) { vm_log("heap_init failed"); return 0; }
    natives_lang();
    natives_api();
    C_Object = cls_find("java/lang/Object");
    if (!C_Object) { vm_log("java/lang/Object missing"); return 0; }
    C_Class = cls_find("java/lang/Class");
    C_String = cls_find("java/lang/String");
    C_Thread = cls_find("java/lang/Thread");
    C_Throwable = cls_find("java/lang/Throwable");
    if (!C_Class || !C_String || !C_Thread || !C_Throwable) { vm_log("core classes missing"); return 0; }
    F_Class_vm = cls_field_off("java/lang/Class", "vmref");
    F_String_value = cls_field_off("java/lang/String", "value");
    F_String_offset = cls_field_off("java/lang/String", "offset");
    F_String_count = cls_field_off("java/lang/String", "count");
    F_Throwable_msg = cls_field_off("java/lang/Throwable", "message");
    C_String->state = CS_INITED;
    C_Class->state = CS_INITED;
    return 1;
}
