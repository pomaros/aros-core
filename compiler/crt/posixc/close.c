/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER

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
        An admitted read or write retains its file independently of the
        descriptor number. It may finish after close and reuse of that number;
        the native handle is released after its last retained operation.

    EXAMPLE

    BUGS

    SEE ALSO
        open(), read(), write(), fopen()

    INTERNALS

******************************************************************************/
{
    fdesc *fdesc;
    fcb *cblock;

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
    __setfdesc(fd, NULL);
    __fdesc_unlock();
    /* Detaching transfers the descriptor's reference to this call. Other
       operations retain their own block, so close/reuse cannot invalidate it. */
    __fcb_lock(cblock);
    __posixc_reclock_close(fdesc);
    __fcb_unlock(cblock);
    __free_fdesc(fdesc);
    __releasefcb(cblock);

    return 0;
} /* close */

