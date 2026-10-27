/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
/* Actual public registry header, with host library and Exec lock boundaries. */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t ULONG;
typedef unsigned char UBYTE;
typedef int BOOL;
typedef void *APTR;
typedef const char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define SPINLOCK_MODE_READ 0
#define SPINLOCK_MODE_WRITE 1
#define LOCKF_DISABLE 1
#ifdef TEST_SMP
#define __AROSEXEC_SMP__ 1
#endif
struct Node { struct Node *ln_Succ, *ln_Pred; char *ln_Name; };
struct MinNode { struct MinNode *mln_Succ, *mln_Pred; };
struct List { struct Node *lh_Head, *lh_Tail, *lh_TailPred; };
struct MinList { struct MinNode *mlh_Head, *mlh_Tail, *mlh_TailPred; };
struct SignalSemaphore { struct Node ss_Link; pthread_rwlock_t lock; };
struct Library { unsigned provider; pthread_t opener; };
static struct { struct List SemaphoreList; } exec_base, *SysBase = &exec_base;
static void NewList(struct List *l) {
    l->lh_Head = (void *)&l->lh_Tail;
    l->lh_Tail = NULL;
    l->lh_TailPred = (void *)&l->lh_Head;
}
#define NEWLIST(l) NewList((struct List *)(l))
static void AddTail(struct List *l, struct Node *n) {
    n->ln_Succ = (void *)&l->lh_Tail; n->ln_Pred = l->lh_TailPred;
    l->lh_TailPred->ln_Succ = n; l->lh_TailPred = n;
}
static void Remove(struct Node *n) {
    n->ln_Pred->ln_Succ = n->ln_Succ; n->ln_Succ->ln_Pred = n->ln_Pred;
}
static pthread_mutex_t namespace_lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int namespace_depth, semaphore_depth, in_leave;
static int no_resource, fail_namespace_write;
static struct Library lock_resource;
static APTR OpenResource(CONST_STRPTR name) {
    assert(!strcmp(name, "execlock.resource"));
#ifdef TEST_SMP
    return no_resource ? NULL : &lock_resource;
#else
    (void)no_resource; (void)lock_resource;
    return NULL;
#endif
}
static BOOL ObtainSystemLock(struct List *list, ULONG mode, ULONG flags) {
    assert(list == &SysBase->SemaphoreList && flags == LOCKF_DISABLE);
    assert(!namespace_depth && !semaphore_depth);
    if (mode == SPINLOCK_MODE_WRITE && fail_namespace_write) return FALSE;
    assert(!pthread_mutex_lock(&namespace_lock)); namespace_depth = 1; return TRUE;
}
static void ReleaseSystemLock(struct List *list, ULONG flags) {
    assert(list == &SysBase->SemaphoreList && flags == LOCKF_DISABLE && namespace_depth);
    namespace_depth = 0; assert(!pthread_mutex_unlock(&namespace_lock));
}
static void Disable(void) {
    assert(!namespace_depth && !semaphore_depth);
    assert(!pthread_mutex_lock(&namespace_lock)); namespace_depth = 1;
}
static void Enable(void) {
    assert(namespace_depth); namespace_depth = 0;
    assert(!pthread_mutex_unlock(&namespace_lock));
}
static struct Node *FindName(struct List *list, CONST_STRPTR name) {
    assert(namespace_depth);
    for (struct Node *n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
        if (!strcmp(n->ln_Name, name)) return n;
    return NULL;
}
static void Enqueue(struct List *list, struct Node *node) {
    assert(namespace_depth); AddTail(list, node);
}
static void InitSemaphore(struct SignalSemaphore *s) {
    assert(!namespace_depth); assert(!pthread_rwlock_init(&s->lock, NULL));
}
static void ObtainSemaphore(struct SignalSemaphore *s) {
    assert(!namespace_depth); assert(!pthread_rwlock_wrlock(&s->lock)); semaphore_depth++;
}
static void ObtainSemaphoreShared(struct SignalSemaphore *s) {
    assert(!namespace_depth); assert(!pthread_rwlock_rdlock(&s->lock)); semaphore_depth++;
}
static void ReleaseSemaphore(struct SignalSemaphore *s) {
    assert(semaphore_depth); semaphore_depth--; assert(!pthread_rwlock_unlock(&s->lock));
}
#define CopyMem(src,dest,size) memcpy((dest),(src),(size))
static void *AllocMem(size_t size, ULONG flags);
static void FreeMem(void *p, size_t size);
static struct Library *OpenLibrary(CONST_STRPTR name, ULONG version);
static void CloseLibrary(struct Library *base);
#include <aros/crt_thread_context.h>

static atomic_int allocations;
static pthread_mutex_t publish_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t publish_changed = PTHREAD_COND_INITIALIZER;
static int publication_race, publish_arrived, fail_allocation;
static void *AllocMem(size_t size, ULONG flags) {
    assert(!namespace_depth && !semaphore_depth && flags == (MEMF_PUBLIC | MEMF_CLEAR));
    if (fail_allocation) return NULL;
    void *p = calloc(1, size); assert(p); atomic_fetch_add(&allocations, 1);
    if (publication_race) {
        assert(!pthread_mutex_lock(&publish_lock));
        publish_arrived++; assert(!pthread_cond_broadcast(&publish_changed));
        while (publish_arrived < 2) assert(!pthread_cond_wait(&publish_changed, &publish_lock));
        assert(!pthread_mutex_unlock(&publish_lock));
    }
    return p;
}
static void FreeMem(void *p, size_t size) {
    assert(!namespace_depth && !semaphore_depth && size == sizeof(struct CRTThreadContexts));
    assert(!pthread_rwlock_destroy(&((struct CRTThreadContexts *)p)->tc_Sem.lock));
    atomic_fetch_sub(&allocations, 1); free(p);
}
#define PROVIDERS 9
struct provider {
    struct CRTThreadContextHooks hooks;
    char name[24];
    int active, opens, captures, entered, left, cancelled, contexts;
    int fail_open, fail_capture, partial_capture, fail_enter, instance, expunge;
};
static struct provider providers[PROVIDERS];
struct context { struct provider *owner; struct Library *worker; };
static int reload_on_open;
static unsigned leave_order[PROVIDERS], leave_count;
static BOOL provider_active(unsigned n) {
    assert(semaphore_depth && !namespace_depth); return providers[n].active;
}
static BOOL provider_capture(unsigned n, APTR *out, int expected_instance) {
    struct provider *p = &providers[n];
    assert(!semaphore_depth && !namespace_depth && p->opens > 0);
    assert(p->instance == expected_instance);
    if (p->fail_capture && !p->partial_capture) return FALSE;
    struct context *c = calloc(1, sizeof(*c)); assert(c); c->owner = p;
    p->contexts++; p->captures++; *out = c;
    return !p->fail_capture;
}
static BOOL provider_enter(unsigned n, APTR data) {
    struct provider *p = &providers[n]; struct context *c = data;
    assert(!semaphore_depth && !namespace_depth && c->owner == p && p->opens > 0);
    if (p->fail_enter) return FALSE;
    c->worker = OpenLibrary(p->name, 0); assert(c->worker); p->entered++; return TRUE;
}
static struct Library *provider_leave(unsigned n, APTR data) {
    struct provider *p = &providers[n]; struct context *c = data;
    assert(!semaphore_depth && !namespace_depth && c->owner == p && p->opens > 0);
    in_leave = 1; struct Library *worker = c->worker;
    p->left++; p->contexts--; leave_order[leave_count++] = n; free(c); in_leave = 0;
    return worker;
}
static void provider_cancel(unsigned n, APTR data) {
    struct provider *p = &providers[n]; struct context *c = data;
    assert(!semaphore_depth && !namespace_depth && c->owner == p && p->opens > 0);
    p->cancelled++; p->contexts--; free(c);
}
#define HOOKS(n) \
    static BOOL active##n(void) { return provider_active(n); } \
    static BOOL capture##n(APTR *o) { return provider_capture(n,o,0); } \
    static BOOL enter##n(APTR c) { return provider_enter(n,c); } \
    static struct Library *leave##n(APTR c) { return provider_leave(n,c); } \
    static void cancel##n(APTR c) { provider_cancel(n,c); }
HOOKS(0) HOOKS(1) HOOKS(2) HOOKS(3) HOOKS(4) HOOKS(5) HOOKS(6) HOOKS(7) HOOKS(8)
static BOOL capture_new(APTR *o) { return provider_capture(0,o,1); }
static struct Library *OpenLibrary(CONST_STRPTR name, ULONG version) {
    assert(!semaphore_depth && !namespace_depth); (void)version;
    unsigned n;
    for (n = 0; n < PROVIDERS; n++) if (!strcmp(name, providers[n].name)) break;
    assert(n < PROVIDERS); struct provider *p = &providers[n];
    if (reload_on_open && n == 0) {
        if (reload_on_open == 1) reload_on_open = 0;
        CRTThreadContextUnregister(&p->hooks);
        p->instance ^= 1; p->hooks.tc_Capture = p->instance ? capture_new : capture0;
        assert(CRTThreadContextRegister(&p->hooks));
    }
    if (p->fail_open) return NULL;
    struct Library *l = malloc(sizeof(*l)); assert(l);
    l->provider = n; l->opener = pthread_self(); p->opens++; return l;
}
static void CloseLibrary(struct Library *l) {
    assert(!semaphore_depth && !namespace_depth && !in_leave);
    assert(pthread_equal(l->opener, pthread_self()));
    struct provider *p = &providers[l->provider]; assert(p->opens > 0); p->opens--; free(l);
    if (!p->opens && p->expunge) CRTThreadContextUnregister(&p->hooks);
}
static void reset(void) {
    for (unsigned n = 0; n < PROVIDERS; n++) {
        struct provider *p = &providers[n]; assert(!p->opens && !p->contexts);
        p->active = n < 2; p->captures = p->entered = p->left = p->cancelled = 0;
        p->fail_open = p->fail_capture = p->partial_capture = p->fail_enter = p->expunge = 0;
    }
    leave_count = 0;
}
static void *publisher(void *unused) { (void)unused; return CRTThreadContextsGet(TRUE); }
struct worker {
    struct CRTThreadContextSet *set;
    pthread_mutex_t lock;
    pthread_cond_t changed;
    int ready, proceed, admitted;
};
static void *worker_main(void *data) {
    struct worker *w = data;
    w->admitted = CRTThreadContextEnter(w->set);
    assert(!pthread_mutex_lock(&w->lock)); w->ready = 1;
    assert(!pthread_cond_broadcast(&w->changed));
    while (!w->proceed) assert(!pthread_cond_wait(&w->changed, &w->lock));
    assert(!pthread_mutex_unlock(&w->lock));
    if (w->admitted) CRTThreadContextLeave(w->set);
    return NULL;
}
static void enter_worker(struct CRTThreadContextSet *set, BOOL success) {
    struct worker w = { .set=set, .lock=PTHREAD_MUTEX_INITIALIZER, .changed=PTHREAD_COND_INITIALIZER };
    pthread_t thread; assert(!pthread_create(&thread,NULL,worker_main,&w));
    assert(!pthread_mutex_lock(&w.lock));
    while (!w.ready) assert(!pthread_cond_wait(&w.changed,&w.lock));
    assert(w.admitted == success);
    if (!success) for (ULONG i=0;i<set->cs_Count;i++)
        assert(set->cs_Entries[i].ce_State==CRT_ENTRY_NONE && !set->cs_Entries[i].ce_Context);
    CRTThreadContextReleasePins(set);
    /* Successful providers now have only the worker's own open instance. */
    for (unsigned n=0;n<PROVIDERS;n++)
        assert(providers[n].opens == (success && providers[n].active ? 1 : 0));
    w.proceed=1; assert(!pthread_cond_broadcast(&w.changed));
    assert(!pthread_mutex_unlock(&w.lock)); assert(!pthread_join(thread,NULL));
    assert(!pthread_mutex_destroy(&w.lock)); assert(!pthread_cond_destroy(&w.changed));
}
#define INIT(n) do { \
    struct provider *p=&providers[n]; snprintf(p->name,sizeof(p->name),"provider%u.library",n); \
    p->hooks=(struct CRTThreadContextHooks){ .tc_Version=CRT_THREAD_CONTEXT_VERSION, \
      .tc_LibName=p->name, .tc_Active=active##n, .tc_Capture=capture##n, \
      .tc_Enter=enter##n, .tc_Leave=leave##n, .tc_Cancel=cancel##n }; \
} while (0)
int main(void) {
    NEWLIST(&SysBase->SemaphoreList);
#ifdef TEST_SMP
    no_resource=1; assert(!CRTThreadContextsGet(TRUE)); no_resource=0;
    fail_namespace_write=1; assert(!CRTThreadContextsGet(TRUE)); fail_namespace_write=0;
    assert(!atomic_load(&allocations));
#endif
    fail_allocation=1; assert(!CRTThreadContextsGet(TRUE)); fail_allocation=0;
    publication_race=1; pthread_t a,b; void *ra,*rb;
    assert(!pthread_create(&a,NULL,publisher,NULL)); assert(!pthread_create(&b,NULL,publisher,NULL));
    assert(!pthread_join(a,&ra)); assert(!pthread_join(b,&rb)); publication_race=0;
    assert(ra && ra==rb && atomic_load(&allocations)==1);
    struct CRTThreadContexts *reg=ra;
    assert(reg->tc_Sem.ss_Link.ln_Name==reg->tc_Name);
    assert(SysBase->SemaphoreList.lh_Head->ln_Succ==(void *)&SysBase->SemaphoreList.lh_Tail);
    INIT(0); INIT(1); INIT(2); INIT(3); INIT(4); INIT(5); INIT(6); INIT(7); INIT(8);
    assert(CRTThreadContextRegister(&providers[0].hooks));
    assert(CRTThreadContextRegister(&providers[1].hooks));
    assert(!CRTThreadContextRegister(&providers[0].hooks));
    struct CRTThreadContextHooks duplicate=providers[0].hooks;
    assert(!CRTThreadContextRegister(&duplicate));
    CRTThreadContextUnregister(&duplicate); /* must not unlink somebody else's node */
    duplicate.tc_LibName=""; assert(!CRTThreadContextRegister(&duplicate));
    duplicate.tc_LibName="a-library-name-that-does-not-fit-in-the-bounded-context-name-buffer.library";
    assert(!CRTThreadContextRegister(&duplicate));
    struct CRTThreadContextSet set;
    reset(); assert(CRTThreadContextCapture(&set)); enter_worker(&set,TRUE);
    assert(leave_count==2 && leave_order[0]==1 && leave_order[1]==0);
    reset(); assert(CRTThreadContextCapture(&set)); CRTThreadContextCancel(&set);
    assert(providers[0].cancelled==1 && providers[1].cancelled==1);
    reset(); providers[1].fail_open=1; assert(!CRTThreadContextCapture(&set));
    assert(!providers[0].opens && !providers[0].captures);
    reset(); providers[1].fail_capture=1; assert(!CRTThreadContextCapture(&set));
    assert(providers[0].cancelled==1);
    reset(); providers[1].fail_capture=providers[1].partial_capture=1;
    assert(!CRTThreadContextCapture(&set));
    assert(providers[0].cancelled==1 && providers[1].cancelled==1 && !providers[1].contexts);
    reset(); providers[1].fail_enter=1; assert(CRTThreadContextCapture(&set)); enter_worker(&set,FALSE);
    assert(providers[0].left==1 && providers[1].cancelled==1);
    reset(); providers[1].active=0; assert(CRTThreadContextCapture(&set)); enter_worker(&set,TRUE);
    assert(!providers[1].captures && !providers[1].entered);
    reset(); providers[0].active=providers[1].active=0;
    assert(CRTThreadContextCapture(&set)); enter_worker(&set,TRUE);
    reset(); reload_on_open=1; assert(CRTThreadContextCapture(&set)); enter_worker(&set,TRUE);
    reset(); reload_on_open=2; assert(!CRTThreadContextCapture(&set)); reload_on_open=0;
    assert(!set.cs_Count && !providers[0].captures && !providers[1].captures);
    reset(); CONST_STRPTR name=providers[1].hooks.tc_LibName;
    providers[1].hooks.tc_LibName="a-library-name-that-does-not-fit-in-the-bounded-context-name-buffer.library";
    assert(!CRTThreadContextCapture(&set)); providers[1].hooks.tc_LibName=name;
    for (unsigned n=2;n<PROVIDERS;n++) assert(CRTThreadContextRegister(&providers[n].hooks));
    reset(); for (unsigned n=0;n<PROVIDERS;n++) providers[n].active=1;
    assert(!CRTThreadContextCapture(&set));
    for (unsigned n=2;n<PROVIDERS;n++) CRTThreadContextUnregister(&providers[n].hooks);
    reset(); providers[0].expunge=1; assert(CRTThreadContextCapture(&set)); enter_worker(&set,TRUE);
    assert(!providers[0].hooks.tc_Node.mln_Succ);
    for (unsigned n=0;n<PROVIDERS;n++) {
        assert(!providers[n].opens && !providers[n].contexts);
        CRTThreadContextUnregister(&providers[n].hooks);
    }
    assert(!reg->tc_Providers.mlh_Head->mln_Succ);
    /* Test-only teardown after all publishers and users have joined. */
    assert(!pthread_mutex_lock(&namespace_lock)); Remove(&reg->tc_Sem.ss_Link);
    assert(!pthread_mutex_unlock(&namespace_lock)); FreeMem(reg,sizeof(*reg));
    assert(!atomic_load(&allocations));
    puts("PASS: registry publication, pins, reload, admission/rollback, partial capture, overflow and owned teardown");
    return 0;
}
