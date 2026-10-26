/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1.
 */
#ifndef STDC_EXEC_LIST_H
#define STDC_EXEC_LIST_H

#include <aros/config.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <resources/execlock.h>
#include <proto/execlock.h>
#include <aros/types/spinlock_s.h>

/* Only bounded, nonblocking list operations are allowed while this guard is
 * held. SMP Exec publishes execlock.resource before tasks can run. UP Exec
 * has no such resource: masking interrupts excludes its scheduler instead.
 * Never call a list operation that takes the same lock recursively. */
struct __crt_list_guard
{
    struct Library *base;
    struct List *list;
};

static inline BOOL __crt_list_enter(struct __crt_list_guard *guard,
                                   struct List *list, ULONG mode)
{
    struct Library *ExecLockBase = (struct Library *)
        OpenResource((CONST_STRPTR)"execlock.resource");
    guard->base = ExecLockBase;
    guard->list = list;
    if (ExecLockBase)
        return ObtainSystemLock(list, mode, LOCKF_DISABLE);
#if defined(__AROSEXEC_SMP__)
    return FALSE;
#else
    Disable();
    return TRUE;
#endif
}

static inline void __crt_list_leave(struct __crt_list_guard *guard)
{
    struct Library *ExecLockBase = guard->base;
    if (ExecLockBase)
        ReleaseSystemLock(guard->list, LOCKF_DISABLE);
    else
        Enable();
}

#endif
