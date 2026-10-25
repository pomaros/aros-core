/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    POSIX.1-2008 utimensat() function
*/

#include <dos/dos.h>
#include <proto/dos.h>

#include <errno.h>
#include <fcntl.h>

#include "__at.h"
#include "__upath.h"
#include "__utimens.h"

/*****************************************************************************

    NAME */
#include <sys/stat.h>

        int utimensat(

/*  SYNOPSIS */
        int dirfd,
        const char *path,
        const struct timespec times[2],
        int flags)

/*  FUNCTION
        Sets the access and modification times of the file named by path,
        relative to the directory dirfd when path is relative (AT_FDCWD:
        the current directory).

    INPUTS
        dirfd - directory descriptor or AT_FDCWD
        path  - name of the file
        times - access time in times[0], modification time in times[1];
                NULL sets both to the current time. A tv_nsec of
                UTIME_NOW uses the current time, UTIME_OMIT leaves that
                time unchanged.
        flags - 0, or AT_SYMLINK_NOFOLLOW to change a soft link itself

    RESULT
        0 on success, -1 with errno set on error.

    NOTES
        DOS keeps one date per object: the modification time is stored
        with a resolution of one tick (1/50 s), and the access time is
        checked but not stored. stat() reads the stored date back.

    EXAMPLE

    BUGS

    SEE ALSO
        futimens(), utimes(), stat()

    INTERNALS

******************************************************************************/
{
    struct DateStamp ds;
    BPTR oldcd = BNULL, dirlock = BNULL;
    const char *apath;
    int date, result;

    if (path == NULL)
    {
        errno = EFAULT;
        return -1;
    }
    if (*path == '\0')
    {
        errno = ENOENT;
        return -1;
    }
    if (flags & ~AT_SYMLINK_NOFOLLOW)
    {
        errno = EINVAL;
        return -1;
    }

    date = __utimens_date(times, &ds);
    if (date <= 0)
        return date;

    if (!__at_isabsolute(path) && __at_enter(dirfd, &oldcd, &dirlock) != 0)
        return -1;

    apath = __path_u2a(path);
    if (apath == NULL)
        result = -1;
    else
        result = __utimens_set(apath, &ds, !(flags & AT_SYMLINK_NOFOLLOW));

    if (!__at_isabsolute(path))
        __at_leave(oldcd, dirlock);
    return result;
}
