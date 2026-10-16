/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <aros/libcall.h>
#include <exec/types.h>
#include <devices/newstyle.h>
#include <exec/errors.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include "timer_intern.h"
#include "timer_macros.h"

static const UWORD commands[] = {
    TR_GETSYSTIME, TR_SETSYSTIME, TR_ADDREQUEST, NSCMD_DEVICEQUERY, 0
};

AROS_LH1(void, BeginIO,
    AROS_LHA(struct timerequest *, request, A1),
    struct TimerBase *, TimerBase, 5, Timer)
{
    AROS_LIBFUNC_INIT
    BOOL queued = FALSE;
    BOOL clear = TRUE;
    AROSAppleTimerLock(TimerBase);
    request->tr_node.io_Message.mn_Node.ln_Type = NT_MESSAGE;
    request->tr_node.io_Error = 0;
    EClockUpdate(TimerBase);
    switch (request->tr_node.io_Command) {
    case TR_GETSYSTIME:
        request->tr_time = TimerBase->tb_CurrentTime;
        clear = FALSE;
        break;
    case TR_SETSYSTIME:
        TimerBase->tb_CurrentTime = request->tr_time;
        EClockSet(TimerBase);
        break;
    case TR_ADDREQUEST: {
        IPTR unit = (IPTR)request->tr_node.io_Unit;
        unsigned list;
        if (unit == UNIT_MICROHZ || unit == UNIT_VBLANK) {
            ADDTIME(&request->tr_time, &TimerBase->tb_Elapsed);
            list = unit;
        } else if (unit == UNIT_WAITUNTIL) {
            if (CMPTIME(&TimerBase->tb_CurrentTime, &request->tr_time) <= 0)
                break;
            list = TL_WAITVBL;
        } else {
            request->tr_node.io_Error = IOERR_NOCMD;
            break;
        }
        if (TimerBase->tb_Platform.tb_DeadlineFault) {
            request->tr_node.io_Error = IOERR_ABORTED;
            break;
        }
        struct MinList *queue = &TimerBase->tb_Lists[list];
        struct timerequest *next;
        ForeachNode(queue, next) {
            if (CMPTIME(&next->tr_time, &request->tr_time) < 0)
                break;
        }
        /* The sentinel's predecessor also handles an empty queue. */
        Insert((struct List *)queue, (struct Node *)request,
            next->tr_node.io_Message.mn_Node.ln_Pred);
        request->tr_node.io_Flags &= ~IOF_QUICK;
        queued = TRUE;
        break;
    }
    case NSCMD_DEVICEQUERY: {
        struct IOStdReq *io = (struct IOStdReq *)request;
        clear = FALSE;
        if (request->tr_node.io_Message.mn_Length < sizeof(*io) ||
            io->io_Length < sizeof(struct NSDeviceQueryResult)) {
            request->tr_node.io_Error = IOERR_BADLENGTH;
        } else {
            struct NSDeviceQueryResult *result = io->io_Data;
            if (!result) {
                request->tr_node.io_Error = IOERR_BADADDRESS;
                break;
            }
            result->DevQueryFormat = 0;
            result->SizeAvailable = sizeof(*result);
            result->DeviceType = NSDEVTYPE_TIMER;
            result->DeviceSubType = 0;
            result->SupportedCommands = (UWORD *)commands;
            io->io_Actual = sizeof(*result);
        }
        break;
    }
    default:
        request->tr_node.io_Error = IOERR_NOCMD;
        break;
    }
    BOOL reply = !queued && !(request->tr_node.io_Flags & IOF_QUICK);
    if (!queued && clear)
        request->tr_time.tv_secs = request->tr_time.tv_micro = 0;
    AROSAppleTimerUnlock(TimerBase);
    /* ReplyMsg may send cross-CPU IPIs. Never call it under the state lock. */
    if (TimerBase->tb_Platform.tb_DeadlineOwned && !AROSAppleTimerProgram(TimerBase))
        AROSAppleTimerFailQueues(TimerBase);
    if (reply)
        ReplyMsg(&request->tr_node.io_Message);
    AROS_LIBFUNC_EXIT
}
