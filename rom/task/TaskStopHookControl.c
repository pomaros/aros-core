/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#include <proto/exec.h>
#include "task_intern.h"
#include "task_stop.h"

/* Registration never allocates. The caller retains hook/code ownership.
 * Holding the same lock as dispatch makes successful removal a join. */
AROS_LH2(BOOL, TaskStopHookControl,
    AROS_LHA(struct Hook *, hook, A0),
    AROS_LHA(BOOL, add, D0),
    struct TaskResBase *, TaskResBase, 23, Task)
{
    AROS_LIBFUNC_INIT
    struct Hook *current;
    BOOL found = FALSE;
#if !defined(EXEC_SELF_EXIT_HANDOFF)
    if (add) return FALSE;
#endif
    if (!hook || IDNESTCOUNT_GET >= 0 || TDNESTCOUNT_GET >= 0)
        return FALSE;
    if (add && (SysBase->LibNode.lib_Version < 51 ||
        (SysBase->LibNode.lib_Version == 51 && SysBase->LibNode.lib_Revision < 12)))
        return FALSE;
    task_stop_lock(TaskResBase);
    ForeachNode(&TaskResBase->trb_StopHooks, current)
        if (current == hook) { found = TRUE; break; }
    if (add && !found)
        AddTail((struct List *)&TaskResBase->trb_StopHooks,
                (struct Node *)&hook->h_MinNode);
    else if (!add && found)
        Remove((struct Node *)&hook->h_MinNode);
    task_stop_unlock(TaskResBase);
    return add ? !found : found;
    AROS_LIBFUNC_EXIT
}
