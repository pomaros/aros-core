/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    BSD login_tty() function
*/

#include <errno.h>

/*****************************************************************************

    NAME */
#include <pty.h>

        int login_tty(

/*  SYNOPSIS */
        int fd)

/*  FUNCTION
        Makes the terminal open on fd the controlling terminal and the
        standard input, output and error of the caller.

    INPUTS
        fd - descriptor of a terminal

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
} /* login_tty() */
