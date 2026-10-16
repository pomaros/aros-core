/* Copyright (C) 2013-2026, The AROS Development Team. All rights reserved.
 * Shared AArch64 scheduler-list transitions, from the native SMP port. */
#include <exec/tasks.h>
#include <proto/exec.h>
#include <proto/kernel.h>
#include <exec_intern.h>
#include <etask.h>
/* No TS_SPIN machinery here: our KrnSpinLock never calls its failhook,
 * so spinners just spin. all-pc has the wired-up version. */

/*
 * Move a task to the list matching newState, for krnSysCallReschedTask.
 * Callers MUST NOT hold tc_SpinLock - we take it, so the whole
 * (read state, pick list, mutate, write state) is atomic to observers.
 * Lock order: tc_SpinLock outer, list-locks inner.
 */
void Exec_ReschedTask(struct Task *task, ULONG newState)
{
    spinlock_t *fromLock = NULL;
    /* Raw masking, not Disable(): this also runs from the FIQ handler,
     * where Disable()'s syscall would nest an exception. */
    unsigned int __if = EXEC_IRQFIQ_DISABLE();

    EXEC_SPINLOCK_LOCK(&task->tc_SpinLock, NULL, SPINLOCK_MODE_WRITE);

    if (newState == TS_READY)
    {
        /* Only migrate a parked or freshly added task: another CPU may
         * have won the wake race already. The signal bits are set, so a
         * running task sees them anyway. */
        switch (task->tc_State)
        {
            case TS_WAIT:
            case TS_INVALID:
            case TS_ADDED:
                break;
            default:
                EXEC_SPINLOCK_UNLOCK(&task->tc_SpinLock);
                EXEC_IRQFIQ_RESTORE(__if);
                return;
        }
    }

    switch (task->tc_State)
    {
        case TS_RUN:
            fromLock = &PrivExecBase(SysBase)->TaskRunningSpinLock;
            break;
        case TS_READY:
            fromLock = &PrivExecBase(SysBase)->TaskReadySpinLock;
            break;
        case TS_WAIT:
            fromLock = &PrivExecBase(SysBase)->TaskWaitSpinLock;
            break;
        default:
            /* Not on a standard scheduler list. */
            break;
    }

    if (fromLock)
    {
        EXEC_SPINLOCK_LOCK(fromLock, NULL, SPINLOCK_MODE_WRITE);
        Remove(&task->tc_Node);
        EXEC_SPINLOCK_UNLOCK(fromLock);
    }

    task->tc_State = newState;

    switch (newState)
    {
        case TS_READY:
            exec_TaskEnqueueReady(task);
            break;
        case TS_WAIT:
            exec_TaskEnqueueWait(task);
            break;
        default:
            /* TS_REMOVED, TS_TOMBSTONED: no enqueue. */
            break;
    }

    EXEC_SPINLOCK_UNLOCK(&task->tc_SpinLock);
    EXEC_IRQFIQ_RESTORE(__if);
}

/* RemTask's self-removal: detach and tombstone for the service task,
 * then return - unlike KrnSwitch() this does NOT dispatch. */
void Exec_SuicideSwitch(void)
{
    struct Task *task = GET_THIS_TASK;
    unsigned int __fiq = EXEC_FIQ_DISABLE();

    Disable();
    /* tc_SpinLock outer, list lock inner. */
    EXEC_SPINLOCK_LOCK(&task->tc_SpinLock, NULL, SPINLOCK_MODE_WRITE);
    EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskRunningSpinLock, NULL, SPINLOCK_MODE_WRITE);
    Remove(&task->tc_Node);
    EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskRunningSpinLock);
    task->tc_State = TS_TOMBSTONED;
    EXEC_SPINLOCK_UNLOCK(&task->tc_SpinLock);
    Enable();
    EXEC_FIQ_RESTORE(__fiq);
}
