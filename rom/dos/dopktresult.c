/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include "dos_intern.h"
#include <proto/dos.h>

/*****************************************************************************/
/* NAME */
AROS_LH4(SIPTR, DoPktResult,
/* SYNOPSIS */
    AROS_LHA(struct MsgPort *, port, A0),
    AROS_LHA(LONG, action, D0),
    AROS_LHA(const SIPTR *, args, A1),
    AROS_LHA(SIPTR *, result2, A2),
/* LOCATION */
    struct DosLibrary *, DOSBase, 228, Dos)
/* FUNCTION
 * Send a DOS packet and return both result values without requiring IoErr.
 * Like DoPkt, this may be called by an NT_TASK as well as a Process.
 *
 * INPUTS
 * args points to seven native-width packet arguments. NULL supplies seven
 * zero arguments. result2 may be NULL; otherwise it receives dp_Res2, or an
 * ERROR_* value for transport failure. A NULL handler retains DoPkt's NIL:
 * behavior and returns secondary result zero.
 *
 * RESULT
 * dp_Res1. A Process also receives the secondary result in pr_Result2;
 * ordinary Tasks have no Process fields and use the explicit result2 output.
 * On the NULL-handler/NIL: path, explicit result2 is zero while a Process's
 * existing pr_Result2 is left unchanged, matching the existing DoPkt path.
 *
 * NOTES
 * Introduced in dos.library 50.85. This is a synchronous call: all referenced
 * packet arguments must remain valid until it returns. It adds no cancellation
 * or forced-task-removal guarantee to the existing packet transport.
 */
{
    AROS_LIBFUNC_INIT
    return dopacket(result2, port, action,
        args ? args[0] : 0, args ? args[1] : 0, args ? args[2] : 0,
        args ? args[3] : 0, args ? args[4] : 0, args ? args[5] : 0,
        args ? args[6] : 0);
    AROS_LIBFUNC_EXIT
}
