/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Hold the up time across a sleep, and re-arm the deadline after.
 */

#include <proto/exec.h>
#include <proto/timer.h>

#include "timer_intern.h"

/*****************************************************************************

    NAME */
#include <proto/timer.h>

        AROS_LH1(BOOL, PauseUpTime,

/*  SYNOPSIS */
        AROS_LHA(BOOL, paused, D0),

/*  LOCATION */
        struct Device *, TimerBase, 13, Timer)

/*  FUNCTION
        The Apple form of timer.device/PauseUpTime(): see the generic
        autodoc. While the up time is held the deadline is armed for the
        wall-clock list alone, so the last dated request can leave the
        timer disarmed with relative requests still queued; resuming
        therefore re-programs the deadline from every list.

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct TimerBase *tb = GetTimerBase(TimerBase);
    BOOL old;

    AROSAppleTimerLock(tb);
    EClockUpdate(tb);
    old = tb->tb_ElapsedPaused != 0;
    tb->tb_ElapsedPaused = paused ? 1 : 0;
    AROSAppleTimerUnlock(tb);

    /* Both ways: a pause drops the relative deadlines, a resume restores them. */
    if (tb->tb_Platform.tb_DeadlineOwned && !AROSAppleTimerProgram(tb))
        AROSAppleTimerFailQueues(tb);

    return old;

    AROS_LIBFUNC_EXIT
}
