/* M480 WIPI - small Java VM (CLDC level) for WIPI / SKVM / MIDP games */
#ifndef JVM_H
#define JVM_H
#include "types.h"
#include <string.h>

struct Object; struct Class; struct Thread; struct Method;

typedef union Slot { s32 i; float f; struct Object *l; intptr_t p; } Slot;

/* ---- heap objects ---- */
typedef struct Object {
    u32 hdr;                 /* block size (bytes, multiple of 8) | flags */
    u32 lock;                /* owner thread id (low 16) | count (high 16) */
    struct Class *cls;
} Object;
#define HDR_MARK  1u
#define HDR_FREE  2u
#define HDR_SIZE(h) ((h) & ~7u)
#define OBJ_FIELDS(o) ((Slot *)((char *)(o) + sizeof(Object)))

typedef struct Array {
    Object o;
    s32 length;
} Array;
#define ARR_HDR ((sizeof(Array) + 7) & ~7u)
#define ARR_DATA(a) ((void *)((char *)(a) + ARR_HDR))
#define ARR_I(a) ((s32 *)ARR_DATA(a))
#define ARR_B(a) ((s8 *)ARR_DATA(a))
#define ARR_C(a) ((u16 *)ARR_DATA(a))
#define ARR_S(a) ((s16 *)ARR_DATA(a))
#define ARR_L(a) ((Object **)ARR_DATA(a))

/* ---- class structures ---- */
typedef struct Field {
    const char *name, *desc;
    u16 acc;
    int off;                 /* instance: slot index; static: index in statics */
    struct Class *cls;
    Slot cval;               /* ConstantValue (statics) */
    int has_cval;
    s64 cval64;
} Field;

typedef struct ExcEntry { u16 start, end, handler, type; } ExcEntry;

typedef int (*NativeFn)(struct Thread *t, Slot *a);
enum { NR_OK = 0, NR_EXC = 1, NR_YIELD = 2, NR_RETRY = 3, NR_INVOKE = 4 };

typedef struct Method {
    const char *name, *desc;
    u16 acc;
    u16 max_stack, max_locals;
    u16 nargs;               /* argument slots incl. this */
    char ret;                /* V I J F D L(=ref) Z B C S */
    u8 *code;
    u32 code_len;
    ExcEntry *exc;
    u16 nexc;
    int vindex;
    struct Class *cls;
    NativeFn native;
} Method;

typedef struct CP {
    u8 tag, flags;
    u16 a, b;
    union {
        s32 i; float f; s64 j; double d;
        const char *utf8;
        struct Class *cls;
        Field *field;
        Method *method;
        Object *str;
    } v;
    struct Class *ic_cls;    /* inline cache for invokeinterface */
    Method *ic_m;
} CP;
#define CPF_RESOLVED 1

enum { CS_LOADED, CS_LINKED, CS_INITING, CS_INITED, CS_ERROR };
#define ACC_PUBLIC 0x0001
#define ACC_PRIVATE 0x0002
#define ACC_PROTECTED 0x0004
#define ACC_STATIC 0x0008
#define ACC_FINAL 0x0010
#define ACC_SYNCHRONIZED 0x0020
#define ACC_SUPER 0x0020
#define ACC_VOLATILE 0x0040
#define ACC_NATIVE 0x0100
#define ACC_INTERFACE 0x0200
#define ACC_ABSTRACT 0x0400

typedef struct Class {
    const char *name;
    struct Class *super;
    u16 acc;
    u8 state;
    u8 is_array;
    struct Class **ifaces;
    int nifaces;
    Field *fields;
    int nfields;
    Method *methods;
    int nmethods;
    CP *cp;
    int ncp;
    int inst_slots;
    u8 *refmap;              /* per instance slot: 1 = reference */
    Slot *statics;
    u8 *static_refmap;
    int nstatics;
    Method **vtable;
    int vtsize;
    struct Class *elem;      /* arrays: component class (NULL = primitive) */
    char elemtype;           /* arrays: B C S I J F D Z L */
    int elemsize;
    struct Object *mirror;   /* java.lang.Class object */
    struct Thread *init_thread;
    void (*finalizer)(Object *);
    struct Class *hnext;
    const char *srcfile;
    void *jar;               /* where the class came from (for resources) */
} Class;

/* ---- threads ---- */
typedef struct Frame {
    Method *m;
    u8 *pc;
    Slot *locals;
    Slot *sp;
    Slot *base;              /* operand stack base */
    Object *sync;
    Class *clinit;           /* frame runs <clinit> of this class */
    int boundary;            /* returning from this frame ends the thread */
} Frame;

enum { TS_RUNNABLE, TS_SLEEPING, TS_WAITING, TS_BLOCKED, TS_EVENTWAIT, TS_DEAD };

typedef struct Thread {
    int id;
    int state;
    Object *jthread;
    Slot *stack, *stack_end;
    Frame *frames;
    int depth, maxdepth;
    u32 wake;                /* ms tick to wake at */
    Object *wait_obj;
    u32 wait_lockcount;
    Object *blocked_on;
    Object *exception;
    int interrupted;
    int ret_kind;
    Method *tail;            /* NR_INVOKE target */
    struct Thread *next;
} Thread;

/* ---- VM globals ---- */
typedef struct VM {
    Thread *threads;
    Thread *cur;
    int next_tid;
    u32 now;
    int quit;
    char fatal[256];
} VM;
extern VM vm;

/* well-known classes */
extern Class *C_Object, *C_String, *C_Class, *C_Thread, *C_Throwable;
extern int F_String_value, F_String_offset, F_String_count, F_Thread_eetop, F_Throwable_msg, F_Class_vm;

/* symbols */
const char *sym(const char *s);
const char *symn(const char *s, int n);

/* class loading (class.c) */
typedef const u8 *(*ClassSource)(void *ctx, const char *name, int *len);
void cls_add_source(ClassSource fn, void *ctx);
Class *cls_find(const char *name);            /* loads + links (no init) */
Class *cls_find_sym(const char *name);
Class *cls_array(Class *elem, char etype);
Class *cls_array_of_desc(const char *desc);    /* "[I" etc. */
Class *cls_prim_array(char t);
int cls_init(Thread *t, Class *c);             /* 1 done, 0 frame pushed (re-execute), -1 exception */
int cls_is_sub(Class *c, Class *target);
Method *cls_method(Class *c, const char *name, const char *desc);   /* search up hierarchy */
Method *cls_imethod(Class *c, const char *name, const char *desc);  /* virtual lookup incl. ifaces */
Field *cls_field(Class *c, const char *name, const char *desc);
Field *cls_field_n(Class *c, const char *name);
int cls_field_off(const char *cls, const char *name);
Object *cls_mirror(Class *c);
void cls_set_finalizer(const char *cls, void (*fn)(Object *));
void native_register(const char *cls, const char *name, const char *desc, NativeFn fn);
NativeFn native_lookup(const char *cls, const char *name, const char *desc);
void cls_gc_mark(void (*mark)(Object *));
void cls_foreach(void (*f)(Class *c, void *u), void *u);

/* heap (heap.c) */
int heap_init(size_t bytes);
Object *obj_new(Thread *t, Class *c);
Array *arr_new(Thread *t, Class *acls, s32 n);
Array *arr_newp(Thread *t, char type, s32 n);
void gc_collect(void);
int heap_is_obj(void *p);
size_t heap_free_bytes(void);
size_t heap_total_bytes(void);
void gc_add_root(Object **p);
void troot(Object *o);            /* keep o alive until the current native returns */
void troot_reset(void);
void gc_del_root(Object **p);

/* strings */
Object *str_new_utf16(Thread *t, const u16 *s, int n);
Object *str_new_utf8(Thread *t, const char *s);       /* modified UTF-8 / UTF-8 */
Object *str_new_ksc(Thread *t, const char *s, int n);  /* EUC-KR / CP949 bytes */
Object *str_intern(Thread *t, Object *s);
int str_len(Object *s);
const u16 *str_chars(Object *s);
char *str_to_utf8(Object *s, char *buf, int max);
int str_eq_c(Object *s, const char *c);
/* EUC-KR <-> UTF-16 (ksc.c) */
int ksc_to_u16(const u8 *in, int n, u16 *out);       /* returns chars */
int u16_to_ksc(const u16 *in, int n, u8 *out);       /* out needs 2n bytes */

/* exceptions */
int throw_new(Thread *t, const char *cls, const char *msg);  /* returns NR_EXC */
int throw_npe(Thread *t);

/* threads + interpreter (interp.c) */
Thread *thread_new(Object *jthread);
int thread_start_method(Thread *t, Method *m, Slot *args, int nargs);
void vm_run(int budget_ms);
int vm_runnable(void);          /* any thread ready right now */
u32 vm_next_wake(void);          /* earliest wake time or 0xFFFFFFFF */
void vm_wake_event(void);        /* wake event-waiters */
int monitor_enter(Thread *t, Object *o);  /* 1 owned, 0 must block */
int monitor_exit(Thread *t, Object *o);
int monitor_wait(Thread *t, Object *o, s64 ms);
int monitor_notify(Thread *t, Object *o, int all);
void vm_print_stack(Thread *t);
int vm_call_static(const char *cls, const char *name, const char *desc, Slot *args, int nargs); /* new thread */

/* natives (native.c and api_*.c) */
void natives_init(void);
void natives_lang(void);
void natives_api(void);

/* properties + resources (set up by the loader) */
void vm_set_prop(const char *k, const char *v);
const char *vm_get_prop(const char *k);
extern u8 *(*vm_resource_hook)(const char *name, u32 *len);
int vm_boot(size_t heap_bytes);

/* misc */
void vm_log(const char *fmt, ...);
u32 vm_ticks(void);
extern Method *cur_native;

static inline s64 slot_j(Slot *s) { s64 v; memcpy(&v, s, 8); return v; }
static inline void slot_setj(Slot *s, s64 v) { memcpy(s, &v, 8); }
static inline double slot_d(Slot *s) { double v; memcpy(&v, s, 8); return v; }
static inline void slot_setd(Slot *s, double v) { memcpy(s, &v, 8); }

#endif
