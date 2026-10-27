/*
  Copyright (C) 2026 John KNIPPER <code@jkn.me> (altered runtime admission)
  Copyright (C) 2014 Szilard Biro
  Copyright (C) 2018 Harry Sintonen
  Copyright (C) 2019 Stefan "Bebbo" Franke - AmigaOS 3 port

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/semaphores.h>
#include <exec/lists.h>

#include <stdio.h>
#include <string.h>

#include "pthread_intern.h"
#include "debug.h"

static void ClearThreadInfo(ThreadInfo *inf)
{
    volatile UBYTE *p = (volatile UBYTE *)inf;
    ULONG n = sizeof(ThreadInfo);

    while (n--)
        *p++ = 0;
}

#ifdef __AROS__
static void ReleaseAdmission(struct PThreadAdmission *admit)
{
    if (__atomic_sub_fetch(&admit->refs, 1, __ATOMIC_ACQ_REL) == 0)
        FreeVec(admit);
}

#endif

static void StarterFunc(void)
{
    ThreadInfo *inf;
    struct Task *waiter;
    int i, j;
    int foundkey = TRUE;
#ifdef USE_ASYNC_CANCEL
    APTR oldexcept;
#endif

    DB2(bug("%s()\n", __FUNCTION__));

#if defined(__AMIGA__) && !defined(__MORPHOS__)
    struct Process * proc = (struct Process *)SysBase->ThisTask;
    inf = (ThreadInfo *)proc->pr_CIS;
    proc->pr_CIS = 0;
#else
    inf = (ThreadInfo *)FindTask(NULL)->tc_UserData;
#endif

    // wait until the parent has set inf->task. Not by signal: a SIGF_SINGLE
    // arriving while DosEntry waits in ObtainSemaphore ends that wait unowned
    ObtainSemaphore(&thread_sem);
    ReleaseSemaphore(&thread_sem);

#ifdef __AROS__
    // admission: enter every captured C-runtime context, then tell the
    // creator, which is waiting and holds the providers' pins until then.
    // Nothing of the token is touched after the reference is dropped.
    {
        struct PThreadAdmission *admit = inf->admit;
        struct Task *creator = admit->creator;
        ULONG mask = admit->mask;
        BOOL admitted = CRTThreadContextEnter(&inf->ctxset);

        inf->admit = NULL;
        admit->result = admitted;
        ReleaseAdmission(admit);
        Signal(creator, mask);
        if (!admitted)
        {
            // refused: start never runs; the creator reports EAGAIN and
            // never looks at this slot again, so free it here
            ObtainSemaphore(&thread_sem);
            ClearThreadInfo(inf);
            ReleaseSemaphore(&thread_sem);
            return;
        }
    }

#endif

    // trim the name
    //inf->task->tc_Node.ln_Name[inf->oldlen];

    // we have to set the priority here to avoid race conditions
    SetTaskPri(inf->task, inf->attr.param.sched_priority);

#ifdef USE_ASYNC_CANCEL
    // set the exception handler for async cancellation
    oldexcept = inf->task->tc_ExceptCode;
#ifdef __AROS__
    inf->task->tc_ExceptCode = &AROS_ASMSYMNAME(CancelHandler);
#else
    inf->task->tc_ExceptCode = &CancelHandler;
#endif
    SetExcept(SIGBREAKF_CTRL_C, SIGBREAKF_CTRL_C);
#endif

    // set a jump point for pthread_exit
    if (!setjmp(inf->jmp))
    {
        // custom stack requires special handling
        if (inf->attr.stackaddr != NULL && inf->attr.stacksize > 0)
        {
#if defined(__AMIGA__) && !defined(__MORPHOS__)
            struct StackSwapStruct stack;
            stack.stk_Lower = inf->attr.stackaddr;
            stack.stk_Upper = (ULONG)((char *)stack.stk_Lower + inf->attr.stacksize);
            stack.stk_Pointer = (APTR)stack.stk_Upper;

            StackSwap(&stack);

            inf->ret = inf->start(inf->arg);
#else
            struct StackSwapArgs swapargs;
            struct StackSwapStruct stack;

            swapargs.Args[0] = (IPTR)inf->arg;
            stack.stk_Lower = inf->attr.stackaddr;
#ifdef __MORPHOS__
            stack.stk_Upper = (IPTR)stack.stk_Lower + inf->attr.stacksize;
            stack.stk_Pointer = (APTR)stack.stk_Upper;
#else
            stack.stk_Upper = (APTR)((IPTR)stack.stk_Lower + inf->attr.stacksize);
            stack.stk_Pointer = stack.stk_Upper;
#endif

            inf->ret = (void *)NewStackSwap(&stack, inf->start, &swapargs);
#endif
        }
        else
        {
            inf->ret = inf->start(inf->arg);
        }
    }

#ifdef USE_ASYNC_CANCEL
    // remove the exception handler
    SetExcept(0, SIGBREAKF_CTRL_C);
    inf->task->tc_ExceptCode = oldexcept;
#endif

    // destroy all non-NULL TLS key values
    // since the destructors can set the keys themselves, we have to do multiple iterations
    // (not when cancelled by the program's exit: as with POSIX exit() the thread
    // just ends, its destructors may wait for the code it was cancelled in)
    ObtainSemaphoreShared(&tls_sem);
    for (j = 0; !__pthread_exiting && foundkey && j < PTHREAD_DESTRUCTOR_ITERATIONS; j++)
    {
        foundkey = FALSE;
        for (i = 0; i < PTHREAD_KEYS_MAX; i++)
        {
            if (tlskeys[i].used && tlskeys[i].destructor && inf->tlsvalues[i])
            {
                void *oldvalue = inf->tlsvalues[i];
                inf->tlsvalues[i] = NULL;
                tlskeys[i].destructor(oldvalue);
                foundkey = TRUE;
            }
        }
    }
    ReleaseSemaphore(&tls_sem);

    // release the C runtime base(s) acquired above, after all user code
    // (including TLS destructors) has run, in reverse admission order
#ifdef __AROS__
    CRTThreadContextLeave(&inf->ctxset);
#endif

    // decide under thread_sem: pthread_detach() and pthread_join() may run
    // concurrently. A joiner is woken even if we were detached meanwhile.
    ObtainSemaphore(&thread_sem);
    waiter = inf->waiter;
    if (!inf->detached)
        inf->finished = TRUE;
    else
        ClearThreadInfo(inf);
    ReleaseSemaphore(&thread_sem);
    // the joiner may unload our code: let it run once we are gone
    Forbid();
    if (waiter)
        Signal(waiter, SIGF_PARENT);
}

/****************************************************************************
    NAME */
        int pthread_create(

/*  SYNOPSIS */
        pthread_t *thread,
        const pthread_attr_t *attr,
        void *(*start)(void *),
        void *arg)

/*  FUNCTION
        Start a worker running start(arg). The worker may execute before this
        call returns. A normal return from start stores its pointer result for
        pthread_join(); pthread_exit() also runs registered cleanup handlers.

        The attributes are copied. With NULL attributes, the worker is joinable,
        uses the creator's priority and stack extent, and starts with deferred
        cancellation enabled. A joinable worker retains its slot until joined;
        a detached worker releases its slot when it finishes.

    INPUTS
        thread - Non-NULL writable output for the new thread identifier.
        attr - Initialized attributes, or NULL for defaults. May be destroyed
            after this call. A supplied stack remains caller-owned and must
            remain valid until the worker has finished using it.
        start - Non-NULL worker entry point. Receives arg unchanged.
        arg - Borrowed argument pointer, which may be NULL. Its storage and
            everything reachable through it must outlive the worker's use.

    RESULT
        0 on success; *thread receives the identifier. EINVAL if thread or start
        is NULL. EAGAIN if no slot is available, a thread record, the
        admission token or a signal cannot be allocated, the process cannot
        be created, or a C runtime refuses to capture the creator's context
        or to enter it in the worker. On error, *thread is unchanged and
        start has not run. Errors are returned directly.

    NOTES
        Include <pthread.h> and link with the pthread link library. Call from
        task context with its runtime initialized. Creation can wait for the
        internal thread semaphore and system allocation; it has no timeout.

        Creation runs the version-3 C-runtime admission of
        <aros/crt_thread_context.h>: every registered runtime captures the
        creator's context first, and the call returns only after the worker
        has entered all of them. A worker that cannot enter them exits
        without running start. Provider code stays pinned until admission has
        been acknowledged; there is no legacy callback fallback.

        The worker runs in a DOS process and applies attr's scheduling priority.
        Scheduling inheritance and contention-scope fields are not implemented
        by this creation path. The start routine must return a pointer whose
        lifetime permits its eventual use by the joiner, not worker-stack data.

        Worker exit runs non-NULL thread-specific destructors before releasing
        its C runtime. Destructors are skipped during program-wide teardown.
        Join or detach every successfully created joinable worker. Identifiers
        are reused; they are not permanent handles.

    BUGS
        Returning from start bypasses the cleanup-handler list in this
        implementation. Use balanced cleanup operations or pthread_exit()
        when those handlers must run.

    SEE ALSO
        pthread_join(),
        pthread_exit(),
        pthread_attr_init(),
        pthread_cancel()

*****************************************************************************/
{
    ThreadInfo *inf;
    char name[NAMELEN];
    size_t oldlen;
    pthread_t threadnew;
    struct Task *task;
#ifdef __AROS__
    struct CRTThreadContextSet set;
    struct PThreadAdmission *admit;
    BYTE sigbit;
    ULONG i;
    BOOL admitted;
#endif

    D(bug("%s(%p, %p, %p, %p)\n", __FUNCTION__, thread, attr, start, arg));

    if (thread == NULL || start == NULL)
        return EINVAL;

#ifdef __AROS__
    // capture the creator's C-runtime contexts before anything exists; this
    // pins each provider's library and may wait, so not under thread_sem
    if (!CRTThreadContextCapture(&set))
        return EAGAIN;
    // AllocVec: memory from a thread's C runtime goes away with the thread
    admit = AllocVec(sizeof(*admit), MEMF_PUBLIC | MEMF_CLEAR);
    sigbit = admit ? AllocSignal(-1) : -1;
    if (sigbit < 0)
    {
        if (admit)
            FreeVec(admit);
        CRTThreadContextCancel(&set);
        return EAGAIN;
    }
    admit->refs = 2;
    admit->creator = GET_THIS_TASK;
    admit->mask = 1UL << sigbit;
    SetSignal(0, admit->mask);

#endif

    ObtainSemaphore(&thread_sem);

    // grab an empty thread slot
    threadnew = GetThreadId(NULL);
    if (threadnew == PTHREAD_THREADS_MAX)
    {
        ReleaseSemaphore(&thread_sem);
#ifdef __AROS__
        FreeSignal(sigbit);
        FreeVec(admit);
        CRTThreadContextCancel(&set);
#endif
        return EAGAIN;
    }

    if (threadnew == threads_used)
    {
        if (!(threads[threadnew] = AllocVec(sizeof(ThreadInfo), MEMF_ANY)))
        {
            ReleaseSemaphore(&thread_sem);
#ifdef __AROS__
            FreeSignal(sigbit);
            FreeVec(admit);
            CRTThreadContextCancel(&set);
#endif
            return EAGAIN;
        }
        __atomic_store_n(&threads_used, threadnew + 1, __ATOMIC_RELEASE);
    }

    // prepare the ThreadInfo structure
    inf = GetThreadInfo(threadnew);
    memset(inf, 0, sizeof(ThreadInfo));
    inf->start = start;
    inf->arg = arg;
    inf->parent = GET_THIS_TASK;
    if (attr)
        inf->attr = *attr;
    else
        pthread_attr_init(&inf->attr);
    NEWLIST((struct List *)&inf->cleanup);
    inf->cancelstate = PTHREAD_CANCEL_ENABLE;
    inf->canceltype = PTHREAD_CANCEL_DEFERRED;
    inf->detached = inf->attr.detachstate == PTHREAD_CREATE_DETACHED;
#ifdef __AROS__
    // the worker gets the contexts; the pins stay in the creator's copy
    inf->ctxset = set;
    for (i = 0; i < inf->ctxset.cs_Count; i++)
        inf->ctxset.cs_Entries[i].ce_Pin = NULL;
    inf->admit = admit;

#endif

    // let's trick CreateNewProc into allocating a larger buffer for the name
    snprintf(name, sizeof(name), "pthread thread #%d", threadnew);
    oldlen = strlen(name);
    memset(name + oldlen, ' ', sizeof(name) - oldlen - 1);
    name[sizeof(name) - 1] = '\0';

    // start the child thread
    task = (struct Task *)CreateNewProcTags(NP_Entry, (IPTR)StarterFunc,
#ifdef __MORPHOS__
        NP_CodeType, CODETYPE_PPC,
        (inf->attr.stackaddr == NULL && inf->attr.stacksize > 0) ? NP_PPCStackSize : TAG_IGNORE, inf->attr.stacksize,
        inf->attr.stacksize68k > 0 ? NP_StackSize : TAG_IGNORE, inf->attr.stacksize68k,
#else
        (inf->attr.stackaddr == NULL && inf->attr.stacksize > 0) ? NP_StackSize : TAG_IGNORE, inf->attr.stacksize,
#endif
#if defined(__AMIGA__) && !defined(__MORPHOS__)
        NP_Input, (IPTR)inf,
#else
        NP_UserData, inf,
#endif
        NP_Name, (IPTR)name,
        TAG_DONE);
    inf->task = task;
    if (!task)
    {
        // no worker ever saw the contexts or the token
        memset(inf, 0, sizeof(ThreadInfo));
        ReleaseSemaphore(&thread_sem);
#ifdef __AROS__
        FreeSignal(sigbit);
        FreeVec(admit);
        CRTThreadContextCancel(&set);
#endif
        return EAGAIN;
    }
    ReleaseSemaphore(&thread_sem);

#ifdef __AROS__
    // success only once the worker has entered its contexts. From here on
    // inf belongs to the worker: a detached one may already be gone.
    Wait(admit->mask);
    admitted = admit->result;
    CRTThreadContextReleasePins(&set);
    FreeSignal(sigbit);
    ReleaseAdmission(admit);
    if (!admitted)
        return EAGAIN;

#endif

    *thread = threadnew;

    return 0;
}
