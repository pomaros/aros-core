/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <aros/libcall.h>
#include <kernel_base.h>
#include "smp.h"
#include "kernel_cpu.h"
#include "kernel_ipi.h"
AROS_LH1(void, KrnScheduleCPU, AROS_LHA(void *, mask, A0),
 struct KernelBase *, KernelBase, 47, Kernel)
{
 AROS_LIBFUNC_INIT
 uint32_t online=AROSAppleSMPOnlineMask(), value;
 if (!mask) return;
 if ((IPTR)mask==TASKAFFINITY_ANY) value=online;
 else if ((IPTR)mask==TASKAFFINITY_ALL_BUT_SELF) value=online & ~(1U<<GetCPUNumber());
 else value=*(uint32_t *)mask & online;
 AROSAppleSMPSendIPI(IPI_SCHEDULE,0,value);
 AROS_LIBFUNC_EXIT
}
