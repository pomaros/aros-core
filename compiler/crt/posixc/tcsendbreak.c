/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    POSIX.1-2008 tcsendbreak() function
*/

#include <errno.h>

/*****************************************************************************

    NAME */
#include <termios.h>

        int tcsendbreak(

/*  SYNOPSIS */
        int fd,
        int duration)

/*  FUNCTION
        Sends a break on the terminal open on fd.

    INPUTS
        fd       - descriptor of a terminal
        duration - 0 for the standard duration

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
    (void)duration;
    errno = ENOSYS;
    return -1;
} /* tcsendbreak() */
