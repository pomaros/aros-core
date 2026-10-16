/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1.

    Desc: Keep a task running through FreezeTasks().
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

        AROS_LH2(BOOL, SetTaskFreezeExempt,

/*  SYNOPSIS */
        AROS_LHA(struct Task *, task,   A0),
        AROS_LHA(BOOL,          exempt, D0),

/*  LOCATION */
        struct ExecBase *, SysBase, 183, Exec)

/*  FUNCTION
        Mark a task so that FreezeTasks() leaves it running, or clear the
        mark. Driver tasks and handlers that the sleep transition needs
        mark themselves; everything else is frozen.

    INPUTS
        task   - The task, or NULL for the calling task.
        exempt - TRUE to exempt, FALSE to freeze with the others.

    RESULT
        The previous setting.

    SEE ALSO
        FreezeTasks(), ThawTasks()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct IntETask *iet;
    BOOL old = FALSE;

    if (task == NULL)
        task = GET_THIS_TASK;

    if (!(task->tc_Flags & TF_ETASK))
        return FALSE;

    iet = IntETask(task->tc_UnionETask.tc_ETask);
    if (iet == NULL)
        return FALSE;

    Disable();
    old = (iet->iet_FreezeFlags & IETF_FREEZE_EXEMPT) != 0;
    if (exempt)
        iet->iet_FreezeFlags |= IETF_FREEZE_EXEMPT;
    else
        iet->iet_FreezeFlags &= ~IETF_FREEZE_EXEMPT;
    Enable();

    return old;

    AROS_LIBFUNC_EXIT
}
