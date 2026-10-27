/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include <assert.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include "test_boundary.h"
static atomic_int allocations, disposed, registry_closed, fail_alloc;
struct Library { int alive; };
struct _fdesc { unsigned identity; };
void *AllocVec(size_t n, unsigned flags) {
    (void)flags;
    if (atomic_exchange(&fail_alloc, 0)) return NULL;
    void *p = calloc(1, n); if (p) atomic_fetch_add(&allocations, 1); return p;
}
void FreeVec(void *p) { if (p) { atomic_fetch_sub(&allocations, 1); free(p); } }
void InitSemaphore(struct SignalSemaphore *s) { assert(!pthread_mutex_init(&s->mutex, NULL)); }
void CloseLibrary(struct Library *library) {
    assert(library->alive); assert(atomic_load(&disposed) == 2);
    library->alive = 0; atomic_fetch_add(&registry_closed, 1); FreeVec(library);
}
#include "NAMESPACE_SOURCE"
static void dispose(struct _fdesc *d, struct Library *library) {
    assert(library && library->alive); assert(d->identity == 44 || d->identity == 55);
    atomic_fetch_add(&disposed, 1); FreeVec(d);
}
static atomic_int ready, go;
static void *worker(void *data) {
    struct __fd_namespace *space = data;
    atomic_fetch_add(&ready, 1);
    while (!atomic_load(&go)) sched_yield();
    assert(space->registry->alive);
    assert(!pthread_mutex_lock(&space->guard.mutex));
    assert(space->array[0]->identity == 44);
    assert(space->array[2]->identity == 55);
    assert(!pthread_mutex_unlock(&space->guard.mutex));
    __fd_namespace_release(space, dispose); return NULL;
}
int main(void) {
    atomic_store(&fail_alloc, 1); assert(!__fd_namespace_create());
    assert(!__fd_namespace_retain(NULL));
    struct __fd_namespace *space = __fd_namespace_create(); assert(space);
    space->registry = AllocVec(sizeof(*space->registry), 0); space->registry->alive = 1;
    space->slots = 3; space->array = AllocVec(3 * sizeof(*space->array), 0);
    for (int i = 0; i < 3; i += 2) {
        space->array[i] = AllocVec(sizeof(*space->array[i]), 0);
        space->array[i]->identity = i ? 55 : 44;
    }
    pthread_t threads[8];
    for (int i = 0; i < 8; i++) {
        assert(__fd_namespace_retain(space));
        assert(!pthread_create(&threads[i], NULL, worker, space));
    }
    while (atomic_load(&ready) != 8) sched_yield();
    /* The creator closes and its runtime storage is no longer involved. */
    __fd_namespace_release(space, dispose);
    assert(atomic_load(&disposed) == 0); assert(atomic_load(&registry_closed) == 0);
    atomic_store(&go, 1);
    for (int i = 0; i < 8; i++) assert(!pthread_join(threads[i], NULL));
    assert(atomic_load(&disposed) == 2); assert(atomic_load(&registry_closed) == 1);
    assert(atomic_load(&allocations) == 0);
    puts("PASS retained FD namespace after creator close, concurrent last release, owned registry order, OOM");
    return 0;
}
