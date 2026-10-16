/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <aros/libcall.h>
#include <exec/errors.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include "timer_intern.h"

AROS_LH1(LONG, AbortIO,
    AROS_LHA(struct timerequest *, request, A1),
    struct TimerBase *, TimerBase, 6, Timer)
{
    AROS_LIBFUNC_INIT
    BOOL found = FALSE;
    AROSAppleTimerLock(TimerBase);
    for (unsigned i = 0; i < NUM_LISTS && !found; ++i) {
        struct timerequest *next;
        ForeachNode(&TimerBase->tb_Lists[i], next) {
            if (next == request) {
                REMOVE(request);
                request->tr_node.io_Error = IOERR_ABORTED;
                request->tr_time.tv_secs = request->tr_time.tv_micro = 0;
                found = TRUE;
                break;
            }
        }
    }
    /* A request detached by expiry is already completing. Do not unlink it
     * from the callback's private completion list or reply to it twice. */
    AROSAppleTimerUnlock(TimerBase);
    if (found) {
        if (TimerBase->tb_Platform.tb_DeadlineOwned && !AROSAppleTimerProgram(TimerBase))
            AROSAppleTimerFailQueues(TimerBase);
        ReplyMsg(&request->tr_node.io_Message);
    }
    return found ? 0 : -1;
    AROS_LIBFUNC_EXIT
}
