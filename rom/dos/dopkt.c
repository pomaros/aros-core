/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.

    Desc:
*/

#include <aros/debug.h>

#include "dos_intern.h"

/*****************************************************************************

    NAME */
#include <proto/dos.h>

        AROS_LH7I(SIPTR, DoPkt,

/*  SYNOPSIS */
        AROS_LHA(struct MsgPort *, port, D1),
        AROS_LHA(LONG            , action, D2),
        AROS_LHA(SIPTR           , arg1, D3),
        AROS_LHA(SIPTR           , arg2, D4),
        AROS_LHA(SIPTR           , arg3, D5),
        AROS_LHA(SIPTR           , arg4, D6),
        AROS_LHA(SIPTR           , arg5, D7),

/*  LOCATION */
        struct DosLibrary *, DOSBase, 40, Dos)

/*  FUNCTION
        Send a dos packet to a filesystem and wait for the action to complete.

    INPUTS

    RESULT

    NOTES
        Callable from a task.

        This function should NOT be used; it's only here for AmigaOS
        compatibility.

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    return dopacket(NULL, port, action, arg1, arg2, arg3, arg4, arg5, 0, 0);

    AROS_LIBFUNC_EXIT
}

/*
 * All Amiga kickstart versions accept most dos packet dos calls without dosbase in A6.
 * So we have this internal routine here for compatibility purposes.
 */
static SIPTR packet_error(struct Process *me, SIPTR *res2, SIPTR error)
{
    if (res2)
        *res2 = error;
    if (__is_process(me))
        me->pr_Result2 = error;
    return DOSFALSE;
}

SIPTR dopacket_after_send(SIPTR *res2, struct MsgPort *port, LONG action, SIPTR arg1, SIPTR arg2, SIPTR arg3, SIPTR arg4, SIPTR arg5, SIPTR arg6, SIPTR arg7, void (*after_send)(APTR), APTR callback_data)
{
    SIPTR res;
    struct Process   *me = (struct Process *)FindTask(NULL);
    struct DosPacket *dp;
    struct MsgPort   *replyPort;

    ASSERT_VALID_TASK((struct Task *)me);

    if (res2)
        *res2 = 0;

    if (port == NULL)
    {
        /* NIL: */
        D(bug("NULL port => handling NIL:\n"));
        return handleNIL(action, arg1, arg2, arg3);
    }

    /*
     * A packet aimed at a damaged or freed port pointer (a stale handler
     * reference, or a corrupted FileHandle/DeviceNode field) would PutMsg
     * into garbage and fault inside exec. Catch it here, name the caller
     * and target, and fail the request instead.
     */
    if (TypeOfMem(port) == 0 || port->mp_Node.ln_Type != NT_MSGPORT)
    {
        bug("[DoPkt] task '%s': BAD handler port 0x%p (action %ld)"
            " - failing request\n",
            me->pr_Task.tc_Node.ln_Name, port, (long)action);
        return packet_error(me, res2, ERROR_OBJECT_WRONG_TYPE);
    }

    /* First I create a regular dos packet */
    dp = allocdospacket();
    if (NULL == dp)
        return packet_error(me, res2, ERROR_NO_FREE_STORE);

    if (__is_process(me))
        replyPort = &me->pr_MsgPort;
    else
    {
        /*
         * Make sure that tasks can use DoPkt().
         * This is needed, for example, by Dos/Init()
         * when creating the initial Shell.
         */
        replyPort = CreateMsgPort();

        if (NULL == replyPort)
        {
            freedospacket(dp);
            return packet_error(me, res2, ERROR_NO_FREE_STORE);
        }
    }
    
    D(bug("dp=0x%p act=%d port=0x%p reply=0x%p proc=%d 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx '%s'\n",
          dp, action, port, replyPort, __is_process(me), arg1, arg2, arg3, arg4, arg5, arg6, arg7, me->pr_Task.tc_Node.ln_Name));
    dp->dp_Type = action;
    dp->dp_Arg1 = arg1;
    dp->dp_Arg2 = arg2;
    dp->dp_Arg3 = arg3;
    dp->dp_Arg4 = arg4;
    dp->dp_Arg5 = arg5;
    dp->dp_Arg6 = arg6;
    dp->dp_Arg7 = arg7;
    dp->dp_Res1 = 0;
    dp->dp_Res2 = 0;

    internal_SendPkt(dp, port, replyPort);
    /* The packet is now ordered before any later message to this port.
       Release a caller's namespace guard before waiting for the handler. */
    if (after_send) after_send(callback_data);

    /* Did we get different packet back? System is in unstable state. */
    if (internal_WaitPkt(replyPort) != dp)
        Alert(AN_AsyncPkt);

    D(bug("res1=%x res2=%x\n", dp->dp_Res1, dp->dp_Res2));

    res = dp->dp_Res1;
    if (res2)
        *res2 = dp->dp_Res2;

    if (__is_process(me))
        me->pr_Result2 = dp->dp_Res2;
    else
        DeleteMsgPort(replyPort);

    freedospacket(dp);
    return res;
}

/* Existing users retain the exact transport and NIL/error behavior. */
SIPTR dopacket(SIPTR *res2, struct MsgPort *port, LONG action, SIPTR arg1, SIPTR arg2, SIPTR arg3, SIPTR arg4, SIPTR arg5, SIPTR arg6, SIPTR arg7)
{
    return dopacket_after_send(res2, port, action, arg1, arg2, arg3,
        arg4, arg5, arg6, arg7, NULL, NULL);
}
