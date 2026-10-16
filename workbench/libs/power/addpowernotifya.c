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
        AROS_LH2(APTR, AddPowerNotifyA,

/*  SYNOPSIS */
        AROS_LHA(struct MsgPort *, port, A0),
        AROS_LHA(const struct TagItem *, tags, A1),

/*  LOCATION */
        struct PowerBase *, PowerBase, 5, Power)

/*  FUNCTION
        Subscribes port to the sleep transitions. Before every freeze the
        port receives a struct PowerMessage with pm_Event POWER_EVENT_SLEEP,
        the depth and pm_Deadline in milliseconds; the transition proceeds
        when the message is replied or at the deadline, whichever is first.
        After every thaw it receives POWER_EVENT_WAKE with the depth, the
        wake source and the seconds slept. Reply every message; the library
        frees it. When the machine reports its power (GetPowerStateA()), a
        change of source or of whole percent sends POWER_EVENT_SOURCE with
        pm_PowerFlags and pm_Percent; without a provider none is sent.
        A subscription holds at most one sleep, one wake and one source
        message: a late reply to the sleep message finds the wake message
        behind it, and a subscriber that never replies its sleep message is
        not sent another until it does. A subscription of the task that
        calls PowerSleepA() is skipped for that transition.

        The subscription belongs to the calling task and ends with it.

    INPUTS
        port - the port to send to; owned by the calling task.
        tags - POWER_Events: mask of (1 << POWER_EVENT_*) wanted, default all.

    RESULT
        A handle for RemPowerNotify(), or NULL when out of memory.

    SEE ALSO
        RemPowerNotify(), PowerSleepA()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct PowerSubscription *subscription;

    if (port == NULL)
        return NULL;
    subscription = AllocVec(sizeof(*subscription), MEMF_PUBLIC | MEMF_CLEAR);
    if (subscription == NULL)
        return NULL;
    subscription->ps_Port = port;
    subscription->ps_Task = FindTask(NULL);
    subscription->ps_Events = GetTagData(POWER_Events,
        (1UL << POWER_EVENT_SLEEP) | (1UL << POWER_EVENT_WAKE) | (1UL << POWER_EVENT_SOURCE), tags);

    power_CollectReplies(PowerBase);
    POWER_LOCK(PowerBase);
    AddTail((struct List *)&PowerBase->pb_Subscriptions, (struct Node *)subscription);
    POWER_UNLOCK(PowerBase);
    if (subscription->ps_Events & (1UL << POWER_EVENT_SOURCE))
        power_StartWatcher(PowerBase);
    return subscription;

    AROS_LIBFUNC_EXIT
}
