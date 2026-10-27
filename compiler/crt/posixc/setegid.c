/*
    Copyright (C) 2017, The AROS Development Team. All rights reserved.

    POSIX.1-2008 function setegid().
*/

#include <aros/debug.h>
#include <errno.h>

#include "__posixc_intbase.h"

/*****************************************************************************

    NAME */
#include <unistd.h>

        int setegid(

/*  SYNOPSIS */
        gid_t gid)

/*  FUNCTION
        Set the effective group id of the calling process to gid.

    INPUTS
        
    RESULT
        
    NOTES

    EXAMPLE

    BUGS
        
    SEE ALSO
        
    INTERNALS

******************************************************************************/
{
    struct PosixCIntBase *PosixCBase =
        __cwd_runtime_base();

    PosixCBase->egid = gid;

    return 0;
} /* setgid() */
