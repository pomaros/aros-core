/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Allocate an Apple AArch64 task context with owned FP/SIMD state.
 */

#include <aros/kernel.h>
#include <aros/libcall.h>
#include <aros/aarch64/cpucontext.h>

#include <kernel_base.h>
#include <kernel_objects.h>

#include "kernel_cpu.h"

AROS_LH0(void *, KrnCreateContext,
    struct KernelBase *, KernelBase, 18, Kernel)
{
    AROS_LIBFUNC_INIT

    struct ExceptionContext *context = krnAllocCPUContext();

    if (context != NULL) {
        context->fpuContext = (APTR)((IPTR)context +
            AROS_APPLE_DISPATCH_SIMD_OFFSET);
        context->Flags = ECF_FPU;
        context->cpsr = 0;
    }
    return context;

    AROS_LIBFUNC_EXIT
}
