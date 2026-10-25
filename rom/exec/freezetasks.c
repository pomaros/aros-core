/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1.

    Desc: Stop every task except the caller and the exempt ones.
*/

#define DEBUG 0
#include <aros/debug.h>

#include <exec/execbase.h>
#include <exec/tasks.h>
#include <aros/libcall.h>
#include <proto/exec.h>

#include "exec_intern.h"
#include "etask.h"
#include "freeze_smp.h"

/*****************************************************************************

    NAME */

        AROS_LH1(ULONG, FreezeTasks,

/*  SYNOPSIS */
        AROS_LHA(ULONG, flags, D0),

/*  LOCATION */
        struct ExecBase *, SysBase, 181, Exec)

/*  FUNCTION
        Stop every task in the system except the calling task and the tasks
        marked with SetTaskFreezeExempt(). A stopped task keeps its state:
        a ready task goes back to the ready list at ThawTasks(), a waiting
        task resumes its Wait(), with the signals that arrived while it was
        frozen. Interrupts keep running, so devices complete their requests
        and the replies wait in the frozen tasks' ports.

        The caller must not touch anything a frozen task owns between
        FreezeTasks() and ThawTasks(): a DOS handler is a task, so no file
        or console I/O, and no Wait() on a semaphore a frozen task holds.

    INPUTS
        flags - reserved, pass 0.

    RESULT
        The number of tasks frozen, or FREEZE_UNSUPPORTED when nothing could
        be frozen and nothing stays frozen.

    NOTES
        This is the machine-wide freeze of the power feature: the sleep
        transition stops every program through it, and the programs need
        no change. Private to this tree.

        On an SMP build a task running on another CPU is marked and that
        CPU rescheduled: the scheduler parks the task when it switches it
        out. When the tasks do not all stop in time (one that stays in
        Forbid() on another CPU), the ones already frozen are thawed and
        the call returns FREEZE_UNSUPPORTED.

    SEE ALSO
        ThawTasks(), SetTaskFreezeExempt()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct Task *thisTask = GET_THIS_TASK;
    struct Task *task, *next;
    ULONG frozen = 0;

    D(bug("[Exec] FreezeTasks(0x%lx) from '%s'\n", flags, thisTask->tc_Node.ln_Name);)

#if defined(__AROSEXEC_SMP__)
    (void)task; (void)next; (void)frozen;
    return exec_FreezeAllSMP();
#else
    Disable();

    /* Ready tasks: off the ready list, parked on the wait list. */
    ForeachNodeSafe(&SysBase->TaskReady, task, next)
    {
        struct IntETask *iet = IntETask(task->tc_UnionETask.tc_ETask);

        if (task == thisTask || task->tc_State != TS_READY ||
            !(task->tc_Flags & TF_ETASK) || iet == NULL ||
            (iet->iet_FreezeFlags & IETF_FREEZE_EXEMPT))
            continue;

        Remove(&task->tc_Node);
        iet->iet_FrozenState = TS_READY;
        task->tc_State = TS_FROZEN;
        Enqueue(&SysBase->TaskWait, &task->tc_Node);
        frozen++;
    }

    /* Waiting tasks: stay where they are, with a state Signal() ignores. */
    ForeachNode(&SysBase->TaskWait, task)
    {
        struct IntETask *iet = IntETask(task->tc_UnionETask.tc_ETask);

        if (task == thisTask || task->tc_State != TS_WAIT ||
            !(task->tc_Flags & TF_ETASK) || iet == NULL ||
            (iet->iet_FreezeFlags & IETF_FREEZE_EXEMPT))
            continue;

        iet->iet_FrozenState = TS_WAIT;
        task->tc_State = TS_FROZEN;
        frozen++;
    }

    Enable();

    D(bug("[Exec] FreezeTasks: %lu tasks frozen\n", frozen);)

    return frozen;
#endif

    AROS_LIBFUNC_EXIT
}
