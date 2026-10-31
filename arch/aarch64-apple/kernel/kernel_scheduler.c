/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "smp.h"
#include "restricted.h"
#include "task_tls.h"
#include "kernel_ipi.h"
static int restricted_park(struct Task *);
#define CORE_SCHEDULER_PARK(task) restricted_park(task)
#define CORE_SCHEDULER_BUG(...) _bug(NULL, __VA_ARGS__)
#define CORE_SCHEDULER_TIME_US() AROSAppleSMPTimeUS()
#define CORE_SCHEDULER_TIME_AVAILABLE() 1
#include "../../aarch64-native/kernel/kernel_scheduler.c"

static struct ExceptionContext *restricted_context(struct Task *task)
{
    return task && (task->tc_Flags & TF_ETASK) && task->tc_UnionETask.tc_ETask ?
        task->tc_UnionETask.tc_ETask->et_RegFrame : NULL;
}
/* Called only with the task lock, after its complete outgoing save or before
 * incoming dispatch. No domain metadata is read under scheduler locks. */
static int restricted_park(struct Task *task)
{
    struct ExceptionContext *context=restricted_context(task);
    if (!context) return 0;
    struct AROSAppleTaskExecution *execution=AROSAppleTaskExecution(context);
    if (!(execution->park_flags&AROS_TASK_PARK_REASONS)) return 0;
    execution->park_flags|=AROS_TASK_PARKED;
    task->tc_SigWait=0;
    return 1;
}
static int restricted_park_control(struct Task *task,struct ExceptionContext *context,
    uint64_t set,uint64_t clear)
{
    if (!task || !context || ((set|clear)&~AROS_TASK_PARK_REASONS) || (set&clear)) return 0;
#if defined(__AROSEXEC_SMP__)
    KrnSpinLock(&task->tc_SpinLock,NULL,SPINLOCK_MODE_WRITE);
#endif
    int ok=0,schedule=0;
    if (restricted_context(task)!=context ||
        (task->tc_State!=TS_RUN && task->tc_State!=TS_READY &&
         task->tc_State!=TS_WAIT && task->tc_State!=TS_FROZEN)) goto done;
    struct AROSAppleTaskExecution *execution=AROSAppleTaskExecution(context);
    if (!execution->root) goto done;
    /* Only the running payload can admit a wait. A controller may suspend
     * any active member, including a ready or already waiting context. */
    if ((set&AROS_TASK_PARK_WAIT) && (task!=GET_THIS_TASK || task->tc_State!=TS_RUN)) goto done;
    uint64_t old=execution->park_flags;
    execution->park_flags=(old|set)&~clear;
    if (execution->park_flags&AROS_TASK_PARK_REASONS) {
        if (task->tc_State==TS_READY) {
            /* READY already owns a complete saved context. Task lock precedes
             * ready/wait list locks, matching dispatch's try-lock protocol. */
#if defined(__AROSEXEC_SMP__)
            KrnSpinLock(&PrivExecBase(SysBase)->TaskReadySpinLock,NULL,SPINLOCK_MODE_WRITE);
#endif
            Remove(&task->tc_Node);
#if defined(__AROSEXEC_SMP__)
            KrnSpinUnLock(&PrivExecBase(SysBase)->TaskReadySpinLock);
#endif
            execution->park_flags|=AROS_TASK_PARKED;
            task->tc_SigWait=0;task->tc_State=TS_WAIT;
#if defined(__AROSEXEC_SMP__)
            exec_TaskEnqueueWait(task);
#else
            Enqueue(&SysBase->TaskWait,&task->tc_Node);
#endif
        } else if (task->tc_State==TS_RUN && (set&AROS_TASK_PARK_SUSPEND)) {
            schedule=1; /* IPI requests save/park; PARKED acknowledges it. */
        }
    } else {
        execution->park_flags=0;
        if ((old&AROS_TASK_PARKED) && task->tc_State==TS_WAIT) {
#if defined(__AROSEXEC_SMP__)
            KrnSpinLock(&PrivExecBase(SysBase)->TaskWaitSpinLock,NULL,SPINLOCK_MODE_WRITE);
#endif
            Remove(&task->tc_Node);
#if defined(__AROSEXEC_SMP__)
            KrnSpinUnLock(&PrivExecBase(SysBase)->TaskWaitSpinLock);
#endif
            task->tc_State=TS_READY;
#if defined(__AROSEXEC_SMP__)
            exec_TaskEnqueueReady(task);
#else
            Enqueue(&SysBase->TaskReady,&task->tc_Node);
#endif
            schedule=1;
        }
        /* External FreezeTasks owns TS_FROZEN. Only its eventual thaw
         * destination changes when every restricted park reason is gone. */
#if defined(__AROSEXEC_SMP__)
        if ((old&AROS_TASK_PARKED) && task->tc_State==TS_FROZEN &&
            GetIntETask(task) && GetIntETask(task)->iet_FrozenState==TS_WAIT)
            GetIntETask(task)->iet_FrozenState=TS_READY;
#endif
    }
    ok=1;
done:
#if defined(__AROSEXEC_SMP__)
    KrnSpinUnLock(&task->tc_SpinLock);
    if (schedule) AROSAppleSMPSendIPI(IPI_SCHEDULE,0,AROSAppleSMPOnlineMask());
#else
    if (schedule) FLAG_SCHEDSWITCH_SET;
#endif
    return ok;
}
int AROSAppleRestrictedSetWait(struct Task *task,struct ExceptionContext *context,int wait)
{
    return restricted_park_control(task,context,wait ? AROS_TASK_PARK_WAIT : 0,
        wait ? 0 : AROS_TASK_PARK_WAIT);
}
int AROSAppleRestrictedSetSuspend(struct Task *task,struct ExceptionContext *context,int suspend)
{
    return restricted_park_control(task,context,suspend ? AROS_TASK_PARK_SUSPEND : 0,
        suspend ? 0 : AROS_TASK_PARK_SUSPEND);
}
int AROSAppleRestrictedCancelPark(struct Task *task,struct ExceptionContext *context)
{
    return restricted_park_control(task,context,0,AROS_TASK_PARK_REASONS);
}
unsigned AROSAppleRestrictedWaitState(struct Task *task,struct ExceptionContext *context)
{
    if (!task || !context) return 0;
#if defined(__AROSEXEC_SMP__)
    KrnSpinLock(&task->tc_SpinLock,NULL,SPINLOCK_MODE_WRITE);
#endif
    unsigned result=0;
    if (restricted_context(task)==context &&
        (task->tc_State==TS_RUN || task->tc_State==TS_READY ||
         task->tc_State==TS_WAIT || task->tc_State==TS_FROZEN)) {
        struct AROSAppleTaskExecution *execution=AROSAppleTaskExecution(context);
        if (execution->root) {
            result=AROS_RESTRICTED_THREAD_ACTIVE;
            if (execution->park_flags&AROS_TASK_PARK_WAIT)
                result|=AROS_RESTRICTED_THREAD_WAITING;
            if (execution->park_flags&AROS_TASK_PARK_SUSPEND)
                result|=AROS_RESTRICTED_THREAD_SUSPENDED;
            if ((execution->park_flags&AROS_TASK_PARKED) && task->tc_State==TS_WAIT)
                result|=AROS_RESTRICTED_THREAD_PARKED;
        }
    }
#if defined(__AROSEXEC_SMP__)
    KrnSpinUnLock(&task->tc_SpinLock);
#endif
    return result;
}
