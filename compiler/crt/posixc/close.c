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
#include "__reclock.h"

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
        /* Not a local posixc file descriptor: the owning subsystem's hook
           closes it (e.g. a bsdsocket socket). */
        LONG err = 0;
        int result;
        __fdesc_unlock();
        result = __fdhook(fd, __FDH_CLOSE, 0, 0, 0, &err);
        if (result < 0) errno = err;
        return result;
    }

    cblock = fdesc->fcb;
    __fcb_lock(cblock);
    lastclose = (--cblock->opencount == 0);
    __setfdesc(fd, NULL);
    __fdesc_unlock();
    /* Closing any descriptor of a file releases the process's record locks
       on it; the descriptor is out of the table and still allocated. */
    __posixc_reclock_close(fdesc, lastclose);
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

