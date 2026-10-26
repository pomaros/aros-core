/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1.

    Desc: Publish a native semaphore exactly once across runtime copies.
*/

#include "exec_intern.h"
#include <exec/semaphores.h>
#include <proto/exec.h>

/*****************************************************************************

    NAME */
        AROS_LH3(BOOL, InitSemaphoreOnce,

/*  SYNOPSIS */
        AROS_LHA(struct SignalSemaphore *, sigSem, A0),
        AROS_LHA(ULONG *, state, A1),
        AROS_LHA(ULONG, flags, D0),

/*  LOCATION */
        struct ExecBase *, SysBase, 186, Exec)

/*  FUNCTION
        Initialize and publish a SignalSemaphore exactly once. Competing tasks
        and independently linked runtimes use the same Exec-owned gate. A
        published semaphore takes only an acquire load of state.

    INPUTS
        sigSem - Shared writable semaphore storage.
        state - Separate, naturally aligned 32-bit ULONG initialized to zero.
                Every caller for sigSem must use the same state storage.
                This word must not overlap the semaphore. Value one is
                reserved for completed initialization; no other values exist.
        flags - Zero to wait for the gate, SSINITF_TRY to return immediately
                when the gate is busy. Other flag bits are invalid.

    RESULT
        TRUE after initialization is published, including when an earlier call
        initialized it. FALSE for invalid arguments, state other than zero/one,
        or a busy gate with SSINITF_TRY. FALSE grants no semaphore ownership.
        The call does not acquire sigSem.

    NOTES
        Call in task context where semaphore acquisition is permitted. The
        nonwaiting mode never enqueues a wait. Exec initializes its private
        gate before publishing the ExecBase to ordinary runtime consumers.
        Initialization allocates no memory and invokes no user callback.
        Retain both objects while any caller or semaphore user may access
        them. Destroy/reset only after external quiescence. An explicit
        initializer may publish state one with release ordering after exclusive
        InitSemaphore setup; concurrent explicit initialization is invalid.
        This API does not serialize later use or destruction of sigSem.

    SEE ALSO
        InitSemaphore(), ObtainSemaphore(), AttemptSemaphore()

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct SignalSemaphore *gate;
    ULONG published;

    if (sigSem == NULL || state == NULL ||
        ((IPTR)state & (sizeof(ULONG) - 1)) || (flags & ~SSINITF_TRY))
        return FALSE;
    published = __atomic_load_n(state, __ATOMIC_ACQUIRE);
    if (published == 1)
        return TRUE;
    if (published != 0)
        return FALSE;

    gate = &PrivExecBase(SysBase)->SemaphoreInitSem;
    if (flags & SSINITF_TRY)
    {
        if (!AttemptSemaphore(gate))
            return FALSE;
    }
    else
        ObtainSemaphore(gate);

    published = __atomic_load_n(state, __ATOMIC_ACQUIRE);
    if (published == 0)
    {
        InitSemaphore(sigSem);
        __atomic_store_n(state, 1, __ATOMIC_RELEASE);
    }
    ReleaseSemaphore(gate);
    return published == 0 || published == 1;

    AROS_LIBFUNC_EXIT
}
