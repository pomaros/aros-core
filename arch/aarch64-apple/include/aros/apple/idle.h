/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: The kernel's CPU idle control, for diagnostics.

    KrnGetSystemAttr(AROS_APPLE_KATTR_IDLE_CONTROL) answers the address of
    this record, or -1. mode is the loop the next idle entry takes: 0 the
    default (deep WFI on an Apple core unless chosen aros,plain-idle = 1),
    1 the clock-gated WFI, 2 the deep WFI (CYC_OVRD WFI_MODE 3). A
    diagnostic may write it and must put the value it found back.
    ticks_held is for a sleep with every task frozen: set it after
    FreezeTasks(), clear it before ThawTasks(), whose schedule IPIs then
    restart each core's tick.
*/
#ifndef AROS_APPLE_IDLE_H
#define AROS_APPLE_IDLE_H

#include <stdint.h>

#define AROS_APPLE_KATTR_IDLE_CONTROL UINT32_C(0x83f10015)
#define AROS_APPLE_IDLE_CONTROL_VERSION UINT32_C(2)

struct AROSAppleIdleControl {
    uint32_t version;
    uint32_t size;
    volatile uint32_t *mode;        /* 0 default, 1 clock-gated, 2 deep */
    volatile uint64_t *entries;     /* idle loop entries, every core */
    volatile uint32_t *ticks_held;  /* non-zero: the secondary cores do not
                                       rearm their scheduler tick, and wait
                                       in idle for an IPI; the first IPI
                                       after it clears rearms the tick */
};

#endif
