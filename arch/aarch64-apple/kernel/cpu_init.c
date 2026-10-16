/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Size Apple AArch64 task contexts including FP/SIMD state.
 */

#include <aros/symbolsets.h>
#include <aros/aarch64/cpucontext.h>

#include "kernel_base.h"
#include "kernel_cpu.h"
#include "page_allocator.h"

_Static_assert(offsetof(struct KernelBase, kb_ContextSize) +
    sizeof(((struct KernelBase *)0)->kb_ContextSize) <=
    sizeof(struct KernelBase), "KernelBase context size field is in bounds");
_Static_assert(((sizeof(struct ExceptionContext) + 15U) & ~15U) ==
    AROS_APPLE_DISPATCH_SIMD_OFFSET, "Apple SIMD context offset");
_Static_assert(AROS_APPLE_DISPATCH_SIMD_OFFSET +
    sizeof(struct AROSAppleSIMDContext) ==
    AROS_APPLE_DISPATCH_CONTEXT_SIZE, "Apple task context size");

static int cpu_InitContextSize(struct KernelBase *KernelBase)
{
    KernelBase->kb_ContextSize = AROS_APPLE_DISPATCH_CONTEXT_SIZE;
    KernelBase->kb_PageSize = AROS_APPLE_PAGE_SIZE;
    return TRUE;
}

ADD2INITLIB(cpu_InitContextSize, 5);
