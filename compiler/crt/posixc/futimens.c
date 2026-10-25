/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    POSIX.1-2008 futimens() function
*/

#include <dos/dos.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <errno.h>

#include "__fdesc.h"
#include "__utimens.h"

/*****************************************************************************

    NAME */
#include <sys/stat.h>

        int futimens(

/*  SYNOPSIS */
        int fd,
        const struct timespec times[2])

/*  FUNCTION
        Sets the access and modification times of the file open on the
        descriptor fd.

    INPUTS
        fd    - an open file or directory descriptor
        times - access time in times[0], modification time in times[1];
                NULL sets both to the current time. A tv_nsec of
                UTIME_NOW uses the current time, UTIME_OMIT leaves that
                time unchanged.

    RESULT
        0 on success, -1 with errno set on error.

    NOTES
        DOS sets a date by name: the descriptor's object is found through
        NameFromFH() or, for a directory, NameFromLock(). DOS keeps one date
        per object, with a resolution of one tick (1/50 s); the access time
        is checked but not stored.

    EXAMPLE

    BUGS

    SEE ALSO
        utimensat(), fstat()

    INTERNALS

******************************************************************************/
{
    fdesc *desc = __getfdesc(fd);
    struct DateStamp ds;
    STRPTR name = NULL;
    ULONG size = 256;
    int date, result;

    if (desc == NULL)
    {
        errno = EBADF;
        return -1;
    }

    date = __utimens_date(times, &ds);
    if (date <= 0)
        return date;

    for (;;)
    {
        BOOL named;

        name = AllocVec(size, MEMF_ANY);
        if (name == NULL)
        {
            errno = ENOMEM;
            return -1;
        }
        __fcb_lock(desc->fcb);
        named = (desc->fcb->privflags & _FCB_ISDIR)
              ? NameFromLock(desc->fcb->handle, name, size)
              : NameFromFH(desc->fcb->handle, name, size);
        __fcb_unlock(desc->fcb);
        if (named)
            break;
        FreeVec(name);
        if (IoErr() != ERROR_LINE_TOO_LONG || size >= 65536)
        {
            errno = __stdc_ioerr2errno(IoErr());
            return -1;
        }
        size *= 2;
    }

    /* The name is the object itself: no soft link to follow. */
    result = __utimens_set((const char *)name, &ds, 0);
    FreeVec(name);
    return result;
}
