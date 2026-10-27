/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include "dos_intern.h"
#include "taskpathresolver.h"
#include <proto/task.h>
#include <proto/dos.h>

/* DOS is resident and non-expungeable. The slot number therefore remains
 * valid for the system lifetime. The value in each Task is caller-owned. */
static LONG path_slot;

struct DosPathResolver *__dos_task_path_resolver(void)
{
    LONG slot = __atomic_load_n(&path_slot, __ATOMIC_ACQUIRE);
    APTR TaskResBase;
    if (slot <= 0 || !(TaskResBase = OpenResource("task.resource"))) return NULL;
    return (struct DosPathResolver *)GetTaskStorageSlot(slot);
}

/* Install one resolver instance per Task, or remove it with NULL. The caller
 * owns its callback, data and resolver until it unregisters them, and must
 * own the callback module throughout registration and any admitted call.
 * Registration changes no Process fields. The resolver must not recursively
 * invoke path resolution in this Task; a DOS Process gate can resolve there.
 * Failure leaves the current registration unchanged. */
AROS_LH1(BOOL, SetTaskPathResolver,
    AROS_LHA(struct DosPathResolver *, resolver, A0),
    struct DosLibrary *, DOSBase, 230, Dos)
{
    AROS_LIBFUNC_INIT
    APTR TaskResBase = OpenResource("task.resource");
    LONG slot = __atomic_load_n(&path_slot, __ATOMIC_ACQUIRE);
    if (!TaskResBase || (resolver &&
        (resolver->version != DOS_PATH_RESOLVER_VERSION || !resolver->accept || !resolver->invoke))) return FALSE;
    struct DosPathResolver *old = __dos_task_path_resolver();
    if (old && old->active) return FALSE;
    if (slot <= 0) {
        LONG candidate;
        if (!resolver) return TRUE;
        candidate = AllocTaskStorageSlot();
        if (candidate <= 0) return FALSE;
        if (__atomic_compare_exchange_n(&path_slot, &slot, candidate,
            FALSE, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) slot = candidate;
        else FreeTaskStorageSlot(candidate);
    }
    return SetTaskStorageSlot(slot, (IPTR)resolver);
    AROS_LIBFUNC_EXIT
}

SIPTR __dos_path_operation(LONG operation, const SIPTR *args, BOOL *handled,
    struct DosLibrary *DOSBase)
{
    struct DosPathResolver *resolver = __dos_task_path_resolver();
    SIPTR result, error = 0;
    *handled = resolver != NULL;
    if (!resolver) return 0;
    if (resolver->active) { SetIoErr(ERROR_TOO_MANY_LEVELS); return 0; }
    resolver->active = TRUE;
    if (!resolver->accept(resolver->data)) {
        resolver->active = FALSE;
        if (FindTask(NULL)->tc_Node.ln_Type == NT_PROCESS) {
            *handled = FALSE;
            return 0;
        }
        SetIoErr(ERROR_TOO_MANY_LEVELS);
        return 0;
    }
    result = resolver->invoke(resolver->data, operation, args, &error, DOSBase);
    resolver->active = FALSE;
    SetIoErr(error);
    return result;
}
