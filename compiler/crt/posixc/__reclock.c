/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1

    POSIX advisory record locks (fcntl F_GETLK, F_SETLK, F_SETLKW).

    Most AROS filesystems (ram-handler, FAT, AFS) do not implement
    ACTION_LOCK_RECORD, so posixc keeps the locks itself, in one table shared
    by every program.

    A lock belongs to a POSIX process: the retained descriptor namespace, which the pthreads of a program share, so threads of one
    program never conflict with each other, as POSIX requires.

    A file is the filesystem object, not its name: posixc keeps a shared DOS
    lock on it (DupLockFromFH) and recognises it with SameLock, so a rename
    while it is open changes nothing, and a new file at the old name is
    another file. An exclusive handle refuses a second lock; no other
    process can open its file, so that handle itself names it. The
    descriptor's fcb keeps the file it resolved to, so closing it needs
    neither memory nor the handler.

    Closing any descriptor of a file releases the process's locks on that
    file, as does the end of the process.
*/

#include <aros/debug.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <dos/dosextens.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

#include <libraries/posixc.h>

#include "__posixc_intbase.h"
#include "__fdesc.h"
#include "__fd_namespace.h"
#include <aros/symbolsets.h>
#include "__reclock.h"

#define RECLOCK_EOF INT64_MAX   /* a lock "to the end of the file" */

/* One file that locks name: kept while an fcb or a lock refers to it */
struct reclock_file
{
    struct MinNode node;
    struct MsgPort *port;       /* its filesystem */
    BPTR lock;                  /* a shared lock on it, or BNULL... */
    BPTR handle;                /* ...for the exclusive handle that names it */
    ULONG refs;
};

struct reclock
{
    struct MinNode node;
    struct reclock_file *file;
    int64_t start, end;         /* inclusive byte range */
    short type;                 /* F_RDLCK or F_WRLCK */
    APTR owner;                 /* retained namespace identity: POSIX process */
    pid_t pid;                  /* for F_GETLK */
};

static struct SignalSemaphore reclock_sem;
static struct MinList reclock_files;
static struct MinList reclock_list;
/* Module initialization precedes publication/OpenLib. A process-local
   Forbid cannot serialize lazy initialization across SMP CPUs. */
static int reclock_init(struct PosixCIntBase *base)
{
    (void)base;
    InitSemaphore(&reclock_sem);
    NEWLIST((struct List *)&reclock_files);
    NEWLIST((struct List *)&reclock_list);
    return TRUE;
}
ADD2INITLIB(reclock_init, 0);

static struct __fd_namespace *owner_namespace(void)
{
    return __fd_namespace_current();
}

static pid_t owner_pid(struct __fd_namespace *owner)
{
    return owner ? (pid_t)owner->process_id : 0;
}

static void put_file(struct reclock_file *rf)
{
    if (--rf->refs)
        return;
    Remove((struct Node *)rf);
    if (rf->lock)
        UnLock(rf->lock);
    FreeVec(rf);
}

/* The file of an fcb, looked up and, with CREATE, added; NULL with errno
   set when it cannot be named. The semaphore is held. */
static struct reclock_file *file_of(fcb *f, BOOL create)
{
    struct reclock_file *rf;
    struct MsgPort *port;
    BPTR lock;

    if (f->reclock)
        return f->reclock;

    if (f->privflags & _FCB_ISDIR)
    {
        port = ((struct FileLock *)BADDR(f->handle))->fl_Task;
        lock = DupLock(f->handle);
    }
    else
    {
        port = ((struct FileHandle *)BADDR(f->handle))->fh_Type;
        lock = DupLockFromFH(f->handle);
    }
    if (!lock)
    {
        LONG err = IoErr();
        if (err != ERROR_OBJECT_IN_USE)
        {
            /* Without the handler's help two processes could not be seen
               to use the same file: refuse rather than not exclude. */
            errno = err == ERROR_ACTION_NOT_KNOWN || err == 0 ?
                ENOLCK : __stdc_ioerr2errno(err);
            return NULL;
        }
    }

    ForeachNode(&reclock_files, rf)
    {
        if (rf->port != port)
            continue;
        if (lock ? rf->lock && SameLock(rf->lock, lock) == LOCK_SAME :
                   rf->handle == f->handle)
        {
            if (lock)
                UnLock(lock);
            rf->refs++;
            f->reclock = rf;
            return rf;
        }
    }
    if (!create)
    {
        if (lock)
            UnLock(lock);
        return NULL;
    }

    rf = AllocVec(sizeof(*rf), MEMF_PUBLIC | MEMF_CLEAR);
    if (!rf)
    {
        if (lock)
            UnLock(lock);
        errno = ENOLCK;
        return NULL;
    }
    rf->port = port;
    rf->lock = lock;
    rf->handle = lock ? BNULL : f->handle;
    rf->refs = 1;               /* the fcb's */
    AddTail((struct List *)&reclock_files, (struct Node *)rf);
    f->reclock = rf;
    return rf;
}

static BOOL overlaps(struct reclock *r, int64_t start, int64_t end)
{
    return r->start <= end && start <= r->end;
}

static struct reclock *find_conflict(struct reclock_file *rf,
    int64_t start, int64_t end, short type, APTR owner)
{
    struct reclock *r;

    ForeachNode(&reclock_list, r)
    {
        if (r->owner != owner && r->file == rf &&
            overlaps(r, start, end) && (r->type == F_WRLCK || type == F_WRLCK))
            return r;
    }
    return NULL;
}

static struct reclock *new_lock(struct reclock_file *rf,
    int64_t start, int64_t end, short type, APTR owner, pid_t pid)
{
    struct reclock *r = AllocVec(sizeof(*r), MEMF_PUBLIC | MEMF_CLEAR);
    if (!r)
        return NULL;
    r->file = rf;
    rf->refs++;
    r->start = start;
    r->end = end;
    r->type = type;
    r->owner = owner;
    r->pid = pid;
    return r;
}

static void free_lock(struct reclock *r, BOOL listed)
{
    if (listed)
        Remove((struct Node *)r);
    put_file(r->file);
    FreeVec(r);
}

/* Remove the owner's locks on [start, end], splitting those that straddle
   it. A split needs one new lock, allocated before anything changes. */
static int clear_range(struct reclock_file *rf, int64_t start, int64_t end,
    APTR owner)
{
    struct reclock *r, *next;

    ForeachNodeSafe(&reclock_list, r, next)
    {
        if (r->owner != owner || r->file != rf || !overlaps(r, start, end))
            continue;
        if (r->start < start && r->end > end)
        {
            struct reclock *tail = new_lock(rf, end + 1, r->end, r->type, owner, r->pid);
            if (!tail)
                return 0;
            AddTail((struct List *)&reclock_list, (struct Node *)tail);
            r->end = start - 1;
        }
        else if (r->start < start)
            r->end = start - 1;
        else if (r->end > end)
            r->start = end + 1;
        else
            free_lock(r, TRUE);
    }
    return 1;
}

/* The inclusive byte range of FL, or -1 with errno set */
static int lock_range(int fd, struct flock *fl, int64_t *pstart, int64_t *pend)
{
    int64_t base, start, len = fl->l_len;

    switch (fl->l_whence)
    {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = lseek(fd, 0, SEEK_CUR);
        break;
    case SEEK_END:
    {
        struct stat st;
        base = fstat(fd, &st) == 0 ? (int64_t)st.st_size : -1;
        break;
    }
    default:
        errno = EINVAL;
        return -1;
    }
    if (base < 0)
        return -1;
    if (fl->l_start > 0 && base > INT64_MAX - (int64_t)fl->l_start)
    {
        errno = EOVERFLOW;
        return -1;
    }
    start = base + fl->l_start;
    if (start < 0)
    {
        errno = EINVAL;
        return -1;
    }
    if (len > 0)
    {
        if (start > INT64_MAX - (len - 1))
        {
            errno = EOVERFLOW;
            return -1;
        }
        *pend = start + len - 1;
    }
    else if (len == 0)
        *pend = RECLOCK_EOF;
    else
    {
        /* [start + len, start - 1] */
        if (len < -start)
        {
            errno = EINVAL;
            return -1;
        }
        *pend = start - 1;
        start += len;
        if (*pend < start)
        {
            errno = EINVAL;
            return -1;
        }
    }
    *pstart = start;
    return 0;
}

int __posixc_reclock(int fd, fdesc *desc, int cmd, struct flock *fl)
{
    struct __fd_namespace *owner = owner_namespace();
    struct reclock_file *rf;
    int64_t start, end;
    int result = 0;

    if (!fl)
    {
        errno = EFAULT;
        return -1;
    }
    if (fl->l_type != F_RDLCK && fl->l_type != F_WRLCK && fl->l_type != F_UNLCK)
    {
        errno = EINVAL;
        return -1;
    }
    if (fl->l_type == F_RDLCK && cmd != F_GETLK && (desc->fcb->flags & O_ACCMODE) == O_WRONLY)
    {
        errno = EBADF;
        return -1;
    }
    if (fl->l_type == F_WRLCK && cmd != F_GETLK && (desc->fcb->flags & O_ACCMODE) == O_RDONLY)
    {
        errno = EBADF;
        return -1;
    }
    if (lock_range(fd, fl, &start, &end) < 0)
        return -1;

    ObtainSemaphore(&reclock_sem);
    rf = file_of(desc->fcb, TRUE);
    ReleaseSemaphore(&reclock_sem);
    if (!rf)
        return -1;

    /* rf lives while the fcb names it, so the loop may drop the semaphore */
    for (;;)
    {
        struct reclock *conflict;

        ObtainSemaphore(&reclock_sem);
        if (cmd == F_GETLK)
        {
            conflict = fl->l_type == F_UNLCK ? NULL :
                find_conflict(rf, start, end, fl->l_type, owner);
            if (conflict)
            {
                fl->l_type = conflict->type;
                fl->l_whence = SEEK_SET;
                fl->l_start = conflict->start;
                fl->l_len = conflict->end == RECLOCK_EOF ? 0 : conflict->end - conflict->start + 1;
                fl->l_pid = conflict->pid;
            }
            else
                fl->l_type = F_UNLCK;
            ReleaseSemaphore(&reclock_sem);
            break;
        }
        if (fl->l_type == F_UNLCK)
        {
            if (!clear_range(rf, start, end, owner))
            {
                errno = ENOLCK;
                result = -1;
            }
            ReleaseSemaphore(&reclock_sem);
            break;
        }
        conflict = find_conflict(rf, start, end, fl->l_type, owner);
        if (!conflict)
        {
            struct reclock *r = new_lock(rf, start, end, fl->l_type,
                owner, owner_pid(owner));
            if (!r || !clear_range(rf, start, end, owner))
            {
                if (r)
                    free_lock(r, FALSE);
                errno = ENOLCK;
                result = -1;
            }
            else
                AddTail((struct List *)&reclock_list, (struct Node *)r);
            ReleaseSemaphore(&reclock_sem);
            break;
        }
        ReleaseSemaphore(&reclock_sem);
        if (cmd == F_SETLK)
        {
            errno = EAGAIN;
            result = -1;
            break;
        }
        /* F_SETLKW: wait for the holder, a CTRL-C ends the wait */
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
        {
            errno = EINTR;
            result = -1;
            break;
        }
        Delay(1);
    }
    return result;
}

void __posixc_reclock_close(fdesc *desc)
{
    struct __fd_namespace *owner;
    struct reclock_file *rf = NULL;
    struct reclock *r;
    fcb *f = desc->fcb;

    owner = owner_namespace();
    ObtainSemaphore(&reclock_sem);
    if (f->reclock)
        rf = f->reclock;
    else
    {
        /* A descriptor never used for locking: look its file up only when
           the process holds locks at all. */
        ForeachNode(&reclock_list, r)
        {
            if (r->owner == owner)
            {
                rf = file_of(f, FALSE);
                if (!rf)
                    D(bug("[posixc] close: file of fcb %p unknown, errno %d\n", f, errno));
                break;
            }
        }
    }
    if (rf)
        clear_range(rf, 0, RECLOCK_EOF, owner);     /* only shrinks: cannot fail */
    ReleaseSemaphore(&reclock_sem);
}

void __posixc_reclock_release_file(fcb *file)
{
    /* The final block reference owns this field exclusively. A non-NULL
       entry proves the metadata semaphore was initialized before publication;
       ordinary files need not inspect the lazy-init flag or take that lock. */
    if (!file->reclock)
        return;
    ObtainSemaphore(&reclock_sem);
    if (file->reclock)
    {
        put_file(file->reclock);
        file->reclock = NULL;
    }
    ReleaseSemaphore(&reclock_sem);
}

void __posixc_reclock_release_namespace(struct __fd_namespace *space)
{
    struct reclock *r, *next;

    ObtainSemaphore(&reclock_sem);
    ForeachNodeSafe(&reclock_list, r, next)
    {
        if (r->owner == space)
            free_lock(r, TRUE);
    }
    ReleaseSemaphore(&reclock_sem);
}
