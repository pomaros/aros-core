#ifndef AROS_APPLE_PLATFORMTIMER_H
#define AROS_APPLE_PLATFORMTIMER_H

/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: ARM architectural counter shared by the Apple kernel and timer.device.
 */

#include <stddef.h>
#include <stdint.h>

#if defined(AROS_APPLE_TIMER_HOST_TEST)
typedef uint32_t ULONG;
typedef uint64_t UQUAD;
typedef int32_t LONG;
#else
#include <exec/types.h>
#endif

#define AROS_APPLE_TIMER_STATE_VERSION UINT32_C(1)
#define AROS_APPLE_TIMER_CONTROL_VERSION UINT32_C(1)

#define AROS_APPLE_TIMER_SVC UINT32_C(0xa506)

#define AROS_APPLE_TIMER_CTL_ENABLE UINT32_C(1U << 0)
#define AROS_APPLE_TIMER_CTL_IMASK UINT32_C(1U << 1)
#define AROS_APPLE_TIMER_CTL_ISTATUS UINT32_C(1U << 2)

enum AROSAppleTimerStatus
{
    AROS_APPLE_TIMER_OK = 0,
    AROS_APPLE_TIMER_ARGUMENT,
    AROS_APPLE_TIMER_FDT,
    AROS_APPLE_TIMER_UNSUPPORTED,
    AROS_APPLE_TIMER_STATE,
    AROS_APPLE_TIMER_OWNERSHIP,
    AROS_APPLE_TIMER_RANGE,
    AROS_APPLE_TIMER_NOT_PENDING
};

enum AROSAppleTimerOperation
{
    AROS_APPLE_TIMER_OP_QUERY = 0,
    AROS_APPLE_TIMER_OP_CLAIM,
    AROS_APPLE_TIMER_OP_ARM,
    AROS_APPLE_TIMER_OP_DISARM,
    AROS_APPLE_TIMER_OP_RELEASE,
    AROS_APPLE_TIMER_OP_NOTIFY
};

struct AROSAppleTimerIO
{
    uint64_t (*read_counter)(void *context);
    uint32_t (*read_control)(void *context);
    void (*write_control)(void *context, uint32_t value);
    void (*write_compare)(void *context, uint64_t value);
    void (*synchronize)(void *context);
};

typedef uint64_t (*AROSAppleTimerCallback)(void *context, uint64_t now);

struct AROSAppleTimerState
{
    uint32_t version;
    uint32_t size;
    uint32_t initialized;
    uint32_t claimed;
    uint32_t armed;
    uint32_t reserved;
    uint64_t frequency;
    uint64_t owner;
    uint64_t programmed_deadline;
    uint64_t generation;
    uint64_t fired_count;
    uint64_t unexpected_count;
    struct AROSAppleTimerIO io;
    void *io_context;
    AROSAppleTimerCallback callback;
    void *callback_context;
};

struct AROSAppleTimerControl
{
    uint32_t version;
    uint32_t size;
    uint32_t operation;
    uint32_t status;
    uint64_t owner;
    uint64_t callback;
    uint64_t deadline;
    uint64_t programmed_deadline;
    uint64_t generation;
    uint64_t fired_count;
    uint64_t unexpected_count;
    uint64_t flags;
};

#define AROS_APPLE_TIMER_CONTROL_CLAIMED UINT64_C(1U << 0)
#define AROS_APPLE_TIMER_CONTROL_ARMED UINT64_C(1U << 1)

/*
 * The system counter is the first half of the Apple platform timer contract.
 * It provides an advancing EClock before the interrupt controller is ready.
 * Deadline ownership is enabled after the selected platform binds the timer.
 * EL0 may read the counter, but only the
 * kernel-owned control boundary may program CNTV_CVAL_EL0/CNTV_CTL_EL0.
 */
struct KrnPlatformTimer
{
    ULONG           kpt_Frequency;
    volatile ULONG  kpt_Flags;
    UQUAD            kpt_InitialCounter;
};

#define KPTF_COUNTER_READY  (1U << 0)
#define KPTF_DEADLINE_READY (1U << 1)

static inline UQUAD KrnPlatformTimerRead(void)
{
    UQUAD value;

    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(value) :: "memory");
    return value;
}

static inline LONG KrnPlatformTimerControl(
    struct AROSAppleTimerControl *control)
{
    register uintptr_t value __asm__("x0") = (uintptr_t)control;

    __asm__ volatile("svc #0xa506" : "+r"(value) :: "memory", "cc");
    return (LONG)value;
}

enum AROSAppleTimerStatus AROSAppleTimerInitialize(
    struct AROSAppleTimerState *state, uint64_t frequency,
    const struct AROSAppleTimerIO *io, void *io_context);
enum AROSAppleTimerStatus AROSAppleTimerClaim(
    struct AROSAppleTimerState *state, uint64_t owner,
    AROSAppleTimerCallback callback, void *callback_context);
enum AROSAppleTimerStatus AROSAppleTimerArm(
    struct AROSAppleTimerState *state, uint64_t owner, uint64_t deadline);
enum AROSAppleTimerStatus AROSAppleTimerDisarm(
    struct AROSAppleTimerState *state, uint64_t owner);
enum AROSAppleTimerStatus AROSAppleTimerRelease(
    struct AROSAppleTimerState *state, uint64_t owner);
/* Owner-CPU only; recompute an armed deadline without requiring an IRQ. */
enum AROSAppleTimerStatus AROSAppleTimerRefresh(
    struct AROSAppleTimerState *state, uint64_t owner);
enum AROSAppleTimerStatus AROSAppleTimerHandle(
    struct AROSAppleTimerState *state);
int AROSAppleTimerArmed(const struct AROSAppleTimerState *state);
const char *AROSAppleTimerStatusName(enum AROSAppleTimerStatus status);

_Static_assert(sizeof(struct AROSAppleTimerControl) == 80,
    "Apple timer control size");

#endif /* AROS_APPLE_PLATFORMTIMER_H */
