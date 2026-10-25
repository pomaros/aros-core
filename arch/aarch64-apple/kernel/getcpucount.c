/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <aros/libcall.h>
#include <kernel_base.h>
#include "smp.h"
AROS_LH0(cpuid_t, KrnGetCPUCount, struct KernelBase *, KernelBase, 40, Kernel)
{
 AROS_LIBFUNC_INIT
 return __builtin_popcount(AROSAppleSMPOnlineMask());
 AROS_LIBFUNC_EXIT
}
