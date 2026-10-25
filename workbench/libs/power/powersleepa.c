/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <proto/exec.h>

#include "power_private.h"
#include LC_LIBDEFS_FILE

/*****************************************************************************

    NAME */
        AROS_LH2(LONG, PowerSleepA,

/*  SYNOPSIS */
        AROS_LHA(ULONG, depth, D0),
        AROS_LHA(const struct TagItem *, tags, A0),

/*  LOCATION */
        struct PowerBase *, PowerBase, 7, Power)

/*  FUNCTION
        Performs a sleep transition from the calling task: sends
        POWER_EVENT_SLEEP to every subscriber and waits for the replies or
        the depth's deadline (2 s at POWER_DEPTH_LIGHT, 5 s deeper), freezes
        every other task, holds the up time, sleeps, releases the up time,
        thaws, and sends POWER_EVENT_WAKE. On a machine without the hardware
        for the depth the sleep is a hold of POWER_Seconds on the wall clock.

        A subscription of the calling task itself is skipped: it could not
        reply while it waits here. The calling task is the one task left
        running: it must not touch
        anything a frozen task owns between the freeze and the thaw, which
        includes every DOS handler, so no file or console I/O; the library
        itself does none.

    INPUTS
        depth - POWER_DEPTH_LIGHT, POWER_DEPTH_DEEP or POWER_DEPTH_HIBERNATE.
        tags  - POWER_Seconds (ULONG, default 5): the commanded hold.
                POWER_Source (ULONG, default POWER_SOURCE_COMMAND): the wake
                source reported to the subscribers.
                POWER_Result (struct PowerSleepResult *): filled with what
                happened.
                POWER_Coalesce (ULONG, default POWER_COALESCE_BATTERY): put
                the scheduled wakes on the grid on battery (and when the
                machine does not report its power), always, or never.
                POWER_GridSeconds (ULONG, default 900): the grid.
                POWER_WakeHook (struct Hook *): asked every POWER_CheckMs
                (default 1000) during the hold, in the calling task, with
                every other task frozen: no DOS, no semaphore another task
                may hold (smc.resource's SMCTryReadKey, not SMCReadKey). It
                answers a POWER_SOURCE_* to wake now, reported to the
                subscribers and in psr_WokenBy, or 0 to sleep on. With a
                hook, POWER_Seconds 0 means no limit.

        The scheduled wakes (AddPowerWakeA()) that fall in the sleep run
        during it, each requester thawed alone with its budget, the up time
        running only while one runs.

    RESULT
        POWER_OK, or POWER_ERROR_BUSY (a transition is in progress),
        POWER_ERROR_UNSUPPORTED (this build cannot freeze tasks; nothing was
        frozen, the subscribers were still told and woken),
        POWER_ERROR_DEVICE (no timer.device), POWER_ERROR_ARGUMENT.

    SEE ALSO
        AddPowerNotifyA(), exec.library/FreezeTasks(), timer.device/PauseUpTime()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct PowerSleepResult local, *result;
    ULONG seconds, source, coalesce, grid, check_ms;
    struct Hook *wake_hook;

    if (depth < POWER_DEPTH_LIGHT || depth > POWER_DEPTH_HIBERNATE)
        return POWER_ERROR_ARGUMENT;
    seconds = GetTagData(POWER_Seconds, 5, tags);
    source = GetTagData(POWER_Source, POWER_SOURCE_COMMAND, tags);
    result = (struct PowerSleepResult *)GetTagData(POWER_Result, (IPTR)&local, tags);
    coalesce = GetTagData(POWER_Coalesce, POWER_COALESCE_BATTERY, tags);
    grid = GetTagData(POWER_GridSeconds, POWER_DEFAULT_GRID_S, tags);
    wake_hook = (struct Hook *)GetTagData(POWER_WakeHook, 0, tags);
    check_ms = GetTagData(POWER_CheckMs, 1000, tags);
    if (coalesce > POWER_COALESCE_NEVER || check_ms < 10)
        return POWER_ERROR_ARGUMENT;
    /* No limit needs something that ends the sleep. */
    if (seconds == 0 && wake_hook == NULL)
        return POWER_ERROR_ARGUMENT;
    return power_Transition(PowerBase, depth, seconds, source, coalesce, grid, wake_hook,
                            check_ms, result);

    AROS_LIBFUNC_EXIT
}
