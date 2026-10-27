/*
    Copyright (C) 2020-2026, The AROS Development Team. All rights reserved.

    Copyright (C) 2026 John KNIPPER <code@jkn.me>.

    Reposition read/write file offset.
*/
#include <errno.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <exec/exec.h>
#include <proto/exec.h>
#include <clib/macros.h>
#include "__fdesc.h"
#include "__dos64.h"

/*****************************************************************************

    NAME */
#include <unistd.h>

        __off64_t lseek64 (

/*  SYNOPSIS */
        int    filedes,
        __off64_t  offset,
        int    whence)

/*  FUNCTION
        Reposition read/write file offset

    INPUTS
        filedef - the filedescriptor being modified
        offset, whence -
                  How to modify the current position. whence
                  can be SEEK_SET, then offset is the absolute position
                  in the file (0 is the first byte), SEEK_CUR then the
                  position will change by offset (ie. -5 means to move
                  5 bytes to the beginning of the file) or SEEK_END.
                  SEEK_END means that the offset is relative to the
                  end of the file (-1 is the last byte and 0 is
                  the EOF).

    RESULT
        The new position on success and -1 on error. If an error occurred, the global
        variable errno is set.

    NOTES

    EXAMPLE

    BUGS
        Since it's not possible to use Seek() for directories, this
        implementation fails with EISDIR for directory file descriptors.

    SEE ALSO
        __posixc_fopen(), __posixc_fwrite()

    INTERNALS

******************************************************************************/
{
    QUAD count = -1;
    int admission_error, saved_errno;
    fcb *file = __holdfcb(filedes, &admission_error);
    if (!file)
    {
        errno = admission_error ? admission_error : EBADF;
        return (__off64_t)-1;
    }

    __fcb_lock(file);
    if (!file->handle)
    {
        errno = EBADF;
        goto done;
    }
    if (file->privflags & _FCB_ISDIR)
    {
        errno = EISDIR;
        goto done;
    }
    switch (whence)
    {
        case SEEK_SET: whence = OFFSET_BEGINNING; break;
        case SEEK_CUR: whence = OFFSET_CURRENT; break;
        case SEEK_END: whence = OFFSET_END; break;
        default: errno = EINVAL; goto done;
    }

    /* Hold one file across seek and position query, including dup'd handles.
       Flush pending DOS buffers without consuming the retry flag on failure. */
    if (file->privflags & _FCB_FLUSHONREAD)
    {
        SetIoErr(0);
        if (!Flush(file->handle))
            goto error;
        file->privflags &= ~_FCB_FLUSHONREAD;
    }
    if (__dos64_seek(file, offset, whence) == -1)
        goto error;
    count = __dos64_getpos(file);
    if (count == -1)
        goto error;
    if (count != (QUAD)(__off64_t)count)
    {
        errno = EOVERFLOW;
        count = -1;
    }
    goto done;
error:
    {
        LONG ioerr = IoErr();
        errno = ioerr == ERROR_OBJECT_TOO_LARGE ? EOVERFLOW :
            ioerr ? __stdc_ioerr2errno(ioerr) : EIO;
        count = -1;
    }
done:
    saved_errno = errno;
    __fcb_unlock(file);
    __releasefcb(file);
    errno = saved_errno;
    return (__off64_t)count;
} /* lseek64 */
