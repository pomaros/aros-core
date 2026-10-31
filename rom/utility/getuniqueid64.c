/*
    Copyright (C) 2026 John KNIPPER. All rights reserved.
    Licensed under the AROS Public License (APL) 1.1; see LICENSE.
*/
#include <proto/exec.h>
#include "intern.h"
#include "uniqueid64.h"

/*****************************************************************************
    NAME */
#include <proto/utility.h>

        AROS_LH0(UQUAD, GetUniqueID64,
/* SYNOPSIS */
        /* void */
/* LOCATION */
        struct UtilityBase *, UtilityBase, 67, Utility)
/* FUNCTION
        Allocate a system-wide identifier unique among successful calls to
        GetUniqueID64() during this boot. Values start at 0x100000000, disjoint
        from the legacy 32-bit GetUniqueID() range. The utility resident retains
        the counter across caller exit and library closes. A shared semaphore
        serializes callers on all processors; no local interrupt masking is used.
   RESULT
        A nonzero 64-bit identifier, or zero after the counter is exhausted.
        Exhaustion is permanent for this boot; identifiers never wrap or reuse.
   NOTES
        Requires utility.library version 51. This function may block on the
        semaphore and must be called from task context, not an interrupt.
   SEE ALSO
        GetUniqueID()
*****************************************************************************/
{
    AROS_LIBFUNC_INIT
    struct IntUtilityBase *base = GetIntUtilityBase(UtilityBase);
    UQUAD result;
    ObtainSemaphore(&base->ub_UniqueID64Lock);
    /* Use an exact-width local to avoid typedef aliasing between UQUAD and
       uint64_t on targets with different 64-bit integer base types. */
    uint64_t previous = base->ub_LastID64;
    result = utility_unique_id64_next(&previous);
    base->ub_LastID64 = previous;
    ReleaseSemaphore(&base->ub_UniqueID64Lock);
    return result;
    AROS_LIBFUNC_EXIT
}
