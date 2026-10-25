/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    POSIX.1-2008 tcdrain() function
*/

#include <proto/dos.h>
#include <errno.h>
#include "__fdesc.h"

/*****************************************************************************

    NAME */
#include <termios.h>

        int tcdrain(

/*  SYNOPSIS */
        int fd)

/*  FUNCTION
        Waits until all output written to the terminal open on fd has
        been transmitted.

    INPUTS
        fd - descriptor of a terminal

    RESULT
        0 on success, -1 with errno set: EBADF, ENOTTY, or the error of
        the flush.

    NOTES
        A DOS console writes synchronously once its handle's buffer is
        flushed, so the call flushes that buffer.

    EXAMPLE

    BUGS

    SEE ALSO
        tcgetattr(), tcsetattr()

    INTERNALS

******************************************************************************/
{
    fdesc *desc = __getfdesc(fd);
    LONG error;

    if (!desc || !desc->fcb || !desc->fcb->handle)
    {
        errno = EBADF;
        return -1;
    }

    __fcb_lock(desc->fcb);
    if ((desc->fcb->privflags & _FCB_ISDIR) || !IsInteractive(desc->fcb->handle))
    {
        __fcb_unlock(desc->fcb);
        errno = ENOTTY;
        return -1;
    }
    SetIoErr(0);
    if (!Flush(desc->fcb->handle))
    {
        error = IoErr();
        __fcb_unlock(desc->fcb);
        errno = error ? __stdc_ioerr2errno(error) : EIO;
        return -1;
    }
    __fcb_unlock(desc->fcb);
    return 0;
} /* tcdrain() */
