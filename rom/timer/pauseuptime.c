/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1.

    Desc: Hold the up time across a sleep.
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
        Hold the up time (GetUpTime(), and the clock behind UNIT_VBLANK and
        UNIT_MICROHZ requests) while the machine sleeps, so that a relative
        request measures awake time only and a program that posted a
        two-hour timeout does not see it expire because the lid was closed.
        The wall clock (GetSysTime(), UNIT_WAITUNTIL) and ReadEClock() keep
        counting.

        The sleep transition pauses the up time after FreezeTasks() and
        resumes it before ThawTasks(). Nothing else should call this.

    INPUTS
        paused - TRUE to hold the up time, FALSE to let it run.

    RESULT
        The previous state.

    NOTES
        Honoured by the platform's EClockUpdate(); a platform that ignores
        tb_ElapsedPaused keeps counting, and the sleep transition reports
        that the up time did not hold. Private to this tree.

    SEE ALSO
        GetUpTime(), exec.library/FreezeTasks()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    BOOL old;

    Disable();
    /* Bring the counters up to date at the boundary, both ways. */
    EClockUpdate(GetTimerBase(TimerBase));
    old = GetTimerBase(TimerBase)->tb_ElapsedPaused != 0;
    GetTimerBase(TimerBase)->tb_ElapsedPaused = paused ? 1 : 0;
    Enable();

    return old;

    AROS_LIBFUNC_EXIT
}
