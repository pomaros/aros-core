/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    POSIX.1-2008 posix_openpt() function
*/

#include <errno.h>

/*****************************************************************************

    NAME */
#include <stdlib.h>

        int posix_openpt(

/*  SYNOPSIS */
        int oflag)

/*  FUNCTION
        Opens the master side of a new pseudo-terminal.

    INPUTS
        oflag - O_RDWR, optionally with O_NOCTTY

    RESULT
        A descriptor of the master side, or -1 with errno set.

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
    (void)oflag;
    errno = ENOSYS;
    return -1;
} /* posix_openpt() */
