/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#ifndef AROS_APPLE_TASK_TLS_H
#define AROS_APPLE_TASK_TLS_H

#include <stddef.h>
#include <stdint.h>
#include <aros/aarch64/cpucontext.h>
#include <aros/aarch64/native_tls.h>
#include <aros/apple/runtime.h>

/* et_RegFrame names registers at offset zero. The old alignment gap,
 * keeping the public 280-byte prefix, SIMD offset 288,
 * holds the task's TLS anchor: TPIDR_EL0 is its address for the task's
 * whole life, set on every dispatch, so compiled TLS descriptor code may
 * keep the thread pointer in a register across accesses and calls. The
 * word itself belongs to the TLS runtime (dos.library), which stores its
 * control block there on the task's first TLS access. Every task context
 * comes from KrnCreateContext, including non-pthreads. */
struct AROSAppleTaskContext {
    struct ExceptionContext registers;
    uint64_t tls_anchor;
};

_Static_assert(offsetof(struct AROSAppleTaskContext, registers) == 0,
    "public task context prefix");
_Static_assert(sizeof(struct ExceptionContext) == 280,
    "public AArch64 context is unchanged");
_Static_assert(offsetof(struct AROSAppleTaskContext, tls_anchor) == 280,
    "TLS anchor uses the old alignment gap");
_Static_assert(sizeof(struct AROSAppleTaskContext) ==
    AROS_APPLE_DISPATCH_SIMD_OFFSET, "private state precedes SIMD");

/* Kernel-owned execution metadata follows the unchanged SIMD block. */
struct AROSAppleTaskExecution {
    uint64_t root;
    uint64_t user_tls;
    void *domain;
    /* Scheduler/task-lock protected. Never acquire the domain coordinator
     * while holding that lock. PARKED acknowledges a completed context save. */
    uint64_t park_flags;
};
#define AROS_TASK_PARK_WAIT UINT64_C(1)
#define AROS_TASK_PARKED UINT64_C(2)
_Static_assert(816 + sizeof(struct AROSAppleTaskExecution) ==
    AROS_APPLE_DISPATCH_CONTEXT_SIZE, "owned execution metadata allocation");
static inline struct AROSAppleTaskExecution *AROSAppleTaskExecution(
    struct ExceptionContext *context)
{
    return (void *)((unsigned char *)context + 816);
}

static inline void AROSAppleTaskTLSInit(struct ExceptionContext *context)
{
    ((struct AROSAppleTaskContext *)context)->tls_anchor = 0;
    *AROSAppleTaskExecution(context) = (struct AROSAppleTaskExecution){0};
}

static inline void AROSAppleTaskTLSRestore(struct ExceptionContext *context)
{
    AROSAArch64WriteTaskPointer(
        (uint64_t)(uintptr_t)&((struct AROSAppleTaskContext *)context)->tls_anchor);
}

#endif
