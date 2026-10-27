/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Copyright (C) 2020, The AROS Development Team. All rights reserved.

    Desc:
*/

#include <aros/debug.h>

#include <proto/exec.h>

#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>

#include <string.h>

#include "dos_intern.h"

/*****************************************************************************

    NAME */
#include <proto/dos.h>

        AROS_LH3(BOOL, AssignAddToList,

/*  SYNOPSIS */
        AROS_LHA(CONST_STRPTR, name, D1),
        AROS_LHA(BPTR, lock, D2),
        AROS_LHA(ULONG, position, D3),

/*  LOCATION */
        struct DosLibrary *, DOSBase, 226, Dos)

/*  FUNCTION

    INPUTS

    RESULT

    NOTES

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT
    struct AssignList *newal;
    STRPTR newname = NULL;
    ULONG retry;
    char lockname[128];
    SIPTR error = ERROR_OBJECT_IN_USE;

    if (!name || !*name || !lock) {
        SetIoErr(ERROR_BAD_NUMBER);
        return DOSFALSE;
    }
    newal = AllocVec(sizeof(*newal), MEMF_PUBLIC | MEMF_CLEAR);
    if (!newal) { SetIoErr(ERROR_NO_FREE_STORE); return DOSFALSE; }
    /* Incoming lock is still caller-owned. Name lookup cannot wait under
     * a namespace guard, even when the handler needs DOS WRITE itself. */
    if (NameFromLock(lock, lockname, sizeof(lockname))) {
        newname = AllocVec(strlen(lockname) + 1, MEMF_PUBLIC);
        if (!newname) { error = ERROR_NO_FREE_STORE; goto failed; }
        CopyMem(lockname, newname, strlen(lockname) + 1);
    }
    for (retry = 0; retry < 64; ++retry) {
        struct DosList *dl;
        struct assign_generation generation, current;
        struct AssignList **link;
        STRPTR path = NULL, oldname = NULL;
        BPTR resolved = BNULL;
        ULONG count;
        BOOL needs_path;
        dl = LockDosList(LDF_ALL | LDF_READ);
        LockDosList(LDF_ENTRY | LDF_READ);
        dl = FindDosEntry(dl, name, LDF_ASSIGNS);
        if (!dl || (dl->dol_Type != DLT_DIRECTORY &&
                dl->dol_Type != DLT_LATE && dl->dol_Type != DLT_NONBINDING)) {
            UnLockDosList(LDF_ENTRY | LDF_READ);
            UnLockDosList(LDF_ALL | LDF_READ);
            error = ERROR_OBJECT_WRONG_TYPE; goto failed;
        }
        generation = assign_generation_current(DOSBase);
        needs_path = !dl->dol_Lock;
        if (needs_path) {
            CONST_STRPTR target = dl->dol_misc.dol_assign.dol_AssignName;
            if (target) {
                path = AllocVec(strlen(target) + 1, MEMF_PUBLIC);
                if (path) CopyMem(target, path, strlen(target) + 1);
            }
            if (!path) {
                UnLockDosList(LDF_ENTRY | LDF_READ);
                UnLockDosList(LDF_ALL | LDF_READ);
                error = target ? ERROR_NO_FREE_STORE : ERROR_INVALID_LOCK;
                goto failed;
            }
        }
        UnLockDosList(LDF_ENTRY | LDF_READ);
        UnLockDosList(LDF_ALL | LDF_READ);
        if (needs_path) {
            resolved = Lock(path, SHARED_LOCK);
            error = IoErr();
            FreeVec(path);
            if (!resolved) goto failed;
        }
        dl = LockDosList(LDF_ALL | LDF_WRITE);
        dl = FindDosEntry(dl, name, LDF_ASSIGNS);
        current = assign_generation_current(DOSBase);
        if (!dl || generation.low != current.low || generation.high != current.high) {
            UnLockDosList(LDF_ALL | LDF_WRITE);
            UnLock(resolved);
            continue;
        }
        count = 0;
        for (link = &dl->dol_misc.dol_assign.dol_List;
                *link && count < position + 1; link = &(*link)->al_Next, ++count)
            ;
        if (!count) {
            newal->al_Lock = dl->dol_Lock ? dl->dol_Lock : resolved;
            resolved = BNULL;
            dl->dol_Lock = lock;
            dl->dol_Task = ((struct FileLock *)BADDR(lock))->fl_Task;
            dl->dol_Type = DLT_DIRECTORY;
            if (newname) {
                oldname = dl->dol_misc.dol_assign.dol_AssignName;
                dl->dol_misc.dol_assign.dol_AssignName = newname;
                newname = NULL;
            }
        } else {
            newal->al_Lock = lock;
            if (!dl->dol_Lock) {
                dl->dol_Lock = resolved;
                resolved = BNULL;
            }
            dl->dol_Task = ((struct FileLock *)BADDR(dl->dol_Lock))->fl_Task;
            dl->dol_Type = DLT_DIRECTORY;
        }
        newal->al_Next = *link;
        *link = newal;
        assign_generation_changed(DOSBase);
        UnLockDosList(LDF_ALL | LDF_WRITE);
        UnLock(resolved);
        FreeVec(oldname);
        FreeVec(newname);
        SetIoErr(0);
        return DOSTRUE;
    }
    error = ERROR_OBJECT_IN_USE;
failed:
    FreeVec(newname);
    FreeVec(newal);
    SetIoErr(error ? error : ERROR_BAD_NUMBER);
    return DOSFALSE;

    AROS_LIBFUNC_EXIT
} /* AssignAddToList */
