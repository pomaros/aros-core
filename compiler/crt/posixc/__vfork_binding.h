/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#ifndef POSIXC_VFORK_BINDING_H
#define POSIXC_VFORK_BINDING_H
#include "__posixc_intbase.h"
#include "__fd_namespace.h"
#include "__fdesc.h"
#include "__cwd_runtime.h"

/* Only this Task's proxy base changes. Worker bases retain their original
 * namespace; neither namespace nor context refers to the real child base.
 * The proxy runtime must remain open and scopes must leave in LIFO order. */
struct __vfork_binding {
    struct __fd_namespace *previous, *current;
    struct PosixCIntBase *owner;
    struct __cwd_context_scope context;
};
static inline BOOL __vfork_binding_enter(struct PosixCIntBase *base,
    struct __fd_namespace *child, struct __cwd_context *context,
    struct __vfork_binding *scope)
{
    if (!base || __cwd_runtime_base() != base || !base->fd_namespace ||
        !scope || scope->current ||
        !__fd_namespace_retain(child)) return FALSE;
    if (!__cwd_runtime_context_push(context, &scope->context)) {
        __fd_namespace_drop(child);
        return FALSE;
    }
    scope->previous = base->fd_namespace; /* transfer base's reference */
    scope->owner = base->fd_owner;
    scope->current = child;
    base->fd_namespace = child;
    base->fd_owner = NULL;
    return TRUE;
}
static inline BOOL __vfork_binding_leave(struct PosixCIntBase *base,
    struct __vfork_binding *scope)
{
    struct __fd_namespace *child;
    if (!base || __cwd_runtime_base() != base || !scope || !scope->current ||
        base->fd_namespace != scope->current ||
        !__cwd_runtime_context_pop(&scope->context)) return FALSE;
    child = scope->current;
    base->fd_namespace = scope->previous;
    base->fd_owner = scope->owner;
    scope->current = scope->previous = NULL;
    __fd_namespace_drop(child);
    return TRUE;
}
#endif
