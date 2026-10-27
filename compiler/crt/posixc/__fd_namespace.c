/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include "__fd_namespace.h"
#include <exec/memory.h>
#include <proto/exec.h>

struct __fd_namespace *__fd_namespace_create(void)
{
    struct __fd_namespace *space = AllocVec(sizeof(*space), MEMF_PUBLIC | MEMF_CLEAR);
    if (space) {
        space->references = 1;
        InitSemaphore(&space->guard);
    }
    return space;
}

BOOL __fd_namespace_retain(struct __fd_namespace *space)
{
    ULONG old;
    if (!space) return FALSE;
    old = __atomic_load_n(&space->references, __ATOMIC_RELAXED);
    do {
        if (!old || old == (ULONG)-1) return FALSE;
    } while (!__atomic_compare_exchange_n(&space->references, &old, old + 1,
        FALSE, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    return TRUE;
}

void __fd_namespace_release(struct __fd_namespace *space,
    void (*dispose)(struct _fdesc *, struct Library *))
{
    int i;
    if (!space || __atomic_sub_fetch(&space->references, 1, __ATOMIC_ACQ_REL)) return;
    /* No instance can enter this table now. Handle close and registry waits
     * must run without the table guard. */
    for (i = 0; i < space->slots; i++)
        if (space->array[i]) dispose(space->array[i], space->registry);
    FreeVec(space->array);
    if (space->registry) CloseLibrary(space->registry);
    FreeVec(space);
}
