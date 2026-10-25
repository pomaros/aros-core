/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#ifndef AROS_AARCH64_NATIVE_TASK_TLS_H
#define AROS_AARCH64_NATIVE_TASK_TLS_H

#include <stddef.h>
#include <stdint.h>
#include <aros/aarch64/cpucontext.h>
#include <aros/aarch64/native_tls.h>

/* Owned task context only. Its alignment gap holds the task's TLS anchor:
 * TPIDR_EL0 is its address for the task's whole life, set on every
 * dispatch, so compiled TLS descriptor code may keep the thread pointer in
 * a register. The word belongs to the TLS runtime (dos.library). */
struct AROSAArch64NativeTaskContext {
    struct ExceptionContext registers;
    uint64_t tls_anchor;
};

#define AARCH64_NATIVE_TASK_PREFIX 288
#define AARCH64_NATIVE_TASK_SIZE 816

_Static_assert(sizeof(struct ExceptionContext) == 280, "public context");
_Static_assert(offsetof(struct AROSAArch64NativeTaskContext, registers) == 0,
    "public context prefix");
_Static_assert(offsetof(struct AROSAArch64NativeTaskContext, tls_anchor) == 280,
    "TLS anchor occupies the alignment gap");
_Static_assert(sizeof(struct AROSAArch64NativeTaskContext) ==
    AARCH64_NATIVE_TASK_PREFIX, "private prefix ends at SIMD");

static inline void AROSAArch64NativeTaskTLSInit(struct ExceptionContext *ctx)
{
    ((struct AROSAArch64NativeTaskContext *)ctx)->tls_anchor = 0;
}

static inline void AROSAArch64NativeTaskTLSRestore(struct ExceptionContext *ctx)
{
    AROSAArch64WriteTaskPointer(
        (uint64_t)(uintptr_t)&((struct AROSAArch64NativeTaskContext *)ctx)->tls_anchor);
}

#endif
