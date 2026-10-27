/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
/* Scope regression recovered from the retained vfork proposal. Exec task
 * storage, DOS locks and library disposal are modeled; production engines,
 * runtime push/pop and vfork scope code are inserted by scope.py. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>
typedef void *APTR;
typedef void *BPTR;
typedef intptr_t SIPTR;
typedef uintptr_t IPTR;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define BNULL NULL
#define BADDR(p) (p)
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define ERROR_INVALID_LOCK 211
#define ERROR_NO_FREE_STORE 103
#define ACTION_COPY_DIR 19
#define ACTION_FREE_LOCK 15
struct MsgPort { int unused; };
struct FileLock { struct MsgPort *fl_Task; unsigned identity; };
struct SignalSemaphore { pthread_mutex_t mutex; };
struct Library { int unused; };
struct DosLibrary;
struct DosPathResolver { int unused; }; /* opaque, unused by scoped paths */
struct _fdesc { unsigned identity; };
struct PosixCIntBase {
    struct __fd_namespace *fd_namespace;
    struct PosixCIntBase *fd_owner;
    BOOL doupath;
};
struct Task { unsigned identity; };
static _Thread_local struct Task *self;
static _Thread_local struct __cwd_binding *current;
static LONG runtime_slot = 1;
static atomic_int allocations, locks, ready, finish, iterations;
static struct MsgPort filesystem;
static struct FileLock native_cwd = { &filesystem, 2 };
static struct FileLock native_progdir = { &filesystem, 3 };

static void *AllocVec(size_t n, unsigned flags)
{
    (void)flags;
    void *p = calloc(1, n);
    assert(p);
    atomic_fetch_add(&allocations, 1);
    return p;
}
static void FreeVec(void *p)
{
    if (p) { atomic_fetch_sub(&allocations, 1); free(p); }
}
static void InitSemaphore(struct SignalSemaphore *s)
{ assert(!pthread_mutex_init(&s->mutex, NULL)); }
static void ObtainSemaphore(struct SignalSemaphore *s)
{ assert(!pthread_mutex_lock(&s->mutex)); }
static void ReleaseSemaphore(struct SignalSemaphore *s)
{ assert(!pthread_mutex_unlock(&s->mutex)); }
static void CloseLibrary(struct Library *p) { assert(!p); }
static struct Task *FindTask(APTR name) { (void)name; return self; }
static IPTR GetTaskStorageSlot(LONG slot)
{ assert(slot == 1); return (IPTR)current; }
static APTR __aros_getbase_PosixCBase(void) { return NULL; }
static SIPTR DoPktResult(struct MsgPort *port, LONG action,
                        const SIPTR *args, SIPTR *error)
{
    struct FileLock *lock = (APTR)args[0];
    assert(port == &filesystem);
    *error = 0;
    if (action == ACTION_COPY_DIR) {
        struct FileLock *copy = malloc(sizeof(*copy));
        assert(copy); *copy = *lock;
        atomic_fetch_add(&locks, 1);
        return (SIPTR)copy;
    }
    assert(action == ACTION_FREE_LOCK);
    atomic_fetch_sub(&locks, 1); free(lock); return 1;
}

/* CONTEXT_HEADER */
/* NAMESPACE_HEADER */
/* RUNTIME_HEADER */
/* CONTEXT_ENGINE */
/* NAMESPACE_ENGINE */
/* BINDING_TYPE */
/* RUNTIME_FUNCTIONS */
static void dispose_descriptor(struct _fdesc *d, struct Library *registry, int fd)
{ (void)fd; assert(!registry); FreeVec(d); }
static void finalize_namespace(struct __fd_namespace *s) { (void)s; }
static void __fd_namespace_drop(struct __fd_namespace *s)
{ __fd_namespace_release(s, dispose_descriptor, finalize_namespace); }
/* VFORK_BINDING */

static struct __fd_namespace *namespace_new(int slots)
{
    struct __fd_namespace *s = __fd_namespace_create(77);
    s->slots = slots;
    s->array = AllocVec(slots * sizeof(*s->array), 0);
    for (int i = 0; i < slots; i++) {
        s->array[i] = AllocVec(sizeof(*s->array[i]), 0);
        s->array[i]->identity = i + 10;
    }
    return s;
}
static struct __cwd_binding *attach(struct __fd_namespace *space,
                                  struct __cwd_context *context)
{
    struct __cwd_binding *b = AllocVec(sizeof(*b), 0);
    b->references = 1;
    b->context = context; /* transfer captured reference */
    b->base = AllocVec(sizeof(*b->base), 0);
    b->base->fd_namespace = space; /* transfer captured reference */
    current = b;
    return b;
}
static void detach(void)
{
    struct __cwd_binding *b = current;
    assert(b && b->references == 1 && !b->context_scope && !b->scope_depth);
    __fd_namespace_drop(b->base->fd_namespace);
    FreeVec(b->base); b->base = NULL;
    binding_release(b); current = NULL;
}
struct worker_data {
    struct __fd_namespace *space;
    struct __cwd_context *context;
};
static void *worker(void *arg)
{
    struct worker_data *w = arg;
    struct Task task = {2}; self = &task;
    struct __cwd_binding *b = attach(w->space, w->context);
    atomic_store(&ready, 1);
    while (!atomic_load(&finish)) {
        struct __cwd_snapshot s;
        assert(__cwd_runtime_context() == w->context);
        assert(__cwd_runtime_snapshot(&s) && !s.unix_paths);
        assert(((struct FileLock *)__cwd_directory_lock(s.cwd))->identity == 2);
        __cwd_snapshot_release(&s);
        ObtainSemaphore(&b->base->fd_namespace->guard);
        assert(b->base->fd_namespace == w->space && w->space->slots == 1);
        assert(w->space->array[0]->identity == 10);
        ReleaseSemaphore(&b->base->fd_namespace->guard);
        atomic_fetch_add(&iterations, 1);
    }
    detach(); return NULL;
}
int main(void)
{
    struct Task parent = {1}, foreign = {3}; self = &parent;
    LONG error;
    struct __cwd_context *context = __cwd_context_create(&native_cwd, &native_progdir, FALSE, &error);
    assert(context && !error);
    struct __fd_namespace *original = namespace_new(1);
    struct __cwd_binding *b = attach(original, context);
    struct PosixCIntBase *base = b->base;
    assert(__cwd_context_retain(context) && __fd_namespace_retain(original));
    struct worker_data w = {original, context};
    pthread_t thread; assert(!pthread_create(&thread, NULL, worker, &w));
    while (!atomic_load(&ready)) sched_yield();
    base->fd_owner = base; /* restore the original owner value exactly */
    b->override = &native_progdir;
    for (int i = 0; i < 2000; i++) {
        struct __fd_namespace *child = namespace_new(2);
        struct __cwd_context *next = __cwd_context_create(&native_progdir, &native_cwd, TRUE, &error);
        struct __vfork_binding scope = {0}, nested = {0};
        assert(next && !error);
        child->references = UINT32_MAX;
        assert(!__vfork_binding_enter(base, child, next, &scope));
        child->references = 1;
        struct PosixCIntBase other_base = *base;
        assert(!__vfork_binding_enter(&other_base, child, next, &scope));
        assert(__cwd_runtime_scope_enter());
        assert(!__vfork_binding_enter(base, child, next, &scope));
        __cwd_runtime_scope_leave();
        assert(base->fd_namespace == original && child->references == 1);
        assert(__vfork_binding_enter(base, child, next, &scope));
        assert(base->fd_namespace == child && !base->fd_owner);
        assert(__cwd_runtime_context() == next && __cwd_runtime_conversion());
        assert(!b->override);
        other_base = *base;
        assert(!__vfork_binding_leave(&other_base, &scope));
        assert(__vfork_binding_enter(base, child, next, &nested));
        assert(!__vfork_binding_leave(base, &scope));
        assert(__vfork_binding_leave(base, &nested));
        __fd_namespace_drop(child); __cwd_context_release(next);
        self = &foreign;
        assert(!__vfork_binding_leave(base, &scope)); self = &parent;
        assert(__vfork_binding_leave(base, &scope));
        assert(!__vfork_binding_leave(base, &scope));
        assert(base->fd_namespace == original && base->fd_owner == base);
        assert(__cwd_runtime_context() == context && !__cwd_runtime_conversion());
        assert(b->override == &native_progdir);
    }
    while (!atomic_load(&iterations)) sched_yield();
    detach(); /* creator base/context ownership ends while worker is alive */
    int before = atomic_load(&iterations);
    while (atomic_load(&iterations) == before) sched_yield();
    atomic_store(&finish, 1); assert(!pthread_join(thread, NULL));
    assert(!atomic_load(&allocations) && !atomic_load(&locks));
    puts("PASS: 2000 proxy scopes, nested/refusal paths, concurrent worker, creator release");
    return 0;
}
