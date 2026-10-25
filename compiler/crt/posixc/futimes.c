/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    BSD futimes() function
*/

#include <errno.h>
#include <sys/stat.h>
#include <aros/types/timespec_s.h>

/*****************************************************************************

    NAME */
#include <sys/time.h>

        int futimes(

/*  SYNOPSIS */
        int fd,
        const struct timeval tvp[2])

/*  FUNCTION
        Sets the access and modification times of the object open on the
        descriptor fd.

    INPUTS
        fd  - open descriptor
        tvp - access time in tvp[0], modification time in tvp[1];
              NULL sets both to the current time

    RESULT
        0 on success, -1 with errno set on error.

    NOTES
        futimens() does the work; its notes on the resolution DOS keeps
        apply.

    EXAMPLE

    BUGS

    SEE ALSO
        utimes(), lutimes(), futimens()

    INTERNALS

******************************************************************************/
{
    struct timespec ts[2];

    if (tvp == NULL)
        return futimens(fd, NULL);

    if (tvp[0].tv_usec < 0 || tvp[0].tv_usec >= 1000000
        || tvp[1].tv_usec < 0 || tvp[1].tv_usec >= 1000000)
    {
        errno = EINVAL;
        return -1;
    }
    ts[0].tv_sec = tvp[0].tv_sec;
    ts[0].tv_nsec = tvp[0].tv_usec * 1000L;
    ts[1].tv_sec = tvp[1].tv_sec;
    ts[1].tv_nsec = tvp[1].tv_usec * 1000L;
    return futimens(fd, ts);
} /* futimes() */
