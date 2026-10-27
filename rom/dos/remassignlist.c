/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Copyright (C) 1995-2013, The AROS Development Team. All rights reserved.

    Desc: RemAssignList() - Remove an entry from a multi-dir assign.
*/

#include <aros/debug.h>
#include "dos_intern.h"
#include <proto/exec.h>

/*****************************************************************************

    NAME */
#include <proto/dos.h>

        AROS_LH2(LONG, RemAssignList,

/*  SYNOPSIS */
        AROS_LHA(CONST_STRPTR, name, D1),
        AROS_LHA(BPTR  , lock, D2),

/*  LOCATION */
        struct DosLibrary *, DOSBase, 106, Dos)

/*  FUNCTION
        Remove an entry from a multi-dir assign. The entry removed will be
        the first one that the SameLock() function called on the 'lock'
        parameter returns that they belong to the same object.

        The entry for this lock will be removed from the lock, and the
        lock for the entry in the list will be unlocked.

    INPUTS
        name    - Name of the device to remove lock from. This should
                  not contain the trailing ':'.
        lock    - Lock on the object to remove from the list.

    RESULT
        success - Have we actually succeeded

    NOTES

    EXAMPLE

    BUGS
        If this is the first lock in a list, this will not set
        dol_Device/dol_Unit correctly.

    SEE ALSO

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT
    ULONG index = 0, retries = 0;
    struct assign_generation expected = {0, 0};
    BOOL have_generation = FALSE;

    if (!name || !*name || !lock) {
        SetIoErr(ERROR_BAD_NUMBER);
        return DOSFALSE;
    }
    for (;;) {
        struct assign_generation generation, current;
        struct DosList *dl;
        struct AssignList *member = NULL, **link = NULL;
        SIPTR error = 0;
        LONG kind = -1;
        BPTR snapshot, detached = BNULL;
        BOOL same, removed_entry = FALSE;
        snapshot = dup_assign_member(name, index, &generation, NULL,
            &kind, &error, DOSBase);
        if (!snapshot) {
            if (error == ERROR_NO_MORE_ENTRIES && have_generation &&
                    (expected.low != generation.low || expected.high != generation.high)) {
                index = 0; have_generation = FALSE;
                if (++retries == 64) break;
                continue;
            }
            if (error == ERROR_NO_MORE_ENTRIES) error = ERROR_OBJECT_NOT_FOUND;
            else if (!error) error = ERROR_OBJECT_WRONG_TYPE;
            SetIoErr(error);
            return DOSFALSE;
        }
        /* A previous non-match is only meaningful for this generation. */
        if (have_generation && (expected.low != generation.low ||
                expected.high != generation.high)) {
            UnLock(snapshot);
            index = 0; have_generation = FALSE;
            if (++retries == 64) break;
            continue;
        }
        same = SameLock(snapshot, lock) == LOCK_SAME;
        UnLock(snapshot); /* no DOS list guard during any handler wait */
        dl = LockDosList(LDF_ALL | LDF_WRITE);
        dl = FindDosEntry(dl, name, LDF_ASSIGNS);
        current = assign_generation_current(DOSBase);
        if (!dl || dl->dol_Type != DLT_DIRECTORY ||
                generation.low != current.low || generation.high != current.high) {
            UnLockDosList(LDF_ALL | LDF_WRITE);
            index = 0; have_generation = FALSE;
            if (++retries == 64) break;
            continue;
        }
        if (!same) {
            expected = generation; have_generation = TRUE; ++index;
            UnLockDosList(LDF_ALL | LDF_WRITE);
            continue;
        }
        if (index == 0) {
            detached = dl->dol_Lock;
            member = dl->dol_misc.dol_assign.dol_List;
            if (member) {
                dl->dol_Lock = member->al_Lock;
                dl->dol_Task = ((struct FileLock *)BADDR(dl->dol_Lock))->fl_Task;
                dl->dol_misc.dol_assign.dol_List = member->al_Next;
            } else {
                RemDosEntry(dl);
                removed_entry = TRUE;
            }
        } else {
            ULONG ordinal;
            link = &dl->dol_misc.dol_assign.dol_List;
            for (ordinal = 1; ordinal < index; ++ordinal)
                link = &(*link)->al_Next;
            member = *link;
            detached = member->al_Lock;
            *link = member->al_Next;
        }
        assign_generation_changed(DOSBase);
        UnLockDosList(LDF_ALL | LDF_WRITE);
        UnLock(detached);
        FreeVec(member);
        if (removed_entry) {
            FreeVec(dl->dol_misc.dol_assign.dol_AssignName);
            FreeDosEntry(dl);
        }
        SetIoErr(0);
        return DOSTRUE;
    }
    SetIoErr(ERROR_OBJECT_IN_USE);
    return DOSFALSE;

    AROS_LIBFUNC_EXIT
} /* RemAssignList */
