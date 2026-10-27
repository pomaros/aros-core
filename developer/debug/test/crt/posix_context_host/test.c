/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>
#include "test_boundary.h"

static atomic_int vectors, locks, budget, copy_budget;
static struct MsgPort filesystem;
static struct FileLock initial = { &filesystem, 101 };
static struct FileLock changed = { &filesystem, 202 };
static struct FileLock program = { &filesystem, 303 };

void *AllocVec(size_t size, unsigned flags)
{
    int left = atomic_load(&budget);
    void *p;
    (void)flags;
    if (left > 0 && atomic_fetch_sub(&budget, 1) == 1) return NULL;
    p = calloc(1, size);
    if (p) atomic_fetch_add(&vectors, 1);
    return p;
}
void FreeVec(void *p) { if (p) { atomic_fetch_sub(&vectors, 1); free(p); } }
void InitSemaphore(struct SignalSemaphore *s) { atomic_flag_clear(&s->held); }
void ObtainSemaphore(struct SignalSemaphore *s)
{
    while (atomic_flag_test_and_set_explicit(&s->held, memory_order_acquire)) sched_yield();
}
void ReleaseSemaphore(struct SignalSemaphore *s) { atomic_flag_clear_explicit(&s->held, memory_order_release); }
SIPTR DoPktResult(struct MsgPort *port, LONG action, const SIPTR *args, SIPTR *error)
{
    struct FileLock *file = (struct FileLock *)args[0], *copy;
    assert(port == &filesystem);
    *error = 0;
    if (action == ACTION_COPY_DIR) {
        int left = atomic_load(&copy_budget);
        if (left > 0 && atomic_fetch_sub(&copy_budget, 1) == 1) {
            *error = 777; return 0;
        }
        copy = malloc(sizeof(*copy)); assert(copy); *copy = *file;
        atomic_fetch_add(&locks, 1); return (SIPTR)copy;
    }
    assert(action == ACTION_FREE_LOCK);
    atomic_fetch_sub(&locks, 1); free(file); return 1;
}

#include "CONTEXT_SOURCE"

static unsigned identity(struct __cwd_directory *directory)
{
    struct FileLock *lock = (struct FileLock *)__cwd_directory_lock(directory);
    assert(lock && lock->fl_Task == &filesystem); return lock->identity;
}
static void clean(void) { assert(!atomic_load(&vectors) && !atomic_load(&locks)); }
static void boundaries(void)
{
    struct __cwd_context *context;
    struct __cwd_directory *next;
    struct __cwd_snapshot old, now;
    LONG error;
    context = __cwd_context_create(&initial, &program, 1, &error);
    assert(context && !error && __cwd_context_snapshot(context, &old));
    assert(identity(old.cwd) == 101 && identity(old.progdir) == 303 && old.unix_paths);
    next = __cwd_directory_capture(&changed, &error); assert(next && !error);
    assert(__cwd_context_set(context, next)); __cwd_directory_release(next);
    __cwd_context_conversion(context, 0);
    assert(__cwd_context_snapshot(context, &now));
    assert(identity(now.cwd) == 202 && identity(now.progdir) == 303 && !now.unix_paths);
    assert(identity(old.cwd) == 101); /* Admitted operation still uses old object. */
    __cwd_context_release(context); /* Creator runtime goes away before snapshots. */
    assert(identity(now.cwd) == 202 && identity(old.cwd) == 101);
    __cwd_snapshot_release(&old); __cwd_snapshot_release(&now); clean();
    context = __cwd_context_create(&initial, 0, 1, &error);
    assert(context && __cwd_context_snapshot(context, &now));
    assert(identity(now.progdir) == 101); __cwd_snapshot_release(&now);
    __cwd_context_release(context); clean();
    for (int fail = 1; fail <= 3; ++fail) {
        atomic_store(&budget, fail);
        assert(!__cwd_context_create(&initial, &program, 1, &error));
        assert(error == ERROR_NO_FREE_STORE); clean(); atomic_store(&budget, 0);
    }
    for (int fail = 1; fail <= 2; ++fail) {
        atomic_store(&copy_budget, fail);
        assert(!__cwd_context_create(&initial, &program, 1, &error));
        assert(error == 777); clean(); atomic_store(&copy_budget, 0);
    }
    assert(!__cwd_context_create(0, &program, 1, &error) && error == ERROR_INVALID_LOCK); clean();
    assert(!__cwd_context_retain(NULL) && !__cwd_directory_retain(NULL));
    assert(!__cwd_context_snapshot(NULL, &now) && !now.cwd && !now.progdir);
}

static void *writer(void *argument)
{
    struct __cwd_context *context = argument;
    for (int i = 0; i != 10000; ++i) {
        LONG error;
        struct __cwd_directory *directory = __cwd_directory_capture(i & 1 ? &initial : &changed, &error);
        assert(directory && !error && __cwd_context_set(context, directory));
        __cwd_directory_release(directory);
    }
    __cwd_context_release(context); return NULL;
}
static void *reader(void *argument)
{
    struct __cwd_context *context = argument;
    for (int i = 0; i != 10000; ++i) {
        struct __cwd_snapshot snapshot;
        unsigned id;
        assert(__cwd_context_snapshot(context, &snapshot));
        id = identity(snapshot.cwd); assert(id == 101 || id == 202);
        assert(identity(snapshot.progdir) == 303);
        __cwd_snapshot_release(&snapshot);
    }
    __cwd_context_release(context); return NULL;
}
int main(void)
{
    struct __cwd_context *context;
    pthread_t first, second;
    LONG error;
    boundaries();
    context = __cwd_context_create(&initial, &program, 1, &error); assert(context);
    assert(__cwd_context_retain(context) && __cwd_context_retain(context));
    assert(!pthread_create(&first, NULL, writer, context));
    assert(!pthread_create(&second, NULL, reader, context));
    __cwd_context_release(context); /* No creator Process/base reference is used by either worker. */
    assert(!pthread_join(first, NULL) && !pthread_join(second, NULL)); clean();
    puts("PASS actual cwd context: shared changes, snapshots, creator release, 10000 swaps/reads, OOM/packet rollback");
    return 0;
}
