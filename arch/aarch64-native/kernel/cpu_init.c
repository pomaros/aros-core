/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: CPU context sizing, AArch64 raspi version.
*/

#include <aros/symbolsets.h>
#include <exec/types.h>

#include "kernel_base.h"
#include "kernel_intern.h"
#include "task_tls.h"

/*
 * Allocate the ExceptionContext and the FPU/NEON block as one chunk;
 * KrnCreateContext points fpuContext at the tail. The old alignment gap
 * holds private TP state, preserving the SIMD offset and allocation.
 */
_Static_assert(AARCH64_NATIVE_TASK_PREFIX == AARCH64_FRAME_FPU,
    "owned and vector SIMD offsets agree");
_Static_assert(AARCH64_NATIVE_TASK_PREFIX + sizeof(struct VFPContext) ==
    AARCH64_NATIVE_TASK_SIZE, "owned allocation size");
_Static_assert(AARCH64_NATIVE_TASK_SIZE == AARCH64_FRAME_SIZE,
    "owned allocation and vector frame sizes agree");

static int cpu_InitContextSize(struct KernelBase *KernelBase)
{
    KernelBase->kb_ContextSize = AARCH64_NATIVE_TASK_PREFIX
                               + sizeof(struct VFPContext);

    return TRUE;
}

ADD2INITLIB(cpu_InitContextSize, 5);
