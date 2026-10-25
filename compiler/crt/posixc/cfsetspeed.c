/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    BSD cfsetspeed() function
*/

/*****************************************************************************

    NAME */
#include <termios.h>

        int cfsetspeed(

/*  SYNOPSIS */
        struct termios *t,
        speed_t speed)

/*  FUNCTION
        Sets both the input and the output speed in t.

    INPUTS
        t     - attributes to change
        speed - one of the B* speeds

    RESULT
        0 on success, -1 with errno set on error.

    NOTES
        cfsetispeed() and cfsetospeed() do the work.

    EXAMPLE

    BUGS

    SEE ALSO
        cfsetispeed(), cfsetospeed()

    INTERNALS

******************************************************************************/
{
    if (cfsetispeed(t, speed) != 0)
        return -1;
    return cfsetospeed(t, speed);
} /* cfsetspeed() */
