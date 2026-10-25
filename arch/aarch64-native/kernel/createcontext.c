/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: Create an empty usable CPU context, AArch64 raspi version.
*/

#include <aros/kernel.h>
#include <aros/libcall.h>

#include <aros/aarch64/cpucontext.h>

#include <kernel_base.h>
#include <kernel_objects.h>

#include "kernel_cpu.h"
#include "kernel_intern.h"
#include "task_tls.h"

/* AllocMem aligns to MEMCHUNK_TOTAL, at least AROS_WORSTALIGN (16 on
 * AArch64). The private pointer must not alias a rounded-down SIMD tail. */
_Static_assert((MEMCHUNK_TOTAL & 15) == 0, "16-byte context allocation");

AROS_LH0(void *, KrnCreateContext,
         struct KernelBase *, KernelBase, 18, Kernel)

{
    AROS_LIBFUNC_INIT

    struct ExceptionContext *ctx;

    /*
     * Common data block and FPU data block are allocated in one chunk
     * (see cpu_Init for the sizing); fpuContext points at the 16-byte
     * aligned tail. Tasks run at EL1t on this port, so the initial
     * PSTATE must say so -- a zeroed cpsr would be EL0t and the first
     * dispatch ERET would fault (EL0 is unusable: no domains, and
     * EL0-writable pages are forced PXN).
     */
    ctx = krnAllocCPUContext();
    if (ctx)
    {
        ctx->fpuContext = (APTR)((char *)ctx + AARCH64_NATIVE_TASK_PREFIX);
        ctx->Flags      = ECF_FPU;
        ctx->cpsr       = CPUMODE_EL1t;
        AROSAArch64NativeTaskTLSInit(ctx);
    }

    return ctx;

    AROS_LIBFUNC_EXIT
}
