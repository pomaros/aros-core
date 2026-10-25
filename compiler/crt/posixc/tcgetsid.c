/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    POSIX.1-2008 tcgetsid() function
*/

#include <errno.h>

/*****************************************************************************

    NAME */
#include <termios.h>

        pid_t tcgetsid(

/*  SYNOPSIS */
        int fd)

/*  FUNCTION
        Returns the session for which the terminal open on fd is the
        controlling terminal.

    INPUTS
        fd - descriptor of a terminal

    RESULT
        The process of the session leader, or -1 with errno set.

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
    errno = ENOSYS;
    return (pid_t)-1;
} /* tcgetsid() */
