/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1.

    Desc: The machine-wide task freeze on an SMP build.

    The uniprocessor freeze walks the ready and wait lists under Disable().
    On SMP three things change:

    - Every (state, list) change happens under the task's tc_SpinLock, task
      lock outer and list lock inner, as the scheduler does. A list walk
      that already holds a list lock only tries the task lock, as
      core_Dispatch() does, and comes back for a task it could not take.
    - A task running on another CPU cannot be moved from here. It is marked
      IETF_FREEZE_PENDING and that CPU gets a schedule IPI: core_Schedule()
      switches a marked task out and core_Switch() parks it frozen on the
      wait list instead of the ready list.
    - Tasks keep moving while the freeze runs (a waiting task woken by an
      interrupt, a running one that calls Wait()), so the passes repeat
      until one finds nothing left to stop. A frozen task never runs and
      Signal() never readies one, so the passes converge.

    When they do not converge in time (a task that stays in Forbid() on
    another CPU), everything frozen so far is thawed and the freeze is
    refused, as on a build that cannot freeze at all.
*/

#include <aros/debug.h>
#include <exec/execbase.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#include <proto/kernel.h>

#include "exec_intern.h"
#include "etask.h"
#include "freeze_smp.h"

#if defined(__AROSEXEC_SMP__)

#define FREEZE_ROUNDS       20000   /* passes before the freeze is refused */
#define FREEZE_PAUSE        2000    /* yields between two passes           */

static void freeze_pause(void)
{
    int i;

    for (i = 0; i < FREEZE_PAUSE; ++i)
        __asm__ volatile("yield" ::: "memory");
}

/* One attempt at the write lock: the caller holds a list lock, and
 * everyone else takes the task lock first. Same encoding as KrnSpinLock. */
static BOOL freeze_trylock(spinlock_t *lock)
{
    unsigned int lock_value, result;

    __asm__ volatile(
            "1:     ldaxr   %w0, [%2]       \n\t"
            "       cbnz    %w0, 2f         \n\t"
            "       stxr    %w1, %w3, [%2]  \n\t"
            "       cbnz    %w1, 1b         \n\t"
            "       b       3f              \n\t"
            "2:     clrex                   \n\t"
            "3:                             \n\t"
            : "=&r"(lock_value), "=&r"(result)
            : "r"(&lock->lock), "r"(0x80000000)
            : "memory");

    if (lock_value != 0)
        return FALSE;
    lock->s_Owner = GET_THIS_TASK;
    return TRUE;
}

static struct IntETask *freeze_candidate(struct Task *task, struct Task *self)
{
    struct IntETask *iet;

    if (task == self || !(task->tc_Flags & TF_ETASK))
        return NULL;
    iet = GetIntETask(task);
    if (iet == NULL || (iet->iet_FreezeFlags & IETF_FREEZE_EXEMPT))
        return NULL;
    return iet;
}

static void set_pending(struct IntETask *iet, BOOL on)
{
    if (on)
        __atomic_fetch_or(&iet->iet_FreezeFlags, IETF_FREEZE_PENDING, __ATOMIC_ACQ_REL);
    else
        __atomic_fetch_and(&iet->iet_FreezeFlags, (UBYTE)~IETF_FREEZE_PENDING, __ATOMIC_ACQ_REL);
}

/* Ready tasks: one at a time off the ready list onto the wait list, the
 * task lock held across the move. */
static ULONG freeze_ready(struct Task *self, BOOL *busy)
{
    ULONG frozen = 0;

    for (;;)
    {
        struct Task *task, *pick = NULL;
        struct IntETask *iet = NULL;
        unsigned int __if = EXEC_IRQFIQ_DISABLE();

        EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskReadySpinLock, NULL, SPINLOCK_MODE_WRITE);
        ForeachNode(&SysBase->TaskReady, task)
        {
            if (task->tc_State != TS_READY || !(iet = freeze_candidate(task, self)))
                continue;
            if (!freeze_trylock(&task->tc_SpinLock))
            {
                *busy = TRUE;
                continue;
            }
            pick = task;
            Remove(&task->tc_Node);
            break;
        }
        EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskReadySpinLock);

        if (pick == NULL)
        {
            EXEC_IRQFIQ_RESTORE(__if);
            return frozen;
        }
        set_pending(iet, FALSE);
        iet->iet_FrozenState = TS_READY;
        pick->tc_State = TS_FROZEN;
        EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskWaitSpinLock, NULL, SPINLOCK_MODE_WRITE);
        Enqueue(&SysBase->TaskWait, &pick->tc_Node);
        EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskWaitSpinLock);
        EXEC_SPINLOCK_UNLOCK(&pick->tc_SpinLock);
        EXEC_IRQFIQ_RESTORE(__if);
        frozen++;
    }
}

/* Waiting tasks stay on the wait list, with a state Signal() leaves alone. */
static ULONG freeze_waiting(struct Task *self, BOOL *busy)
{
    struct Task *task;
    struct IntETask *iet;
    ULONG frozen = 0;
    unsigned int __if = EXEC_IRQFIQ_DISABLE();

    EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskWaitSpinLock, NULL, SPINLOCK_MODE_WRITE);
    ForeachNode(&SysBase->TaskWait, task)
    {
        if (task->tc_State != TS_WAIT || !(iet = freeze_candidate(task, self)))
            continue;
        if (!freeze_trylock(&task->tc_SpinLock))
        {
            *busy = TRUE;
            continue;
        }
        if (task->tc_State == TS_WAIT)
        {
            set_pending(iet, FALSE);
            iet->iet_FrozenState = TS_WAIT;
            task->tc_State = TS_FROZEN;
            frozen++;
        }
        EXEC_SPINLOCK_UNLOCK(&task->tc_SpinLock);
    }
    EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskWaitSpinLock);
    EXEC_IRQFIQ_RESTORE(__if);
    return frozen;
}

/* Running tasks on the other CPUs: marked, and their CPUs told to
 * reschedule. Returns whether any is left. */
static BOOL freeze_running(struct Task *self)
{
    struct Task *task;
    struct IntETask *iet;
    ULONG mask[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    BOOL any = FALSE;
    unsigned int __if = EXEC_IRQFIQ_DISABLE();

    EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskRunningSpinLock, NULL, SPINLOCK_MODE_READ);
    ForeachNode(&PrivExecBase(SysBase)->TaskRunning, task)
    {
        if (task->tc_State != TS_RUN || !(iet = freeze_candidate(task, self)))
            continue;
        set_pending(iet, TRUE);
        if (iet->iet_CpuNumber < 32)
            mask[0] |= 1UL << iet->iet_CpuNumber;
        any = TRUE;
    }
    EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskRunningSpinLock);
    EXEC_IRQFIQ_RESTORE(__if);

    if (mask[0])
        KrnScheduleCPU(mask);
    return any;
}

/* Clear every pending mark a task on the running list still carries. */
static void clear_running_marks(void)
{
    struct Task *task;
    unsigned int __if = EXEC_IRQFIQ_DISABLE();

    EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskRunningSpinLock, NULL, SPINLOCK_MODE_READ);
    ForeachNode(&PrivExecBase(SysBase)->TaskRunning, task)
        if ((task->tc_Flags & TF_ETASK) && GetIntETask(task))
            set_pending(GetIntETask(task), FALSE);
    EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskRunningSpinLock);
    EXEC_IRQFIQ_RESTORE(__if);
}

/* Back to the state it was frozen in; called with the task lock held and
 * the wait list locked. Returns TRUE when the task must go to the ready
 * list, which the caller does after dropping the wait-list lock. */
static BOOL thaw_locked(struct Task *task)
{
    struct IntETask *iet = GetIntETask(task);

    if (iet != NULL)
        set_pending(iet, FALSE);
    if (iet != NULL && iet->iet_FrozenState == TS_WAIT &&
        !(task->tc_SigRecvd & (task->tc_SigWait | task->tc_SigExcept)))
    {
        task->tc_State = TS_WAIT;
        return FALSE;
    }
    Remove(&task->tc_Node);
    return TRUE;
}

static void wake_cpus(void)
{
    KrnScheduleCPU((void *)TASKAFFINITY_ALL_BUT_SELF);
}

ULONG exec_ThawAllSMP(void)
{
    ULONG thawed = 0;

    for (;;)
    {
        struct Task *task, *pick = NULL;
        BOOL busy = FALSE, ready;
        unsigned int __if = EXEC_IRQFIQ_DISABLE();

        EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskWaitSpinLock, NULL, SPINLOCK_MODE_WRITE);
        ForeachNode(&SysBase->TaskWait, task)
        {
            if (task->tc_State != TS_FROZEN ||
                (GetIntETask(task) && (__atomic_load_n(&GetIntETask(task)->iet_FreezeFlags,
                    __ATOMIC_ACQUIRE) & IETF_RETIRE_PENDING)))
                continue;
            if (!freeze_trylock(&task->tc_SpinLock))
            {
                busy = TRUE;
                continue;
            }
            pick = task;
            break;
        }
        if (pick == NULL)
        {
            EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskWaitSpinLock);
            EXEC_IRQFIQ_RESTORE(__if);
            if (busy)
            {
                freeze_pause();
                continue;
            }
            break;
        }
        ready = thaw_locked(pick);
        EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskWaitSpinLock);
        if (ready)
        {
            pick->tc_State = TS_READY;
            EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskReadySpinLock, NULL, SPINLOCK_MODE_WRITE);
            Enqueue(&SysBase->TaskReady, &pick->tc_Node);
            EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskReadySpinLock);
        }
        EXEC_SPINLOCK_UNLOCK(&pick->tc_SpinLock);
        EXEC_IRQFIQ_RESTORE(__if);
        thawed++;
    }
    clear_running_marks();
    if (thawed)
    {
        wake_cpus();
        Reschedule();
    }
    return thawed;
}

ULONG exec_FreezeAllSMP(void)
{
    struct Task *self = GET_THIS_TASK;
    ULONG frozen = 0;
    int round;

    for (round = 0; round < FREEZE_ROUNDS; ++round)
    {
        BOOL busy = FALSE;

        frozen += freeze_ready(self, &busy);
        frozen += freeze_waiting(self, &busy);
        if (freeze_running(self))
            busy = TRUE;
        if (!busy)
        {
            D(bug("[Exec] FreezeTasks: %lu tasks frozen in %d rounds\n", frozen, round + 1));
            return frozen;
        }
        freeze_pause();
    }
    D(bug("[Exec] FreezeTasks: no convergence, thawing %lu\n", frozen));
    exec_ThawAllSMP();
    return FREEZE_UNSUPPORTED;
}

BOOL exec_FreezeOneSMP(struct Task *task)
{
    struct IntETask *iet;
    int round;

    if (task == NULL || task == GET_THIS_TASK || !(task->tc_Flags & TF_ETASK) ||
        (iet = GetIntETask(task)) == NULL)
        return FALSE;

    for (round = 0; round < FREEZE_ROUNDS; ++round)
    {
        BOOL frozen = FALSE, running = FALSE;
        ULONG mask[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        unsigned int __if = EXEC_IRQFIQ_DISABLE();

        EXEC_SPINLOCK_LOCK(&task->tc_SpinLock, NULL, SPINLOCK_MODE_WRITE);
        switch (task->tc_State)
        {
        case TS_FROZEN:
            frozen = TRUE;
            break;
        case TS_READY:
            EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskReadySpinLock, NULL, SPINLOCK_MODE_WRITE);
            Remove(&task->tc_Node);
            EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskReadySpinLock);
            set_pending(iet, FALSE);
            iet->iet_FrozenState = TS_READY;
            task->tc_State = TS_FROZEN;
            EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskWaitSpinLock, NULL, SPINLOCK_MODE_WRITE);
            Enqueue(&SysBase->TaskWait, &task->tc_Node);
            EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskWaitSpinLock);
            frozen = TRUE;
            break;
        case TS_WAIT:
            set_pending(iet, FALSE);
            iet->iet_FrozenState = TS_WAIT;
            task->tc_State = TS_FROZEN;
            frozen = TRUE;
            break;
        case TS_RUN:
            set_pending(iet, TRUE);
            if (iet->iet_CpuNumber < 32)
                mask[0] = 1UL << iet->iet_CpuNumber;
            running = TRUE;
            break;
        default:
            break;
        }
        EXEC_SPINLOCK_UNLOCK(&task->tc_SpinLock);
        EXEC_IRQFIQ_RESTORE(__if);

        if (!running)
            return frozen;
        KrnScheduleCPU(mask);
        freeze_pause();
    }
    set_pending(iet, FALSE);
    return FALSE;
}

BOOL exec_ThawOneSMP(struct Task *task)
{
    BOOL thawed = FALSE, ready = FALSE;
    unsigned int __if;

    if (task == NULL || !(task->tc_Flags & TF_ETASK))
        return FALSE;

    __if = EXEC_IRQFIQ_DISABLE();
    EXEC_SPINLOCK_LOCK(&task->tc_SpinLock, NULL, SPINLOCK_MODE_WRITE);
    if (GetIntETask(task) && (__atomic_load_n(&GetIntETask(task)->iet_FreezeFlags,
        __ATOMIC_ACQUIRE) & IETF_RETIRE_PENDING))
    {
        EXEC_SPINLOCK_UNLOCK(&task->tc_SpinLock);
        EXEC_IRQFIQ_RESTORE(__if);
        return FALSE;
    }
    if (task->tc_State == TS_FROZEN)
    {
        thawed = TRUE;
        EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskWaitSpinLock, NULL, SPINLOCK_MODE_WRITE);
        ready = thaw_locked(task);
        EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskWaitSpinLock);
        if (ready)
        {
            task->tc_State = TS_READY;
            EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskReadySpinLock, NULL, SPINLOCK_MODE_WRITE);
            Enqueue(&SysBase->TaskReady, &task->tc_Node);
            EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskReadySpinLock);
        }
    }
    else if (GetIntETask(task))
        set_pending(GetIntETask(task), FALSE);
    EXEC_SPINLOCK_UNLOCK(&task->tc_SpinLock);
    EXEC_IRQFIQ_RESTORE(__if);

    if (ready)
    {
        wake_cpus();
        Reschedule();
    }
    return thawed;
}

#endif /* __AROSEXEC_SMP__ */
