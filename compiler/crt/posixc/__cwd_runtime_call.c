/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include "__cwd_runtime.h"
#include <dos/dosextens.h>
#include <dos/var.h>
#include <proto/dos.h>

/* A named device/volume/assign returns its native DevProc ownership flags.
 * Relative, current-volume and PROGDIR resolutions instead borrow the
 * helper's directory locks and must be made independent before it exits. */
static BOOL helper_directory(const char *name)
{
    const char *p = name;
    static const char program[] = "PROGDIR:";
    unsigned i;
    for (i = 0; i < sizeof(program) - 1; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (c != (unsigned char)program[i]) break;
    }
    if (i == sizeof(program) - 1) return TRUE;
    /* Native GetDeviceProc recognizes a colon even after a slash. Such a
     * named assign must keep its native borrowed-lock iteration contract. */
    while (*p && *p != ':') p++;
    return *p != ':' || p == name;
}

SIPTR __cwd_native_call(APTR data, struct DosLibrary *DOSBase)
{
    const struct __cwd_native_call *call = data;
    const SIPTR *a = call->args;
    SetIoErr(0);
    switch (call->operation) {
    case DOS_PATH_GET_DEVICE: {
        struct DevProc *device = GetDeviceProc((CONST_STRPTR)a[0], (struct DevProc *)a[1]);
        if (device && !(device->dvp_Flags & DVPF_UNLOCK) && device->dvp_Lock &&
            helper_directory((const char *)a[0])) {
            BPTR copy = DupLock(device->dvp_Lock);
            if (!copy) {
                LONG error = IoErr();
                FreeDeviceProc(device);
                SetIoErr(error ? error : ERROR_INVALID_LOCK);
                return 0;
            }
            device->dvp_Lock = copy;
            device->dvp_Flags |= DVPF_UNLOCK;
        }
        return (SIPTR)device;
    }
    case DOS_PATH_LOCK: return (SIPTR)Lock((CONST_STRPTR)a[0], (LONG)a[1]);
    case DOS_PATH_OPEN: return (SIPTR)Open((CONST_STRPTR)a[0], (LONG)a[1]);
    case DOS_PATH_CREATE_DIR: return (SIPTR)CreateDir((CONST_STRPTR)a[0]);
    case DOS_PATH_DELETE: return DeleteFile((CONST_STRPTR)a[0]);
    case DOS_PATH_RENAME: return Rename((CONST_STRPTR)a[0], (CONST_STRPTR)a[1]);
    case DOS_PATH_REPLACE_OBJECT: return ReplaceObject((CONST_STRPTR)a[0], (CONST_STRPTR)a[1]);
    case DOS_PATH_MAKE_LINK: return MakeLink((CONST_STRPTR)a[0], a[1], (LONG)a[2]);
    case DOS_PATH_READ_LINK: return ReadLink((struct MsgPort *)a[0], (BPTR)a[1],
        (CONST_STRPTR)a[2], (STRPTR)a[3], (LONG)a[4]);
    case DOS_PATH_SET_PROTECTION: return SetProtection((CONST_STRPTR)a[0], (LONG)a[1]);
    case DOS_PATH_SET_DATE: return SetFileDate((CONST_STRPTR)a[0], (struct DateStamp *)a[1]);
    case DOS_PATH_SET_COMMENT: return SetComment((CONST_STRPTR)a[0], (CONST_STRPTR)a[1]);
    case DOS_PATH_SET_OWNER: return SetOwner((CONST_STRPTR)a[0], (LONG)a[1]);
    case POSIXC_CONTEXT_NAME_FROM_LOCK: return NameFromLock((BPTR)a[0], (STRPTR)a[1], (LONG)a[2]);
    case POSIXC_CONTEXT_LOAD_SEG: return (SIPTR)LoadSeg((CONST_STRPTR)a[0]);
    case POSIXC_CONTEXT_SYSTEM: {
        char **environment = (char **)a[4];
        *(BOOL *)a[5] = TRUE;
        for (; environment && *environment; environment++) {
            char *equals = *environment;
            while (*equals && *equals != '=') equals++;
            if (*equals) {
                BOOL ok;
                *equals = 0;
                ok = SetVar((CONST_STRPTR)*environment, (CONST_STRPTR)(equals + 1),
                    -1, GVF_LOCAL_ONLY);
                *equals = '=';
                if (!ok) return -1;
            }
        }
        SetIoErr(0);
        return SystemTags((CONST_STRPTR)a[0], SYS_Input, (IPTR)a[1],
            SYS_Output, (IPTR)a[2], SYS_Error, (IPTR)a[3], TAG_DONE);
    }
    default: SetIoErr(ERROR_ACTION_NOT_KNOWN); return 0;
    }
}
