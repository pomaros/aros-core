#ifndef AROS_APPLE_TIMER_PLATFORM_H
#define AROS_APPLE_TIMER_PLATFORM_H

/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Apple timer.device private counter state.
 */

#include <aros/apple/platformtimer.h>

struct PlatformTimer
{
    struct KrnPlatformTimer *tb_KPT;
    UQUAD                    tb_LastTime;
    ULONG                    tb_DeadlineOwned;
    ULONG                    tb_DeadlineFault;
    ULONG                    tb_StateLock;
};

struct TimerBase;
void AROSAppleTimerLock(struct TimerBase *);
void AROSAppleTimerUnlock(struct TimerBase *);

BOOL AROSAppleTimerProgram(struct TimerBase *TimerBase);
void AROSAppleTimerFailQueues(struct TimerBase *TimerBase);
void AROSAppleEClockUpdateAt(struct TimerBase *TimerBase, UQUAD now);

#endif /* AROS_APPLE_TIMER_PLATFORM_H */
