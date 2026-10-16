/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <proto/exec.h>

#include "power_private.h"
#include LC_LIBDEFS_FILE

/*****************************************************************************

    NAME */
        AROS_LH1(ULONG, RemPowerWake,

/*  SYNOPSIS */
        AROS_LHA(APTR, handle, A0),

/*  LOCATION */
        struct PowerBase *, PowerBase, 10, Power)

/*  FUNCTION
        Ends a wake request and says how it went. A scheduled message
        still out is kept until it is replied, then freed.

    INPUTS
        handle - what AddPowerWakeA() returned; NULL is ignored.

    RESULT
        POWER_WAKE_PENDING (it never fired), POWER_WAKE_RAN or
        POWER_WAKE_EXPIRED.

    SEE ALSO
        AddPowerWakeA()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct PowerWake *wake = handle;
    ULONG status;

    if (wake == NULL)
        return POWER_WAKE_PENDING;
    power_CollectReplies(PowerBase);
    POWER_LOCK(PowerBase);
    status = wake->pw_Status;
    if (wake->pw_Message == NULL && !PowerBase->pb_InTransition)
    {
        Remove((struct Node *)wake);
        FreeVec(wake);
    }
    else
        wake->pw_Removed = TRUE;
    POWER_UNLOCK(PowerBase);
    return status;

    AROS_LIBFUNC_EXIT
}
