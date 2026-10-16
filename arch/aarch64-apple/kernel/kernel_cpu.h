/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Apple AArch64 kernel CPU and syscall contract.
 */

#ifndef AROS_AARCH64_APPLE_KERNEL_CPU_H
#define AROS_AARCH64_APPLE_KERNEL_CPU_H

#include <aros/config.h>
#include <exec/interrupts.h>
#include <aros/apple/runtime.h>


#define EXCEPTIONS_COUNT AROS_APPLE_RUNTIME_VECTOR_COUNT

/* KrnCreateContext stores the public AArch64 ExceptionContext verbatim. */
#define AROSCPUContext ExceptionContext

/* The live scheduler frame is the runtime vector frame. */
typedef struct AROSAppleRuntimeFrame regs_t;

/*
 * An interrupt handler that ran on a current-EL vector calls this before
 * returning. When the CPU was in the dispatcher's idle context it dispatches
 * whatever the handler made ready (or re-enters idle) and returns nonzero;
 * otherwise it does nothing and returns zero.
 */
int cpu_IdleInterrupted(regs_t *regs);

struct AROSAppleSIMDContext {
    uint64_t words[66];
};

_Static_assert(sizeof(struct AROSAppleSIMDContext) ==
    AROS_APPLE_SIMD_STATE_SIZE, "Apple SIMD context size");

/* Memory is not privilege-separated before the MMU task policy exists. */
#define cpumode_t __unused char
#define goSuper() 0
#define goUser()
#define goBack(mode)

#define krnSysCall(number)                                                \
    do {                                                                  \
        __asm__ volatile("svc %0" ::                                     \
            "i"(AROS_APPLE_RUNTIME_SVC_KERNEL_BASE + (number))           \
            : "memory", "cc");                                           \
    } while (0)

uint64_t cpu_RequiredCurrentTaskMasks(void);
void cpu_ApplyTaskMasks(uint64_t *spsr, int nesting);
void cpu_ApplyCurrentTaskMasks(uint64_t *spsr);

#endif /* AROS_AARCH64_APPLE_KERNEL_CPU_H */
