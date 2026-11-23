/* class loading, linking, lookup, initialization */
#include "jvm.h"
#include <stdlib.h>
#include <stdio.h>

static Class *make_stub(const char *name, int iface);
static int stubbable(const char *name);
static Class *load_class(const char *name);
static Class *tab_get(const char *name);
Class *C_Object, *C_String, *C_Class, *C_Thread, *C_Throwable;
int F_String_value, F_String_offset, F_String_count, F_Thread_eetop, F_Throwable_msg, F_Class_vm;

/* ---------------- symbols ---------------- */
#define SYMH 4096
typedef struct Sym { struct Sym *next; char s[1]; } Sym;
static Sym *symtab[SYMH];

static u32 hstr(const char *s, int n)
{
    u32 h = 2166136261u;
    int i;
    for (i = 0; i < n; i++) h = (h ^ (u8)s[i]) * 16777619u;
    return h;
}

const char *symn(const char *s, int n)
{
    u32 h = hstr(s, n) & (SYMH - 1);
    Sym *e;
    for (e = symtab[h]; e; e = e->next)
        if (!strncmp(e->s, s, n) && e->s[n] == 0) return e->s;
    e = (Sym *)malloc(sizeof(Sym) + n);
    memcpy(e->s, s, n);
    e->s[n] = 0;
    e->next = symtab[h];
    symtab[h] = e;
    return e->s;
}
const char *sym(const char *s) { return symn(s, (int)strlen(s)); }

/* ---------------- class table ---------------- */
#define CLSH 1024
static Class *clstab[CLSH];
static struct { ClassSource fn; void *ctx; } sources[4];
static int nsources;

void cls_add_source(ClassSource fn, void *ctx)
{
    if (nsources < 4) { sources[nsources].fn = fn; sources[nsources].ctx = ctx; nsources++; }
}

static Class *tab_get(const char *name)
{
    Class *c;
    for (c = clstab[((uintptr_t)name >> 3) & (CLSH - 1)]; c; c = c->hnext)
        if (c->name == name) return c;
    return NULL;
}
static void tab_put(Class *c)
{
    u32 h = ((uintptr_t)c->name >> 3) & (CLSH - 1);
    c->hnext = clstab[h];
    clstab[h] = c;
}

void cls_foreach(void (*f)(Class *c, void *u), void *u)
{
    int i;
    Class *c;
    for (i = 0; i < CLSH; i++)
        for (c = clstab[i]; c; c = c->hnext) f(c, u);
}

/* ---------------- natives registry ---------------- */
typedef struct NatEnt { const char *cls, *name, *desc; NativeFn fn; struct NatEnt *next; } NatEnt;
static NatEnt *natives;

void native_register(const char *cls, const char *name, const char *desc, NativeFn fn)
{
    NatEnt *e = (NatEnt *)malloc(sizeof(NatEnt));
    e->cls = sym(cls); e->name = sym(name); e->desc = desc ? sym(desc) : NULL; e->fn = fn;
    e->next = natives;
    natives = e;
}
NativeFn native_lookup(const char *cls, const char *name, const char *desc)
{
    NatEnt *e;
    for (e = natives; e; e = e->next)
        if (e->cls == cls && e->name == name && (!e->desc || e->desc == desc)) return e->fn;
    return NULL;
}

Method *cur_native;
static int nat_missing(Thread *t, Slot *a)
{
    Method *m = cur_native;
    (void)a;
    vm_log("missing native %s.%s%s", m->cls->name, m->name, m->desc);
    return throw_new(t, "java/lang/UnsatisfiedLinkError", m->name);
}

/* native finalizers by class name (inherited by subclasses at link time) */
typedef struct FinEnt { const char *cls; void (*fn)(Object *); struct FinEnt *next; } FinEnt;
static FinEnt *fins;
void cls_set_finalizer(const char *cls, void (*fn)(Object *))
{
    FinEnt *e = (FinEnt *)malloc(sizeof(FinEnt));
    e->cls = sym(cls); e->fn = fn; e->next = fins; fins = e;
}

/* ---------------- descriptor helpers ---------------- */
static void parse_desc(Method *m, int is_static)
{
    const char *p = m->desc + 1;
    int n = is_static ? 0 : 1;
    while (*p && *p != ')') {
        if (*p == 'J' || *p == 'D') { n += 2; p++; continue; }
        if (*p == 'L') { while (*p != ';') p++; p++; n++; continue; }
        if (*p == '[') {
            while (*p == '[') p++;
            if (*p == 'L') { while (*p != ';') p++; }
            p++; n++; continue;
        }
        n++; p++;
    }
    m->nargs = (u16)n;
    p++;
    m->ret = (*p == '[' || *p == 'L') ? 'L' : *p;
}

/* ---------------- classfile parsing ---------------- */
typedef struct { const u8 *p, *end; int err; } Rd;
static u8 r1(Rd *r) { if (r->p + 1 > r->end) { r->err = 1; return 0; } return *r->p++; }
static u16 r2(Rd *r) { u16 v; if (r->p + 2 > r->end) { r->err = 1; return 0; } v = (u16)((r->p[0] << 8) | r->p[1]); r->p += 2; return v; }
static u32 r4(Rd *r) { u32 v; if (r->p + 4 > r->end) { r->err = 1; return 0; } v = ((u32)r->p[0] << 24) | ((u32)r->p[1] << 16) | ((u32)r->p[2] << 8) | r->p[3]; r->p += 4; return v; }

static const char *cp_utf(Class *c, int i) { return (i > 0 && i < c->ncp && c->cp[i].tag == 1) ? c->cp[i].v.utf8 : ""; }
static const char *cp_clsname(Class *c, int i) { return (i > 0 && i < c->ncp && c->cp[i].tag == 7) ? cp_utf(c, c->cp[i].a) : NULL; }

static Class *parse_class(const u8 *data, int len, void *jar)
{
    Rd r;
    Class *c;
    int i, n;
    r.p = data; r.end = data + len; r.err = 0;
    if (r4(&r) != 0xCAFEBABEu) return NULL;
    r2(&r); r2(&r);
    c = (Class *)calloc(1, sizeof(Class));
    c->jar = jar;
    c->ncp = r2(&r);
    c->cp = (CP *)calloc(c->ncp ? c->ncp : 1, sizeof(CP));
    for (i = 1; i < c->ncp && !r.err; i++) {
        CP *e = &c->cp[i];
        e->tag = r1(&r);
        switch (e->tag) {
        case 1: n = r2(&r); if (r.p + n > r.end) { r.err = 1; break; } e->v.utf8 = symn((const char *)r.p, n); r.p += n; break;
        case 3: e->v.i = (s32)r4(&r); break;
        case 4: { u32 b = r4(&r); memcpy(&e->v.f, &b, 4); break; }
        case 5: case 6: { u64 hi = r4(&r), lo = r4(&r); u64 b = (hi << 32) | lo; memcpy(&e->v.j, &b, 8); i++; break; }
        case 7: case 8: case 16: case 19: case 20: e->a = r2(&r); break;
        case 9: case 10: case 11: case 12: case 17: case 18: e->a = r2(&r); e->b = r2(&r); break;
        case 15: r1(&r); e->a = r2(&r); break;
        default: r.err = 1; break;
        }
    }
    if (r.err) { free(c->cp); free(c); return NULL; }
    c->acc = r2(&r);
    c->name = cp_clsname(c, r2(&r));
    {
        const char *sn = cp_clsname(c, r2(&r));
        c->super = sn ? (Class *)sn : NULL;        /* temporarily holds the name */
    }
    c->nifaces = r2(&r);
    c->ifaces = (Class **)calloc(c->nifaces + 1, sizeof(Class *));
    for (i = 0; i < c->nifaces; i++) c->ifaces[i] = (Class *)cp_clsname(c, r2(&r));
    c->nfields = r2(&r);
    c->fields = (Field *)calloc(c->nfields + 1, sizeof(Field));
    for (i = 0; i < c->nfields && !r.err; i++) {
        Field *f = &c->fields[i];
        int na, k;
        f->acc = r2(&r);
        f->name = cp_utf(c, r2(&r));
        f->desc = cp_utf(c, r2(&r));
        f->cls = c;
        na = r2(&r);
        for (k = 0; k < na && !r.err; k++) {
            const char *an = cp_utf(c, r2(&r));
            u32 al = r4(&r);
            if (an == sym("ConstantValue") && al == 2) {
                int ci = r2(&r);
                if (ci > 0 && ci < c->ncp) {
                    CP *e = &c->cp[ci];
                    f->has_cval = e->tag;
                    if (e->tag == 5 || e->tag == 6) f->cval64 = e->v.j;
                    else if (e->tag == 3) f->cval.i = e->v.i;
                    else if (e->tag == 4) f->cval.f = e->v.f;
                    else if (e->tag == 8) f->cval.i = ci;
                }
            } else r.p += al;
        }
    }
    c->nmethods = r2(&r);
    c->methods = (Method *)calloc(c->nmethods + 1, sizeof(Method));
    for (i = 0; i < c->nmethods && !r.err; i++) {
        Method *m = &c->methods[i];
        int na, k;
        m->acc = r2(&r);
        m->name = cp_utf(c, r2(&r));
        m->desc = cp_utf(c, r2(&r));
        m->cls = c;
        m->vindex = -1;
        parse_desc(m, m->acc & ACC_STATIC);
        na = r2(&r);
        for (k = 0; k < na && !r.err; k++) {
            const char *an = cp_utf(c, r2(&r));
            u32 al = r4(&r);
            const u8 *aend = r.p + al;
            if (an == sym("Code")) {
                int ne, x;
                m->max_stack = r2(&r);
                m->max_locals = r2(&r);
                m->code_len = r4(&r);
                if (r.p + m->code_len > r.end) { r.err = 1; break; }
                m->code = (u8 *)malloc(m->code_len + 4);
                memcpy(m->code, r.p, m->code_len);
                r.p += m->code_len;
                ne = r2(&r);
                m->nexc = (u16)ne;
                m->exc = (ExcEntry *)calloc(ne + 1, sizeof(ExcEntry));
                for (x = 0; x < ne; x++) {
                    m->exc[x].start = r2(&r); m->exc[x].end = r2(&r);
                    m->exc[x].handler = r2(&r); m->exc[x].type = r2(&r);
                }
            }
            r.p = aend;
        }
        if (m->max_locals < m->nargs) m->max_locals = m->nargs;
    }
    if (r.err) { vm_log("bad classfile %s", c->name ? c->name : "?"); snprintf(vm_last_error, sizeof vm_last_error, "클래스 파일 손상: %s", c->name ? c->name : "?"); return NULL; }
    return c;
}

/* ---------------- linking ---------------- */
static int link_class(Class *c)
{
    int i, slots, ns;
    const char *sname = (const char *)c->super;
    if (sname) {
        c->super = cls_find_sym(sname);
        if (!c->super) {
            vm_log("superclass %s of %s not found", sname, c->name);
            snprintf(vm_last_error, sizeof vm_last_error, "%s 의 부모 클래스 %s 없음", c->name, sname);
            return 0;
        }
    } else c->super = NULL;
    for (i = 0; i < c->nifaces; i++) {
        const char *in = (const char *)c->ifaces[i];
        c->ifaces[i] = tab_get(in);
        if (!c->ifaces[i]) c->ifaces[i] = load_class(in);
        if (!c->ifaces[i] && stubbable(in)) c->ifaces[i] = make_stub(in, 1);
        if (!c->ifaces[i]) { vm_log("interface %s of %s not found", in, c->name); snprintf(vm_last_error, sizeof vm_last_error, "%s 의 인터페이스 %s 없음", c->name, in); return 0; }
    }
    /* field layout */
    slots = c->super ? c->super->inst_slots : 0;
    ns = 0;
    for (i = 0; i < c->nfields; i++) {
        Field *f = &c->fields[i];
        int w = (f->desc[0] == 'J' || f->desc[0] == 'D') ? 2 : 1;
        if (f->acc & ACC_STATIC) { f->off = ns; ns += w; }
        else { f->off = slots; slots += w; }
    }
    c->inst_slots = slots;
    c->refmap = (u8 *)calloc(slots + 1, 1);
    if (c->super && c->super->inst_slots) memcpy(c->refmap, c->super->refmap, c->super->inst_slots);
    c->nstatics = ns;
    c->statics = (Slot *)calloc(ns + 1, sizeof(Slot));
    c->static_refmap = (u8 *)calloc(ns + 1, 1);
    for (i = 0; i < c->nfields; i++) {
        Field *f = &c->fields[i];
        int isref = f->desc[0] == 'L' || f->desc[0] == '[';
        if (f->acc & ACC_STATIC) c->static_refmap[f->off] = (u8)isref;
        else c->refmap[f->off] = (u8)isref;
    }
    if (c->super && c->super->finalizer) c->finalizer = c->super->finalizer;
    {
        FinEnt *fe;
        for (fe = fins; fe; fe = fe->next) if (fe->cls == c->name) c->finalizer = fe->fn;
    }
    /* vtable */
    {
        int cap = (c->super ? c->super->vtsize : 0) + c->nmethods + 1;
        c->vtable = (Method **)calloc(cap, sizeof(Method *));
        if (c->super) { memcpy(c->vtable, c->super->vtable, c->super->vtsize * sizeof(Method *)); c->vtsize = c->super->vtsize; }
        for (i = 0; i < c->nmethods; i++) {
            Method *m = &c->methods[i];
            int k;
            if ((m->acc & ACC_NATIVE) && !m->native) {
                m->native = native_lookup(c->name, m->name, m->desc);
                if (!m->native) { vm_log("no native for %s.%s%s", c->name, m->name, m->desc); m->native = nat_missing; }
            }
            if ((m->acc & (ACC_STATIC | ACC_PRIVATE)) || m->name[0] == '<') continue;
            for (k = 0; k < c->vtsize; k++)
                if (c->vtable[k]->name == m->name && c->vtable[k]->desc == m->desc) break;
            m->vindex = k;
            c->vtable[k] = m;
            if (k == c->vtsize) c->vtsize++;
        }
    }
    c->state = CS_LINKED;
    return 1;
}

/* ---------------- stand-ins for classes the game needs but we do not have ---------------- */
int cls_stub_enabled;
char vm_last_error[256];
static int nat_stub(Thread *t, Slot *a)
{
    Method *m = cur_native;
    (void)t;
    a[0].p = 0;
    if (m->ret == 'J' || m->ret == 'D') a[1].p = 0;
    return NR_OK;
}
static Class *make_stub(const char *name, int iface)
{
    Class *c = (Class *)calloc(1, sizeof(Class));
    c->name = name;
    c->acc = ACC_PUBLIC | (iface ? (ACC_INTERFACE | ACC_ABSTRACT) : 0);
    c->super = (Class *)sym("java/lang/Object");
    c->ifaces = (Class **)calloc(1, sizeof(Class *));
    c->fields = (Field *)calloc(1, sizeof(Field));
    c->cp = (CP *)calloc(1, sizeof(CP));
    c->ncp = 1;
    c->is_stub = 1;
    if (!iface) {
        Method *m;
        c->methods = (Method *)calloc(1, sizeof(Method));
        c->nmethods = 1;
        m = &c->methods[0];
        m->name = sym("<init>"); m->desc = sym("()V"); m->acc = ACC_PUBLIC | ACC_NATIVE;
        m->cls = c; m->vindex = -1; m->nargs = 1; m->ret = 'V'; m->native = nat_stub;
    } else c->methods = (Method *)calloc(1, sizeof(Method));
    tab_put(c);
    vm_log("stub class %s (missing in runtime)", name);
    snprintf(vm_last_error, sizeof vm_last_error, "없는 클래스 대체: %s", name);
    if (!link_class(c)) return NULL;
    c->state = CS_INITED;
    return c;
}
Method *cls_stub_method(Class *k, const char *name, const char *desc, int is_static)
{
    Class *c;
    Method *m;
    for (c = k; c && !c->is_stub; c = c->super) {}
    if (!c) return NULL;
    m = (Method *)calloc(1, sizeof(Method));
    m->name = name; m->desc = desc; m->acc = ACC_PUBLIC | ACC_NATIVE | (is_static ? ACC_STATIC : 0);
    m->cls = c; m->vindex = -1; m->native = nat_stub;
    parse_desc(m, is_static);
    vm_log("stub method %s.%s%s", c->name, name, desc);
    return m;
}
static int stubbable(const char *name)
{
    return cls_stub_enabled && strncmp(name, "java/", 5) && strncmp(name, "m480/", 5) && name[0] != '[';
}

static Class *load_class(const char *name)
{
    int i;
    for (i = 0; i < nsources; i++) {
        int len = 0;
        const u8 *d = sources[i].fn(sources[i].ctx, name, &len);
        if (d) {
            Class *c = parse_class(d, len, sources[i].ctx);
            free((void *)d);
            if (!c) return NULL;
            if (c->name != name) { vm_log("class name mismatch %s / %s", name, c->name); return NULL; }
            tab_put(c);
            if (!link_class(c)) { c->state = CS_ERROR; return NULL; }
            return c;
        }
    }
    return NULL;
}

Class *cls_find_sym(const char *name)
{
    Class *c = tab_get(name);
    if (c) return c->state == CS_ERROR ? NULL : c;
    if (name[0] == '[') return cls_array_of_desc(name);
    c = load_class(name);
    if (!c && !tab_get(name) && stubbable(name)) c = make_stub(name, 0);
    return c;
}
Class *cls_find(const char *name) { return cls_find_sym(sym(name)); }

static int prim_size(char t)
{
    switch (t) { case 'B': case 'Z': return 1; case 'C': case 'S': return 2; case 'J': case 'D': return 8; case 'L': return (int)sizeof(Object *); default: return 4; }
}

Class *cls_array_of_desc(const char *desc)
{
    const char *name = sym(desc);
    Class *c = tab_get(name), *elem = NULL;
    char et;
    if (c) return c;
    et = name[1];
    if (et == 'L') {
        char buf[256];
        int n = (int)strlen(name) - 3;
        if (n <= 0 || n > 250) return NULL;
        memcpy(buf, name + 2, n); buf[n] = 0;
        elem = cls_find(buf);
        if (!elem) return NULL;
    } else if (et == '[') {
        elem = cls_array_of_desc(name + 1);
        if (!elem) return NULL;
        et = 'L';
    } else if (!strchr("BCSIJFDZ", et)) return NULL;
    c = (Class *)calloc(1, sizeof(Class));
    c->name = name;
    c->is_array = 1;
    c->elem = elem;
    c->elemtype = et;
    c->elemsize = prim_size(et);
    c->super = C_Object;
    c->acc = ACC_PUBLIC | ACC_FINAL;
    if (C_Object) { c->vtable = C_Object->vtable; c->vtsize = C_Object->vtsize; c->inst_slots = 0; }
    c->refmap = (u8 *)calloc(1, 1);
    c->state = CS_INITED;
    tab_put(c);
    return c;
}

Class *cls_array(Class *elem, char etype)
{
    char buf[300];
    if (etype != 'L') { buf[0] = '['; buf[1] = etype; buf[2] = 0; return cls_array_of_desc(buf); }
    if (elem->is_array) snprintf(buf, sizeof buf, "[%s", elem->name);
    else snprintf(buf, sizeof buf, "[L%s;", elem->name);
    return cls_array_of_desc(buf);
}
Class *cls_prim_array(char t) { char b[3] = { '[', t, 0 }; return cls_array_of_desc(b); }

/* ---------------- subtype checks ---------------- */
static int implements(Class *c, Class *iface)
{
    int i;
    for (; c; c = c->super) {
        for (i = 0; i < c->nifaces; i++)
            if (c->ifaces[i] == iface || implements(c->ifaces[i], iface)) return 1;
    }
    return 0;
}

int cls_is_sub(Class *c, Class *t)
{
    Class *k;
    if (c == t || t == C_Object) return 1;
    if (c->is_array) {
        if (!t->is_array) return (t->acc & ACC_INTERFACE) && (!strcmp(t->name, "java/lang/Cloneable") || !strcmp(t->name, "java/io/Serializable"));
        if (c->elemtype != 'L' || t->elemtype != 'L') return c->elemtype == t->elemtype && c->elemtype != 'L' ? 1 : 0;
        return cls_is_sub(c->elem, t->elem);
    }
    if (t->is_array) return 0;
    if (t->acc & ACC_INTERFACE) return implements(c, t);
    for (k = c->super; k; k = k->super) if (k == t) return 1;
    return 0;
}

/* ---------------- member lookup ---------------- */
Method *cls_method(Class *c, const char *name, const char *desc)
{
    int i;
    Class *k;
    name = sym(name); desc = sym(desc);
    for (k = c; k; k = k->super)
        for (i = 0; i < k->nmethods; i++)
            if (k->methods[i].name == name && k->methods[i].desc == desc) return &k->methods[i];
    return NULL;
}

static Method *iface_search(Class *c, const char *name, const char *desc)
{
    int i;
    for (; c; c = c->super)
        for (i = 0; i < c->nifaces; i++) {
            Class *ic = c->ifaces[i];
            int k;
            Method *m;
            for (k = 0; k < ic->nmethods; k++)
                if (ic->methods[k].name == name && ic->methods[k].desc == desc) return &ic->methods[k];
            m = iface_search(ic, name, desc);
            if (m) return m;
        }
    return NULL;
}

Method *cls_imethod(Class *c, const char *name, const char *desc)
{
    Method *m;
    int i;
    for (i = 0; i < c->vtsize; i++)
        if (c->vtable[i]->name == name && c->vtable[i]->desc == desc) return c->vtable[i];
    m = cls_method(c, name, desc);
    if (m) return m;
    return iface_search(c, name, desc);   /* default/abstract */
}

Field *cls_field(Class *c, const char *name, const char *desc)
{
    int i;
    Class *k;
    for (k = c; k; k = k->super) {
        for (i = 0; i < k->nfields; i++)
            if (k->fields[i].name == name && (!desc || k->fields[i].desc == desc)) return &k->fields[i];
        for (i = 0; i < k->nifaces; i++) {
            Field *f = cls_field(k->ifaces[i], name, desc);
            if (f) return f;
        }
    }
    return NULL;
}
Field *cls_field_n(Class *c, const char *name) { return cls_field(c, sym(name), NULL); }

int cls_field_off(const char *cls, const char *name)
{
    Class *c = cls_find(cls);
    Field *f = c ? cls_field_n(c, name) : NULL;
    if (!f) { vm_log("field %s.%s missing", cls, name); return 0; }
    return f->off;
}

/* ---------------- mirrors ---------------- */
Object *cls_mirror(Class *c)
{
    if (!c->mirror) {
        Object *o = obj_new(vm.cur, C_Class);
        if (!o) return NULL;
        OBJ_FIELDS(o)[F_Class_vm].p = (intptr_t)c;
        c->mirror = o;
    }
    return c->mirror;
}

/* ---------------- initialization ---------------- */
int interp_push(Thread *t, Method *m, Slot *args);

int cls_init(Thread *t, Class *c)
{
    int i;
    Method *ci;
    if (c->state == CS_INITED) return 1;
    if (c->state == CS_ERROR) { throw_new(t, "java/lang/NoClassDefFoundError", c->name); return -1; }
    if (c->state == CS_INITING) return 1;  /* recursive or another green thread: proceed */
    if (c->super && c->super->state != CS_INITED) {
        int r = cls_init(t, c->super);
        if (r <= 0) return r;
    }
    c->state = CS_INITING;
    c->init_thread = t;
    for (i = 0; i < c->nfields; i++) {
        Field *f = &c->fields[i];
        if (!(f->acc & ACC_STATIC) || !f->has_cval) continue;
        if (f->has_cval == 5 || f->has_cval == 6) slot_setj(&c->statics[f->off], f->cval64);
        else if (f->has_cval == 8) {
            CP *e = &c->cp[f->cval.i];
            Object *s = str_intern(t, str_new_utf8(t, cp_utf(c, e->a)));
            c->statics[f->off].l = s;
        } else c->statics[f->off] = f->cval;
    }
    ci = NULL;
    for (i = 0; i < c->nmethods; i++)
        if (c->methods[i].name == sym("<clinit>")) { ci = &c->methods[i]; break; }
    if (!ci) { c->state = CS_INITED; return 1; }
    if (!interp_push(t, ci, NULL)) return -1;
    t->frames[t->depth - 1].clinit = c;
    return 0;
}

/* ---------------- GC roots ---------------- */
static void (*g_mark)(Object *);
static void mark_cls(Class *c, void *u)
{
    int i;
    (void)u;
    if (c->mirror) g_mark(c->mirror);
    for (i = 0; i < c->nstatics; i++)
        if (c->static_refmap[i] && c->statics[i].l) g_mark(c->statics[i].l);
    if (!c->is_array)
        for (i = 1; i < c->ncp; i++)
            if (c->cp[i].tag == 8 && (c->cp[i].flags & CPF_RESOLVED) && c->cp[i].v.str) g_mark(c->cp[i].v.str);
}
void cls_gc_mark(void (*mark)(Object *)) { g_mark = mark; cls_foreach(mark_cls, NULL); }
