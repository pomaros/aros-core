/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#include <proto/exec.h>
#include "task_intern.h"
#if defined(EXEC_SELF_EXIT_HANDOFF)
#include "task_stop.h"
#endif

/* Exec service handoff only. This API does not stop a Task; its caller must
 * already own a detached Task allocation until dispatch returns. */
AROS_LH1(void, NotifyTaskStopped,
    AROS_LHA(struct Task *, task, A0),
    struct TaskResBase *, TaskResBase, 24, Task)
{
    AROS_LIBFUNC_INIT
#if defined(EXEC_SELF_EXIT_HANDOFF)
    struct Hook *hook;
    if (!task || task->tc_State != TS_INVALID || task == FindTask(NULL) ||
        !PrivExecBase(SysBase)->ServicePort ||
        FindTask(NULL) != PrivExecBase(SysBase)->ServicePort->mp_SigTask ||
        IDNESTCOUNT_GET >= 0 || TDNESTCOUNT_GET >= 0)
        return;
    task_stop_lock(TaskResBase);
    ForeachNode(&TaskResBase->trb_StopHooks, hook)
        CALLHOOKPKT(hook, task, NULL);
    task_stop_unlock(TaskResBase);
#else
    (void)task;
    (void)TaskResBase;
#endif
    AROS_LIBFUNC_EXIT
}
