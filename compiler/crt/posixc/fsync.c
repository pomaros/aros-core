/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.

    Copyright (C) 2026 John KNIPPER <code@jkn.me>.

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
    int admission_error, result = -1, saved_errno;
    fcb *file = __holdfcb(fd, &admission_error);
    struct MsgPort *handler;
    LONG error;

    if (!file)
    {
        errno = admission_error ? admission_error : EBADF;
        return -1;
    }

    __fcb_lock(file);
    if (!file->handle)
    {
        errno = EBADF;
        goto done;
    }
    if (file->privflags & _FCB_ISDIR)
    {
        struct FileLock *lock = BADDR(file->handle);
        handler = lock->fl_Task;
    }
    else
    {
        struct FileHandle *handle = BADDR(file->handle);
        handler = handle->fh_Type;
        SetIoErr(0);
        if (!Flush(file->handle))
        {
            error = IoErr();
            errno = error ? __stdc_ioerr2errno(error) : EIO;
            goto done;
        }
    }

    if (!handler)
    {
        errno = EINVAL;
        goto done;
    }

    SetIoErr(0);
    if (!DoPkt(handler, ACTION_FLUSH, 0, 0, 0, 0, 0))
    {
        error = IoErr();
        errno = error ? __stdc_ioerr2errno(error) : EIO;
        goto done;
    }

    result = 0;
done:
    saved_errno = errno;
    __fcb_unlock(file);
    __releasefcb(file);
    errno = saved_errno;
    return result;
} /* fsync */

