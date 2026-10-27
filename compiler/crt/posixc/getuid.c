/*
    Copyright (C) 2003-2016, The AROS Development Team. All rights reserved.

    POSIX.1-2008 function getuid().
*/

#include "__posixc_intbase.h"

/*****************************************************************************

    NAME */
#include <unistd.h>

        uid_t getuid(

/*  SYNOPSIS */
        void)

/*  FUNCTION
        Returns the real user ID of the calling process.

    INPUTS

    RESULT

    NOTES

    EXAMPLE

    BUGS

    SEE ALSO
        geteuid()
        
    INTERNALS

******************************************************************************/
{
    struct PosixCIntBase *PosixCBase =
        __cwd_runtime_base();

    return PosixCBase->uid;
} /* getuid() */

