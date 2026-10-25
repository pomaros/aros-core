/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    POSIX.1-2008 tcflush() function
*/

#include <errno.h>

/*****************************************************************************

    NAME */
#include <termios.h>

        int tcflush(

/*  SYNOPSIS */
        int fd,
        int queue)

/*  FUNCTION
        Discards data written to the terminal open on fd and not yet
        transmitted, or received and not yet read.

    INPUTS
        fd    - descriptor of a terminal
        queue - TCIFLUSH, TCOFLUSH or TCIOFLUSH

    RESULT
        0 on success, -1 with errno set on error.

    NOTES
        AROS has no terminal-control layer for this request yet. The call
        fails with ENOSYS until the terminal work provides one; it then
        keeps this vector.

    EXAMPLE

    BUGS

    SEE ALSO
        tcgetattr(), tcsetattr()

    INTERNALS

******************************************************************************/
{
    (void)fd;
    (void)queue;
    errno = ENOSYS;
    return -1;
} /* tcflush() */
