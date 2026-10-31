/*
    Copyright (C) 2011-2014, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER <code@jkn.me>

    Desc: Low-level filesystem access functions, packet version
*/

#include <aros/debug.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "dos_intern.h"
#include "fs_driver.h"

LONG fs_LocateObject(BPTR *ret, struct MsgPort *port, BPTR parent, CONST_STRPTR name, LONG accessMode, struct DosLibrary *DOSBase)
{
    SIPTR error = 0;
    BSTR bstrname = C2BSTR(name);

    if (!bstrname)
        return ERROR_NO_FREE_STORE;

    *ret = (BPTR)dopacket3(DOSBase, &error, port, ACTION_LOCATE_OBJECT, parent, bstrname, accessMode);
    FREEC2BSTR(bstrname);

    return (*ret) ? 0 : error;
}

static LONG fs_open(struct FileHandle *handle, struct MsgPort *port, BPTR lock, LONG mode, CONST_STRPTR name, ULONG *information, struct DosLibrary *DOSBase)
{
    ULONG action;
    BSTR bstrname;
    SIPTR error = 0;
    LONG status;

    /* The MODE_* flags exactly match their corresponding ACTION_*
     * flags:
     *
     * MODE_READWRITE = ACTION_FINDUPDATE = 1004
     * MODE_OLDFILE   = ACTION_FINDINPUT  = 1005
     * MODE_NEWFILE   = ACTION_FINDOUTPUT = 1006
     *
     * Even so, we don't want bad data propagating
     * down, so check that the mode is valid.
     */
    if ((mode == MODE_READWRITE) ||
        (mode == MODE_OLDFILE) ||
        (mode == MODE_NEWFILE))
        action = mode;
    else
        return ERROR_NOT_IMPLEMENTED;

    if (information) action = ACTION_FINDOUTPUT_INFO;

    bstrname = C2BSTR(name);
    if (!bstrname)
        return ERROR_NO_FREE_STORE;

    /* Open may retry this same handle on another member of a multiassign.
     * A failed handler (or our default below) must not select the port for
     * the next attempt. Successful handlers may still override the port. */
    handle->fh_Type = NULL;
    status = dopacket3(DOSBase, &error, port, action, MKBADDR(handle), lock, bstrname);
    FREEC2BSTR(bstrname);

    /* A handler that serves its clients on a port other than the one we
     * found it on says so by filling in fh_Type itself. Handlers that do
     * not care leave it alone and keep talking on the port we used here.
     */
    if (!handle->fh_Type)
        handle->fh_Type = port;
    if (status && information) {
        if (status != OPENINFO_CREATED && status != OPENINFO_TRUNCATED) {
            /* Release backing, not the caller-owned FileHandle allocation. */
            dopacket1(DOSBase, NULL, handle->fh_Type, ACTION_END, handle->fh_Arg1);
            return ERROR_BAD_NUMBER;
        }
        *information = status;
    }
    return status ? 0 : (error ? error : ERROR_ACTION_NOT_KNOWN);
}

LONG fs_Open(struct FileHandle *handle, struct MsgPort *port, BPTR lock,
    LONG mode, CONST_STRPTR name, struct DosLibrary *DOSBase)
{
    return fs_open(handle, port, lock, mode, name, NULL, DOSBase);
}

LONG fs_OpenWithInfo(struct FileHandle *handle, struct MsgPort *port, BPTR lock,
    CONST_STRPTR name, ULONG *information, struct DosLibrary *DOSBase)
{
    return fs_open(handle, port, lock, MODE_NEWFILE, name, information, DOSBase);
}

LONG fs_ReadLink(BPTR parent, struct DevProc *dvp, CONST_STRPTR path, STRPTR buffer, ULONG size, struct DosLibrary *DOSBase)
{
    struct MsgPort *port;

    if (parent)
    {
        struct FileLock *fl = BADDR(parent);

        port = fl->fl_Task;
    }
    else
    {
        port   = dvp->dvp_Port;
        parent = dvp->dvp_Lock;
    }

    return ReadLink(port, parent, path, buffer, size);
}

LONG fs_ChangeSignal(BPTR handle, struct Process *task, struct DosLibrary *DOSBase)
{
    SIPTR error = 0;
    LONG status;
    struct FileHandle *fh = BADDR(handle);

    status = dopacket3(DOSBase, &error, fh->fh_Type, ACTION_CHANGE_SIGNAL, fh->fh_Arg1, (IPTR)task, (SIPTR)NULL);

    return status ? 0 : error;
}

LONG fs_AddNotify(struct NotifyRequest *notify, struct DevProc *dvp, BPTR lock, struct DosLibrary *DOSBase)
{
    SIPTR err = 0;
    LONG status = dopacket1(DOSBase, &err, notify->nr_Handler, ACTION_ADD_NOTIFY, (SIPTR)notify);

    return status ? 0 : err;
}
