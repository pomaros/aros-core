/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER

    POSIX.1-2008 function read().
*/

#include <errno.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <libraries/fd.h>
#include <aros/debug.h>
#include "__fdesc.h"
#include "__dos64.h"

/*****************************************************************************

    NAME */
#include <unistd.h>

        ssize_t read (

/*  SYNOPSIS */
        int    fd,
        void * buf,
        size_t count)

/*  FUNCTION
        Read an amount of bytes from a file descriptor.

    INPUTS
        fd - The file descriptor to read from
        buf - The buffer to read the bytes into
        count - Read this many bytes.

    RESULT
        The number of characters read (may range from 0 when the file
        descriptor contains no more characters to count) or -1 on error.

    NOTES

    EXAMPLE

    BUGS

    SEE ALSO
        open(), read(), fread()

    INTERNALS

******************************************************************************/
{
    ssize_t cnt;
    int admission_error;
    fcb *file = __holdfcb(fd, &admission_error);

    if (!file)
    {
        if (admission_error)
        {
            errno = admission_error;
            return -1;
        }
        LONG err=0;
        ssize_t result=(ssize_t)__fdhook(fd,__FDH_READ,(IPTR)buf,count,0,&err);
        if (result<0) errno=err;
        return result;
    }

    __fcb_lock(file);
    BOOL directory = (file->privflags & _FCB_ISDIR) != 0;
    __fcb_unlock(file);
    if (directory)
    {
        errno = EISDIR;
        __releasefcb(file);
        return -1;
    }

    cnt = __dos64_read (file, buf, count);

    if (cnt == -1)
        errno = __stdc_ioerr2errno (IoErr ());

    __releasefcb(file);
    return cnt;
} /* read */

