/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <proto/exec.h>

#include "power_private.h"
#include LC_LIBDEFS_FILE

/*****************************************************************************

    NAME */
        AROS_LH1(void, RemPowerNotify,

/*  SYNOPSIS */
        AROS_LHA(APTR, handle, A0),

/*  LOCATION */
        struct PowerBase *, PowerBase, 6, Power)

/*  FUNCTION
        Ends a subscription made with AddPowerNotifyA(). A message of this
        subscription still in the caller's port must be replied first; one
        that is out is kept until it comes back, and the subscription is
        freed then.

    INPUTS
        handle - what AddPowerNotifyA() returned; NULL is ignored.

    SEE ALSO
        AddPowerNotifyA()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct PowerSubscription *subscription = handle;

    if (subscription == NULL)
        return;
    power_CollectReplies(PowerBase);
    POWER_LOCK(PowerBase);
    if (subscription->ps_Sleep == NULL && subscription->ps_Wake == NULL && subscription->ps_SourceMsg == NULL)
    {
        Remove((struct Node *)subscription);
        FreeVec(subscription);
    }
    else
    {
        subscription->ps_Removed = TRUE;
        subscription->ps_Port = NULL;
    }
    POWER_UNLOCK(PowerBase);

    AROS_LIBFUNC_EXIT
}
