/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    BSD lutimes() function
*/

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <aros/types/timespec_s.h>

/*****************************************************************************

    NAME */
#include <sys/time.h>

        int lutimes(

/*  SYNOPSIS */
        const char *path,
        const struct timeval tvp[2])

/*  FUNCTION
        Sets the access and modification times of the object named by
        path. A soft link is changed itself, not the object it names.

    INPUTS
        path - name of the object
        tvp  - access time in tvp[0], modification time in tvp[1];
               NULL sets both to the current time

    RESULT
        0 on success, -1 with errno set on error.

    NOTES
        utimensat() with AT_SYMLINK_NOFOLLOW does the work; its notes on
        the resolution DOS keeps apply.

    EXAMPLE

    BUGS

    SEE ALSO
        utimes(), futimes(), utimensat()

    INTERNALS

******************************************************************************/
{
    struct timespec ts[2];

    if (tvp == NULL)
        return utimensat(AT_FDCWD, path, NULL, AT_SYMLINK_NOFOLLOW);

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
    return utimensat(AT_FDCWD, path, ts, AT_SYMLINK_NOFOLLOW);
} /* lutimes() */
