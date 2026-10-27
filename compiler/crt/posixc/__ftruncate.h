/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License 1.1.

    Shared implementation for both public truncate widths.
*/
#ifndef POSIXC_FTRUNCATE_H
#define POSIXC_FTRUNCATE_H

#include "__dos64.h"

static int __posixc_resize(int fd, QUAD length)
{
    int admission_error, saved_errno, error = 0, result = -1;
    QUAD oldpos = -1, size, resized;
    fcb *file = __holdfcb(fd, &admission_error);

    if (!file)
    {
        errno = admission_error ? admission_error : EBADF;
        return -1;
    }
    __fcb_lock(file);
    if (!file->handle)
        error = EBADF;
    else if (file->privflags & _FCB_ISDIR)
        error = EISDIR;
    /* AROS access modes are read/write bits (1/2/3), not Unix's 0/1/2.
     * O_RDWR includes the read bit, so testing its union admits O_RDONLY. */
    else if (!(file->flags & O_WRONLY))
        error = EBADF;
    else if (length < 0)
        error = EINVAL;
    if (error)
        goto done;

    /* Flush before inspecting or changing DOS's buffered file position. */
    SetIoErr(0);
    if (file->privflags & _FCB_FLUSHONREAD)
    {
        if (!Flush(file->handle))
            goto native_error;
        file->privflags &= ~_FCB_FLUSHONREAD;
    }
    SetIoErr(0);
    oldpos = __dos64_getpos(file);
    if (oldpos == -1)
        goto native_error;
    SetIoErr(0);
    size = __dos64_getsize(file);
    if (size == -1)
        goto native_error;
    SetIoErr(0);
    resized = __dos64_setfilesize(file, length, OFFSET_BEGINNING);
    if (resized == -1)
        goto native_error;
    if (resized != length)
    {
        error = EIO;
        goto restore;
    }
    if (size < length)
    {
        /* Not every DOS handler zeroes growth. Check every buffered byte
           transfer until that native contract is shared by the handlers. */
        const unsigned char zeros[512] = {0};
        QUAD left = length - size;
        SetIoErr(0);
        if (__dos64_seek(file, size, OFFSET_BEGINNING) == -1)
            goto native_error;
        while (left)
        {
            LONG count = left > (QUAD)sizeof(zeros) ? sizeof(zeros) : left;
            LONG written;
            SetIoErr(0);
            file->privflags |= _FCB_FLUSHONREAD;
            written = FWrite(file->handle, zeros, 1, count);
            if (written <= 0)
                goto native_error;
            if (written > count)
            {
                error = EIO;
                goto restore;
            }
            left -= written;
        }
        SetIoErr(0);
        if (!Flush(file->handle))
            goto native_error;
        file->privflags &= ~_FCB_FLUSHONREAD;
    }
    goto restore;

native_error:
    {
        LONG ioerr = IoErr();
        error = ioerr == ERROR_OBJECT_TOO_LARGE ? EFBIG :
            ioerr ? __stdc_ioerr2errno(ioerr) : EIO;
    }
restore:
    /* getsize's classic seek pair can also fail after moving the cursor.
       Keep the first error if restoring the original position fails too. */
    if (oldpos != -1)
    {
        SetIoErr(0);
        if (__dos64_seek(file, oldpos, OFFSET_BEGINNING) == -1 && !error)
        {
            LONG ioerr = IoErr();
            error = ioerr == ERROR_OBJECT_TOO_LARGE ? EOVERFLOW :
                ioerr ? __stdc_ioerr2errno(ioerr) : EIO;
        }
    }
    if (!error)
        result = 0;
done:
    saved_errno = error ? error : errno;
    __fcb_unlock(file);
    __releasefcb(file);
    errno = saved_errno;
    return result;
}

#endif
