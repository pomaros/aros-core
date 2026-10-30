/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * CPU-owned privilege/interrupt state, anchored by TPIDRRO_EL0. */
#ifndef AROS_APPLE_RUNTIME_LOCAL_H
#define AROS_APPLE_RUNTIME_LOCAL_H
#define AROS_CPU_TRANSITION_OFFSET 4
#define AROS_CPU_RETURN_PC_OFFSET 24
#define AROS_CPU_TRUSTED_ROOT_OFFSET 104
#define AROS_CPU_KERNEL_TLS_OFFSET 112
#ifndef __ASSEMBLER__
#include <stdint.h>
#include <stddef.h>
struct AROSAppleRuntimeProof;
struct AROSAppleRuntimeLocal {
    volatile uint32_t active;
    volatile uint32_t transition_active;
    volatile uint32_t irq_ready;
    volatile uint32_t fiq_ready;
    volatile uint32_t interrupt_depth;
    volatile uint32_t idle_active;
    uintptr_t return_pc;
    struct AROSAppleRuntimeProof *proof;
    void *exec_tls;
    uint32_t irq_mask_depth;
    uint32_t irq_mask_refusal_reported;
    uint64_t irq_mask_abandoned;
    uint64_t irq_mask_entries;
    uint64_t irq_mask_rejected;
    uint64_t irq_mask_started;
    uint64_t irq_mask_max_ticks;
    const void *exec_smp_api;
    /* EL1-owned transition state. The entry island, exception stack and this
     * record must be mapped privileged in every admitted root. Zero root
     * disables transitions during early bootstrap. Publication is CPU-local
     * with exceptions masked, before any restricted return. */
    uint64_t trusted_ttbr0;
    uint64_t kernel_tls;
};
_Static_assert(offsetof(struct AROSAppleRuntimeLocal, transition_active)==AROS_CPU_TRANSITION_OFFSET,
    "CPU transition assembly offset");
_Static_assert(offsetof(struct AROSAppleRuntimeLocal, return_pc)==AROS_CPU_RETURN_PC_OFFSET,
    "CPU return PC assembly offset");
_Static_assert(offsetof(struct AROSAppleRuntimeLocal, trusted_ttbr0)==AROS_CPU_TRUSTED_ROOT_OFFSET,
    "CPU trusted root assembly offset");
_Static_assert(offsetof(struct AROSAppleRuntimeLocal, kernel_tls)==AROS_CPU_KERNEL_TLS_OFFSET,
    "CPU kernel TLS assembly offset");
/* CPU-local exclusion never overrides Disable or interrupt-controller readiness. */
static inline uint64_t AROSAppleRequiredTaskMasks(
    const struct AROSAppleRuntimeLocal *local, int nesting)
{
    if (nesting >= 0 || local->irq_mask_depth)
        return UINT64_C(0xc0);
    return (local->irq_ready ? 0 : UINT64_C(0x80)) |
        (local->fiq_ready ? 0 : UINT64_C(0x40));
}
/* A dispatch frame describes the destination task; handler depth belongs
 * only to the current handler. Restore/STI must additionally respect it. */
static inline uint64_t AROSAppleRequiredIRQMasks(
    const struct AROSAppleRuntimeLocal *local, int nesting)
{
    return local->interrupt_depth ? UINT64_C(0xc0) :
        AROSAppleRequiredTaskMasks(local, nesting);
}
/* Callable at EL0 and EL1; never changes when switching tasks on a CPU. */
struct AROSAppleRuntimeLocal *AROSAppleRuntimeLocal(void);
#endif
#endif
