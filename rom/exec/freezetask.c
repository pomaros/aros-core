/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1.

    Desc: Stop one task in place, as FreezeTasks() stops them all.
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

        AROS_LH1(BOOL, FreezeTask,

/*  SYNOPSIS */
        AROS_LHA(struct Task *, task, A0),

/*  LOCATION */
        struct ExecBase *, SysBase, 184, Exec)

/*  FUNCTION
        Stop one ready or waiting task in place with the state TS_FROZEN,
        exactly as FreezeTasks() does for every task. Used by a scheduled
        wake to put its requester back to sleep after the wake.

    INPUTS
        task - a task other than the caller.

    RESULT
        TRUE when the task is frozen after the call (it was already, or it
        was ready or waiting); FALSE for the caller, a removed task, or on
        an SMP build, where nothing is touched.

    SEE ALSO
        ThawTask(), FreezeTasks()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

#if defined(__AROSEXEC_SMP__)
    return FALSE;
#else
    struct IntETask *iet;
    BOOL frozen = FALSE;

    if (task == NULL || task == GET_THIS_TASK || !(task->tc_Flags & TF_ETASK))
        return FALSE;
    iet = IntETask(task->tc_UnionETask.tc_ETask);
    if (iet == NULL)
        return FALSE;

    Disable();
    switch (task->tc_State)
    {
    case TS_FROZEN:
        frozen = TRUE;
        break;
    case TS_READY:
        Remove(&task->tc_Node);
        iet->iet_FrozenState = TS_READY;
        task->tc_State = TS_FROZEN;
        Enqueue(&SysBase->TaskWait, &task->tc_Node);
        frozen = TRUE;
        break;
    case TS_WAIT:
        iet->iet_FrozenState = TS_WAIT;
        task->tc_State = TS_FROZEN;
        frozen = TRUE;
        break;
    default:
        break;
    }
    Enable();
    return frozen;
#endif

    AROS_LIBFUNC_EXIT
}
