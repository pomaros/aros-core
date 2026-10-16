/*
    Copyright (C) 2013-2026, The AROS Development Team. All rights reserved.

    AArch64 exec platform initialization.
*/

#define DEBUG 1

#include <aros/debug.h>
#include <aros/cpu.h>
#include <aros/kernel.h>
#include <aros/symbolsets.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <exec/alerts.h>
#include <exec/execbase.h>
#include <asm/io.h>

#include <proto/exec.h>
#include <proto/kernel.h>

#include <strings.h>
#include <stdio.h>

#include "kernel_cpu.h"
#include "kernel_ipi.h"

#include "exec_intern.h"
#if defined(__AROSEXEC_SMP__)
#include "etask.h"
#endif

/* Linked from kernel.resource */
extern IPTR stack[];
extern IPTR stack_super[];

extern void IdleTask(struct ExecBase *);

int Exec_ARMCPUInit(struct ExecBase *SysBase)
{
    struct Task *BootTask, *CPUIdleTask;
#if defined(__AROSEXEC_SMP__)
    int cpu, cpunum = KrnGetCPUCount();
    void *cpuMask = NULL;
#endif
    char *taskName;

    D(bug("[Exec] %s()\n", __PRETTY_FUNCTION__));

    BootTask = GET_THIS_TASK;

    D(bug("[Exec] %s: launched from %s @ 0x%p\n", __PRETTY_FUNCTION__, BootTask->tc_Node.ln_Name, BootTask));

#if defined(__AROSEXEC_SMP__)
    if (cpunum == 0)
    {
#endif
        BootTask->tc_SPLower = stack;
        BootTask->tc_SPUpper = stack + AROS_STACKSIZE;
#if defined(__AROSEXEC_SMP__)
    }

    for (cpu = 0; cpu < cpunum; cpu++)
    {
        taskName = AllocVec(15, MEMF_CLEAR);
        sprintf(taskName, "CPU #%02d Idle", cpu);
        cpuMask = KrnAllocCPUMask();
        if (cpuMask)
            KrnGetCPUMask(cpu, cpuMask);
#else
    taskName = "System Idle";
#endif
        CPUIdleTask = NewCreateTask(TASKTAG_NAME   , taskName,
#if defined(__AROSEXEC_SMP__)
                                TASKTAG_AFFINITY   , cpuMask,
#endif
                                TASKTAG_PRI        , -127,
                                TASKTAG_PC         , IdleTask,
                                TASKTAG_ARG1       , SysBase,
                                TAG_DONE);

        if (CPUIdleTask)
        {
            D(
                bug("[Exec] %s: %s Task created @ 0x%p\n", __PRETTY_FUNCTION__, CPUIdleTask->tc_Node.ln_Name, CPUIdleTask);
#if defined(__AROSEXEC_SMP__)
                bug("[Exec] %s: CPU Affinity : %08x\n", __PRETTY_FUNCTION__, GetIntETask(CPUIdleTask)->iet_CpuAffinity);
#endif
            )
#if defined(__AROSEXEC_SMP__)
            /* KATTR_CPULoad derives a core's load from its idle task,
             * and only exec knows the mapping. */
            if (cpu < AARCH64_MAXCPUS)
                aarch64_IdleTask[cpu] = CPUIdleTask;
#endif
        }
#if defined(__AROSEXEC_SMP__)
    }
#endif

    return TRUE;
}

#if defined(__AROSEXEC_SMP__)
#include "../../aarch64-all/exec/smp_tasks.c"

int Exec_ARMCPUSMPInit(struct ExecBase *SysBase)
{
    /* Gate for signal.c's cross-CPU paths; without it remote wakes
     * silently fail. */
    PrivExecBase(SysBase)->IntFlags |= EXECF_CPUAffinity;

    /* The boot task predates EXECF_CPUAffinity, so it has no affinity -
     * but the coldstart sequence is not migration-safe. */
    {
        struct Task *bootTask = GET_THIS_TASK;
        struct IntETask *iet = bootTask ? (struct IntETask *)GetETask(bootTask) : NULL;

        if (iet && !iet->iet_CpuAffinity)
        {
            void *aff = KrnAllocCPUMask();
            if (aff)
            {
                KrnGetCPUMask(0, aff);
                iet->iet_CpuAffinity = aff;
                iet->iet_CpuNumber = 0;
            }
        }
    }

    return TRUE;
}

ADD2INITLIB(Exec_ARMCPUSMPInit, -127)
#endif

ADD2INITLIB(Exec_ARMCPUInit, 0)
