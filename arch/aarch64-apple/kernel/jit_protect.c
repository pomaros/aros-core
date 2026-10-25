/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <exec/types.h>
#include <aros/apple/jit.h>
#include <aros/aarch64/cpucontext.h>
#include <exec/tasks.h>
#include <exec/execbase.h>
#include <proto/exec.h>

#include "kernel_base.h"
#include "kernel_cpu.h"
#include "kernel_intern.h"
#include "jit_protect.h"
#include "platform_cpu.h"

/* /chosen aros,jit: 2 forces NONE on every platform. */
#define JIT_PROPERTY "aros,jit"
#define JIT_PROPERTY_NONE 2U
static int jit_mode = AROS_APPLE_JIT_NONE;
static int mode_chosen;
void AROSAppleJitProtectInitCPU(void)
{
    if (!__atomic_load_n(&mode_chosen, __ATOMIC_ACQUIRE)) {
        uint64_t wanted = AROSAppleKernelChosenDecimal(JIT_PROPERTY);

        jit_mode = AROSPlatformJitChoose(wanted);
        if (wanted == JIT_PROPERTY_NONE)
            jit_mode = AROS_APPLE_JIT_NONE;
        __atomic_store_n(&mode_chosen, 1, __ATOMIC_RELEASE);
    }
    AROSPlatformJitInitCPU(jit_mode);
}

int AROSAppleJitMode(void)
{
    return __atomic_load_n(&mode_chosen, __ATOMIC_ACQUIRE) ? jit_mode :
        AROS_APPLE_JIT_NONE;
}

static struct ExceptionContext *context_of(struct Task *task)
{
    struct ETask *etask;

    if (task == NULL || (task->tc_Flags & TF_ETASK) == 0)
        return NULL;
    etask = task->tc_UnionETask.tc_ETask;
    return etask ? (struct ExceptionContext *)etask->et_RegFrame : NULL;
}

void AROSAppleJitRestore(struct Task *task)
{
    struct ExceptionContext *context;

    if (jit_mode != AROS_APPLE_JIT_SPRR)
        return;
    context = context_of(task);
    AROSPlatformJitRestore(jit_mode,
        context != NULL && (context->Flags & ECF_JITWRITE));
}

uint64_t AROSAppleJitSetWritable(struct Task *task, int writable)
{
    struct ExceptionContext *context;

    if (jit_mode == AROS_APPLE_JIT_NONE)
        return UINT64_MAX;
    context = context_of(task);
    if (context == NULL)
        return UINT64_MAX;
    if (writable)
        context->Flags |= ECF_JITWRITE;
    else
        context->Flags &= ~ECF_JITWRITE;
    AROSAppleJitRestore(task);
    return (uint64_t)jit_mode;
}
