/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1.

    Desc: Restart one frozen task, as ThawTasks() restarts them all.
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

        AROS_LH1(BOOL, ThawTask,

/*  SYNOPSIS */
        AROS_LHA(struct Task *, task, A0),

/*  LOCATION */
        struct ExecBase *, SysBase, 185, Exec)

/*  FUNCTION
        Restart one task stopped by FreezeTasks() or FreezeTask(), with the
        rule ThawTasks() applies: ready again when it was ready or a signal
        it waits for arrived, back in its Wait() otherwise. The other frozen
        tasks stay frozen. Used by a scheduled wake to run its requester
        alone while the machine sleeps.

    INPUTS
        task - the frozen task.

    RESULT
        TRUE when the task was frozen and is thawed; FALSE otherwise, and on
        an SMP build.

    SEE ALSO
        FreezeTask(), ThawTasks()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

#if defined(__AROSEXEC_SMP__)
    return exec_ThawOneSMP(task);
#else
    struct IntETask *iet;
    BOOL thawed = FALSE;

    if (task == NULL || !(task->tc_Flags & TF_ETASK))
        return FALSE;
    iet = IntETask(task->tc_UnionETask.tc_ETask);

    Disable();
    if (task->tc_State == TS_FROZEN)
    {
        thawed = TRUE;
        if (iet != NULL && iet->iet_FrozenState == TS_WAIT &&
            !(task->tc_SigRecvd & (task->tc_SigWait | task->tc_SigExcept)))
            task->tc_State = TS_WAIT;
        else
        {
            Remove(&task->tc_Node);
            task->tc_State = TS_READY;
            Enqueue(&SysBase->TaskReady, &task->tc_Node);
        }
    }
    Enable();
    if (thawed)
        Reschedule();
    return thawed;
#endif

    AROS_LIBFUNC_EXIT
}
