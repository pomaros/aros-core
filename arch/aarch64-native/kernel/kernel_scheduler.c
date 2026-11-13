/*
    Copyright (C) 2015-2026, The AROS Development Team. All rights reserved.
*/

#include <exec/alerts.h>
#include <exec/execbase.h>
#include <exec/lists.h>
#include <proto/exec.h>
#include <proto/kernel.h>

#include <asm/cpu.h>

//#include <kernel_base.h>
struct KernelBase;
#include <kernel_debug.h>
#include <kernel_scheduler.h>

#include <kernel_cpu.h>

#define AROS_NO_ATOMIC_OPERATIONS
#include <exec_platform.h>

#include <aros/types/spinlock_s.h>

#include <etask.h>

#undef bug
#include "exec_intern.h"
#undef bug
#include <kernel_intern.h>

#define DSCHED(x)
#ifndef CORE_SCHEDULER_BUG
#define CORE_SCHEDULER_BUG(...) bug(__VA_ARGS__)
#endif

#ifndef CORE_SCHEDULER_TIME_US
#define CORE_SCHEDULER_TIME_US() __arm_arosintern.ARMI_GetTime()
#define CORE_SCHEDULER_TIME_AVAILABLE() (__arm_arosintern.ARMI_GetTime != NULL)
#endif

/* When the last CPU-usage window was closed (microseconds). */
static ULONG core_TaskUsageStamp = 0;

#if defined(__AROSEXEC_SMP__)
/* iet_CpuAffinity is a cpumask buffer or the TASKAFFINITY_ANY sentinel,
 * never a raw bitmask. NULL is invalid and matches no CPU - masks only
 * count once exec has given every task one (EXECF_CPUAffinity). */
static inline BOOL core_AffinityMatch(struct Task *t, uint32_t cpumask)
{
    void *aff;

#ifdef CORE_SCHEDULER_RESUME_CPU
    /* A context saved inside an access to one CPU's scheduler state resumes
     * there, whatever the affinity says since. */
    {
        int cpu = CORE_SCHEDULER_RESUME_CPU(t);

        if (cpu >= 0)
            return cpumask == (1U << cpu);
    }
#endif
    if (!(PrivExecBase(SysBase)->IntFlags & EXECF_CPUAffinity))
        return TRUE;

    aff = (void *)(IPTR)GetIntETask(t)->iet_CpuAffinity;
    if ((IPTR)aff == TASKAFFINITY_ANY)
        return TRUE;
    if (!aff || (IPTR)aff == TASKAFFINITY_ALL_BUT_SELF)
        return FALSE;

    return (((uint32_t *)aff)[0] & cpumask) != 0;
}

/* Single attempt: everyone else takes task-lock before list-lock, so
 * blocking here (we hold the list lock) would be an AB-BA deadlock. */
static inline BOOL core_TrySpinLockWrite(spinlock_t *lock)
{
    unsigned int lock_value, result;

    asm volatile(
            "1:     ldaxr   %w0, [%2]       \n\t"   // Load the lock value, gaining exclusive access
            "       cbnz    %w0, 2f         \n\t"   // Taken - fail without spinning
            "       stxr    %w1, %w3, [%2]  \n\t"   // Try to exclusively write the lock value
            "       cbnz    %w1, 1b         \n\t"   // Exclusive access lost - re-examine the lock
            "       b       3f              \n\t"
            "2:     clrex                   \n\t"   // Drop the dangling exclusive monitor
            "3:                             \n\t"
            : "=&r"(lock_value), "=&r"(result)
            : "r"(&lock->lock), "r"(SPINLOCKF_WRITE)
            : "memory"
    );

    if (lock_value != 0)
        return FALSE;

    lock->s_Owner = GET_THIS_TASK;
    return TRUE;
}
#endif

/*
 * Wake-up preemption. A task that left Wait (IETF_WOKEN) is first among the
 * ready tasks of its priority; it also takes the CPU from a running task of
 * the same priority that has had it for this long, instead of waiting for
 * that task's quantum to end. Without it a handler woken by a request waits
 * tens of milliseconds whenever every CPU computes. The preempted task
 * keeps what is left of its quantum for its next turn.
 */
#define CORE_SCHEDULER_WAKE_PREEMPT_US 500
#if defined(__AROSEXEC_SMP__) && defined(CORE_SCHEDULER_TIME_US)
static UQUAD core_dispatched_us[32];
static inline BOOL core_WakePreempts(struct Task *next, struct Task *running, int cpunum)
{
    struct IntETask *iet = GetIntETask(next);
    return iet && cpunum < 32 && next->tc_Node.ln_Pri == running->tc_Node.ln_Pri &&
        (__atomic_load_n(&iet->iet_FreezeFlags, __ATOMIC_ACQUIRE) & IETF_WOKEN) &&
        CORE_SCHEDULER_TIME_AVAILABLE() &&
        CORE_SCHEDULER_TIME_US() - core_dispatched_us[cpunum] >= CORE_SCHEDULER_WAKE_PREEMPT_US;
}
#else
#define core_WakePreempts(next, running, cpunum) FALSE
#endif

#if defined(__AROSEXEC_SMP__) && defined(CORE_SCHEDULER_TIME_US)
/*
 * The last scheduling decisions of all CPUs, for a debugger on the host to
 * read (wincompat-aros tools/sched-trace.py reads it from a stopped or
 * running virtual machine): which task each CPU took and gave up, when, and
 * in what state it left it. Nothing in the system reads it. One entry is
 * four stores; the ring is 24 KiB.
 */
#define CORE_TRACE_ENTRIES 1024
#define CORE_TRACE_DISPATCH 1   /* the CPU starts running the task */
#define CORE_TRACE_SWITCH   2   /* the CPU stops running it; state = what it became */
#define CORE_TRACE_IDLE     3   /* the CPU found no task to run */
struct core_trace_entry
{
    UQUAD        us;
    struct Task *task;
    UBYTE        cpu, event, state, flags;  /* flags: 1 quantum over, 2 queued first */
    BYTE         pri;
    UBYTE        pad[3];
};
struct
{
    ULONG                   magic, entries, next, size;
    struct core_trace_entry entry[CORE_TRACE_ENTRIES];
} core_SchedulerTrace = { 0x53434854, CORE_TRACE_ENTRIES, 0, sizeof(struct core_trace_entry) };

static inline void core_Trace(UBYTE event, struct Task *task, UBYTE state, UBYTE flags)
{
    struct core_trace_entry *e;

    if (!CORE_SCHEDULER_TIME_AVAILABLE())
        return;
    e = &core_SchedulerTrace.entry[__atomic_fetch_add(&core_SchedulerTrace.next, 1,
        __ATOMIC_RELAXED) % CORE_TRACE_ENTRIES];
    e->us = CORE_SCHEDULER_TIME_US();
    e->task = task;
    e->cpu = (UBYTE)GetCPUNumber();
    e->event = event;
    e->state = state;
    e->flags = flags;
    e->pri = task ? task->tc_Node.ln_Pri : 0;
}
#else
#define core_Trace(event, task, state, flags) do { } while (0)
#endif

#if defined(__AROSEXEC_SMP__)
/* Is the task this CPU would run next of higher priority than `task`?
 * Called with interrupts off and task->tc_SpinLock held (task lock outer,
 * list lock inner). */
static BOOL core_HigherPriorityReady(struct Task *task)
{
    uint32_t cpumask = (1U << GetCPUNumber());
    struct Task *next;
    BOOL higher = FALSE;

    KrnSpinLock(&PrivExecBase(SysBase)->TaskReadySpinLock, NULL,
                SPINLOCK_MODE_READ);
    for (next = (struct Task *)GetHead(&SysBase->TaskReady); next != NULL;
         next = (struct Task *)GetSucc(next))
    {
        if (core_AffinityMatch(next, cpumask))
        {
            higher = next->tc_Node.ln_Pri > task->tc_Node.ln_Pri;
            break;
        }
    }
    KrnSpinUnLock(&PrivExecBase(SysBase)->TaskReadySpinLock);
    return higher;
}
#endif

/* CORE_SCHEDULER_OFFER(task): a port's way to tell an idle CPU that a ready
 * task it may run is waiting. Without it a task made ready on a busy CPU
 * waited for that CPU while another one stayed idle until its next tick. */
#ifndef CORE_SCHEDULER_OFFER
#define CORE_SCHEDULER_OFFER(task) do { } while (0)
#endif

/* Check if the currently running task on this cpu should be rescheduled */
BOOL core_Schedule(void)
{
#if defined(DEBUG)
    int cpunum = GetCPUNumber();
    (void)cpunum;
#endif
    struct Task *task = GET_THIS_TASK;
    BOOL corereschedule = TRUE;

    DSCHED(bug("[Kernel:%02d] core_Schedule()\n", cpunum));

    FLAG_SCHEDSWITCH_CLEAR;

#if defined(__AROSEXEC_SMP__)
    /* FreezeTasks() on another CPU marked it: switch it out, core_Switch()
     * parks it. */
    if (GetIntETask(task) &&
        (__atomic_load_n(&GetIntETask(task)->iet_FreezeFlags, __ATOMIC_ACQUIRE) & IETF_FREEZE_PENDING))
        return TRUE;
#endif

    /* If task has pending exception, reschedule it so that the dispatcher may handle the exception */
    if (!(task->tc_Flags & TF_EXCEPT))
    {
#if defined(__AROSEXEC_SMP__)
        /* IRQ+FIQ off: the IPI and the timer IRQ take this lock as a
         * write, and re-entering it self-deadlocks this CPU. */
        unsigned int __if = EXEC_IRQFIQ_DISABLE();
        KrnSpinLock(&PrivExecBase(SysBase)->TaskReadySpinLock, NULL,
                    SPINLOCK_MODE_READ);
#endif
        /* Is the TaskReady empty? If yes, then the running task is the only one. Let it work */
        if (IsListEmpty(&SysBase->TaskReady))
            corereschedule = FALSE;
        else
        {
            struct Task *nexttask;
#if defined(__AROSEXEC_SMP__)
            int cpunum = GetCPUNumber();
            uint32_t cpumask = (1U << cpunum);
#endif
            /*
                    If there are tasks ready for this cpu that have equal or lower priority,
                    and the current task has used its alloted time - reschedule so they can run
                */
            for (nexttask = (struct Task *)GetHead(&SysBase->TaskReady); nexttask != NULL; nexttask = (struct Task *)GetSucc(nexttask))
            {
#if defined(__AROSEXEC_SMP__)
                if (core_AffinityMatch(nexttask, cpumask))
                {
#endif
                    if (nexttask->tc_Node.ln_Pri <= task->tc_Node.ln_Pri)
                    {
                        /* If the running task did not used it's whole quantum yet, let it work,
                         * unless a task that just woke has waited long enough for it */
                        if (!FLAG_SCHEDQUANTUM_ISSET && !core_WakePreempts(nexttask, task, cpunum))
                        {
                            corereschedule = FALSE;
                            /* It waits for this CPU: another may be idle. */
                            CORE_SCHEDULER_OFFER(nexttask);
                        }
                    }
                    break;
#if defined(__AROSEXEC_SMP__)
                }
#endif
            }
        }
#if defined(__AROSEXEC_SMP__)
        KrnSpinUnLock(&PrivExecBase(SysBase)->TaskReadySpinLock);
        EXEC_IRQFIQ_RESTORE(__if);
#endif
    }

    DSCHED
        (
            if (corereschedule)
                CORE_SCHEDULER_BUG("[Kernel:%02d] '%s' @ 0x%p needs rescheduled ..\n", cpunum, task->tc_Node.ln_Name, task);
        )

    return corereschedule;
}

/* Switch the currently running task on this cpu to ready state */
void core_Switch(void)
{
#if defined(DEBUG)
    int cpunum = GetCPUNumber();
#endif
    struct Task *task = GET_THIS_TASK;
#if defined(__AROSEXEC_SMP__)
    unsigned int __if;
#endif

    DSCHED(bug("[Kernel:%02d] core_Switch(%08x)\n", cpunum, task->tc_State));

    /* Every outgoing task, not only TS_RUN: Wait() switches away in
     * TS_WAIT holding a Disable() level. */
    task->tc_IDNestCnt = IDNESTCOUNT_GET;

    /* Carry the unused slice with the task, or a task preempted more
     * often than the heartbeat ticks never expires its quantum. */
    if (GetIntETask(task))
        GetIntETask(task)->iet_QuantumLeft = SCHEDELAPSED_GET;

    if (task->tc_State != TS_RUN)
        core_Trace(CORE_TRACE_SWITCH, task, task->tc_State, 0);
    if (task->tc_State == TS_RUN)
    {
        DSCHED(bug("[Kernel:%02d] Switching away from '%s' @ 0x%p\n", cpunum, task->tc_Node.ln_Name, task));
#if defined(__AROSEXEC_SMP__)
        /* tc_SpinLock across the whole RUN->READY (state, list) change, or
         * an observer can see TS_READY while the task is on no list.
         * Task-lock outer, list-locks inner. IRQ+FIQ off: the IPI and the
         * timer IRQ take this same lock. */
        __if = EXEC_IRQFIQ_DISABLE();
        KrnSpinLock(&task->tc_SpinLock, NULL, SPINLOCK_MODE_WRITE);
        exec_TaskRemoveRunning(task);
#endif
        task->tc_State = TS_READY;

        /* if the current task has gone out of stack bounds, suspend it to prevent further damage to the system */
        if (task->tc_SPReg <= task->tc_SPLower || task->tc_SPReg > task->tc_SPUpper)
        {
            CORE_SCHEDULER_BUG("[Kernel:%02d] '%s' @ 0x%p went out of stack limits\n", cpunum, task->tc_Node.ln_Name, task);
            CORE_SCHEDULER_BUG("[Kernel:%02d]  - Lower 0x%p, upper 0x%p, SP 0x%p\n", cpunum, task->tc_SPLower, task->tc_SPUpper, task->tc_SPReg);

            task->tc_SigWait    = 0;
            task->tc_State      = TS_WAIT;
#if defined(__AROSEXEC_SMP__)
            exec_TaskEnqueueWait(task);
#else
            Enqueue(&SysBase->TaskWait, &task->tc_Node);
#endif

            Alert(AN_StackProbe);
        }

        if (task->tc_Flags & TF_SWITCH)
            AROS_UFC1NR(void, task->tc_Switch, AROS_UFCA(struct ExecBase *, SysBase, A6));

#if defined(__AROSEXEC_SMP__)
        /* Marked by FreezeTasks() while it ran: parked frozen on the wait
         * list, as the freeze leaves a ready task, under the same lock. */
        if (task->tc_State == TS_READY && GetIntETask(task) &&
            (__atomic_load_n(&GetIntETask(task)->iet_FreezeFlags, __ATOMIC_ACQUIRE) & IETF_FREEZE_PENDING))
        {
            __atomic_fetch_and(&GetIntETask(task)->iet_FreezeFlags, (UBYTE)~IETF_FREEZE_PENDING,
                               __ATOMIC_ACQ_REL);
            GetIntETask(task)->iet_FrozenState = TS_READY;
            task->tc_State = TS_FROZEN;
            exec_TaskEnqueueWait(task);
        }
#endif
#ifdef CORE_SCHEDULER_PARK
        if (task->tc_State==TS_READY && CORE_SCHEDULER_PARK(task))
        {
            task->tc_State=TS_WAIT;
#if defined(__AROSEXEC_SMP__)
            exec_TaskEnqueueWait(task);
#else
            Enqueue(&SysBase->TaskWait,&task->tc_Node);
#endif
        }
#endif
        if (task->tc_State == TS_READY)
        {
            DSCHED(bug("[Kernel:%02d] Setting '%s' @ 0x%p as ready\n", cpunum, task->tc_Node.ln_Name, task));
#if defined(__AROSEXEC_SMP__)
            /* A task that loses the CPU to one of higher priority has not
             * had its turn: it goes back to the head of its own priority
             * and runs again when the other is done. At the tail, a caller
             * preempted by the handler it had just signalled waited behind
             * every task of its priority, a whole quantum each, for an
             * answer that took microseconds. A task whose quantum is over,
             * or that gives the CPU to an equal, queues at the tail. */
            if (!FLAG_SCHEDQUANTUM_ISSET && core_HigherPriorityReady(task))
            {
                core_Trace(CORE_TRACE_SWITCH, task, TS_READY, 2);
                exec_TaskEnqueueReadyFirst(task);
            }
            else
            {
                core_Trace(CORE_TRACE_SWITCH, task, TS_READY, FLAG_SCHEDQUANTUM_ISSET ? 1 : 0);
                exec_TaskEnqueueReady(task);
            }
            /* Ready and without a CPU: an idle one can take it at once. */
            CORE_SCHEDULER_OFFER(task);
#else
            Enqueue(&SysBase->TaskReady, &task->tc_Node);
#endif
        }
#if defined(__AROSEXEC_SMP__)
        KrnSpinUnLock(&task->tc_SpinLock);
        EXEC_IRQFIQ_RESTORE(__if);
#endif
    }
}

/* Dispatch a "new" ready task on this cpu */
struct Task *core_Dispatch(void)
{
    struct Task *newtask;
    struct Task *task = GET_THIS_TASK;
    BOOL taskStateLocked = FALSE;
#if defined(__AROSEXEC_SMP__) || defined(DEBUG)
    int cpunum = GetCPUNumber();
    (void)cpunum;
#endif
#if defined(__AROSEXEC_SMP__)
    uint32_t cpumask = (1U << cpunum);
    BOOL sawContended;
    /* IRQ+FIQ off for the whole dispatch: it holds the scheduler list
     * locks and tc_SpinLock, which the IPI and the timer IRQ also take.
     * Nestable across the recursive call below. */
    unsigned int __if = EXEC_IRQFIQ_DISABLE();
#endif

    DSCHED(bug("[Kernel:%02d] core_Dispatch()\n", cpunum));

#ifdef CORE_SCHEDULER_PARK
dispatch_select:
#endif
#if defined(__AROSEXEC_SMP__)
dispatch_rescan:
    sawContended = FALSE;
    KrnSpinLock(&PrivExecBase(SysBase)->TaskReadySpinLock, NULL,
                SPINLOCK_MODE_WRITE);
#endif
    for (newtask = (struct Task *)GetHead(&SysBase->TaskReady); newtask != NULL; newtask = (struct Task *)GetSucc(newtask))
    {
#if defined(__AROSEXEC_SMP__)
        if (core_AffinityMatch(newtask, cpumask))
        {
            /* Lock before pulling it off the list, so the (state, list)
             * change is atomic to anyone trusting tc_State. */
            if (!core_TrySpinLockWrite(&newtask->tc_SpinLock))
            {
                sawContended = TRUE;
                continue;
            }
            taskStateLocked = TRUE;
            Remove(&newtask->tc_Node);
            break;
        }
#else
        Remove(&newtask->tc_Node);
        break;
#endif
    }
#if defined(__AROSEXEC_SMP__)
    KrnSpinUnLock(&PrivExecBase(SysBase)->TaskReadySpinLock);

    /* Every candidate was momentarily locked elsewhere - rescan rather
     * than idle with runnable work on the list. */
    if (!newtask && sawContended)
        goto dispatch_rescan;
#endif

#if !defined(__AROSEXEC_SMP__)
    if ((!newtask) && (task) && (task->tc_State != TS_WAIT))
        newtask = task;
#else
    (void)task;
#endif

    if (newtask != NULL)
    {
#ifdef CORE_SCHEDULER_PARK
        /* Native signals can make a parked task ready. Recheck while the
         * task lock still serializes a concurrent wake, before publishing
         * ThisTask or allowing any user instruction. */
        if (newtask->tc_State==TS_READY && CORE_SCHEDULER_PARK(newtask))
        {
            newtask->tc_State=TS_WAIT;
#if defined(__AROSEXEC_SMP__)
            exec_TaskEnqueueWait(newtask);
            KrnSpinUnLock(&newtask->tc_SpinLock);
            taskStateLocked=FALSE;
#else
            Enqueue(&SysBase->TaskWait,&newtask->tc_Node);
#endif
            goto dispatch_select;
        }
#endif
        if (newtask->tc_State == TS_READY || newtask->tc_State == TS_RUN)
        {
            DSCHED(bug("[Kernel:%02d] Preparing to run '%s' @ 0x%p\n",
                cpunum, newtask->tc_Node.ln_Name, newtask));

            SysBase->DispCount++;
            IDNESTCOUNT_SET(newtask->tc_IDNestCnt);
            SET_THIS_TASK(newtask);
            core_Trace(CORE_TRACE_DISPATCH, newtask, newtask->tc_State, 0);
#if defined(__AROSEXEC_SMP__) && defined(CORE_SCHEDULER_TIME_US)
            if (GetIntETask(newtask))
                __atomic_fetch_and(&GetIntETask(newtask)->iet_FreezeFlags, (UBYTE)~IETF_WOKEN, __ATOMIC_RELEASE);
            if (cpunum < 32 && CORE_SCHEDULER_TIME_AVAILABLE())
                core_dispatched_us[cpunum] = CORE_SCHEDULER_TIME_US();
#endif
            /* Only a fresh slice when the task changes or used its own,
             * or a fast-dispatching core never expires a quantum. */
            {
                struct IntETask *iet = GetIntETask(newtask);
                ULONG left = iet ? iet->iet_QuantumLeft : 0;

                SCHEDELAPSED_SET(left ? left : SCHEDQUANTUM_GET);
            }
            FLAG_SCHEDQUANTUM_CLEAR;

            /* Check the stack of the task we are about to launch. */
            if ((newtask->tc_SPReg <= newtask->tc_SPLower) ||
                (newtask->tc_SPReg > newtask->tc_SPUpper))
            {
#if defined(__AROSEXEC_SMP__)
                if (!taskStateLocked)
                {
                    KrnSpinLock(&newtask->tc_SpinLock, NULL, SPINLOCK_MODE_WRITE);
                    taskStateLocked = TRUE;
                }
#endif
                newtask->tc_State     = TS_WAIT;
            }
            else {
#if defined(__AROSEXEC_SMP__)
                /* Publish ownership while tc_SpinLock still excludes Signal. */
                GetIntETask(newtask)->iet_CpuNumber = cpunum;
#endif
                newtask->tc_State = TS_RUN;
            }
        }

        BOOL launchtask = TRUE;
        if (newtask->tc_State == TS_WAIT)
        {
#if defined(__AROSEXEC_SMP__)
            if (!taskStateLocked)
            {
                KrnSpinLock(&newtask->tc_SpinLock, NULL, SPINLOCK_MODE_WRITE);
                taskStateLocked = TRUE;
            }
#endif
#if defined(__AROSEXEC_SMP__)
            exec_TaskRemoveRunning(newtask);
            KrnSpinLock(&PrivExecBase(SysBase)->TaskWaitSpinLock, NULL,
                        SPINLOCK_MODE_WRITE);
#endif
            Enqueue(&SysBase->TaskWait, &newtask->tc_Node);
#if defined(__AROSEXEC_SMP__)
            KrnSpinUnLock(&PrivExecBase(SysBase)->TaskWaitSpinLock);
            KrnSpinUnLock(&newtask->tc_SpinLock);
            taskStateLocked = FALSE;
#endif
            launchtask = FALSE;
        }

#if defined(__AROSEXEC_SMP__)
        /* Launch path; the TS_WAIT block already unlocked. */
        if (taskStateLocked)
        {
            KrnSpinUnLock(&newtask->tc_SpinLock);
            taskStateLocked = FALSE;
        }
#endif

        if (!launchtask)
        {
            /* if the new task shouldn't run - force a reschedule */
            DSCHED(bug("[Kernel:%02d] Skipping '%s' @ 0x%p (state %08x)\n", cpunum, newtask->tc_Node.ln_Name, newtask, newtask->tc_State));

            core_Switch();
            newtask = core_Dispatch();
        }
        else
        {
            DSCHED(bug("[Kernel:%02d] Launching '%s' @ 0x%p (state %08x)\n", cpunum, newtask->tc_Node.ln_Name, newtask, newtask->tc_State));
        }
    }
    else
    {
        /* Go idle if there is nothing to do */
        DSCHED(bug("[Kernel:%02d] No ready Task(s) - entering sleep mode\n", cpunum));

        /*
         * Idle counter is incremented every time when we enter here,
         * not only once. This is correct.
         */
        SysBase->IdleCount++;
        FLAG_SCHEDSWITCH_SET;
    }

#if defined(__AROSEXEC_SMP__)
    EXEC_IRQFIQ_RESTORE(__if);
#endif
    return newtask;
}

/*
 * Usage = growth of iet_private2 (cumulative busy us, kept by cpu_Switch)
 * over the window, scaled to 0..0xffffffff. The field is not reset -
 * KATTR_CPULoad reads it too - so keep the previous sample per task.
 * 32-bit maths throughout, for the counter's ~71 minute wrap.
 */
static void core_TaskUsageWindow(struct Task *t, ULONG now)
{
    struct IntETask *iet;
    ULONG busyNow, window;

    if (!(t->tc_Flags & TF_ETASK) || !t->tc_UnionETask.tc_ETask)
        return;

    iet = IntETask(t->tc_UnionETask.tc_ETask);
    busyNow = (ULONG)iet->iet_private2;

    /* A running task has not committed its current slice yet.
     * iet_private1 == 0 means never dispatched. */
    if ((t->tc_State == TS_RUN) && iet->iet_private1)
        busyNow += now - (ULONG)iet->iet_private1;

    window = now - (ULONG)iet->iet_LastUsageStamp;

    if (iet->iet_LastUsageStamp && window)
    {
        ULONG busy = busyNow - (ULONG)iet->iet_LastBusy;

        if (busy >= window)
            iet->iet_CpuUsage = 0xffffffff;
        else
            iet->iet_CpuUsage = (ULONG)(((UQUAD)busy << 32) / window);
    }
    else
        iet->iet_CpuUsage = 0;      /* first sample */

    iet->iet_LastBusy = busyNow;
    iet->iet_LastUsageStamp = now;
}

/*
 * Refresh iet_CpuUsage for TaskTag_CPUUsage. Driven from the VBlank IRQ,
 * not the per-core CNTP heartbeat: that is an FIQ, and the list locks
 * below are also taken by the IPI handler in FIQ context.
 */
void core_TaskCPUUsage(void)
{
    struct Task *t;
    ULONG now;
#if defined(__AROSEXEC_SMP__)
    unsigned int __if;
#endif

    if (!SysBase || !CORE_SCHEDULER_TIME_AVAILABLE())
        return;

    now = (ULONG)CORE_SCHEDULER_TIME_US();
    if (core_TaskUsageStamp && ((now - core_TaskUsageStamp) < TASKUSAGE_WINDOW))
        return;
    core_TaskUsageStamp = now;

#if defined(__AROSEXEC_SMP__)
    __if = EXEC_IRQFIQ_DISABLE();

    KrnSpinLock(&PrivExecBase(SysBase)->TaskRunningSpinLock, NULL, SPINLOCK_MODE_READ);
    ForeachNode(&PrivExecBase(SysBase)->TaskRunning, t)
        core_TaskUsageWindow(t, now);
    KrnSpinUnLock(&PrivExecBase(SysBase)->TaskRunningSpinLock);

    KrnSpinLock(&PrivExecBase(SysBase)->TaskSpinningLock, NULL, SPINLOCK_MODE_READ);
    ForeachNode(&PrivExecBase(SysBase)->TaskSpinning, t)
        core_TaskUsageWindow(t, now);
    KrnSpinUnLock(&PrivExecBase(SysBase)->TaskSpinningLock);

    KrnSpinLock(&PrivExecBase(SysBase)->TaskReadySpinLock, NULL, SPINLOCK_MODE_READ);
#else
    t = GET_THIS_TASK;
    if (t)
        core_TaskUsageWindow(t, now);
#endif
    ForeachNode(&SysBase->TaskReady, t)
        core_TaskUsageWindow(t, now);
#if defined(__AROSEXEC_SMP__)
    KrnSpinUnLock(&PrivExecBase(SysBase)->TaskReadySpinLock);

    KrnSpinLock(&PrivExecBase(SysBase)->TaskWaitSpinLock, NULL, SPINLOCK_MODE_READ);
#endif
    ForeachNode(&SysBase->TaskWait, t)
        core_TaskUsageWindow(t, now);
#if defined(__AROSEXEC_SMP__)
    KrnSpinUnLock(&PrivExecBase(SysBase)->TaskWaitSpinLock);

    EXEC_IRQFIQ_RESTORE(__if);
#endif
}
