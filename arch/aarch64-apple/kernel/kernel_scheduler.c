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
    if (!(execution->park_flags&AROS_TASK_PARK_WAIT)) return 0;
    execution->park_flags|=AROS_TASK_PARKED;
    task->tc_SigWait=0;
    return 1;
}
int AROSAppleRestrictedSetWait(struct Task *task,struct ExceptionContext *context,int wait)
{
    if (!task || !context) return 0;
#if defined(__AROSEXEC_SMP__)
    KrnSpinLock(&task->tc_SpinLock,NULL,SPINLOCK_MODE_WRITE);
#endif
    int ok=0,ready=0;
    if (restricted_context(task)!=context ||
        (task->tc_State!=TS_RUN && task->tc_State!=TS_READY &&
         task->tc_State!=TS_WAIT && task->tc_State!=TS_FROZEN)) goto done;
    struct AROSAppleTaskExecution *execution=AROSAppleTaskExecution(context);
    if (!execution->root) goto done;
    if (wait) {
        /* Admission is for the current running payload only. The scheduler
         * performs the list transition after cpu_SaveContext completes. */
        if (task!=GET_THIS_TASK || task->tc_State!=TS_RUN) goto done;
        execution->park_flags|=AROS_TASK_PARK_WAIT;
    } else {
        uint64_t old=execution->park_flags;
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
            ready=1;
        }
        /* External FreezeTasks owns TS_FROZEN. Retain the freeze, but its
         * eventual thaw must not restore a wait already satisfied here. */
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
    if (ready) AROSAppleSMPSendIPI(IPI_SCHEDULE,0,AROSAppleSMPOnlineMask());
#else
    if (ready) FLAG_SCHEDSWITCH_SET;
#endif
    return ok;
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
            if ((execution->park_flags&AROS_TASK_PARKED) && task->tc_State==TS_WAIT)
                result|=AROS_RESTRICTED_THREAD_PARKED;
        }
    }
#if defined(__AROSEXEC_SMP__)
    KrnSpinUnLock(&task->tc_SpinLock);
#endif
    return result;
}
