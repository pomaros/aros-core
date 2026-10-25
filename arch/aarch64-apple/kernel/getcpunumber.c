/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <aros/libcall.h>
#include <kernel_base.h>
AROS_LH0(cpuid_t, KrnGetCPUNumber, struct KernelBase *, KernelBase, 41, Kernel)
{
 AROS_LIBFUNC_INIT
 return GetCPUNumber();
 AROS_LIBFUNC_EXIT
}
