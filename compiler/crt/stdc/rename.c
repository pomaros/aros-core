/*
    Copyright (C) 1995-2020, The AROS Development Team. All rights reserved.

    Copyright (C) 2026 John KNIPPER.

    C99 function rename().
*/
#include <proto/dos.h>
#include <errno.h>

#include <aros/debug.h>

#include "debug.h"

/*****************************************************************************

    NAME */
#include <stdio.h>

        int rename (

/*  SYNOPSIS */
        const char * oldpath,
        const char * newpath)

/*  FUNCTION
        Renames a file or directory.

    INPUTS
        oldpath - Complete path to existing file or directory.
        newpath - Complete path to the new file or directory.

    RESULT
        0 on success and -1 on error. In case of an error, errno is set.
        
    NOTES

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS

******************************************************************************/
{
    LONG error;

    if (Rename((CONST_STRPTR)oldpath, (CONST_STRPTR)newpath))
        return 0;
    error = IoErr();
    if (error == ERROR_OBJECT_EXISTS)
    {
        /* Replacement is one filesystem operation. Never delete the old
         * destination to emulate an unsupported handler operation. */
        if (ReplaceObject((CONST_STRPTR)oldpath, (CONST_STRPTR)newpath))
            return 0;
        error = IoErr();
    }
    errno = error == 0 ? EIO : __stdc_ioerr2errno(error);
    SetIoErr(error);
    return -1;
} /* rename */
