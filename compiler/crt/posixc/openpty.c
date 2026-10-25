/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    BSD openpty() function
*/

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

/*****************************************************************************

    NAME */
#include <pty.h>

        int openpty(

/*  SYNOPSIS */
        int *amaster,
        int *aslave,
        char *name,
        const struct termios *termp,
        const struct winsize *winp)

/*  FUNCTION
        Opens a new pseudo-terminal and returns both of its sides.

    INPUTS
        amaster - receives the master side's descriptor
        aslave  - receives the slave side's descriptor
        name    - NULL, or receives the slave side's name
        termp   - NULL, or the terminal settings of the slave side
        winp    - NULL, or the window size of the slave side

    RESULT
        0 on success, -1 with errno set on error; no descriptor stays open
        after an error.

    NOTES
        Built on posix_openpt(), grantpt(), unlockpt() and ptsname(), so it
        works wherever they do. The caller provides name storage of at
        least the length of the returned name plus one; BSD documents no
        bound, and callers should pass NULL.

    EXAMPLE

    BUGS

    SEE ALSO
        posix_openpt(), login_tty()

    INTERNALS

******************************************************************************/
{
    int master, slave, error;
    char *slavename;

    if (amaster == NULL || aslave == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0)
        return -1;
    if (grantpt(master) != 0 || unlockpt(master) != 0
        || (slavename = ptsname(master)) == NULL)
        goto fail_master;

    slave = open(slavename, O_RDWR | O_NOCTTY);
    if (slave < 0)
        goto fail_master;
    if (termp != NULL && tcsetattr(slave, TCSAFLUSH, termp) != 0)
        goto fail_slave;
    if (winp != NULL && ioctl(slave, TIOCSWINSZ, winp) != 0)
        goto fail_slave;

    if (name != NULL)
        strcpy(name, slavename);
    *amaster = master;
    *aslave = slave;
    return 0;

fail_slave:
    error = errno;
    close(slave);
    errno = error;
fail_master:
    error = errno;
    close(master);
    errno = error;
    return -1;
} /* openpty() */
