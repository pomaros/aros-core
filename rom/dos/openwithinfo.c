/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1.
 *
 * Open a regular file with a handler-atomic creation outcome. The first
 * supported mode is MODE_NEWFILE (exclusive create-or-truncate). Handlers
 * without ACTION_FINDOUTPUT_INFO refuse; there is no check-then-open fallback.
 * Soft links are returned as ERROR_IS_SOFT_LINK, not followed by this API.
 */
#include <proto/dos.h>
#include "dos_intern.h"
#include "taskpathresolver.h"
#include <string.h>

AROS_LH3(BPTR, OpenWithInfo,
    AROS_LHA(CONST_STRPTR, name, D1),
    AROS_LHA(LONG, mode, D2),
    AROS_LHA(ULONG *, information, A0),
    struct DosLibrary *, DOSBase, 232, Dos)
{
    AROS_LIBFUNC_INIT
    DOS_PATH_DISPATCH(DOS_PATH_OPEN_WITH_INFO, BPTR,
        (SIPTR)name, (SIPTR)mode, (SIPTR)information);
    struct DevProc *device;
    struct FileHandle *handle;
    CONST_STRPTR relative;
    ULONG result = 0;
    LONG error;

    if (!name || !information || mode != MODE_NEWFILE) {
        SetIoErr(ERROR_BAD_NUMBER);
        return BNULL;
    }
    *information = 0;
    device = GetDeviceProc(name, NULL);
    if (!device) return BNULL;
    handle = AllocDosObject(DOS_FILEHANDLE, NULL);
    if (!handle) {
        FreeDeviceProc(device);
        SetIoErr(ERROR_NO_FREE_STORE);
        return BNULL;
    }
    relative = strchr(name, ':');
    error = fs_OpenWithInfo(handle, device->dvp_Port, device->dvp_Lock,
        relative ? relative + 1 : name, &result, DOSBase);
    FreeDeviceProc(device);
    if (error) {
        FreeDosObject(DOS_FILEHANDLE, handle);
        SetIoErr(error);
        return BNULL;
    }
    *information = result;
    SetIoErr(0);
    return MKBADDR(handle);
    AROS_LIBFUNC_EXIT
}
