/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#ifndef TASK_STOP_PRIVATE_H
#define TASK_STOP_PRIVATE_H
static void task_stop_lock(struct TaskResBase *TaskResBase)
{
    Disable();
#if defined(__AROSEXEC_SMP__)
    EXEC_SPINLOCK_LOCK(&TaskResBase->trb_StopLock, NULL, SPINLOCK_MODE_WRITE);
#else
    (void)TaskResBase;
#endif
}
static void task_stop_unlock(struct TaskResBase *TaskResBase)
{
#if defined(__AROSEXEC_SMP__)
    EXEC_SPINLOCK_UNLOCK(&TaskResBase->trb_StopLock);
#else
    (void)TaskResBase;
#endif
    Enable();
}
#endif
