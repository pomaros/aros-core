/*
    Copyright (C) 1995-2017, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
*/

#define DEBUG 0

#include <exec/execbase.h>
#include <exec/tasks.h>

#include "exec_intern.h"
#include "exec_util.h"

/*****************************************************************************

    NAME */
#include <proto/exec.h>

        AROS_LH1(struct Task *, FindTaskByPID,

/*  SYNOPSIS */
        AROS_LHA(ULONG, id, D0),

/*  LOCATION */
        struct ExecBase *, SysBase, 166, Exec)

/*  FUNCTION
        Find a task whose et_UniqueID field matches. SMP kernels use an
        identity registry independent of scheduler-list transitions.
        Reserved identities become visible after context initialization and
        disappear before task context deletion.

    INPUTS
        id      -   The task ID to match.

    RESULT
        Address of the Task control structure that matches, or
        NULL otherwise.

    NOTES
        This function is source-compatible with MorphOS. The returned pointer
        does not retain the task: callers must separately protect its lifetime
        before dereferencing it. Forbid does not provide that protection on SMP.

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS

******************************************************************************/
{
    AROS_LIBFUNC_INIT

#if defined(__AROSEXEC_SMP__)
    return Exec_FindRegisteredTask(id, SysBase);
#else
    struct Task *t;
    struct ETask *et;

    Disable();
    t = GET_THIS_TASK;
    if (t != NULL && (et = GetETask(t)) != NULL && et->et_UniqueID == id)
    {
        Enable();
        return t;
    }
    ForeachNode(&SysBase->TaskReady, t)
    {
        et = GetETask(t);
        if (et != NULL && et->et_UniqueID == id)
        {
            Enable();
            return t;
        }
    }
    ForeachNode(&SysBase->TaskWait, t)
    {
        et = GetETask(t);
        if (et != NULL && et->et_UniqueID == id)
        {
            Enable();
            return t;
        }
    }
    Enable();
    return NULL;
#endif

    AROS_LIBFUNC_EXIT
}
