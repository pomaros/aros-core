/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#ifndef POSIXC_FD_NAMESPACE_H
#define POSIXC_FD_NAMESPACE_H
#include <exec/types.h>
#include <exec/semaphores.h>

struct _fdesc;
struct Library;
/* No creator Task, Process, library base, or private allocation pool is
 * borrowed. Each attached runtime owns a reference. Admission retains one
 * before the new Task is started. */
struct __fd_namespace {
    ULONG references;
    struct SignalSemaphore guard;
    int slots;
    struct _fdesc **array;
    struct Library *registry;
};
struct __fd_namespace *__fd_namespace_create(void);
BOOL __fd_namespace_retain(struct __fd_namespace *space);
/* Called after the last admission/instance reference is gone. The callback
 * receives the owned registry explicitly, never a former creator's base. */
void __fd_namespace_release(struct __fd_namespace *space,
    void (*dispose)(struct _fdesc *, struct Library *));
#endif
