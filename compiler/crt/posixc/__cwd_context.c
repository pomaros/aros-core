/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include "__cwd_context.h"
#include <dos/dosextens.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <limits.h>

struct __cwd_directory {
    ULONG references;
    BPTR lock;
    struct MsgPort *port;
};
struct __cwd_context {
    ULONG references;
    struct SignalSemaphore guard;
    struct __cwd_directory *cwd, *progdir;
    BOOL unix_paths;
};

static BOOL retain(ULONG *references)
{
    ULONG old = __atomic_load_n(references, __ATOMIC_RELAXED);
    do {
        if (!old || old == (ULONG)-1) return FALSE;
    } while (!__atomic_compare_exchange_n(references, &old, old + 1,
        FALSE, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    return TRUE;
}

struct __cwd_directory *__cwd_directory_capture(BPTR lock, LONG *error)
{
    struct __cwd_directory *directory;
    struct MsgPort *port;
    SIPTR args[7] = {0}, secondary = 0, result;
    if (error) *error = 0;
    if (!lock || !(port = ((struct FileLock *)BADDR(lock))->fl_Task)) {
        if (error) *error = ERROR_INVALID_LOCK;
        return NULL;
    }
    directory = AllocVec(sizeof(*directory), MEMF_PUBLIC | MEMF_CLEAR);
    if (!directory) {
        if (error) *error = ERROR_NO_FREE_STORE;
        return NULL;
    }
    args[0] = (SIPTR)lock;
    result = DoPktResult(port, ACTION_COPY_DIR, args, &secondary);
    if (!result) {
        FreeVec(directory);
        if (error) *error = secondary ? (LONG)secondary : ERROR_INVALID_LOCK;
        return NULL;
    }
    directory->references = 1;
    directory->lock = (BPTR)result;
    directory->port = port;
    return directory;
}

BOOL __cwd_directory_retain(struct __cwd_directory *directory)
{
    return directory && retain(&directory->references);
}

void __cwd_directory_release(struct __cwd_directory *directory)
{
    SIPTR args[7] = {0}, secondary;
    if (!directory || __atomic_sub_fetch(&directory->references, 1, __ATOMIC_ACQ_REL)) return;
    args[0] = (SIPTR)directory->lock;
    DoPktResult(directory->port, ACTION_FREE_LOCK, args, &secondary);
    FreeVec(directory);
}

BPTR __cwd_directory_lock(const struct __cwd_directory *directory)
{
    return directory ? directory->lock : BNULL;
}

struct __cwd_context *__cwd_context_create(BPTR cwd, BPTR progdir,
    BOOL unix_paths, LONG *error)
{
    struct __cwd_context *context = AllocVec(sizeof(*context), MEMF_PUBLIC | MEMF_CLEAR);
    if (!context) {
        if (error) *error = ERROR_NO_FREE_STORE;
        return NULL;
    }
    context->references = 1;
    InitSemaphore(&context->guard);
    context->unix_paths = unix_paths;
    context->cwd = __cwd_directory_capture(cwd, error);
    if (!context->cwd) goto fail;
    if (progdir) context->progdir = __cwd_directory_capture(progdir, error);
    else if (__cwd_directory_retain(context->cwd)) context->progdir = context->cwd;
    if (!context->progdir) goto fail;
    return context;
fail:
    __cwd_directory_release(context->cwd);
    FreeVec(context);
    return NULL;
}

BOOL __cwd_context_retain(struct __cwd_context *context)
{
    return context && retain(&context->references);
}

void __cwd_context_release(struct __cwd_context *context)
{
    if (!context || __atomic_sub_fetch(&context->references, 1, __ATOMIC_ACQ_REL)) return;
    __cwd_directory_release(context->cwd);
    __cwd_directory_release(context->progdir);
    FreeVec(context);
}

BOOL __cwd_context_snapshot(struct __cwd_context *context, struct __cwd_snapshot *snapshot)
{
    BOOL ok;
    snapshot->cwd = snapshot->progdir = NULL;
    if (!context) return FALSE;
    ObtainSemaphore(&context->guard);
    ok = __cwd_directory_retain(context->cwd);
    if (ok) {
        snapshot->cwd = context->cwd;
        ok = __cwd_directory_retain(context->progdir);
        if (ok) snapshot->progdir = context->progdir;
    }
    snapshot->unix_paths = context->unix_paths;
    ReleaseSemaphore(&context->guard);
    if (!ok) __cwd_snapshot_release(snapshot);
    return ok;
}

void __cwd_snapshot_release(struct __cwd_snapshot *snapshot)
{
    __cwd_directory_release(snapshot->cwd);
    __cwd_directory_release(snapshot->progdir);
    snapshot->cwd = snapshot->progdir = NULL;
}

BOOL __cwd_context_set(struct __cwd_context *context, struct __cwd_directory *directory)
{
    struct __cwd_directory *old;
    if (!context || !__cwd_directory_retain(directory)) return FALSE;
    ObtainSemaphore(&context->guard);
    old = context->cwd;
    context->cwd = directory;
    ReleaseSemaphore(&context->guard);
    __cwd_directory_release(old);
    return TRUE;
}

void __cwd_context_conversion(struct __cwd_context *context, BOOL unix_paths)
{
    ObtainSemaphore(&context->guard);
    context->unix_paths = unix_paths;
    ReleaseSemaphore(&context->guard);
}
