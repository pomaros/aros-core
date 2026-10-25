/*
    Copyright (C) 1995-2011, The AROS Development Team. All rights reserved.

    Desc:
*/

#include <aros/debug.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <string.h>

#include "dos_intern.h"

BOOL getpacketinfo(struct DosLibrary *DOSBase, CONST_STRPTR name, struct PacketHelperStruct *phs)
{
    CONST_STRPTR colon = strchr(name, ':');
    BSTR bstrname;

    phs->name = BNULL;
    phs->port = NULL;
    phs->lock = BNULL;
    phs->dp = NULL;
    if (colon == NULL)
    {
        struct Process *me = (struct Process *)FindTask(NULL);
        BPTR cur;
        struct FileLock *fl;

        ASSERT_VALID_PROCESS(me);
        bstrname = C2BSTR(name);
        if (!bstrname) {
            SetIoErr(ERROR_NO_FREE_STORE);
            return FALSE;
        }
        cur = me->pr_CurrentDir;
        if (cur && cur != (BPTR)-1) {
            fl = BADDR(cur);
            phs->port = fl->fl_Task;
            phs->lock = cur;
        } else {
            phs->port = DOSBase->dl_Root->rn_BootProc;
            phs->lock = BNULL;
        }
        phs->name = bstrname;
        return TRUE;
    } else {
        struct DevProc *dvp = GetDeviceProc(name, NULL);
        if (dvp == NULL)
            return FALSE;
        /* GetDeviceProc resolves the original device/assign and parent.
         * Handler names are relative to that parent, as for Open and Lock. */
        bstrname = C2BSTR(colon + 1);
        if (!bstrname) {
            FreeDeviceProc(dvp);
            SetIoErr(ERROR_NO_FREE_STORE);
            return FALSE;
        }
        phs->name = bstrname;
        phs->port = dvp->dvp_Port;
        phs->lock = dvp->dvp_Lock;
        phs->dp = dvp;
        return TRUE;
    }
}

BOOL getdevpacketinfo(struct DosLibrary *DOSBase, CONST_STRPTR devname, CONST_STRPTR name, struct PacketHelperStruct *phs)
{
    if ((phs->dp = GetDeviceProc(devname, NULL)) == NULL)
        return DOSFALSE;
    /* we're only interested in real devices */
    if (phs->dp->dvp_DevNode == NULL ||
        phs->dp->dvp_DevNode->dol_Type != DLT_DEVICE) {
        FreeDeviceProc(phs->dp);
        SetIoErr(ERROR_DEVICE_NOT_MOUNTED);
        return DOSFALSE;
    }
    phs->port = phs->dp->dvp_Port;
    phs->lock = BNULL;
    phs->name = BNULL;
    if (!name)
        return TRUE;
    phs->name = C2BSTR(name);
    if (!phs->name) {
        FreeDeviceProc(phs->dp);
        SetIoErr(ERROR_NO_FREE_STORE);
        return DOSFALSE;
    }
    return TRUE;
}

void freepacketinfo(struct DosLibrary *DOSBase, struct PacketHelperStruct *phs)
{
    LONG error = IoErr();
    /* A nonbinding assign owns a lock: FreeDeviceProc can call UnLock.
     * Cleanup must preserve the handler's packet error for its caller. */
    if (phs->dp)
        FreeDeviceProc(phs->dp);
    FREEC2BSTR(phs->name);
    SetIoErr(error);
}
