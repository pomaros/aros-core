/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.

    Synchronise an open object through its native filesystem handler.
*/

#include <exec/types.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <errno.h>
#include <fcntl.h>
#include "__fdesc.h"

/*****************************************************************************

    NAME */
#include <unistd.h>

        int fsync(

/*  SYNOPSIS */
        int fd)

/*  FUNCTION
        Flush the descriptor's DOS buffer, then ask its filesystem handler
        to commit pending changes with ACTION_FLUSH. Directory descriptors
        synchronise their owning handler without treating the directory lock
        as a file handle.

    INPUTS

    RESULT

    NOTES
        AROS accepts read-only file and directory descriptors for this
        operation. The barrier covers changes already accepted by the handler;
        it does not flush user-space buffers belonging to other descriptors.
        Filesystems must implement ACTION_FLUSH and report its completion.
        Unsupported barriers fail rather than claiming persistence.

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS

******************************************************************************/
{
    fdesc *desc = __getfdesc(fd);
    struct MsgPort *handler;
    LONG error;

    if (!desc || !desc->fcb->handle)
    {
        errno = EBADF;
        return -1;
    }

    __fcb_lock(desc->fcb);
    if (desc->fcb->privflags & _FCB_ISDIR)
    {
        struct FileLock *lock = BADDR(desc->fcb->handle);
        handler = lock->fl_Task;
    }
    else
    {
        struct FileHandle *file = BADDR(desc->fcb->handle);
        handler = file->fh_Type;
        SetIoErr(0);
        if (!Flush(desc->fcb->handle))
        {
            error = IoErr();
            __fcb_unlock(desc->fcb);
            errno = error ? __stdc_ioerr2errno(error) : EIO;
            return -1;
        }
    }

    if (!handler)
    {
        __fcb_unlock(desc->fcb);
        errno = EINVAL;
        return -1;
    }

    SetIoErr(0);
    if (!DoPkt(handler, ACTION_FLUSH, 0, 0, 0, 0, 0))
    {
        error = IoErr();
        __fcb_unlock(desc->fcb);
        errno = error ? __stdc_ioerr2errno(error) : EIO;
        return -1;
    }

    __fcb_unlock(desc->fcb);
    return 0;
} /* fsync */

