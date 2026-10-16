/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * This file is distributed under the AROS Public License, version 1.1.
 */
#include <aros/debug.h>
#include <aros/symbolsets.h>
#include <devices/input.h>
#include <devices/rawkeycodes.h>
#include <libraries/kms.h>
#include <proto/exec.h>
#include <proto/keymap.h>

#include "kms_intern.h"

/* The input device supplies the initialized library state with each call. */
AROS_UFH2(static struct InputEvent *, kms_InputHandler,
          AROS_UFHA(struct InputEvent *, event, A0),
          AROS_UFHA(struct kms_base *, KMSBase, A1))
{
    AROS_USERFUNC_INIT
    struct Library *KeymapBase = KMSBase->keymap;

    return HandleKeyMapSwitcher(event) ? NULL : event;
    AROS_USERFUNC_EXIT
}

static ULONG KMS_Init(struct kms_base *KMSBase)
{
    struct MsgPort *mp;
    struct IOStdReq *ioreq;
    LONG error = TRUE;

    /* Version 43 owns synchronized switching, without function-vector patches. */
    if (KeymapBase->lib_Version < 43)
        return FALSE;
    InitSemaphore(&KMSBase->akmd_lock);

    KMSBase->kmr = OpenResource("keymap.resource");
    if (!KMSBase->kmr)
	return FALSE;

    KMSBase->keymap = KeymapBase;

    mp = CreateMsgPort();
    if (!mp)
	return FALSE;

    ioreq = CreateIORequest(mp, sizeof(struct IOStdReq));
    if (ioreq)
    {
	error = OpenDevice("input.device", 0, (struct IORequest *)ioreq, 0);
	if (!error)
	{
	    /*
	     * Keymap switcher should work no matter what.
	     * So we use the highest possible priority.
	     */
	    KMSBase->input_Int.is_Node.ln_Name = "Keymap switcher";
	    KMSBase->input_Int.is_Node.ln_Pri  = 127;
	    KMSBase->input_Int.is_Code = (void (*)())kms_InputHandler;
	    KMSBase->input_Int.is_Data = KMSBase;

	    ioreq->io_Command = IND_ADDHANDLER;
	    ioreq->io_Data = &KMSBase->input_Int;

	    error = DoIO((struct IORequest *)ioreq);

	    D(bug("[KMS] Installed input handler\n"));
	    
	    CloseDevice((struct IORequest *)ioreq);
	}

	DeleteIORequest((struct IORequest *)ioreq);
    }
    DeleteMsgPort(mp);

    if (error)
	return FALSE;

    return TRUE;
}

ADD2INITLIB(KMS_Init, 0)
