/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL), Version 1.1.
 *
 * The production dlopen(), dlsym(), dlclose(), dlerror() and their internals
 * against modeled exec and DOS: lists, semaphores (recursive mutexes), tasks
 * (threads), LoadSeg() of fake modules whose entry the model answers. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- exec types, lists and semaphores ---- */
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef uint16_t UWORD;
typedef unsigned char UBYTE;
typedef short BOOL;
typedef uintptr_t IPTR;
typedef intptr_t SIPTR;
typedef void *APTR;
typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR;
typedef void *BPTR;
#define TRUE 1
#define FALSE 0
#define BNULL ((BPTR)0)
#define BADDR(x) ((APTR)(x))
#define RETURN_FAIL 20
#define SHARED_LOCK -2
#define MEMF_ANY 0
#define MEMF_CLEAR (1 << 16)
#define D(x)
#define __aarch64__ 1

struct Node { struct Node *ln_Succ, *ln_Pred; UBYTE ln_Type; };
struct MinNode { struct MinNode *mln_Succ, *mln_Pred; };
struct List { struct Node *lh_Head, *lh_Tail, *lh_TailPred; };
struct MinList { struct MinNode *mlh_Head, *mlh_Tail, *mlh_TailPred; };
#define NEWLIST(l) ((l)->lh_Head = (struct Node *)&(l)->lh_Tail, (l)->lh_Tail = NULL, \
                    (l)->lh_TailPred = (struct Node *)&(l)->lh_Head)
static void list_addtail(struct List *l, struct Node *n)
{ n->ln_Succ = (struct Node *)&l->lh_Tail; n->ln_Pred = l->lh_TailPred; l->lh_TailPred->ln_Succ = n; l->lh_TailPred = n; }
static void list_addhead(struct List *l, struct Node *n)
{ n->ln_Succ = l->lh_Head; n->ln_Pred = (struct Node *)&l->lh_Head; l->lh_Head->ln_Pred = n; l->lh_Head = n; }
static void list_remove(struct Node *n) { n->ln_Pred->ln_Succ = n->ln_Succ; n->ln_Succ->ln_Pred = n->ln_Pred; }
static struct Node *list_remhead(struct List *l)
{ struct Node *n = l->lh_Head; if (!n->ln_Succ) return NULL; list_remove(n); return n; }
static struct Node *list_remtail(struct List *l)
{ struct Node *n = l->lh_TailPred; if (!n->ln_Pred) return NULL; list_remove(n); return n; }
#define ADDTAIL(l, n) list_addtail((struct List *)(l), (struct Node *)(n))
#define ADDHEAD(l, n) list_addhead((struct List *)(l), (struct Node *)(n))
#define REMOVE(n) list_remove((struct Node *)(n))
#define REMHEAD(l) list_remhead((struct List *)(l))
#define REMTAIL(l) list_remtail((struct List *)(l))
#define ForeachNode(l, n) for (n = (void *)(((struct List *)(l))->lh_Head); \
    ((struct Node *)(n))->ln_Succ; n = (void *)(((struct Node *)(n))->ln_Succ))

struct SignalSemaphore { pthread_mutex_t m; };
static void InitSemaphore(struct SignalSemaphore *s)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    assert(pthread_mutex_init(&s->m, &a) == 0);
}
static void ObtainSemaphore(struct SignalSemaphore *s) { assert(pthread_mutex_lock(&s->m) == 0); }
static void ReleaseSemaphore(struct SignalSemaphore *s) { assert(pthread_mutex_unlock(&s->m) == 0); }

struct Task { int id; };
struct FullJumpVec { unsigned long jmp; unsigned long vec; };
struct ExecBase { int unused; };
static struct ExecBase sysbase, *SysBase = &sysbase;
static _Thread_local struct Task *current_task;
static struct Task main_task = {0};
static struct Task *FindTask(const char *name) { (void)name; return current_task ? current_task : &main_task; }

static atomic_long allocs, frees;
static void *AllocVec(size_t size, ULONG flags)
{
    void *p = (flags & MEMF_CLEAR) ? calloc(1, size) : malloc(size);
    if (p) allocs++;
    return p;
}
static void FreeVec(void *p) { if (p) { frees++; free(p); } }

/* ---- the posixc and stdc pieces the code uses ---- */
struct AtExitNode { struct Node node; union { struct { void (*fn)(int, void *); void *arg; } on; } func; };
#define AEN_ON 1
struct StdCIntBase { struct List atexit_list; };
static struct StdCIntBase stdcbase;
static struct StdCIntBase *__aros_getbase_StdCBase(void) { return &stdcbase; }

struct PosixCIntBase
{
    struct PosixCIntBase *fd_owner;
    struct MinList dl_modules;
    struct SignalSemaphore dl_sem;
    APTR dl_release;
    struct MinList dl_errors;
};
static struct PosixCIntBase owner_base, worker_base;
static _Thread_local struct PosixCIntBase *current_base;
static struct PosixCIntBase *__aros_getbase_PosixCBase(void) { return current_base ? current_base : &owner_base; }
static const char *__path_u2a(const char *path) { return path; }
static int (*test_openlib)(struct PosixCIntBase *);
#define ADD2OPENLIB(f, pri) static int (*test_openlib_##f)(struct PosixCIntBase *) __attribute__((unused)) = f; \
    __attribute__((constructor)) static void register_##f(void) { test_openlib = f; }

/* ---- the module descriptor (aros/dlmodule.h, verbatim layout) ---- */
#define AROS_DLMODULE_MAGIC 0x41444C4DUL
#define AROS_DLMODULE_VERSION 1
#define AROS_DLMODULE_MARK_OFFSET 4
struct AROSDLExport { const char *de_Name; void *de_Address; };
struct AROSDLModule
{
    ULONG dm_Magic; UWORD dm_Version; UWORD dm_Reserved;
    BOOL (*dm_Init)(void); void (*dm_Expunge)(void);
    const struct AROSDLExport *dm_Exports; ULONG dm_ExportCount;
};

/* ---- the modeled file system and modules ---- */
static int alpha_a, alpha_b, beta_a;
static const struct AROSDLExport alpha_exports[] = {
    {"alpha_a", &alpha_a}, {"alpha_b", &alpha_b}, {"shared", &alpha_a},
};
static const struct AROSDLExport beta_exports[] = { {"beta_a", &beta_a}, {"shared", &beta_a} };
static atomic_int alpha_inits, alpha_expunges, beta_inits, beta_expunges;
static int fail_init;
static BOOL alpha_init(void) { if (fail_init) return FALSE; alpha_inits++; return TRUE; }
static void alpha_expunge(void) { alpha_expunges++; }
static BOOL beta_init(void) { beta_inits++; return TRUE; }
static void beta_expunge(void) { beta_expunges++; }
static const struct AROSDLModule alpha = { AROS_DLMODULE_MAGIC, 1, 0, alpha_init, alpha_expunge, alpha_exports, 3 };
static const struct AROSDLModule beta = { AROS_DLMODULE_MAGIC, 1, 0, beta_init, beta_expunge, beta_exports, 2 };
static const struct AROSDLModule wrong_version = { AROS_DLMODULE_MAGIC, 2, 0, alpha_init, alpha_expunge, alpha_exports, 3 };

/* What exists: the name a Lock() takes, the full name NameFromLock() gives,
   the module LoadSeg() makes of it and whether its code carries the mark. */
struct file { const char *name, *full; const struct AROSDLModule *module; int marked; int jump; };
static const struct file files[] = {
    {"Work:Mods/alpha.so", "Work:Mods/alpha.so", &alpha, 1, 0},
    {"PROGDIR:alpha.so", "Work:Mods/alpha.so", &alpha, 1, 0},
    {"LIBS:dl/beta.so", "SYS:Libs/dl/beta.so", &beta, 1, 1},
    {"PROGDIR:beta.so", "Work:Prog/beta.so", &beta, 1, 1},
    {"LIBS:dl/libvk.so", "SYS:Libs/dl/libvk.so", &alpha, 1, 0},
    {"Work:Prog/Program", "Work:Prog/Program", NULL, 0, 0},
    {"Work:Mods/newer.so", "Work:Mods/newer.so", &wrong_version, 1, 0},
    {"Work:Mods/silent.so", "Work:Mods/silent.so", NULL, 1, 0},
};
#define NFILES (sizeof(files) / sizeof(files[0]))
static const struct file *find_file(const char *name)
{
    for (size_t i = 0; i < NFILES; i++)
        if (strcmp(files[i].name, name) == 0 || strcmp(files[i].full, name) == 0)
            return &files[i];
    return NULL;
}
static LONG ioerr;
static LONG IoErr(void) { return ioerr; }
static atomic_int locks;
static BPTR Lock(CONST_STRPTR name, LONG mode)
{
    (void)mode;
    const struct file *f = find_file((const char *)name);
    if (!f) { ioerr = 205; return BNULL; }
    locks++;
    return (BPTR)f;
}
static void UnLock(BPTR lock) { assert(lock); locks--; }
static BOOL NameFromLock(BPTR lock, STRPTR buf, LONG size)
{
    const struct file *f = lock;
    if ((LONG)strlen(f->full) + 1 > size) return FALSE;
    strcpy((char *)buf, f->full);
    return TRUE;
}
static LONG Fault(LONG code, CONST_STRPTR header, STRPTR buf, LONG size)
{ (void)header; snprintf((char *)buf, (size_t)size, "error %d", (int)code); return 1; }

/* A loaded segment, laid out as a LoadSeg() hunk: its size, the BPTR the
   seglist points at, then the code: a branch, the mark, the version; or,
   for an aligned code section, a FullJumpVec to that code further on. */
struct code { ULONG branch, magic, version; } __attribute__((packed));
struct segment { ULONG size; BPTR next; union { struct code direct; struct FullJumpVec jump; } start;
                 struct code aligned; const struct file *file; } __attribute__((packed));
#define SEG(bptr) ((struct segment *)((UBYTE *)(bptr) - offsetof(struct segment, next)))
static atomic_int loads, unloads, entries;
static BPTR LoadSeg(CONST_STRPTR name)
{
    const struct file *f = find_file((const char *)name);
    struct segment *s;
    if (!f) { ioerr = 205; return BNULL; }
    s = calloc(1, sizeof(*s));
    s->size = sizeof(*s);
    struct code *c = &s->start.direct;
    if (f->jump)
    {
        s->start.jump.jmp = 0xd61f020058000050UL;
        s->start.jump.vec = (unsigned long)&s->aligned;
        c = &s->aligned;
    }
    c->magic = f->marked ? AROS_DLMODULE_MAGIC : 0xd65f03c0;
    c->version = 1;
    s->file = f;
    loads++;
    return (BPTR)&s->next;
}
static void UnLoadSeg(BPTR seg) { unloads++; free(SEG(seg)); }
static IPTR call_entry(APTR code, STRPTR argstr, ULONG argsize, struct ExecBase *base)
{
    struct segment *s = SEG((UBYTE *)code - sizeof(BPTR));
    struct code *c = s->file->jump ? &s->aligned : &s->start.direct;
    (void)argstr; (void)base;
    entries++;
    assert(c->magic == AROS_DLMODULE_MAGIC);  /* never called unmarked */
    assert(argsize == AROS_DLMODULE_MAGIC);
    return s->file->module ? (IPTR)s->file->module : (IPTR)RETURN_FAIL;
}
#define AROS_UFCA(type, value, reg) (value)
#define AROS_UFC3(type, entry, a, b, c) ((type)call_entry((APTR)(entry), (a), (b), (c)))

/* <dlfcn.h> */
#define RTLD_LAZY 0x0001
#define RTLD_NOW 0x0002
#define RTLD_NOLOAD 0x0004
#define RTLD_GLOBAL 0x0100
#define RTLD_LOCAL 0x0000
#define RTLD_NODELETE 0x1000
#define RTLD_DEFAULT ((void *)0)
#define RTLD_NEXT ((void *)-1L)

#include "production-dl.c"
#include "production-dlopen.c"
#include "production-dlsym.c"
#include "production-dlclose.c"
#include "production-dlerror.c"
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } Dl_info;
#include "production-dladdr.c"

static void run_exit_list(void)
{
    struct AtExitNode *aen;
    while ((aen = (struct AtExitNode *)REMHEAD(&stdcbase.atexit_list)) != NULL)
    {
        aen->func.on.fn(0, aen->func.on.arg);
        free(aen);
    }
}

static void *worker(void *arg)
{
    static struct Task tasks[8];
    int id = (int)(intptr_t)arg;
    current_task = &tasks[id];
    current_base = (id & 1) ? &worker_base : &owner_base;   /* borrowers share the owner's */
    for (int i = 0; i < 3000; i++)
    {
        void *h = aros_test_dlopen("Work:Mods/alpha.so", RTLD_NOW);
        assert(h != NULL);
        assert(aros_test_dlsym(h, "alpha_b") == &alpha_b);
        assert(aros_test_dlclose(h) == 0);
    }
    return NULL;
}

int main(void)
{
    void *a, *a2, *b, *self;
    char *e;
    struct Task other = {1};

    NEWLIST(&stdcbase.atexit_list);
    assert(test_openlib(&owner_base) == 1);
    assert(test_openlib(&worker_base) == 1);
    worker_base.fd_owner = &owner_base;

    /* Lookup: explicit path, PROGDIR: before LIBS:dl/, the version fallback. */
    a = aros_test_dlopen("Work:Mods/alpha.so", RTLD_NOW);
    assert(a && alpha_inits == 1);
    assert(aros_test_dlopen("alpha.so", RTLD_LAZY) == a);          /* PROGDIR:, same file */
    b = aros_test_dlopen("beta.so", RTLD_NOW);                      /* PROGDIR: wins */
    assert(b && strcmp(((struct __dl_module *)b)->path, "Work:Prog/beta.so") == 0);
    void *vk = aros_test_dlopen("libvk.so.1.3", RTLD_NOW);         /* LIBS:dl/libvk.so */
    assert(vk && strcmp(((struct __dl_module *)vk)->path, "SYS:Libs/dl/libvk.so") == 0);
    assert(vk == aros_test_dlopen("libvk.so", RTLD_NOW) && aros_test_dlclose(vk) == 0);
    assert(aros_test_dlclose(vk) == 0);
    assert(aros_test_dlopen("nothing.so", RTLD_NOW) == NULL);
    e = aros_test_dlerror();
    assert(e && strstr(e, "nothing.so") && strstr(e, "cannot find"));
    assert(aros_test_dlerror() == NULL);

    /* Refusals: unmarked code is never called, a wrong version, no
       descriptor, a failing Init; each unloads what it loaded. */
    int before_entries = entries, before_unloads = unloads;
    assert(aros_test_dlopen("Work:Prog/Program", RTLD_NOW) == NULL);
    assert(entries == before_entries && unloads == before_unloads + 1);
    assert(strstr(aros_test_dlerror(), "not a dlopen module"));
    assert(aros_test_dlopen("Work:Mods/newer.so", RTLD_NOW) == NULL && aros_test_dlerror());
    assert(aros_test_dlopen("Work:Mods/silent.so", RTLD_NOW) == NULL && aros_test_dlerror());
    assert(aros_test_dlclose(a) == 0 && aros_test_dlclose(a) == 0);  /* both references */
    assert(alpha_expunges == 2);    /* libvk.so shares alpha's descriptor */
    fail_init = 1;
    assert(aros_test_dlopen("Work:Mods/alpha.so", RTLD_NOW) == NULL);
    assert(strstr(aros_test_dlerror(), "initialisation failed"));
    fail_init = 0;

    /* Modes. */
    assert(aros_test_dlopen("Work:Mods/alpha.so", RTLD_NOW | RTLD_GLOBAL) == NULL);
    assert(strstr(aros_test_dlerror(), "RTLD_GLOBAL"));
    assert(aros_test_dlopen("Work:Mods/alpha.so", 0x40000) == NULL && aros_test_dlerror());
    assert(aros_test_dlopen("Work:Mods/alpha.so", RTLD_NOLOAD) == NULL && aros_test_dlerror());
    self = aros_test_dlopen(NULL, RTLD_NOW);
    assert(self != NULL && aros_test_dlclose(self) == 0 && aros_test_dlclose(self) == 0);

    /* dlsym: every export, oldest-first global search, refusals. */
    a = aros_test_dlopen("Work:Mods/alpha.so", RTLD_NOW);
    a2 = aros_test_dlopen("Work:Mods/alpha.so", RTLD_NOLOAD);
    assert(a2 == a);
    assert(aros_test_dlsym(a, "alpha_a") == &alpha_a && aros_test_dlsym(a, "alpha_b") == &alpha_b);
    assert(aros_test_dlsym(a, "beta_a") == NULL && aros_test_dlerror());
    assert(aros_test_dlsym(b, "beta_a") == &beta_a);
    assert(aros_test_dlsym(RTLD_DEFAULT, "shared") == &beta_a);    /* beta loaded first */
    assert(aros_test_dlsym(self, "alpha_b") == &alpha_b);
    assert(aros_test_dlsym((void *)&other, "x") == NULL && strstr(aros_test_dlerror(), "invalid handle"));
    assert(aros_test_dlsym(a, NULL) == NULL && aros_test_dlerror());

    /* dladdr: the module that holds an address, or none. */
    Dl_info info;
    struct segment *seg_b = SEG(((struct __dl_module *)b)->seglist);
    assert(aros_test_dladdr(&seg_b->aligned.magic, &info) != 0);
    assert(strcmp(info.dli_fname, "Work:Prog/beta.so") == 0);
    assert(info.dli_fbase == (void *)&seg_b->aligned);  /* behind the jump */
    assert(aros_test_dladdr(&info, &info) == 0);
    assert(aros_test_dladdr(&seg_b->aligned.magic, NULL) == 0);

    /* dlerror is per task. */
    current_task = &other;
    assert(aros_test_dlopen("nope.so", RTLD_NOW) == NULL);
    current_task = NULL;
    assert(aros_test_dlerror() == NULL);
    current_task = &other;
    assert(aros_test_dlerror() != NULL && aros_test_dlerror() == NULL);
    current_task = NULL;

    /* NODELETE survives its last dlclose. */
    assert(aros_test_dlclose(a) == 0 && aros_test_dlclose(a2) == 0);
    a = aros_test_dlopen("Work:Mods/alpha.so", RTLD_NOW | RTLD_NODELETE);
    int expunged = alpha_expunges;
    assert(aros_test_dlclose(a) == 0 && alpha_expunges == expunged);
    assert(aros_test_dlopen("Work:Mods/alpha.so", RTLD_NOLOAD) == a);
    assert(aros_test_dlclose(a) == 0);
    assert(aros_test_dlclose(b) == 0 && beta_expunges == 1);
    assert(aros_test_dlclose(b) == -1 && aros_test_dlerror());

    /* Concurrency: owner and borrower tasks load, look up and release one
       module; the count must come back to zero and Init/Expunge pair. */
    pthread_t threads[8];
    int inits = alpha_inits, exps = alpha_expunges;
    for (int i = 0; i < 8; i++)
        assert(pthread_create(&threads[i], NULL, worker, (void *)(intptr_t)i) == 0);
    for (int i = 0; i < 8; i++)
        assert(pthread_join(threads[i], NULL) == 0);
    assert(alpha_inits - inits >= 0);
    /* a was NODELETE and stays loaded: every thread found it, none reloaded */
    assert(alpha_inits == inits && alpha_expunges == exps);

    /* Exit: one node queued, which unloads what is left and frees records. */
    b = aros_test_dlopen("beta.so", RTLD_NOW);
    int queued = 0;
    struct Node *n;
    ForeachNode(&stdcbase.atexit_list, n) queued++;
    assert(queued == 1);
    run_exit_list();
    assert(beta_expunges == 2 && alpha_expunges == exps + 1);
    assert(owner_base.dl_release == NULL);
    assert(loads == unloads);
    assert(locks == 0);
    assert(allocs == frees);
    puts("PASS dlopen/dlsym/dlclose/dlerror host fixture");
    return 0;
}
