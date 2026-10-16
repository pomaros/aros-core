/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Convert the Apple architectural counter into AROS timer time.
 */

#include <proto/exec.h>
#include "timer_intern.h"

/* No replies, allocation or blocking Exec calls while holding this lock. */
void AROSAppleTimerLock(struct TimerBase *TimerBase)
{
    Disable();
    while (__atomic_exchange_n(&TimerBase->tb_Platform.tb_StateLock, 1, __ATOMIC_ACQUIRE))
        __asm__ volatile("yield");
}

void AROSAppleTimerUnlock(struct TimerBase *TimerBase)
{
    __atomic_store_n(&TimerBase->tb_Platform.tb_StateLock, 0, __ATOMIC_RELEASE);
    Enable();
}


static ULONG ticks_to_microseconds(struct TimerBase *TimerBase, ULONG ticks)
{
    return (ULONG)(((UQUAD)ticks * 1000000ULL) /
        TimerBase->tb_eclock_rate);
}

static UQUAD microseconds_to_ticks(struct TimerBase *TimerBase, UQUAD usec)
{
    return (usec * TimerBase->tb_eclock_rate) / 1000000ULL;
}

static void add_ticks(
    struct TimerBase *TimerBase, struct timeval *time,
    ULONG *fraction, UQUAD difference)
{
    UQUAD value = *fraction + difference;

    if (value >= TimerBase->tb_eclock_rate)
    {
        time->tv_secs += (ULONG)(value / TimerBase->tb_eclock_rate);
        value %= TimerBase->tb_eclock_rate;
    }
    *fraction = (ULONG)value;
    time->tv_micro = ticks_to_microseconds(TimerBase, (ULONG)value);
}

void AROSAppleEClockUpdateAt(struct TimerBase *TimerBase, UQUAD now)
{
    UQUAD difference = now - TimerBase->tb_Platform.tb_LastTime;

    TimerBase->tb_Platform.tb_LastTime = now;
    TimerBase->tb_ticks_total += difference;
    add_ticks(TimerBase, &TimerBase->tb_CurrentTime,
        &TimerBase->tb_ticks_sec, difference);
    /* PauseUpTime(): the up time holds, so relative requests measure
     * awake time only. The wall clock and EClock keep counting. */
    if (!TimerBase->tb_ElapsedPaused)
        add_ticks(TimerBase, &TimerBase->tb_Elapsed,
            &TimerBase->tb_ticks_elapsed, difference);
}

void EClockUpdate(struct TimerBase *TimerBase)
{
    AROSAppleEClockUpdateAt(TimerBase, KrnPlatformTimerRead());
}

void EClockSet(struct TimerBase *TimerBase)
{
    /* BeginIO updates elapsed time under the state lock before replacing
     * tb_CurrentTime. Only its fractional wall-clock component changes here.
     * EClock/uptime and their counter sample must survive both forward and
     * backward clock corrections, including time spent processing this call. */
    TimerBase->tb_ticks_sec = (ULONG)microseconds_to_ticks(
        TimerBase, TimerBase->tb_CurrentTime.tv_micro);
}
