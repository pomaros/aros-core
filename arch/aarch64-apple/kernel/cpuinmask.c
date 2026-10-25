/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "../exec/tls.h"
#include <aros/libcall.h>
#include <kernel_base.h>
#include "smp.h"
AROS_LH2(BOOL, KrnCPUInMask, AROS_LHA(uint32_t,id,D0), AROS_LHA(void *,mask,A0),
 struct KernelBase *,KernelBase,46,Kernel)
{
 AROS_LIBFUNC_INIT
 if(id>=AROS_APPLE_SMP_MAX_CPUS) return FALSE;
 return !mask || (IPTR)mask==TASKAFFINITY_ANY || (*(uint32_t *)mask&(1U<<id));
 AROS_LIBFUNC_EXIT
}
