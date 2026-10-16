/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <exec/memory.h>
#include <proto/exec.h>

#include "power_private.h"
#include LC_LIBDEFS_FILE

/*****************************************************************************

    NAME */
        AROS_LH3(APTR, AddPowerWakeA,

/*  SYNOPSIS */
        AROS_LHA(const struct timeval *, date, A0),
        AROS_LHA(struct MsgPort *, port, A1),
        AROS_LHA(const struct TagItem *, tags, A2),

/*  LOCATION */
        struct PowerBase *, PowerBase, 9, Power)

/*  FUNCTION
        Asks to run while the machine sleeps. When the wall-clock date
        (GetSysTime() time) comes during a sleep, the calling task alone is
        thawed, with the tasks named by POWER_WakeWith, and port receives a
        POWER_EVENT_SCHEDULED message whose pm_Deadline is the budget. The
        task does its work and replies; it is then put back to sleep with
        the rest of the machine. A task that has not replied when the budget
        runs out is put back to sleep there, and the wake ends EXPIRED; its
        message stays with it until it replies after the final thaw.

        On battery the wakes are coalesced onto a grid (PowerSleepA()
        POWER_Coalesce and POWER_GridSeconds, 900 s by default), so wakes
        close together cost one wake of the machine. Every other frozen
        task, the DOS handlers included, stays frozen: a wake that needs a
        handler names its task with POWER_WakeWith.

        A wake is one-shot. It fires during the first sleep that covers its
        date, or at once in a sleep that starts after its date.

    INPUTS
        date - when, in GetSysTime() time.
        port - receives the message; owned by the calling task.
        tags - POWER_Budget (ULONG ms, default 30000),
               POWER_WakeWith (struct Task ** NULL-terminated, up to 8).

    RESULT
        A handle for RemPowerWake(), or NULL.

    SEE ALSO
        RemPowerWake(), PowerSleepA()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct PowerWake *wake;
    struct Task **with;

    if (date == NULL || port == NULL)
        return NULL;
    wake = AllocVec(sizeof(*wake), MEMF_PUBLIC | MEMF_CLEAR);
    if (wake == NULL)
        return NULL;
    wake->pw_Task = FindTask(NULL);
    wake->pw_Port = port;
    wake->pw_Date = *date;
    wake->pw_BudgetMs = GetTagData(POWER_Budget, POWER_DEFAULT_BUDGET_MS, tags);
    if (wake->pw_BudgetMs == 0)
        wake->pw_BudgetMs = POWER_DEFAULT_BUDGET_MS;
    with = (struct Task **)GetTagData(POWER_WakeWith, 0, tags);
    while (with != NULL && *with != NULL && wake->pw_WithCount < POWER_WAKE_WITH_MAX)
        wake->pw_With[wake->pw_WithCount++] = *with++;
    wake->pw_Status = POWER_WAKE_PENDING;

    POWER_LOCK(PowerBase);
    AddTail((struct List *)&PowerBase->pb_Wakes, (struct Node *)wake);
    POWER_UNLOCK(PowerBase);
    return wake;

    AROS_LIBFUNC_EXIT
}
