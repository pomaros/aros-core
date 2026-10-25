/*
    Copyright (C) 1995-2012, The AROS Development Team. All rights reserved.

    POSIX.1-2008 utimes() function
*/

#include <dos/dos.h>
#include <proto/dos.h>

#include <errno.h>

#include "__upath.h"
#include "__utimens.h"

/*****************************************************************************

    NAME */
#include <sys/time.h>

        int utimes(
/*  SYNOPSIS */

        const char *file,
        const struct timeval tvp[2])

/*  FUNCTION
        Change last access and last modification time of the given file to
        times specified in tvp array. If tvp is NULL, the current time will be
        used instead.

    INPUTS
        filename - Name of the file
        buf - Pointer to an array of two timeval structures. First structure
                specifies the last access time, second specifies the last
                modification time

    RESULT
        0 on success and -1 on error. If an error occurred, the global
        variable errno is set.

    NOTES
        The timeval structure has microsecond resolution; the date is
        stored with a resolution of 1 tick (1/50 s). A soft link is
        followed. stat() reads the stored date back.

    EXAMPLE

    BUGS
        Since AROS has no notion of last access time, it's silently ignored
        and only modification time of the file is set.

    SEE ALSO
        utime(), utimensat()

    INTERNALS

******************************************************************************/
{
    struct timespec ts[2];
    struct DateStamp ds;

    if (!file) /*safety check */
    {
        errno = EFAULT;
        return -1;
    }

    if (tvp != NULL)
    {
        int i;

        for (i = 0; i < 2; i++)
        {
            if (tvp[i].tv_usec < 0 || tvp[i].tv_usec >= 1000000)
            {
                errno = EINVAL;
                return -1;
            }
            ts[i].tv_sec  = tvp[i].tv_sec;
            ts[i].tv_nsec = tvp[i].tv_usec * 1000L;
        }
    }
    if (__utimens_date(tvp != NULL ? ts : NULL, &ds) < 0)
        return -1;

    file = __path_u2a(file);
    if (!file)
        return -1;

    return __utimens_set(file, &ds, 1);
}
