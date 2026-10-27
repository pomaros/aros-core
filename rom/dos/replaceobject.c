/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Desc: Atomically replace a regular file on one filesystem.
*/
#include <proto/exec.h>
#include <proto/dos.h>
#include "dos_intern.h"
#include "taskpathresolver.h"
#include <string.h>

AROS_LH2(BOOL, ReplaceObject,
    AROS_LHA(CONST_STRPTR, source, D1),
    AROS_LHA(CONST_STRPTR, target, D2),
    struct DosLibrary *, DOSBase, 227, Dos)
{
    AROS_LIBFUNC_INIT
    DOS_PATH_DISPATCH(DOS_PATH_REPLACE_OBJECT, BOOL, (SIPTR)source, (SIPTR)target);
    struct DevProc *from, *to;
    BSTR source_name, target_name;
    LONG result, error;
    CONST_STRPTR source_relative, target_relative;

    from = GetDeviceProc(source, NULL);
    if (from == NULL)
        return DOSFALSE;
    to = GetDeviceProc(target, NULL);
    if (to == NULL) {
        error = IoErr();
        FreeDeviceProc(from);
        SetIoErr(error);
        return DOSFALSE;
    }
    if (from->dvp_Port != to->dvp_Port) {
        FreeDeviceProc(to);
        FreeDeviceProc(from);
        SetIoErr(ERROR_RENAME_ACROSS_DEVICES);
        return DOSFALSE;
    }
    /* DeviceProc has already resolved the original assign and parent.
     * Passing its prefix again would reset an assign-relative handler path
     * to the volume root. Empty suffixes still address the resolved root. */
    source_relative = strchr(source, ':');
    target_relative = strchr(target, ':');
    source_name = (BSTR)C2BSTR(source_relative ? source_relative + 1 : source);
    if (!source_name) {
        FreeDeviceProc(to);
        FreeDeviceProc(from);
        SetIoErr(ERROR_NO_FREE_STORE);
        return DOSFALSE;
    }
    target_name = (BSTR)C2BSTR(target_relative ? target_relative + 1 : target);
    if (!target_name) {
        FREEC2BSTR(source_name);
        FreeDeviceProc(to);
        FreeDeviceProc(from);
        SetIoErr(ERROR_NO_FREE_STORE);
        return DOSFALSE;
    }
    result = dopacket4(DOSBase, NULL, from->dvp_Port,
        ACTION_REPLACE_OBJECT, from->dvp_Lock, source_name,
        to->dvp_Lock, target_name);
    error = IoErr();
    FREEC2BSTR(target_name);
    FREEC2BSTR(source_name);
    FreeDeviceProc(to);
    FreeDeviceProc(from);
    SetIoErr(error);
    return result;
    AROS_LIBFUNC_EXIT
}
