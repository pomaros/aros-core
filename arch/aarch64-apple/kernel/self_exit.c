/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "self_exit.h"
enum { EMPTY, ARMING, PENDING, PUBLISHING };
int AppleSelfExitQueue(struct AppleSelfExit *slot, unsigned cpu,
    void *task, void *base, AppleExitPublish publish)
{
    unsigned expected = EMPTY;
    if (!slot || !task || !base || !publish ||
        !__atomic_compare_exchange_n(&slot->state, &expected, ARMING, 0,
            __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return 0;
    slot->cpu = cpu;
    slot->task = task;
    slot->base = base;
    slot->publish = publish;
    __atomic_store_n(&slot->state, PENDING, __ATOMIC_RELEASE);
    return 1;
}
int AppleSelfExitDetached(struct AppleSelfExit *slot, unsigned cpu, void *departing)
{
    unsigned expected;
    void *task, *base;
    AppleExitPublish publish;
    if (!slot) return 0;
    expected = __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE);
    if (expected == EMPTY) return APPLE_EXIT_EMPTY;
    if (expected != PENDING || slot->cpu != cpu || slot->task != departing)
        return 0;
    if (!__atomic_compare_exchange_n(&slot->state, &expected, PUBLISHING, 0,
        __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) return 0;
    task = slot->task;
    base = slot->base;
    publish = slot->publish;
    slot->task = 0;
    slot->base = 0;
    slot->publish = 0;
    /* May free task immediately on another CPU. Nothing below reads it.
     * Retain PUBLISHING across the callback to reject reentrant admission. */
    publish(task, base);
    __atomic_store_n(&slot->state, EMPTY, __ATOMIC_RELEASE);
    return APPLE_EXIT_PUBLISHED;
}
