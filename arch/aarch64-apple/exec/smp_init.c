/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <aros/symbolsets.h>
#include <proto/exec.h>
#include <proto/kernel.h>
#include <exec_intern.h>
#include <etask.h>
static int AppleExecSMPInit(struct ExecBase *SysBase)
{
    struct Task *task=GET_THIS_TASK;
    struct IntETask *iet=task ? GetIntETask(task) : NULL;
    if (!iet) return FALSE;
    if (!iet->iet_CpuAffinity) {
        void *mask=KrnAllocCPUMask();
        if (!mask) return FALSE;
        KrnGetCPUMask(KrnGetCPUNumber(),mask);
        iet->iet_CpuAffinity=mask;
    }
    iet->iet_CpuNumber=KrnGetCPUNumber();
    /* Coldstart children inherit BSP affinity. Only explicit SMP workers
     * may run on APs until their subsystem has been qualified. */
    PrivExecBase(SysBase)->IntFlags |= EXECF_CPUAffinity;
    return TRUE;
}
ADD2INITLIB(AppleExecSMPInit,-127)
