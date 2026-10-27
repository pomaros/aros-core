/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.

    Desc: GetDeviceProc() - Find the filesystem for a path.
*/

#include <aros/debug.h>

#include <proto/exec.h>
#include <proto/utility.h>

#include "dos_intern.h"
#include "taskpathresolver.h"

#include <string.h>

/* room for a per-user assign's target once its tokens are expanded */
#define USERPATH_MAX    512

extern struct Process *r(struct DeviceNode *dn, struct DosLibrary *DOSBase);
static struct DevProc *deviceproc_internal(struct DosLibrary *DOSBase, CONST_STRPTR name, struct DevProc *dp);
static struct MsgPort *start_handler(struct DeviceNode *device, CONST_STRPTR path, struct DosLibrary *DOSBase);

/*****************************************************************************

    NAME */
#include <proto/dos.h>

        AROS_LH2(struct DevProc *, GetDeviceProc,

/*  SYNOPSIS */
        AROS_LHA(CONST_STRPTR, name, D1),
        AROS_LHA(struct DevProc *, dp, D2),

/*  LOCATION */
        struct DosLibrary *, DOSBase, 107, Dos)

/*  FUNCTION
        GetDeviceProc() will search for the filesystem handler which
        you should send a command to for a specific path.

        By calling GetDeviceProc() multiple times, the caller will
        be able to handle multi-assign paths.

        The first call to GetDeviceProc() should have the |dp| parameter
        as NULL.

    INPUTS
        name - Name of the object to find.
        dp   - Previous result of GetDeviceProc() or NULL.

    RESULT
        A pointer to a DevProc structure containing the information
        required to send a command to a filesystem.

    NOTES

    EXAMPLE

    BUGS
        Currently doesn't return dvp_DevNode for locks which are
        relative to "PROGDIR:", ":", or the current directory.

    SEE ALSO
        FreeDeviceProc()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT
    DOS_PATH_DISPATCH(DOS_PATH_GET_DEVICE, struct DevProc *,
        (SIPTR)name, (SIPTR)dp);

    struct DevProc *dp2;
    
    D(bug("[GetDeviceProc] '%s':0x%p\n", name, dp));
    dp2 = deviceproc_internal(DOSBase, name, dp);

#if DEBUG
    bug("[GetDeviceProc] = 0x%p", dp2);
    if (dp2)
        bug(", port=0x%p lock=0x%p dv=0x%p\n", dp2->dvp_Port, dp2->dvp_Lock, dp2->dvp_DevNode);
    RawPutChar('\n');
#endif

    return dp2;

    AROS_LIBFUNC_EXIT

} /* GetDeviceProc */

/* skip past *all consecutive entries* with the same lock */
static struct AssignList *deviceproc_nextassignlock(struct AssignList *al, BPTR lock) {
    while (al != NULL && al->al_Lock == lock) {
        al = al->al_Next;
    }
    return al;
}

/* Deferred assign resolution owns its target text across every handler wait.
 * Entry/target snapshots are protected by ALL READ plus ENTRY READ; no borrowed
 * DosList or string survives dropping those guards. */
static BPTR deviceproc_resolve_target(CONST_STRPTR target, STRPTR expanded,
    BOOL nonbinding, struct DosLibrary *DOSBase)
{
    BPTR result;
    struct TagItem tags[] = {{secT_BufferSize, USERPATH_MAX}, {TAG_DONE, 0}};
    if (!nonbinding || !SECURITY_ACTIVE || !strchr(target, '%'))
        return Lock(target, SHARED_LOCK);
    if (!secExpandPathA(target, expanded, tags))
        return IoErr() == ERROR_NOT_IMPLEMENTED ? Lock(target, SHARED_LOCK) : BNULL;
    result = Lock(expanded, SHARED_LOCK);
    if (!result && IoErr() == ERROR_OBJECT_NOT_FOUND) {
        BPTR created = CreateDir(expanded);
        if (created) {
            UnLock(created);
            result = Lock(expanded, SHARED_LOCK);
        }
    }
    return result;
}

/* Enters and returns with ALL READ held, including failures. A nonbinding
 * success returns an owned lock and no borrowed DosList node. A late success
 * promotes the revalidated entry, retaining the resolved lock in the list. */
static BOOL deviceproc_resolve_deferred(struct DosLibrary *DOSBase,
    struct DosList **entry, BPTR *owned)
{
    ULONG attempt;
    *owned = BNULL;
    for (attempt = 0; attempt < 64; ++attempt) {
        struct DosList *dl = *entry, *current;
        struct assign_generation before, after;
        LONG kind, error;
        size_t name_length, target_length;
        STRPTR copy, target, expanded;
        BPTR resolved;
        BOOL unchanged;

        LockDosList(LDF_ENTRY | LDF_READ);
        kind = dl->dol_Type;
        if (kind != DLT_LATE && kind != DLT_NONBINDING) {
            UnLockDosList(LDF_ENTRY | LDF_READ);
            return TRUE;
        }
        if (kind == DLT_LATE && dl->dol_Lock) {
            UnLockDosList(LDF_ENTRY | LDF_READ);
            LockDosList(LDF_ENTRY | LDF_WRITE);
            if (dl->dol_Type == DLT_LATE) {
                dl->dol_Task = ((struct FileLock *)BADDR(dl->dol_Lock))->fl_Task;
                dl->dol_Type = DLT_DIRECTORY;
                assign_generation_changed(DOSBase);
            }
            UnLockDosList(LDF_ENTRY | LDF_WRITE);
            return TRUE;
        }
        if (!dl->dol_misc.dol_assign.dol_AssignName) {
            UnLockDosList(LDF_ENTRY | LDF_READ);
            SetIoErr(ERROR_INVALID_LOCK);
            return FALSE;
        }
        name_length = AROS_BSTR_strlen(dl->dol_Name);
        target_length = strlen(dl->dol_misc.dol_assign.dol_AssignName);
        if (target_length > __SIZE_MAX__ - USERPATH_MAX - 2 ||
            name_length > __SIZE_MAX__ - USERPATH_MAX - 2 - target_length) {
            UnLockDosList(LDF_ENTRY | LDF_READ);
            SetIoErr(ERROR_NO_FREE_STORE);
            return FALSE;
        }
        /* No expunge/reclaim callbacks may reenter DOS under these guards. */
        copy = AllocVec(name_length + target_length + USERPATH_MAX + 2,
            MEMF_PUBLIC | MEMF_NO_EXPUNGE);
        if (!copy) {
            UnLockDosList(LDF_ENTRY | LDF_READ);
            SetIoErr(ERROR_NO_FREE_STORE);
            return FALSE;
        }
        CopyMem(AROS_BSTR_ADDR(dl->dol_Name), copy, name_length);
        copy[name_length] = 0;
        target = copy + name_length + 1;
        CopyMem(dl->dol_misc.dol_assign.dol_AssignName, target, target_length + 1);
        expanded = target + target_length + 1;
        before = assign_generation_current(DOSBase);
        UnLockDosList(LDF_ENTRY | LDF_READ);
        UnLockDosList(LDF_ALL | LDF_READ);

        resolved = deviceproc_resolve_target(target, expanded,
            kind == DLT_NONBINDING, DOSBase);
        error = resolved ? 0 : IoErr();

        dl = LockDosList(LDF_ALL | LDF_READ);
        LockDosList(LDF_ENTRY | LDF_WRITE);
        current = FindDosEntry(dl, copy, LDF_ALL);
        after = assign_generation_current(DOSBase);
        unchanged = current && current->dol_Type == kind &&
            before.low == after.low && before.high == after.high;
        if (unchanged) {
            *entry = current;
            if (resolved && kind == DLT_LATE) {
                current->dol_Lock = resolved;
                current->dol_Task = ((struct FileLock *)BADDR(resolved))->fl_Task;
                current->dol_Type = DLT_DIRECTORY;
                assign_generation_changed(DOSBase);
            } else if (resolved) {
                *owned = resolved;
                *entry = NULL;
            }
            UnLockDosList(LDF_ENTRY | LDF_WRITE);
            FreeVec(copy);
            if (!resolved) SetIoErr(error ? error : ERROR_BAD_NUMBER);
            return resolved != BNULL;
        }
        UnLockDosList(LDF_ENTRY | LDF_WRITE);
        UnLockDosList(LDF_ALL | LDF_READ);
        if (resolved) UnLock(resolved);
        /* Refetch after releasing a discarded result: UnLock may itself
         * reenter DOS and change the namespace. No old node is reused. */
        dl = LockDosList(LDF_ALL | LDF_READ);
        LockDosList(LDF_ENTRY | LDF_READ);
        current = FindDosEntry(dl, copy, LDF_ALL);
        UnLockDosList(LDF_ENTRY | LDF_READ);
        FreeVec(copy);
        *entry = current;
        if (!current) {
            SetIoErr(ERROR_DEVICE_NOT_MOUNTED);
            return FALSE;
        }
    }
    SetIoErr(ERROR_OBJECT_IN_USE);
    return FALSE;
}

static struct DevProc *deviceproc_internal(struct DosLibrary *DOSBase, CONST_STRPTR name, struct DevProc *dp)
{
    struct Process *pr = (struct Process *)FindTask(NULL);
    struct DosList *dl = NULL;
    char vol[32];
    LONG len;
    BPTR lock = BNULL;
    BOOL res;
    CONST_STRPTR origname = name;
    struct FileLock *fl;

    ASSERT_VALID_PROCESS(pr);

    /* if they passed us the result of a previous call, then they want to
     * loop over the targets of a multidirectory assign */
    if (dp != NULL) {

        /* if what they passed us is not a multidirectory assign, then there's
         * nothing for us to do */
        if (!(dp->dvp_Flags & DVPF_ASSIGN) || dp->dvp_DevNode == NULL) {

            /* cleanup */
            if (dp->dvp_Flags & DVPF_UNLOCK)
                UnLock(dp->dvp_Lock);

            FreeMem(dp, sizeof(struct DevProc));
            SetIoErr(ERROR_NO_MORE_ENTRIES);
            return NULL;
        }

        /* it's fine, we'll start from here */
        dl = dp->dvp_DevNode;

        /* lock the dos list here, to match the result of the next block */
        LockDosList(LDF_ALL | LDF_READ);
    }

    /* otherwise we need to find a place to start in the doslist based on the
     * name they passed in */
    else {
        if (name == NULL || Strnicmp(name, "NIL:", 4) == 0) {
            SetIoErr(ERROR_DEVICE_NOT_MOUNTED);
            return NULL;
        }

         /* allocate structure for return */
        if ((dp = AllocMem(sizeof(struct DevProc), MEMF_ANY | MEMF_CLEAR)) == NULL) {
            SetIoErr(ERROR_NO_FREE_STORE);
            return NULL;
        }

        /* something real, work out what it's relative to */
        if (Strnicmp(name, "PROGDIR:", 8) == 0) {
            lock = pr->pr_HomeDir;
            /* I am not sure if these are correct but AOS does return
             * non-NULL PROGDIR: handle even if pr_HomeDir is cleared */
            if (!lock)
                lock = pr->pr_CurrentDir;
            if (lock) {
                fl = BADDR(lock);
                dp->dvp_Port = fl->fl_Task;
                dp->dvp_Lock = lock;
                dp->dvp_DevNode = BADDR(fl->fl_Volume);
            } else {
                dp->dvp_Port = DOSBase->dl_Root->rn_BootProc;
                dp->dvp_Lock = BNULL;
                dp->dvp_DevNode = NULL;
            }
            dp->dvp_Flags = 0;
            return dp;
        }

        /* extract the volume name */
        len = SplitName(name, ':', vol, 0, sizeof(vol) - 1);

        /* if there wasn't one (or we found a lone ':') -> current dir */
        if (len <= 1) {
            lock = pr->pr_CurrentDir;
            /* if we got NULL, then it's relative to the system root lock */
            if (lock != BNULL) {
                fl = BADDR(lock);
                dp->dvp_Port = fl->fl_Task;
                dp->dvp_Lock = lock;
                dp->dvp_DevNode = BADDR(fl->fl_Volume);
            } else {
                dp->dvp_Port = DOSBase->dl_Root->rn_BootProc;
                dp->dvp_Lock = BNULL;
                dp->dvp_DevNode = NULL;
            }

            /* Jump to the root directory if name[0] == ':' */
            if (name[0] == ':')
                dp->dvp_Lock = (dp->dvp_DevNode) ? dp->dvp_DevNode->dol_Lock : BNULL;

            dp->dvp_Flags = 0;
            return dp;
        }
 
        do {
            /* now find the doslist entry for the named volume */
            dl = LockDosList(LDF_ALL | LDF_READ);
            dl = FindDosEntry(dl, vol, LDF_ALL);

            /* not found, bail out */
            if (dl == NULL) {
                UnLockDosList(LDF_ALL | LDF_READ);

                if (ErrorReport(ERROR_DEVICE_NOT_MOUNTED, REPORT_INSERT, (IPTR)vol, NULL) == DOSTRUE) {
                    FreeMem(dp, sizeof(struct DevProc));
                    return NULL;
                }
            }
        } while(dl == NULL);
    }

    /* at this point, we have an allocated devproc in dp, the doslist is
     * locked for read, and we have the entry for the named "volume"
     * (device, assign, etc) in dl and a filename relative to that in name */

    if (!deviceproc_resolve_deferred(DOSBase, &dl, &lock)) {
        UnLockDosList(LDF_ALL | LDF_READ);
        FreeMem(dp, sizeof(*dp));
        return NULL;
    }
    if (lock) {
        dp->dvp_Port = ((struct FileLock *)BADDR(lock))->fl_Task;
        dp->dvp_Lock = lock;
        dp->dvp_Flags = DVPF_UNLOCK;
        dp->dvp_DevNode = NULL;
        UnLockDosList(LDF_ALL | LDF_READ);
        return dp;
    }

    /* devices and volumes are easy */
    if (dl->dol_Type == DLT_DEVICE || dl->dol_Type == DLT_VOLUME)
    {
        struct MsgPort *newhandler = NULL;

        if (dl->dol_Type == DLT_DEVICE)
        {
            /* Check if the handler is not started */
            newhandler = dl->dol_Task;
            if (!newhandler)
            {
                D(bug("[GetDeviceProc] Accessing device '%b', path='%s'\n", dl->dol_Name, origname));

                /*
                 * Unlock before starting handler, handler may internally
                 * require dos list locks, for example to add volume node.
                 */
                UnLockDosList(LDF_ALL | LDF_READ);

                newhandler = start_handler((struct DeviceNode *)dl, origname, DOSBase);
                if (!newhandler)
                {
                    FreeMem(dp, sizeof(struct DevProc));
                    SetIoErr(ERROR_DEVICE_NOT_MOUNTED);
                    return NULL;
                }

                LockDosList(LDF_ALL | LDF_READ);
            }
        }
        else
        {
            res = TRUE;

            while (res && !dl->dol_Task)
            {
                D(bug("[GetDeviceProc] Accessing offline volume '%b'\n", dl->dol_Name));
                res = !ErrorReport(ERROR_DEVICE_NOT_MOUNTED, REPORT_VOLUME, (IPTR)dl, NULL);
            }

            if (!res)
            {
                UnLockDosList(LDF_ALL | LDF_READ);
                FreeMem(dp, sizeof(struct DevProc));
                SetIoErr(ERROR_DEVICE_NOT_MOUNTED);
                return NULL;
            }
        }

        /*
         * A handler theoretically may choose to use custom MsgPort for communications.
         * Pick up its preference if specified.
         */
        dp->dvp_Port = dl->dol_Task ? dl->dol_Task : newhandler;
        dp->dvp_Lock = BNULL;
        dp->dvp_Flags = 0;
        dp->dvp_DevNode = dl;

        UnLockDosList(LDF_ALL | LDF_READ);

        return dp;
    }

    /* sanity check */
    if (dl->dol_Type != DLT_DIRECTORY) {
        UnLockDosList(LDF_ALL | LDF_READ);
        FreeMem(dp, sizeof(struct DevProc));
        kprintf("%s:%d: DosList entry 0x%p has unknown type %d. Probably a bug, report it!\n"
                "    GetDeviceProc() called for '%s'\n",
                __SRCFILENAME__, __LINE__, dl, dl->dol_Type, name);
        SetIoErr(ERROR_BAD_NUMBER);
        return NULL;
    }

    /* real assigns. first, see if it's just pointing to a single dir */
    if (dp->dvp_Flags != DVPF_ASSIGN) {
        /* just a plain assign, easy */
        dp->dvp_Port = dl->dol_Task;
        dp->dvp_Lock = dl->dol_Lock;
        dp->dvp_DevNode = dl;
        
        /* note multidirectory assigns so the caller knows to loop */
        dp->dvp_Flags = dl->dol_misc.dol_assign.dol_List != NULL ? DVPF_ASSIGN : 0;

        UnLockDosList(LDF_ALL | LDF_READ);
        return dp;
    }

    /* finally the tricky bit - multidirectory assigns */

    /* if we're pointing at the "primary" lock, then we just take the first
     * different one in the list */
    if (dp->dvp_Lock == dl->dol_Lock) {
        struct AssignList *al = dl->dol_misc.dol_assign.dol_List;
        al = deviceproc_nextassignlock(al, dp->dvp_Lock);
        if (al == NULL) {
            UnLockDosList(LDF_ALL | LDF_READ);
            FreeMem(dp, sizeof(struct DevProc));

            SetIoErr(ERROR_NO_MORE_ENTRIES);
            return NULL;
        }
        dp->dvp_Lock = al->al_Lock;
    /* otherwise we're finding the next */
    } else {
        struct AssignList *al = dl->dol_misc.dol_assign.dol_List;

        /* Find the first occurrence *after* the current lock */
        if (dp->dvp_Lock != BNULL) {
            /* Skip over entries until we find the current lock */
            for (; al != NULL && al->al_Lock != dp->dvp_Lock; al = al->al_Next)
                ;
            al = deviceproc_nextassignlock(al, dp->dvp_Lock);
        }

        /* Check if we reached the end */
        if (al == NULL) {
            UnLockDosList(LDF_ALL | LDF_READ);
            FreeMem(dp, sizeof(struct DevProc));

            SetIoErr(ERROR_NO_MORE_ENTRIES);
            return NULL;
        }

        /* fill out the lock from the new entry */
        dp->dvp_Lock = al->al_Lock;
    }

    /* final pieces */
    dp->dvp_Port = ((struct FileLock *) BADDR(dp->dvp_Lock))->fl_Task;
    dp->dvp_Flags = DVPF_ASSIGN;
    dp->dvp_DevNode = dl;

    UnLockDosList(LDF_READ|LDF_ALL);
    /* phew */
    SetIoErr(0);
    return dp;
}

/*
 * One start per device. Tasks that ask for a device whose handler is not
 * running all found dn_Task empty, and each ran a handler process for the
 * same DeviceNode; the loser's failed startup cleared dn_Task and its caller
 * saw the device as not mounted (the first pipe() of two threads at once).
 * The first task starts the handler; the others wait for that start and use
 * the port it published. Nested starts (a handler that needs another device
 * while starting) concern other devices, so they do not wait on this one.
 */
struct HandlerStart
{
    struct MinNode          node;
    struct DeviceNode      *device;
    struct SignalSemaphore  done;   /* held by the starting task */
    ULONG                   users;  /* the starter and its waiters; the last one frees */
};

static struct MsgPort *start_handler(struct DeviceNode *device, CONST_STRPTR path, struct DosLibrary *DOSBase)
{
    struct List *starts = &IDosBase(DOSBase)->handlerStarts;
    struct HandlerStart *mine, *running = NULL;
    struct Node *node;
    struct MsgPort *port;
    BOOL last;

    mine = AllocMem(sizeof(*mine), MEMF_PUBLIC | MEMF_CLEAR);
    if (mine == NULL)
        return RunHandler(device, path, DOSBase);
    mine->device = device;
    mine->users = 1;
    InitSemaphore(&mine->done);
    ObtainSemaphore(&mine->done);

    Forbid();
    port = device->dn_Task;
    if (port == NULL)
    {
        for (node = starts->lh_Head; node->ln_Succ != NULL; node = node->ln_Succ)
        {
            if (((struct HandlerStart *)node)->device == device)
            {
                running = (struct HandlerStart *)node;
                running->users++;
                break;
            }
        }
        if (running == NULL)
            AddTail(starts, (struct Node *)mine);
    }
    Permit();

    if (port != NULL || running != NULL)
    {
        ReleaseSemaphore(&mine->done);
        FreeMem(mine, sizeof(*mine));
        if (port != NULL)
            return port;

        /* Another task is starting this handler: wait for the end of its start */
        ObtainSemaphore(&running->done);
        ReleaseSemaphore(&running->done);
        Forbid();
        port = device->dn_Task;
        last = --running->users == 0;
        Permit();
        if (last)
            FreeMem(running, sizeof(*running));
        if (port == NULL)
            SetIoErr(ERROR_DEVICE_NOT_MOUNTED);
        return port;
    }

    port = RunHandler(device, path, DOSBase);

    Forbid();
    Remove((struct Node *)mine);
    last = --mine->users == 0;
    Permit();
    ReleaseSemaphore(&mine->done);
    if (last)
        FreeMem(mine, sizeof(*mine));
    return port;
}
