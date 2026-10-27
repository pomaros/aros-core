/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include "dos_intern.h"
#include <proto/dos.h>

struct assign_copy_guard {
    struct DosLibrary *dos;
    BOOL held, entry_held;
};
static void assign_copy_release(APTR data)
{
    struct assign_copy_guard *guard = data;
    struct DosLibrary *DOSBase = guard->dos;
    if (guard->entry_held) {
        guard->entry_held = FALSE;
        UnLockDosList(LDF_ENTRY | LDF_READ);
    }
    if (guard->held) {
        guard->held = FALSE;
        UnLockDosList(LDF_ALL | LDF_READ);
    }
}

void assign_generation_changed(struct DosLibrary *DOSBase)
{
    if (++IDosBase(DOSBase)->assignGenerationLow == 0)
        ++IDosBase(DOSBase)->assignGenerationHigh;
}

struct assign_generation assign_generation_current(struct DosLibrary *DOSBase)
{
    struct assign_generation result;
    result.low = IDosBase(DOSBase)->assignGenerationLow;
    result.high = IDosBase(DOSBase)->assignGenerationHigh;
    return result;
}

BPTR dup_assign_member(CONST_STRPTR name, ULONG index,
    struct assign_generation *generation, struct MsgPort **handler,
    LONG *kind, SIPTR *result2, struct DosLibrary *DOSBase)
{

    struct assign_copy_guard guard = { DOSBase, FALSE, FALSE };
    struct DosList *list, *entry;
    struct MsgPort *port = NULL;
    struct FileLock *lock;
    BPTR source;
    struct AssignList *member;
    ULONG ordinal;
    SIPTR error = 0;
    BPTR result = BNULL;

    if (handler) *handler = NULL;
    if (kind) *kind = -1;
    if (result2) *result2 = 0;
    if (!name || !*name) { error = ERROR_BAD_NUMBER; goto out; }
    list = LockDosList(LDF_ALL | LDF_READ);
    if (!list) { error = ERROR_BAD_NUMBER; goto out; }
    guard.held = TRUE;
    LockDosList(LDF_ENTRY | LDF_READ);
    guard.entry_held = TRUE;
    entry = FindDosEntry(list, name, LDF_ALL);
    if (!entry) { error = ERROR_DEVICE_NOT_MOUNTED; goto out; }
    if (kind) *kind = entry->dol_Type;
    if (entry->dol_Type != DLT_DIRECTORY) {
        if (entry->dol_Type != DLT_DEVICE && entry->dol_Type != DLT_VOLUME &&
            entry->dol_Type != DLT_LATE && entry->dol_Type != DLT_NONBINDING)
            error = ERROR_BAD_NUMBER;
        goto out;
    }
    if (generation) *generation = assign_generation_current(DOSBase);
    source = entry->dol_Lock;
    if (index) {
        member = entry->dol_misc.dol_assign.dol_List;
        for (ordinal = 1; member && ordinal < index; ++ordinal)
            member = member->al_Next;
        if (!member) { error = ERROR_NO_MORE_ENTRIES; goto out; }
        source = member->al_Lock;
    }
    lock = BADDR(source);
    if (!lock || !(port = lock->fl_Task)) { error = ERROR_INVALID_LOCK; goto out; }
    result = (BPTR)dopacket_after_send(&error, port, ACTION_COPY_DIR,
        (SIPTR)source, 0, 0, 0, 0, 0, 0, assign_copy_release, &guard);
    if (!result && !error) error = ERROR_BAD_NUMBER;
    if (result && handler) *handler = port;
out:
    assign_copy_release(&guard);
    if (result2) *result2 = error;
    SetIoErr((LONG)error);
    return result;
}

/*****************************************************************************/
/* NAME */
AROS_LH4(BPTR, DupAssignLock,
/* SYNOPSIS */
    AROS_LHA(CONST_STRPTR, name, A0),
    AROS_LHA(struct MsgPort **, handler, A1),
    AROS_LHA(LONG *, kind, A2),
    AROS_LHA(SIPTR *, result2, A3),
/* LOCATION */
    struct DosLibrary *, DOSBase, 231, Dos)
/* FUNCTION
 * Capture an owned copy of a bound assign's primary native lock.
 * name is the DOS entry name without a trailing colon. kind receives its
 * DLT_* type, or -1 if no entry was found. Devices/volumes and deferred or
 * nonbinding assignments return BNULL with result2 zero and their type;
 * their handler startup/path resolution belongs outside the list guard.
 * Multiassigns select the primary lock, not a search-path iteration.
 *
 * A nonzero result is caller-owned and handler receives its original port.
 * COPY_DIR is enqueued under READ, then READ is released before waiting.
 * Any later assign removal enqueues FREE_LOCK behind that copy on the same
 * port. Allocation/validation failures before enqueue leave the guard for
 * local cleanup. result2 receives the explicit error; Process IoErr is also
 * set. Ordinary Tasks may call this without any Process fields being read.
 *
 * Introduced in dos.library 50.87. This synchronous transport has no new
 * cancellation or forced-task-removal guarantee.
 */
{
    AROS_LIBFUNC_INIT
    return dup_assign_member(name, 0, NULL, handler, kind, result2, DOSBase);
    AROS_LIBFUNC_EXIT
}
