/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include LC_LIBDEFS_FILE
#include "__cwd_runtime.h"
#include "__fd_namespace.h"
#include "__fdesc.h"
#include <exec/memory.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/task.h>
#include <aros/symbolsets.h>
#include <limits.h>

extern APTR TaskResBase;
/* Generated per-task-base metadata; no duplicate layout in the runtime. */
extern ULONG __GM_GetBaseOpenCount(struct PosixCIntBase *base);
static LONG runtime_slot, admission_slot;
struct __cwd_binding {
    ULONG references;
    struct PosixCIntBase *base; /* same-Task only; cleared before base free */
    ULONG owner_id; /* immutable ETask incarnation, no borrowed Task pointer */
    struct __cwd_context *context;
    struct DosPathResolver resolver;
    struct __cwd_binding *previous; /* retained, may have closed its base */
    BPTR override;
    ULONG scope_depth;
    struct __cwd_snapshot scope;
    struct __cwd_context_scope *context_scope;
};
struct __cwd_admission {
    struct __cwd_context *context;
    struct __fd_namespace *descriptors;
    struct Library *worker;
};

static struct __cwd_binding *binding(void)
{
    struct __cwd_binding *b = runtime_slot > 0 ?
        (APTR)GetTaskStorageSlot(runtime_slot) : NULL;
    while (b && !b->base) b = b->previous;
    return b;
}
static void binding_release(struct __cwd_binding *b)
{
    while (b && !__atomic_sub_fetch(&b->references, 1, __ATOMIC_ACQ_REL)) {
        struct __cwd_binding *previous = b->previous;
        if (b->scope_depth) __cwd_snapshot_release(&b->scope);
        __cwd_context_release(b->context);
        FreeVec(b);
        b = previous;
    }
}
struct PosixCIntBase *__cwd_runtime_base(void)
{
    struct __cwd_binding *b = binding();
    return b ? b->base : (struct PosixCIntBase *)__aros_getbase_PosixCBase();
}
struct __cwd_context *__cwd_runtime_context(void)
{
    struct __cwd_binding *b = binding();
    return b ? b->context : NULL;
}
struct __cwd_context *__cwd_runtime_context_capture(void)
{
    struct __cwd_context *context = __cwd_runtime_context();
    return __cwd_context_retain(context) ? context : NULL;
}
BOOL __cwd_runtime_context_push(struct __cwd_context *next,
    struct __cwd_context_scope *scope)
{
    struct __cwd_binding *b = binding();
    if (!scope || !b || b->scope_depth ||
        b->references == (ULONG)-1 || !__cwd_context_retain(next)) return FALSE;
    __atomic_add_fetch(&b->references, 1, __ATOMIC_RELAXED);
    scope->binding = b;
    scope->task = FindTask(NULL);
    scope->previous = b->context; /* transfer the old binding reference */
    scope->override = b->override;
    scope->outer = b->context_scope;
    b->context_scope = scope;
    b->context = next;
    b->override = BNULL;
    b->base->doupath = __cwd_runtime_conversion();
    return TRUE;
}
BOOL __cwd_runtime_context_pop(struct __cwd_context_scope *scope)
{
    struct __cwd_binding *b;
    struct __cwd_context *next;
    if (!scope || !(b = scope->binding) || scope->task != FindTask(NULL) ||
        binding() != b || b->scope_depth || b->context_scope != scope) return FALSE;
    next = b->context;
    b->context = scope->previous;
    b->override = scope->override;
    b->context_scope = scope->outer;
    b->base->doupath = __cwd_runtime_conversion();
    scope->binding = scope->task = NULL;
    scope->previous = NULL;
    __cwd_context_release(next);
    binding_release(b);
    return TRUE;
}
BOOL __cwd_runtime_snapshot(struct __cwd_snapshot *snapshot)
{
    struct __cwd_binding *b = binding();
    if (!b) return FALSE;
    if (!b->scope_depth) return __cwd_context_snapshot(b->context, snapshot);
    *snapshot = b->scope;
    if (!__cwd_directory_retain(snapshot->cwd)) return FALSE;
    if (!__cwd_directory_retain(snapshot->progdir)) {
        __cwd_directory_release(snapshot->cwd);
        return FALSE;
    }
    return TRUE;
}
BPTR __cwd_runtime_override(BPTR directory)
{
    struct __cwd_binding *b = binding();
    BPTR old = b ? b->override : BNULL;
    if (b) b->override = directory;
    return old;
}
BOOL __cwd_runtime_scope_enter(void)
{
    struct __cwd_binding *b = binding();
    if (!b || b->scope_depth == (ULONG)-1) return FALSE;
    if (!b->scope_depth && !__cwd_context_snapshot(b->context, &b->scope)) return FALSE;
    b->scope_depth++;
    return TRUE;
}
void __cwd_runtime_scope_leave(void)
{
    struct __cwd_binding *b = binding();
    if (b && b->scope_depth && !--b->scope_depth) __cwd_snapshot_release(&b->scope);
}
BOOL __cwd_runtime_set(struct __cwd_directory *directory)
{
    struct __cwd_binding *b = binding();
    return b && __cwd_context_set(b->context, directory);
}
BOOL __cwd_runtime_conversion(void)
{
    struct __cwd_snapshot s;
    BOOL mode;
    if (!__cwd_runtime_snapshot(&s)) return FALSE;
    mode = s.unix_paths;
    __cwd_snapshot_release(&s);
    return mode;
}
void __cwd_runtime_conversion_set(BOOL mode)
{
    struct __cwd_binding *b = binding();
    if (b) {
        __cwd_context_conversion(b->context, mode);
        b->base->doupath = mode;
    }
}
static SIPTR invoke(APTR data, LONG operation, const SIPTR *args,
    SIPTR *error, struct DosLibrary *DOSBase)
{
    struct __cwd_binding *b = data;
    struct __cwd_snapshot s;
    struct __cwd_native_call call = { operation, args };
    SIPTR result;
    if (!__cwd_runtime_snapshot(&s)) { *error = ERROR_NO_FREE_STORE; return 0; }
    result = RunWithContext(b->override ? b->override : __cwd_directory_lock(s.cwd),
        __cwd_directory_lock(s.progdir), __cwd_native_call, &call, error);
    __cwd_snapshot_release(&s);
    return result;
}
SIPTR __cwd_runtime_native(LONG operation, const SIPTR *args, SIPTR *error)
{
    struct __cwd_binding *b = binding();
    if (!b) { *error = ERROR_INVALID_LOCK; return 0; }
    return invoke(b, operation, args, error, DOSBase);
}
static int runtime_init(struct PosixCIntBase *base)
{
    (void)base;
    runtime_slot = AllocTaskStorageSlot();
    admission_slot = AllocTaskStorageSlot();
    if (runtime_slot <= 0 || admission_slot <= 0) {
        if (runtime_slot > 0) FreeTaskStorageSlot(runtime_slot);
        if (admission_slot > 0) FreeTaskStorageSlot(admission_slot);
        runtime_slot = admission_slot = 0;
        return FALSE;
    }
    return TRUE;
}
static int runtime_expunge(struct PosixCIntBase *base)
{
    (void)base;
    if (runtime_slot > 0) FreeTaskStorageSlot(runtime_slot);
    if (admission_slot > 0) FreeTaskStorageSlot(admission_slot);
    runtime_slot = admission_slot = 0;
    return TRUE;
}
/* OpenLib runs while an older runtime's path resolver may still be installed.
 * Bootstrap from explicit native Process locks, never Lock("") through that
 * resolver. A missing native cwd denotes the boot root, also for plain Tasks. */
static BPTR native_initial_directory(struct Task *task, BPTR *program)
{
    BPTR cwd = BNULL;
    *program = BNULL;
    if (task->tc_Node.ln_Type == NT_PROCESS)
    {
        cwd = ((struct Process *)task)->pr_CurrentDir;
        *program = GetProgramDir();
    }
    if (cwd) return DupLock(cwd);
    SIPTR args[] = { (SIPTR)"", SHARED_LOCK }, secondary;
    struct __cwd_native_call call = { DOS_PATH_LOCK, args };
    return (BPTR)RunWithContext(BNULL, *program, __cwd_native_call, &call, &secondary);
}
static int runtime_open(struct PosixCIntBase *base)
{
    struct __cwd_binding *b, *old = binding();
    struct __cwd_admission *admission = (APTR)GetTaskStorageSlot(admission_slot);
    struct Task *task = FindTask(NULL);
    BPTR initial = BNULL;
    LONG error = 0;
    /* A plain Task has no native DOS standard streams to initialize. It may
       open this runtime only through admission with an owned descriptor table. */
    if (task->tc_Node.ln_Type != NT_PROCESS && !admission) return FALSE;
    if (DOSBase->dl_lib.lib_Version < 50 ||
        (DOSBase->dl_lib.lib_Version == 50 && DOSBase->dl_lib.lib_Revision < 87)) return FALSE;
    b = AllocVec(sizeof(*b), MEMF_PUBLIC | MEMF_CLEAR);
    if (!b) return FALSE;
    b->references = 1;
    b->base = base;
    b->owner_id = GetETaskID(task);
    if (!b->owner_id) { FreeVec(b); return FALSE; }
    if (admission) {
        if (__cwd_context_retain(admission->context)) b->context = admission->context;
    } else {
        BPTR program = BNULL;
        initial = native_initial_directory(task, &program);
        if (initial) b->context = __cwd_context_create(initial, program, base->doupath, &error);
        if (initial) UnLock(initial);
    }
    if (!b->context) { FreeVec(b); return FALSE; }
    b->resolver.version = DOS_PATH_RESOLVER_VERSION;
    b->resolver.invoke = invoke;
    b->resolver.data = b;
    if (!SetTaskStorageSlot(runtime_slot, (IPTR)b)) { binding_release(b); return FALSE; }
    if (!SetTaskPathResolver(&b->resolver)) {
        SetTaskStorageSlot(runtime_slot, (IPTR)old);
        binding_release(b);
        return FALSE;
    }
    if (old) __atomic_add_fetch(&old->references, 1, __ATOMIC_RELAXED);
    b->previous = old;
    base->cwd_runtime = b;
    base->doupath = __cwd_runtime_conversion();
    return TRUE;
}
/* Called before generated CloseLib changes counts, slots or offset tables.
 * Only explicit-base fields and Exec's current Task identity may be read:
 * relative CRT services could already resolve a different Task's base. A
 * refused reference keeps the runtime/module loaded for its owner. Forced
 * owner removal requires the separate post-stop cleanup protocol. Nested
 * instances close in binding order: generated task-slot restoration must never
 * publish a predecessor whose base has already been freed. */
static int runtime_can_close(struct PosixCIntBase *base)
{
    struct __cwd_binding *b = base->cwd_runtime;
    if (!b) return TRUE;
    if (!b->owner_id || b->owner_id != GetETaskID(FindTask(NULL))) return FALSE;
    /* Releasing a creator's admission pin cannot tear down this base while
       another reference owns it. Only its final close unwinds the binding. */
    return __GM_GetBaseOpenCount(base) > 1 ||
        (binding() == b && !b->scope_depth && !b->context_scope);
}
static void runtime_close(struct PosixCIntBase *base)
{
    struct __cwd_binding *b = base->cwd_runtime;
    struct __cwd_binding *current = binding(), *previous;
    if (!b) return;
    base->cwd_runtime = NULL;
    b->base = NULL;
    if (current == b) {
        previous = b->previous;
        while (previous && !previous->base) previous = previous->previous;
        /* Normal close is outside the resolver callback. The live base owns
         * this instance and its code until these synchronous calls return. */
        SetTaskPathResolver(previous ? &previous->resolver : NULL);
        SetTaskStorageSlot(runtime_slot, (IPTR)previous);
    }
    binding_release(b);
}
APTR __cwd_runtime_source(void)
{
    return binding();
}
struct __cwd_admission *__cwd_admission_capture(APTR source)
{
    struct __cwd_admission *a;
    struct __cwd_binding *selected = source;
    struct __cwd_context *context;
    if (!selected || !selected->base ||
        selected->owner_id != GetETaskID(FindTask(NULL))) return NULL;
    context = selected->context;
    a = AllocVec(sizeof(*a), MEMF_PUBLIC | MEMF_CLEAR);
    if (!a) return NULL;
    if (!__cwd_context_retain(context)) { FreeVec(a); return NULL; }
    a->context = context;
    a->descriptors = selected->base->fd_namespace;
    if (!__fd_namespace_retain(a->descriptors)) a->descriptors = NULL;
    if (!a->descriptors) { __cwd_context_release(context); FreeVec(a); return NULL; }
    return a;
}
BOOL __cwd_admission_enter(struct __cwd_admission *a)
{
    if (!a || !SetTaskStorageSlot(admission_slot, (IPTR)a)) return FALSE;
    a->worker = OpenLibrary("posixc.library", 0);
    SetTaskStorageSlot(admission_slot, 0);
    if (a->worker)
    {
        struct __cwd_binding *b = binding();
        struct PosixCIntBase *base = (struct PosixCIntBase *)a->worker;
        if (!b || b->base != base || b->context != a->context ||
            base->fd_namespace != a->descriptors)
        {
            /* An already-open independent instance did not consume this
               admission. Refuse rather than run with the wrong context. */
            CloseLibrary(a->worker);
            a->worker = NULL;
        }
    }
    return a->worker != NULL;
}
struct Library *__cwd_admission_leave(struct __cwd_admission *a)
{
    struct Library *worker = a ? a->worker : NULL;
    if (a) {
        __fd_namespace_drop(a->descriptors);
        __cwd_context_release(a->context);
        FreeVec(a);
    }
    return worker; /* caller closes after returning out of this module */
}
void __cwd_admission_cancel(struct __cwd_admission *a)
{
    (void)__cwd_admission_leave(a); /* only before successful Enter */
}
struct __fd_namespace *__cwd_admission_namespace(BOOL *pending)
{
    struct __cwd_admission *a = admission_slot > 0 ?
        (APTR)GetTaskStorageSlot(admission_slot) : NULL;
    if (pending) *pending = a != NULL;
    return a && __fd_namespace_retain(a->descriptors) ? a->descriptors : NULL;
}
ADD2INITLIB(runtime_init, 1);
ADD2CANCLOSELIB(runtime_can_close, 0);
ADD2EXPUNGELIB(runtime_expunge, 1);
ADD2OPENLIB(runtime_open, -40);
ADD2CLOSELIB(runtime_close, -40);
ADD2ABORTOPENLIB(runtime_close, -40);
