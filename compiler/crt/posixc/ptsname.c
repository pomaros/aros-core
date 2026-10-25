/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    POSIX.1-2008 ptsname() function
*/

#include <errno.h>

/*****************************************************************************

    NAME */
#include <stdlib.h>

        char * ptsname(

/*  SYNOPSIS */
        int fd)

/*  FUNCTION
        Returns the name of the slave side of the pseudo-terminal whose
        master side is open on fd.

    INPUTS
        fd - descriptor of a master side

    RESULT
        The name, or NULL with errno set.

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
    return NULL;
} /* ptsname() */
