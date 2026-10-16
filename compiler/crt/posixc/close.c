/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.

    POSIX.1-2008 function close().
*/

#include <unistd.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <errno.h>
#include <libraries/fd.h>
#include "__fdesc.h"

/*****************************************************************************

    NAME */
#include <unistd.h>

        int close (

/*  SYNOPSIS */
        int fd)

/*  FUNCTION
        Closes an open file. If this is the last file descriptor
        associated with this file, then all allocated resources
        are freed, too.

    INPUTS
        fd - The result of a successful open()

    RESULT
        -1 for error or zero on success.

    NOTES
        This function must not be used in a shared library or
        in a threaded application.

    EXAMPLE

    BUGS

    SEE ALSO
        open(), read(), write(), fopen()

    INTERNALS

******************************************************************************/
{
    fdesc *fdesc;
    fcb *cblock;
    BOOL lastclose;

    __fdesc_lock();
    if (!(fdesc = __getfdesc(fd)))
    {
        /* Not a local posixc file descriptor: dispatch to the owning
           subsystem's close hook (e.g. a bsdsocket socket).  The owner
           releases its own fd.library reservation. */
        APTR data;
        const struct fd_hooks *hooks;
        __fdesc_unlock();
        hooks = __getfdhooks(fd, &data);
        if (hooks && hooks->fdh_close)
        {
            LONG err = 0;
            if (hooks->fdh_close(data, fd, &err) < 0)
            {
                errno = err;
                return -1;
            }
            return 0;
        }
        errno = EBADF;

        return -1;
    }

    cblock = fdesc->fcb;
    __fcb_lock(cblock);
    lastclose = (--cblock->opencount == 0);
    __setfdesc(fd, NULL);
    __fdesc_unlock();
    __free_fdesc(fdesc);

    if (lastclose)
    {
        /* DOS releases the handle even when Close reports an error. */
        if (!(cblock->privflags & _FCB_DONTCLOSE_FH))
        {
            // don't close directories because we don't Open() them.
            if (cblock->privflags & _FCB_ISDIR)
            {
                UnLock(cblock->handle);
            }
            else
            {
                Close(cblock->handle);
            }
        }

        __fcb_unlock(cblock);
        FreeVec(cblock);
    }
    else
        __fcb_unlock(cblock);

    return 0;
} /* close */

