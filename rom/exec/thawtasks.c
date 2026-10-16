/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1.

    Desc: Restart the tasks FreezeTasks() stopped.
*/

#define DEBUG 0
#include <aros/debug.h>

#include <exec/execbase.h>
#include <exec/tasks.h>
#include <aros/libcall.h>
#include <proto/exec.h>

#include "exec_intern.h"
#include "etask.h"

/*****************************************************************************

    NAME */

        AROS_LH1(ULONG, ThawTasks,

/*  SYNOPSIS */
        AROS_LHA(ULONG, flags, D0),

/*  LOCATION */
        struct ExecBase *, SysBase, 182, Exec)

/*  FUNCTION
        Restart every task FreezeTasks() stopped. A task frozen while ready
        becomes ready again; a task frozen in Wait() becomes ready if one of
        the signals it waits for arrived meanwhile, and keeps waiting
        otherwise. The caller is rescheduled if a thawed task outranks it.

    INPUTS
        flags - reserved, pass 0.

    RESULT
        The number of tasks thawed, or FREEZE_UNSUPPORTED on an SMP build.

    NOTES
        Relative timer.device requests of the frozen tasks measure awake
        time only when the caller paused the up time around the freeze;
        see timer.device/PauseUpTime().

    SEE ALSO
        FreezeTasks(), SetTaskFreezeExempt()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct Task *task, *next;
    ULONG thawed = 0;

    D(bug("[Exec] ThawTasks(0x%lx)\n", flags);)

#if defined(__AROSEXEC_SMP__)
    /* FreezeTasks() refused, so there is nothing to thaw. */
    return FREEZE_UNSUPPORTED;
#else
    Disable();
    ForeachNodeSafe(&SysBase->TaskWait, task, next)
    {
        struct IntETask *iet;

        if (task->tc_State != TS_FROZEN)
            continue;

        iet = IntETask(task->tc_UnionETask.tc_ETask);
        thawed++;

        if (iet != NULL && iet->iet_FrozenState == TS_WAIT &&
            !(task->tc_SigRecvd & (task->tc_SigWait | task->tc_SigExcept)))
        {
            /* Nothing arrived: back to the wait it was in. */
            task->tc_State = TS_WAIT;
            continue;
        }

        /* Was ready, or its signal came while frozen: ready again. */
        Remove(&task->tc_Node);
        task->tc_State = TS_READY;
        Enqueue(&SysBase->TaskReady, &task->tc_Node);
    }
    Enable();

    if (thawed)
        Reschedule();

    D(bug("[Exec] ThawTasks: %lu tasks thawed\n", thawed);)

    return thawed;
#endif

    AROS_LIBFUNC_EXIT
}
