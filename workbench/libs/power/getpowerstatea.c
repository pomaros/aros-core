/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <proto/exec.h>

#include "power_private.h"
#include LC_LIBDEFS_FILE

/*****************************************************************************

    NAME */
        AROS_LH2(LONG, GetPowerStateA,

/*  SYNOPSIS */
        AROS_LHA(struct PowerState *, state, A0),
        AROS_LHA(const struct TagItem *, tags, A1),

/*  LOCATION */
        struct PowerBase *, PowerBase, 8, Power)

/*  FUNCTION
        Reads what the machine runs on: the battery, the adapter, the
        charge, the current and an estimate of the minutes to empty or to
        full, from the platform's single source of that state. A program
        reads it once and then subscribes to POWER_EVENT_SOURCE with
        AddPowerNotifyA(); it never needs to poll.

    INPUTS
        state - filled; every field is zero without a provider.
        tags  - none yet.

    RESULT
        POWER_OK, or POWER_ERROR_UNAVAILABLE when the machine reports
        nothing about its power (a virtual machine, a desktop without an
        SMC); POWERSTF_STALE is set when the provider's sample is older
        than ten seconds.

    SEE ALSO
        AddPowerNotifyA()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    (void)tags;
    if (state == NULL)
        return POWER_ERROR_ARGUMENT;
    return power_ReadState(PowerBase, state) ? POWER_OK : POWER_ERROR_UNAVAILABLE;

    AROS_LIBFUNC_EXIT
}
