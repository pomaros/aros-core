/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.

    Copyright (C) 2026 John KNIPPER <code@jkn.me>.

    POSIX.1-2008 function ftruncate().
*/

#include <dos/dos.h>
#include <proto/dos.h>
#include <fcntl.h>
#include <errno.h>
#include "__fdesc.h"
#include "__ftruncate.h"

/*****************************************************************************

    NAME */
#include <unistd.h>

        int ftruncate (

/*  SYNOPSIS */
        int   fd,
        off_t length)

/*  FUNCTION
        Truncate a file to a specified length

    INPUTS
        fd     - the descriptor of the file being truncated.
                 The file must be open for writing
        length - The required file size, which must not be negative

    RESULT
        0 on success or -1 on error.

    NOTES
        Shrinking discards the trailing data. Extending fills the added
        range with zero bytes. The file position is preserved. A failure
        after resizing can leave the file size or contents changed.


    EXAMPLE

    BUGS

    SEE ALSO
        open(), truncate()

    INTERNALS

******************************************************************************/
{
    return __posixc_resize(fd, (QUAD)length);
}
