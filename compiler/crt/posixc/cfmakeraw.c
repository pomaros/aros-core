/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    BSD cfmakeraw() function
*/

#include <stddef.h>

/*****************************************************************************

    NAME */
#include <termios.h>

        void cfmakeraw(

/*  SYNOPSIS */
        struct termios *t)

/*  FUNCTION
        Sets the terminal attributes in t for raw mode: input is available
        character by character, without echo, signal characters or input
        and output processing, with eight-bit characters.

    INPUTS
        t - attributes to change, typically read with tcgetattr()

    RESULT
        None.

    NOTES
        Only the structure changes; tcsetattr() applies it.

    EXAMPLE

    BUGS

    SEE ALSO
        tcgetattr(), tcsetattr()

    INTERNALS

******************************************************************************/
{
    if (t == NULL)
        return;
    t->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t->c_oflag &= ~OPOST;
    t->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t->c_cflag &= ~(CSIZE | PARENB);
    t->c_cflag |= CS8;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
} /* cfmakeraw() */
