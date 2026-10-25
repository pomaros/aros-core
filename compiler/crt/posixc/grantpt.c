/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    POSIX.1-2008 grantpt() function
*/

#include <errno.h>

/*****************************************************************************

    NAME */
#include <stdlib.h>

        int grantpt(

/*  SYNOPSIS */
        int fd)

/*  FUNCTION
        Gives the caller access to the slave side of the pseudo-terminal
        whose master side is open on fd.

    INPUTS
        fd - descriptor of a master side

    RESULT
        0 on success, -1 with errno set on error.

    NOTES
        AROS has no pseudo-terminal device yet. The call fails with
        ENOSYS until the terminal work provides one; it then keeps this
        vector.

    EXAMPLE

    BUGS

    SEE ALSO
        posix_openpt(), grantpt(), unlockpt(), ptsname(), openpty()

    INTERNALS

******************************************************************************/
{
    (void)fd;
    errno = ENOSYS;
    return -1;
} /* grantpt() */
