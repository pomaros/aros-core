/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Distributed under the AROS Public License 1.1.

    Owned C-runtime admission: the actual aros/crt_thread_context.h and
    the actual pthread_create()/StarterFunc() admission, with each AROS Task
    a host thread. Two providers, "a.library" and "b.library", record what
    their hooks see.
*/
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

__thread int sem_depth; atomic_int live_mem, fail_alloc_at, alloc_count;
__thread struct Task *this_task;

/* ---- the AROS pthread names, apart from the host's ---- */
typedef unsigned int aros_pthread_t;
struct sched_param_aros { int sched_priority; };
typedef struct { void *stackaddr; size_t stacksize; int detachstate; struct sched_param_aros param; } aros_pthread_attr_t;
#undef PTHREAD_KEYS_MAX
#undef PTHREAD_THREADS_MAX
#undef PTHREAD_DESTRUCTOR_ITERATIONS
#define PTHREAD_KEYS_MAX 64
#define PTHREAD_THREADS_MAX 16
#define PTHREAD_DESTRUCTOR_ITERATIONS 4
#define AROS_CREATE_DETACHED 1
#define EINVAL 22
#define EAGAIN 35
#define NAMELEN 32
#define SIGF_PARENT (1UL << 16)
#define GET_THIS_TASK FindTask(NULL)
#define D(x)
#define DB2(x)
static inline void Forbid(void) {}
static inline void SetTaskPri(struct Task *t, int p) { (void)t; (void)p; }
static inline int aros_pthread_attr_init(aros_pthread_attr_t *a) { memset(a, 0, sizeof(*a)); return 0; }
#define NP_Entry 1
#define NP_StackSize 2
#define NP_UserData 3
#define NP_Name 4
#define TAG_IGNORE 5
#define TAG_DONE 0
struct SignalSemaphore thread_sem, tls_sem;
typedef struct { void (*destructor)(void *); BOOL used; } TLSKey;
TLSKey tlskeys[PTHREAD_KEYS_MAX];
volatile BOOL __pthread_exiting;
static inline APTR NewStackSwap(APTR s, APTR f, APTR a) { (void)s; (void)f; (void)a; abort(); }
struct StackSwapArgs { IPTR Args[8]; }; struct StackSwapStruct { APTR stk_Lower, stk_Upper, stk_Pointer; };

/* ---- libraries and the two providers ---- */
struct provider {
    const char *name; struct Library lib; int opens, loaded, expunge, dead, instance;
    int active, fail_capture, fail_enter, enter_delay_ms, open_activates, fail_open;
    atomic_int live_ctx, captured, entered, left, cancelled, actives;
    struct Task *last_pin_closer; atomic_int creator_closes;
    struct CRTThreadContextHooks *hooks;
} prov[2] = { { .name = "a.library" }, { .name = "b.library" } };
struct ctx { struct provider *p; struct Library *base; };
static __thread int in_leave[2]; /* per Task: inside this provider's Leave */
static pthread_mutex_t lib_lock = PTHREAD_MUTEX_INITIALIZER;
static int reload_on_open = -1; /* test: unregister and reload a provider during capture */
static struct Task *creator_task;

#include "aros/crt_thread_context.h"

static void provider_register(struct provider *p);
struct Library *OpenLibrary(CONST_STRPTR name, ULONG v)
{
    (void)v;
    int i = !strcmp((const char *)name, "b.library");
    struct provider *p = &prov[i];
    if (reload_on_open == i)
    {
        /* the module was expunged and loaded again between copy and pin */
        reload_on_open = -1;
        CRTThreadContextUnregister(p->hooks);
        p->hooks->tc_Version = 0xdead;    /* the old hooks are gone */
        free(p->hooks);p->hooks=NULL;
        p->instance++;                    /* a new copy of the code */
        provider_register(p);
    }
    if (p->fail_open) return NULL;
    pthread_mutex_lock(&lib_lock);
    p->opens++; p->loaded = 1;
    if (p->open_activates) p->active = 1;   /* opening creates the state Active reports */
    pthread_mutex_unlock(&lib_lock);
    return &p->lib;
}
void CloseLibrary(struct Library *l)
{
    struct provider *p = l == &prov[0].lib ? &prov[0] : &prov[1];
    assert(l == &prov[0].lib || l == &prov[1].lib);
    assert(!in_leave[p == &prov[1]]);     /* never closed from its own hook */
    pthread_mutex_lock(&lib_lock);
    assert(p->opens > 0);
    if (--p->opens == 0 && p->expunge) { p->loaded = 0; p->dead = 1; }
    p->last_pin_closer = this_task;
    if (this_task == creator_task) atomic_fetch_add(&p->creator_closes, 1);
    pthread_mutex_unlock(&lib_lock);
}
#define CODE_ALIVE(p) assert((p)->loaded && !(p)->dead)

static APTR hook_select(struct provider *p) { assert(sem_depth > 0); assert(p->hooks->tc_Version == CRT_THREAD_CONTEXT_VERSION); CODE_ALIVE(p); atomic_fetch_add(&p->actives, 1); return p->active ? p : NULL; }
static BOOL hook_capture(struct provider *p, APTR source, APTR *out)
{
    assert(sem_depth == 0 && source == p); CODE_ALIVE(p);
    if (p->fail_capture) return FALSE;
    struct ctx *c = AllocMem(sizeof(*c), MEMF_CLEAR);
    if (!c) return FALSE;
    c->p = p; atomic_fetch_add(&p->live_ctx, 1); atomic_fetch_add(&p->captured, 1);
    *out = c; return TRUE;
}
static BOOL hook_enter(struct provider *p, APTR x)
{
    struct ctx *c = x;
    assert(sem_depth == 0 && c->p == p && this_task != creator_task);
    if (p->enter_delay_ms) usleep(p->enter_delay_ms * 1000);
    CODE_ALIVE(p);
    if (p->fail_enter) return FALSE;
    c->base = OpenLibrary((CONST_STRPTR)p->name, 0);   /* the worker's own instance */
    this_task->crt_live++;
    atomic_fetch_add(&p->entered, 1);
    return TRUE;
}
static struct Library *hook_leave(struct provider *p, APTR x)
{
    struct ctx *c = x; struct Library *base = c->base;
    CODE_ALIVE(p);
    this_task->crt_live--;
    in_leave[p == &prov[1]] = 1;
    FreeMem(c, sizeof(*c)); atomic_fetch_sub(&p->live_ctx, 1); atomic_fetch_add(&p->left, 1);
    in_leave[p == &prov[1]] = 0;
    return base;
}
static void hook_cancel(struct provider *p, APTR x)
{
    struct ctx *c = x; CODE_ALIVE(p);
    FreeMem(c, sizeof(*c)); atomic_fetch_sub(&p->live_ctx, 1); atomic_fetch_add(&p->cancelled, 1);
}
/* Each loaded instance of a provider's code has its own functions; calling
   an older instance's function is calling unloaded code. */
#define HOOKS(n, i) \
 static APTR a##n##_##i(void) { assert(prov[n].instance == i); return hook_select(&prov[n]); } \
 static BOOL c##n##_##i(APTR s, APTR *o) { assert(prov[n].instance == i); return hook_capture(&prov[n], s, o); } \
 static BOOL e##n##_##i(APTR x) { assert(prov[n].instance == i); return hook_enter(&prov[n], x); } \
 static struct Library *l##n##_##i(APTR x) { assert(prov[n].instance == i); return hook_leave(&prov[n], x); } \
 static void x##n##_##i(APTR x) { assert(prov[n].instance == i); hook_cancel(&prov[n], x); }
HOOKS(0, 0) HOOKS(1, 0) HOOKS(0, 1) HOOKS(1, 1)
static void provider_register(struct provider *p)
{
    int n = p == &prov[1];
    struct CRTThreadContextHooks *h = calloc(1, sizeof(*h));
    h->tc_Version = CRT_THREAD_CONTEXT_VERSION; h->tc_LibName = (CONST_STRPTR)p->name;
    int i = p->instance;
    h->tc_Select = n ? (i ? a1_1 : a1_0) : (i ? a0_1 : a0_0);
    h->tc_Capture = n ? (i ? c1_1 : c1_0) : (i ? c0_1 : c0_0);
    h->tc_Enter = n ? (i ? e1_1 : e1_0) : (i ? e0_1 : e0_0);
    h->tc_Leave = n ? (i ? l1_1 : l1_0) : (i ? l0_1 : l0_0);
    h->tc_Cancel = n ? (i ? x1_1 : x1_0) : (i ? x0_1 : x0_0);
    p->hooks = h;
    assert(CRTThreadContextRegister(h));
}

static struct Task *CreateNewProcTags(IPTR tag, ...);
/* A C-library call from pthread's worker code: only while a runtime is in */
static void *crt_memset(void *d, int c, size_t n) { assert(this_task->crt_live > 0); return memset(d, c, n); }
/* ---- the actual pthread code ---- */
#include "pthread_actual.c"

/* ---- the CreateNewProc mock: a host thread per Task ---- */
static int fail_create;
struct newtask { struct Task task; void (*entry)(void); };
static void *task_main(void *x) { struct newtask *t = x; this_task = &t->task; t->entry(); return NULL; }
static pthread_t host_threads[64]; static struct newtask *host_tasks[64]; static int host_count;
static struct Task *CreateNewProcTags(IPTR tag, ...)
{
    if (fail_create) return NULL;
    struct newtask *t = calloc(1, sizeof(*t));
    pthread_mutex_init(&t->task.m, NULL); pthread_cond_init(&t->task.c, NULL);
    va_list ap; va_start(ap, tag);
    for (IPTR k = tag; k != TAG_DONE; k = va_arg(ap, IPTR))
    {
        IPTR v = va_arg(ap, IPTR);
        if (k == NP_Entry) t->entry = (void (*)(void))v;
        if (k == NP_UserData) t->task.tc_UserData = (APTR)v;
    }
    va_end(ap);
    host_tasks[host_count]=t;
    assert(pthread_create(&host_threads[host_count++], NULL, task_main, t) == 0);
    return &t->task;
}

/* ---- scenarios ---- */
static atomic_int started, start_saw_worker;
static void *start_fn(void *arg) { (void)arg; atomic_fetch_add(&started, 1); atomic_store(&start_saw_worker, (this_task->crt_live > 0)); return NULL; }
static atomic_int gate;
static void *gated_fn(void *arg) { (void)arg; while (!atomic_load(&gate)) usleep(1000); atomic_fetch_add(&started, 1); return NULL; }
static void join_all(void) {
    for (int i=0;i<host_count;i++) {
        assert(!pthread_join(host_threads[i],NULL));
        assert(!pthread_mutex_destroy(&host_tasks[i]->task.m));
        assert(!pthread_cond_destroy(&host_tasks[i]->task.c));
        free(host_tasks[i]);host_tasks[i]=NULL;
    }
    host_count=0;
}
static void reset(void)
{
    join_all();
    for (int i = 0; i < 2; i++)
    {
        struct provider *p = &prov[i];
        assert(p->opens == 0 && atomic_load(&p->live_ctx) == 0);
        p->active = 1; p->fail_capture = p->fail_enter = p->enter_delay_ms = p->open_activates = p->fail_open = 0; p->expunge = 0; p->dead = 0; p->loaded = 1;
        atomic_store(&p->captured, 0); atomic_store(&p->entered, 0); atomic_store(&p->left, 0);
        atomic_store(&p->cancelled, 0); atomic_store(&p->actives, 0); p->last_pin_closer = NULL; atomic_store(&p->creator_closes, 0);
    }
    atomic_store(&started, 0); atomic_store(&start_saw_worker, 0); fail_create = 0;
    atomic_store(&fail_alloc_at, 0); atomic_store(&alloc_count, 0);
    for (int i = 0; i < PTHREAD_THREADS_MAX; i++) if (threads[i]) assert(threads[i]->task == NULL || threads[i]->finished || threads[i]->detached);
}
static void free_slot(aros_pthread_t id) { join_all(); if (threads[id] && !threads[id]->detached) memset(threads[id], 0, sizeof(ThreadInfo)); }


int main(void)
{
    static struct newtask main_task;
    pthread_mutex_init(&main_task.task.m, NULL); pthread_cond_init(&main_task.task.c, NULL);
    this_task = creator_task = &main_task.task;
    pthread_rwlock_init(&thread_sem.lock, NULL); pthread_rwlock_init(&tls_sem.lock, NULL);

    NEWLIST(&SysBase->SemaphoreList);
    assert(CRTThreadContextsGet(TRUE));
    provider_register(&prov[0]); provider_register(&prov[1]);
    aros_pthread_t id; aros_pthread_attr_t detached; aros_pthread_attr_init(&detached); detached.detachstate = AROS_CREATE_DETACHED;

    /* 1. joinable, A active, B inactive: success only after A entered; the
          worker has an admitted runtime; pins closed by the
          creator; the worker's base closed after Leave returned */
    reset(); prov[1].active = 0; prov[0].enter_delay_ms = 20;
    assert(aros_pthread_create(&id, NULL, start_fn, NULL) == 0);
    assert(atomic_load(&prov[0].entered) == 1 && atomic_load(&prov[1].captured) == 0);
    /* A's pin closed by the creator; B, inactive, never opened */
    assert(atomic_load(&prov[0].creator_closes) == 1 && atomic_load(&prov[1].creator_closes) == 0 && !(this_task->crt_live > 0));
    free_slot(id);
    assert(atomic_load(&started) == 1 && atomic_load(&start_saw_worker) == 1 && atomic_load(&prov[0].left) == 1);

    /* 2. no provider active: no runtime is invented for the worker. */
    reset(); prov[0].active = prov[1].active = 0;
    assert(aros_pthread_create(&id, NULL, start_fn, NULL) == 0); free_slot(id);
    assert(atomic_load(&start_saw_worker) == 0);

    /* 3. the second Enter fails: A left and its base closed, B cancelled,
          start never runs, EAGAIN, the slot is free */
    reset(); prov[1].fail_enter = 1;
    assert(aros_pthread_create(&id, NULL, start_fn, NULL) == EAGAIN); join_all();
    assert(!atomic_load(&started) && atomic_load(&prov[0].left) == 1 && atomic_load(&prov[1].cancelled) == 1);
    assert(GetThreadId(NULL) != PTHREAD_THREADS_MAX);

    /* 4. capture refused: A cancelled, no Task created */
    reset(); prov[1].fail_capture = 1; int before = host_count;
    assert(aros_pthread_create(&id, NULL, start_fn, NULL) == EAGAIN);
    assert(host_count == before && atomic_load(&prov[0].cancelled) == 1);

    /* 5. the token cannot be allocated: contexts cancelled */
    reset(); atomic_store(&fail_alloc_at, 3);   /* ctx A, ctx B, then the token */
    assert(aros_pthread_create(&id, NULL, start_fn, NULL) == EAGAIN);
    assert(atomic_load(&prov[0].cancelled) == 1 && atomic_load(&prov[1].cancelled) == 1);

    /* 6. CreateNewProc fails: contexts cancelled, slot free */
    reset(); fail_create = 1;
    assert(aros_pthread_create(&id, NULL, start_fn, NULL) == EAGAIN);
    assert(atomic_load(&prov[0].cancelled) == 1 && GetThreadId(NULL) != PTHREAD_THREADS_MAX);

    /* 7. the creator's runtime closes right after creation: the worker
          holds its own instance, so Leave still runs live code */
    reset(); prov[1].active = 0; atomic_store(&gate, 0);
    assert(aros_pthread_create(&id, NULL, gated_fn, NULL) == 0);
    prov[0].expunge = 1;                 /* the creator closed its last base */
    assert(prov[0].opens == 1 && prov[0].loaded); /* only the worker's own instance */
    atomic_store(&gate, 1);
    free_slot(id);
    assert(atomic_load(&prov[0].left) == 1 && prov[0].dead);
    prov[0].dead = 0; prov[0].expunge = 0;

    /* 7b. the creator's runtime is already closing when it creates: only the
           creator's pin keeps the provider loaded until the worker has
           entered its own instance */
    reset(); prov[1].active = 0; prov[0].expunge = 1; prov[0].enter_delay_ms = 20;
    assert(aros_pthread_create(&id, NULL, start_fn, NULL) == 0);
    free_slot(id);
    assert(atomic_load(&prov[0].entered) == 1 && atomic_load(&prov[0].left) == 1 && prov[0].dead);
    prov[0].dead = 0; prov[0].expunge = 0;

    /* 8. a fast detached worker finishes before the creator wakes */
    reset(); prov[1].active = 0;
    detached.detachstate = AROS_CREATE_DETACHED;
    assert(aros_pthread_create(&id, &detached, start_fn, NULL) == 0); join_all();
    assert(atomic_load(&started) == 1 && atomic_load(&prov[0].left) == 1);
    assert(threads[id]->task == NULL && !threads[id]->detached);   /* the worker cleared its own slot */

    /* 9. a provider expunged and reloaded between copy and pin: the
          capture retries, never calling the old hooks */
    reset(); prov[1].active = 0; reload_on_open = 0;
    assert(aros_pthread_create(&id, NULL, start_fn, NULL) == 0); free_slot(id);
    assert(atomic_load(&prov[0].entered) == 1);

    /* 10. opening a provider would make it active: an inactive provider is
           never opened nor captured */
    reset(); prov[1].active = 0; prov[1].open_activates = 1;
    assert(aros_pthread_create(&id, NULL, start_fn, NULL) == 0); free_slot(id);
    assert(atomic_load(&prov[1].captured) == 0 && !prov[1].active && atomic_load(&prov[0].entered) == 1);

    /* 11. an active provider's pin fails: refused, nothing captured or run */
    reset(); prov[1].fail_open = 1; before = host_count;
    assert(aros_pthread_create(&id, NULL, start_fn, NULL) == EAGAIN);
    assert(host_count == before && !atomic_load(&prov[0].captured) && !atomic_load(&prov[1].captured) && prov[0].opens == 0);

    /* 12. an active provider whose name does not fit: refused, not dropped */
    reset(); CONST_STRPTR name = prov[1].hooks->tc_LibName;
    prov[1].hooks->tc_LibName = (CONST_STRPTR)"a-library-name-that-is-longer-than-the-sixty-four-bytes-a-set-holds.library";
    assert(aros_pthread_create(&id, NULL, start_fn, NULL) == EAGAIN);
    assert(!atomic_load(&prov[0].captured) && prov[0].opens == 0);
    prov[1].hooks->tc_LibName = name;

    reset();
    /* left: the permanent registry and pthread's ThreadInfo slots; every
       context and every admission token was freed */
    assert(atomic_load(&live_mem) == 1 + (int)threads_used);
    printf("PASS: owned admission: success after Enter, second-Enter failure, capture/token/create failures, creator runtime closed, fast detached worker, reload during capture, inactive never opened, pin failure, unrepresentable set\n");
    return 0;
}
