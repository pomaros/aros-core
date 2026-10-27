#ifndef AROS_CRT_THREAD_CONTEXT_H
#define AROS_CRT_THREAD_CONTEXT_H

/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Distributed under the AROS Public License 1.1.

    Desc: C-runtime thread-context admission, version 2.

    A C runtime (the provider) that keeps per-task state, such as posixc's
    descriptors and current directory, registers one set of hooks in a
    permanent public registry. Whoever creates a Task that must run C code
    (the creator: pthread_create, or any code that creates a plain Task)
    runs the protocol below, so the new Task gets its own runtime instance
    with the creator's context, and the creator learns whether it did before
    it reports success.

    Protocol, in this order:

    1. Creator, before the Task exists: CRTThreadContextCapture(&set).
       Under the registry semaphore, where no provider can unregister,
       tc_Active() is asked of each provider and the hooks and library name
       of the active ones are copied; nothing is opened before that, since
       opening a provider's library may create the state tc_Active()
       reports. More than CRT_THREAD_CONTEXTS_MAX active providers, or an
       active one whose name does not fit, refuses the creation. Each
       active provider's library is then pinned with OpenLibrary() outside
       the semaphore; a pin that fails refuses the creation. If a provider
       unregistered meanwhile, the pins are closed and the capture starts
       again. Each active provider's tc_Capture() then runs outside the
       semaphore and returns an opaque heap context holding its own
       references, never a pointer to the creator's base. A FALSE from
       tc_Capture() refuses the creation: what was captured is cancelled
       and the pins are closed.
    2. Creator: create the Task. On failure, CRTThreadContextCancel(&set).
       Otherwise hand the set to the new Task.
    3. New Task, first thing: CRTThreadContextEnter(&set). Each tc_Enter()
       opens the provider's own instance for this Task. On failure, pending
       contexts are cancelled and entered instances are left in reverse order.
       Enter returns FALSE and the Task must not execute application C code.
    4. New Task: report the result to the creator. Creator: wait for it,
       then CRTThreadContextReleasePins(&set) in the creator Task. The pins
       kept every provider's code loaded until the Task held its own
       instance. Only after this may the creator report success.
    5. New Task, after all its C code: CRTThreadContextLeave(&set). Each
       tc_Leave() undoes its tc_Enter() and returns the library base the
       Task opened, or NULL; that base is closed after the hook returns, so
       no hook ever closes the library whose code it is running.

    Rules for a provider:
    - Register once (CRTThreadContextRegister) at library init and
      unregister (CRTThreadContextUnregister) at expunge. Unregister takes
      the registry semaphore exclusively, so it waits for any creator that
      is between steps of point 1.
    - tc_Active() runs under the registry semaphore: no waiting, no
      OpenLibrary(), no allocation that can wait.
    - tc_Capture(), tc_Enter(), tc_Leave() and tc_Cancel() run outside it.
      tc_Cancel() may run in the creator or in the new Task, and only for a
      context that tc_Enter() did not take. Capture must initialize its output;
      if it refuses with a partial non-NULL context, that context is cancelled
      as well. A context belongs to the hook
      that received it: tc_Enter() keeps it on TRUE and must free it in
      tc_Leave(); on FALSE it stays the caller's, who cancels it.
    - tc_LibName and the hooks are copied; the hooks structure itself must
      stay valid and immutable until unregister returns. Duplicate nodes or
      library names are refused. Withdraw a provider only as part of its library
      expunge protocol: outstanding library opens must prevent code unload.

    Nothing here waits or opens a library while holding the registry
    semaphore. The registry is never freed.
*/

#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/lists.h>
#include <exec/semaphores.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <aros/config.h>
#include <resources/execlock.h>
#include <proto/execlock.h>
#include <aros/types/spinlock_s.h>

#define CRT_THREAD_CONTEXTS_SEMNAME  "crt.thread-contexts.v2"
#define CRT_THREAD_CONTEXT_VERSION   2
#define CRT_THREAD_CONTEXTS_MAX      8
#define CRT_THREAD_CONTEXT_NAMELEN   64
#define CRT_THREAD_CONTEXT_RETRIES   8

struct CRTThreadContextHooks
{
    struct MinNode   tc_Node;        /* registry link, owned by the registry  */
    ULONG            tc_Version;     /* CRT_THREAD_CONTEXT_VERSION            */
    CONST_STRPTR     tc_LibName;     /* pinned in the creator during capture  */
    ULONG            tc_MinVersion;
    BOOL           (*tc_Active)(void);
    BOOL           (*tc_Capture)(APTR *context);
    BOOL           (*tc_Enter)(APTR context);
    struct Library *(*tc_Leave)(APTR context);
    void           (*tc_Cancel)(APTR context);
};

/* The public registry: a named semaphore in SysBase->SemaphoreList. */
struct CRTThreadContexts
{
    struct SignalSemaphore tc_Sem;
    struct MinList         tc_Providers;
    ULONG                  tc_Generation;  /* bumped by every unregister    */
    /* The semaphore's name, owned by the registry: the module that created
       it may be expunged, so its string literal cannot be the name. */
    char                   tc_Name[sizeof(CRT_THREAD_CONTEXTS_SEMNAME)];
};

#define CRT_ENTRY_NONE      0
#define CRT_ENTRY_CAPTURED  1
#define CRT_ENTRY_ENTERED   2

struct CRTThreadContextEntry
{
    BOOL           (*ce_Enter)(APTR context);
    struct Library *(*ce_Leave)(APTR context);
    void           (*ce_Cancel)(APTR context);
    struct Library  *ce_Pin;     /* the creator's pin, closed by the creator */
    APTR             ce_Context;
    UBYTE            ce_State;
};

/* Everything one creation needs. The creator owns it until the new Task
   runs CRTThreadContextEnter(); afterwards ce_Pin stays the creator's (it
   is closed in CRTThreadContextReleasePins) and the rest is the Task's. */
struct CRTThreadContextSet
{
    struct CRTThreadContexts    *cs_Registry;
    ULONG                        cs_Count;
    struct CRTThreadContextEntry cs_Entries[CRT_THREAD_CONTEXTS_MAX];
};

/* Namespace guards protect only bounded list operations. Allocation,
   semaphore initialization and provider calls happen outside them. */
static inline BOOL __crt_tc_namespace_enter(struct Library **base, ULONG mode)
{
    struct Library *ExecLockBase = (struct Library *)
        OpenResource((CONST_STRPTR)"execlock.resource");
    *base = ExecLockBase;
    if (ExecLockBase)
        return ObtainSystemLock(&SysBase->SemaphoreList, mode, LOCKF_DISABLE);
#if defined(__AROSEXEC_SMP__)
    return FALSE;
#else
    Disable();
    return TRUE;
#endif
}

static inline void __crt_tc_namespace_leave(struct Library *ExecLockBase)
{
    if (ExecLockBase)
        ReleaseSystemLock(&SysBase->SemaphoreList, LOCKF_DISABLE);
    else
        Enable();
}

/* The first fully initialized candidate is published under the namespace
   write lock. No duplicate registry is ever visible to another caller. */
static inline struct CRTThreadContexts *CRTThreadContextsGet(BOOL create)
{
    struct CRTThreadContexts *reg, *candidate;
    struct Library *namespace_base;

    if (!__crt_tc_namespace_enter(&namespace_base, SPINLOCK_MODE_READ))
        return NULL;
    reg = (struct CRTThreadContexts *)FindName(&SysBase->SemaphoreList,
                                             CRT_THREAD_CONTEXTS_SEMNAME);
    __crt_tc_namespace_leave(namespace_base);
    if (reg || !create)
        return reg;

    candidate = AllocMem(sizeof(*candidate), MEMF_PUBLIC | MEMF_CLEAR);
    if (!candidate)
        return NULL;
    InitSemaphore(&candidate->tc_Sem);
    NEWLIST(&candidate->tc_Providers);
    CopyMem((APTR)CRT_THREAD_CONTEXTS_SEMNAME, candidate->tc_Name,
            sizeof(candidate->tc_Name));
    candidate->tc_Sem.ss_Link.ln_Name = candidate->tc_Name;
    if (!__crt_tc_namespace_enter(&namespace_base, SPINLOCK_MODE_WRITE))
    {
        FreeMem(candidate, sizeof(*candidate));
        return NULL;
    }
    reg = (struct CRTThreadContexts *)FindName(&SysBase->SemaphoreList,
                                             CRT_THREAD_CONTEXTS_SEMNAME);
    if (!reg)
    {
        /* AddSemaphore would acquire this lock recursively and reinitialize
           the semaphore. Publish the constructed node directly. */
        Enqueue(&SysBase->SemaphoreList, &candidate->tc_Sem.ss_Link);
        reg = candidate;
        candidate = NULL;
    }
    __crt_tc_namespace_leave(namespace_base);
    if (candidate)
        FreeMem(candidate, sizeof(*candidate));
    return reg;
}

/* Registration may run before a per-task C runtime exists. Use bounded
   scalar operations and Exec primitives, never libc initialization calls. */
static inline ULONG __crt_tc_name_length(CONST_STRPTR name)
{
    ULONG length = 0;
    while (length < CRT_THREAD_CONTEXT_NAMELEN && name[length])
        length++;
    return length;
}

static inline BOOL __crt_tc_names_equal(CONST_STRPTR a, CONST_STRPTR b)
{
    ULONG i;
    for (i = 0; i < CRT_THREAD_CONTEXT_NAMELEN; i++)
    {
        if (a[i] != b[i])
            return FALSE;
        if (!a[i])
            return TRUE;
    }
    return FALSE;
}

static inline BOOL CRTThreadContextRegister(struct CRTThreadContextHooks *hooks)
{
    struct CRTThreadContexts *reg;

    if (!hooks || hooks->tc_Version != CRT_THREAD_CONTEXT_VERSION
        || !hooks->tc_LibName || !hooks->tc_LibName[0]
        || __crt_tc_name_length(hooks->tc_LibName) == CRT_THREAD_CONTEXT_NAMELEN
        || !hooks->tc_Active || !hooks->tc_Capture
        || !hooks->tc_Enter || !hooks->tc_Leave || !hooks->tc_Cancel)
        return FALSE;
    reg = CRTThreadContextsGet(TRUE);
    if (!reg)
        return FALSE;
    ObtainSemaphore(&reg->tc_Sem);
    {
        struct CRTThreadContextHooks *current;
        for (current = (struct CRTThreadContextHooks *)reg->tc_Providers.mlh_Head;
             current->tc_Node.mln_Succ;
             current = (struct CRTThreadContextHooks *)current->tc_Node.mln_Succ)
        {
            if (current == hooks || __crt_tc_names_equal(current->tc_LibName, hooks->tc_LibName))
            {
                ReleaseSemaphore(&reg->tc_Sem);
                return FALSE;
            }
        }
    }
    AddTail((struct List *)&reg->tc_Providers, (struct Node *)&hooks->tc_Node);
    ReleaseSemaphore(&reg->tc_Sem);
    return TRUE;
}

static inline void CRTThreadContextUnregister(struct CRTThreadContextHooks *hooks)
{
    struct CRTThreadContexts *reg = CRTThreadContextsGet(FALSE);

    if (!reg || !hooks)
        return;
    ObtainSemaphore(&reg->tc_Sem);
    {
        struct CRTThreadContextHooks *current;
        for (current = (struct CRTThreadContextHooks *)reg->tc_Providers.mlh_Head;
             current->tc_Node.mln_Succ;
             current = (struct CRTThreadContextHooks *)current->tc_Node.mln_Succ)
        {
            if (current != hooks)
                continue;
            Remove((struct Node *)&hooks->tc_Node);
            hooks->tc_Node.mln_Succ = hooks->tc_Node.mln_Pred = NULL;
            reg->tc_Generation++;
            break;
        }
    }
    ReleaseSemaphore(&reg->tc_Sem);
}

/* Undo a capture that no Task entered: cancel the contexts, close the pins. */
static inline void CRTThreadContextCancel(struct CRTThreadContextSet *set)
{
    ULONG i;

    for (i = set->cs_Count; i-- > 0;)
    {
        struct CRTThreadContextEntry *e = &set->cs_Entries[i];
        if (e->ce_State == CRT_ENTRY_CAPTURED)
            e->ce_Cancel(e->ce_Context);
        e->ce_State = CRT_ENTRY_NONE;
        e->ce_Context = NULL;
        if (e->ce_Pin)
            CloseLibrary(e->ce_Pin);
        e->ce_Pin = NULL;
    }
    set->cs_Count = 0;
}

static inline BOOL CRTThreadContextCapture(struct CRTThreadContextSet *set)
{
    struct CRTThreadContexts *reg;
    struct CRTThreadContextHooks copy[CRT_THREAD_CONTEXTS_MAX];
    char names[CRT_THREAD_CONTEXTS_MAX][CRT_THREAD_CONTEXT_NAMELEN];
    ULONG n, i, generation, attempt;

    set->cs_Count = 0;
    set->cs_Registry = NULL;
    if (!(reg = CRTThreadContextsGet(TRUE)))
        return FALSE;
    set->cs_Registry = reg;

    for (attempt = 0; attempt < CRT_THREAD_CONTEXT_RETRIES; attempt++)
    {
        struct CRTThreadContextHooks *h;
        BOOL unrepresentable = FALSE, stale, pinned = TRUE;

        /* 1. Under the semaphore, where no provider can unregister and so
              its code stays loaded: ask each one whether it is active, and
              copy the hooks and the name of the active ones. Nothing is
              opened first: opening a provider's library may itself create
              the very state tc_Active() reports. An active provider that
              does not fit refuses the creation instead of being dropped. */
        n = 0;
        ObtainSemaphoreShared(&reg->tc_Sem);
        generation = reg->tc_Generation;
        for (h = (struct CRTThreadContextHooks *)reg->tc_Providers.mlh_Head;
             h->tc_Node.mln_Succ;
             h = (struct CRTThreadContextHooks *)h->tc_Node.mln_Succ)
        {
            ULONG len;
            if (!h->tc_Active())
                continue;
            len = __crt_tc_name_length(h->tc_LibName);
            if (n == CRT_THREAD_CONTEXTS_MAX || len >= CRT_THREAD_CONTEXT_NAMELEN)
            {
                unrepresentable = TRUE;
                break;
            }
            copy[n].tc_MinVersion = h->tc_MinVersion;
            copy[n].tc_Capture = h->tc_Capture;
            copy[n].tc_Enter = h->tc_Enter;
            copy[n].tc_Leave = h->tc_Leave;
            copy[n].tc_Cancel = h->tc_Cancel;
            CopyMem((APTR)h->tc_LibName, names[n], len + 1);
            n++;
        }
        ReleaseSemaphore(&reg->tc_Sem);
        if (unrepresentable)
            return FALSE;

        /* 2. Pin the active providers' libraries, outside the semaphore. A
              pin that fails refuses the creation. */
        for (i = 0; i < n; i++)
        {
            set->cs_Entries[i].ce_State = CRT_ENTRY_NONE;
            set->cs_Entries[i].ce_Context = NULL;
            set->cs_Entries[i].ce_Pin = OpenLibrary((CONST_STRPTR)names[i], copy[i].tc_MinVersion);
            set->cs_Count = i + 1;
            if (!set->cs_Entries[i].ce_Pin)
            {
                pinned = FALSE;
                break;
            }
        }
        if (!pinned)
        {
            CRTThreadContextCancel(set);
            return FALSE;
        }

        /* 3. No provider unregistered meanwhile? Otherwise the copied hooks
              may name code that is gone: start again. */
        ObtainSemaphoreShared(&reg->tc_Sem);
        stale = reg->tc_Generation != generation;
        ReleaseSemaphore(&reg->tc_Sem);
        if (!stale)
            break;
        CRTThreadContextCancel(set);
    }
    if (attempt == CRT_THREAD_CONTEXT_RETRIES)
        return FALSE;

    /* 4. Capture each active provider's context, outside the semaphore. */
    for (i = 0; i < n; i++)
    {
        struct CRTThreadContextEntry *e = &set->cs_Entries[i];
        e->ce_Enter = copy[i].tc_Enter;
        e->ce_Leave = copy[i].tc_Leave;
        e->ce_Cancel = copy[i].tc_Cancel;
        e->ce_Context = NULL;
        {
            BOOL captured = copy[i].tc_Capture(&e->ce_Context);
            if (captured || e->ce_Context)
                e->ce_State = CRT_ENTRY_CAPTURED;
            if (!captured)
            {
                CRTThreadContextCancel(set);
                return FALSE;
            }
        }
    }
    return TRUE;
}

/* In the new Task. FALSE: nothing is left entered and the Task must not
   run C code. Never touches ce_Pin. */
static inline BOOL CRTThreadContextEnter(struct CRTThreadContextSet *set)
{
    ULONG i, j;

    for (i = 0; i < set->cs_Count; i++)
    {
        struct CRTThreadContextEntry *e = &set->cs_Entries[i];
        if (e->ce_State != CRT_ENTRY_CAPTURED)
            continue;
        if (e->ce_Enter(e->ce_Context))
        {
            e->ce_State = CRT_ENTRY_ENTERED;
            continue;
        }
        /* Cancel this one and the rest; leave what was entered. */
        for (j = i; j < set->cs_Count; j++)
            if (set->cs_Entries[j].ce_State == CRT_ENTRY_CAPTURED)
            {
                set->cs_Entries[j].ce_Cancel(set->cs_Entries[j].ce_Context);
                set->cs_Entries[j].ce_State = CRT_ENTRY_NONE;
                set->cs_Entries[j].ce_Context = NULL;
            }
        for (j = i; j-- > 0;)
            if (set->cs_Entries[j].ce_State == CRT_ENTRY_ENTERED)
            {
                struct Library *base = set->cs_Entries[j].ce_Leave(set->cs_Entries[j].ce_Context);
                set->cs_Entries[j].ce_State = CRT_ENTRY_NONE;
                set->cs_Entries[j].ce_Context = NULL;
                if (base)
                    CloseLibrary(base);
            }
        return FALSE;
    }
    return TRUE;
}

/* In the creator, once the new Task has reported its Enter result. */
static inline void CRTThreadContextReleasePins(struct CRTThreadContextSet *set)
{
    ULONG i;

    for (i = 0; i < set->cs_Count; i++)
        if (set->cs_Entries[i].ce_Pin)
        {
            CloseLibrary(set->cs_Entries[i].ce_Pin);
            set->cs_Entries[i].ce_Pin = NULL;
        }
}

/* In the new Task, after all its C code. */
static inline void CRTThreadContextLeave(struct CRTThreadContextSet *set)
{
    ULONG i;

    for (i = set->cs_Count; i-- > 0;)
    {
        struct CRTThreadContextEntry *e = &set->cs_Entries[i];
        if (e->ce_State == CRT_ENTRY_ENTERED)
        {
            struct Library *base = e->ce_Leave(e->ce_Context);
            e->ce_State = CRT_ENTRY_NONE;
            e->ce_Context = NULL;
            if (base)
                CloseLibrary(base);
        }
    }
}

#endif /* AROS_CRT_THREAD_CONTEXT_H */
