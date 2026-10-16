/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <aros/symbolsets.h>
#include <resources/task.h>
#include <proto/exec.h>
#include <proto/task.h>

#include "power_private.h"
#include LC_LIBDEFS_FILE

/* A subscription dies with its task, like a port does. Runs in the context
 * of the caller of RemTask(), outside Forbid(), before exec removes the task. */
AROS_UFH3(IPTR, PowerTaskHook,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(struct Task *, task, A2),
    AROS_UFHA(struct TaskNotifyMsg *, message, A1))
{
    AROS_USERFUNC_INIT
    if (message->tnm_Action == TNA_REMOVED)
        power_TaskRemoved((struct PowerBase *)hook->h_Data, task);
    return 0;
    AROS_USERFUNC_EXIT
}

static int Power_Init(LIBBASETYPEPTR LIBBASE)
{
    APTR TaskResBase;

#if defined(__AROSEXEC_SMP__)
    InitSemaphore(&LIBBASE->pb_Lock);
#endif
    NEWLIST(&LIBBASE->pb_Subscriptions);
    NEWLIST(&LIBBASE->pb_Wakes);
    LIBBASE->pb_ReplyPort.mp_Flags = PA_IGNORE;
    LIBBASE->pb_ReplyPort.mp_Node.ln_Type = NT_MSGPORT;
    NEWLIST(&LIBBASE->pb_ReplyPort.mp_MsgList);
    LIBBASE->pb_UtilityBase = OpenLibrary("utility.library", 36);
    if (LIBBASE->pb_UtilityBase == NULL)
        return FALSE;

    LIBBASE->pb_TaskHook.h_Entry = (HOOKFUNC)PowerTaskHook;
    LIBBASE->pb_TaskHook.h_Data = LIBBASE;
    /* Without the hook a dead task's subscription would stay and receive
     * messages in a freed port: no hook, no library. */
    TaskResBase = OpenResource("task.resource");
    if (TaskResBase == NULL || !AddTaskNotifyHook(&LIBBASE->pb_TaskHook))
    {
        CloseLibrary(LIBBASE->pb_UtilityBase);
        return FALSE;
    }
    LIBBASE->pb_TaskResBase = TaskResBase;
    /* The platform's power source, when it has one. */
    if (power_ProviderInit(LIBBASE))
        LIBBASE->pb_Provider = LIBBASE;
    return TRUE;
}

ADD2INITLIB(Power_Init, 0)
