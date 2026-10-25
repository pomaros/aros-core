/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    POSIX.1-2008 tcflow() function
*/

#include <errno.h>

/*****************************************************************************

    NAME */
#include <termios.h>

        int tcflow(

/*  SYNOPSIS */
        int fd,
        int action)

/*  FUNCTION
        Suspends or restarts output or input on the terminal open on fd.

    INPUTS
        fd     - descriptor of a terminal
        action - TCOOFF, TCOON, TCIOFF or TCION

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
    (void)action;
    errno = ENOSYS;
    return -1;
} /* tcflow() */
