/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/

#include <exec/processmessage.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include "exec_util.h"

/*****************************************************************************

    NAME */
        AROS_LH4(ULONG, TryProcessMessage,

/*  SYNOPSIS */
        AROS_LHA(struct Task *, task, A0),
        AROS_LHA(ULONG, id, D0),
        AROS_LHA(struct Message *, message, A1),
        AROS_LHA(ULONG, signals, D1),

/*  LOCATION */
        struct ExecBase *, SysBase, 180, Exec)

/*  FUNCTION
        Try to send a message to a process's embedded pr_MsgPort, and/or send
        signals. Match both the recorded task pointer and its unique ID before
        dereferencing the destination. With message NULL and signals zero,
        query the identity without sending anything.

        The SMP implementation excludes task retirement during the operation
        and reserves notification capacity before publishing the message.
        It does not wait for a free notification slot or invoke arbitrary
        callbacks while holding lifetime protection.

    INPUTS
        task    - Previously recorded process task pointer; never dereferenced
                  until its registry identity has been verified.
        id      - That process's et_UniqueID.
        message - Caller-owned message, or NULL for signals/query only.
        signals - Additional signal bits, or zero.

    RESULT
        TPM_OK transfers message ownership. All other results leave it intact.
        TPM_DEAD: the identity is absent or the pointer does not match.
        TPM_BUSY: retiring/unpublished identity or no notification capacity;
                  release caller locks before retrying.
        TPM_INVALID: not a process, or an incompatible embedded message port.
        TPM_UNSUPPORTED: this SMP platform lacks non-waiting IPI reservation.

    NOTES
        Introduced in exec.library 51.9. Callers must check both library
        version and revision before using the vector on an older system.
        Success means accepted, not processed. Process failure after acceptance
        can still prevent a reply. This is not a task-pointer lifetime lease.
        Never use TPM_BUSY as evidence that the owner has died.

    SEE ALSO
        PutMsg(), Signal(), FindTaskByPID()
******************************************************************************/
{
    AROS_LIBFUNC_INIT
#if defined(__AROSEXEC_SMP__)
    return Exec_TryProcessMessage(task, id, message, signals, SysBase);
#else
    struct MsgPort *port;
    ULONG result = TPM_DEAD;

    Disable();
    if (task != NULL && id != 0 && FindTaskByPID(id) == task)
    {
        result = TPM_INVALID;
        if (task->tc_Node.ln_Type == NT_PROCESS)
        {
            port = &((struct Process *)task)->pr_MsgPort;
            if (message == NULL || ((port->mp_Flags & PF_ACTION) == PA_SIGNAL &&
                port->mp_SigTask == task && port->mp_SigBit < 32))
            {
                if (message != NULL)
                    PutMsg(port, message);
                if (signals != 0)
                    Signal(task, signals);
                result = TPM_OK;
            }
        }
    }
    Enable();
    return result;
#endif
    AROS_LIBFUNC_EXIT
}
