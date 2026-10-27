/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#ifndef POSIXC_CWD_RUNTIME_H
#define POSIXC_CWD_RUNTIME_H
#include "__cwd_context.h"
#include <dos/context.h>
struct PosixCIntBase;
struct __fd_namespace;
struct Library;
struct __cwd_admission;

struct PosixCIntBase *__cwd_runtime_base(void);
struct __cwd_context *__cwd_runtime_context(void); /* borrowed by current Task */
struct __cwd_context *__cwd_runtime_context_capture(void); /* retained */
/* Synchronous same-Task proxy scope. The caller keeps its runtime open and
 * closes any nested runtime before pop. Scopes are popped in reverse order.
 * A successful push owns next and the
 * previous context independently of the child/creator library bases. */
struct __cwd_context_scope {
    APTR binding, task;
    struct __cwd_context *previous;
    BPTR override;
    struct __cwd_context_scope *outer;
};
BOOL __cwd_runtime_context_push(struct __cwd_context *next,
    struct __cwd_context_scope *scope);
BOOL __cwd_runtime_context_pop(struct __cwd_context_scope *scope);
BOOL __cwd_runtime_snapshot(struct __cwd_snapshot *snapshot);
BPTR __cwd_runtime_override(BPTR directory);
BOOL __cwd_runtime_scope_enter(void);
void __cwd_runtime_scope_leave(void);
struct __cwd_scope_guard { BOOL entered; };
static inline void __cwd_scope_end(struct __cwd_scope_guard *guard)
{
    if (guard->entered) __cwd_runtime_scope_leave();
}
/* Function-local cleanup covers every early return. Callers include errno.h. */
#define POSIXC_CWD_SCOPE_OR_RETURN(result) \
    struct __cwd_scope_guard cwd_scope __attribute__((cleanup(__cwd_scope_end))) = \
        { __cwd_runtime_scope_enter() }; \
    if (!cwd_scope.entered) { errno = ENOMEM; return (result); }
BOOL __cwd_runtime_set(struct __cwd_directory *directory);
BOOL __cwd_runtime_conversion(void);
void __cwd_runtime_conversion_set(BOOL mode);
/* Captured before task creation. Context and descriptors are retained, with
 * no borrowed creator Process/base. Caller pins callback module until Enter
 * has opened the worker's own base, or Cancel has completed. */
APTR __cwd_runtime_source(void); /* synchronous creator selection only */
struct __cwd_admission *__cwd_admission_capture(APTR source);
BOOL __cwd_admission_enter(struct __cwd_admission *admission);
struct Library *__cwd_admission_leave(struct __cwd_admission *admission);
void __cwd_admission_cancel(struct __cwd_admission *admission);
struct __fd_namespace *__cwd_admission_namespace(BOOL *pending); /* retained result */

#define POSIXC_CONTEXT_NAME_FROM_LOCK 100
#define POSIXC_CONTEXT_SYSTEM 101
#define POSIXC_CONTEXT_LOAD_SEG 102
SIPTR __cwd_runtime_native(LONG operation, const SIPTR *args, SIPTR *error);
struct __cwd_native_call { LONG operation; const SIPTR *args; };
/* DOS-only callback: no per-Task stdc/stdcio/posixc calls in the helper. */
SIPTR __cwd_native_call(APTR data, struct DosLibrary *DOSBase);
#endif
