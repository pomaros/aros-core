/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#ifndef POSIXC_FD_NAMESPACE_H
#define POSIXC_FD_NAMESPACE_H
#include <exec/types.h>
#include <exec/semaphores.h>

struct _fdesc;
struct Library;
/* No creator Task, Process, library base, or private allocation pool is
 * borrowed. Each attached runtime owns a reference. A pre-creation admission
 * must retain its reference before publishing the context to a new Task. */
struct __fd_namespace {
    ULONG references;
    ULONG process_id; /* immutable identity, never a borrowed creator Task */
    struct SignalSemaphore guard;
    int slots;
    struct _fdesc **array;
    struct Library *registry;
};
struct __fd_namespace *__fd_namespace_create(ULONG process_id);
BOOL __fd_namespace_retain(struct __fd_namespace *space);
/* Called after the last admission/instance reference is gone. The callback
 * receives the owned registry and array index, never a creator base. Owner
 * finalization runs once before descriptor disposal, outside the table guard. */
void __fd_namespace_release(struct __fd_namespace *space,
    void (*dispose)(struct _fdesc *, struct Library *, int),
    void (*finalize_owner)(struct __fd_namespace *));
#endif
