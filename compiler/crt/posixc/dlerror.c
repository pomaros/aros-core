/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    POSIX.1-2008 function dlerror().
*/

#include "__dl.h"

/*****************************************************************************

    NAME */
#include <dlfcn.h>

        char *dlerror(

/*  SYNOPSIS */
        void)

/*  FUNCTION
        Returns the reason the last dlopen(), dlsym() or dlclose() failed,
        and forgets it.

    INPUTS

    RESULT
        The text, or NULL when nothing failed since the last call.

    NOTES
        Each task has its own text: a pthread sees only its own failures.
        The text stays valid until that task's next failing call.

    EXAMPLE

    BUGS

    SEE ALSO
        dlopen(), dlsym(), dlclose()

    INTERNALS

******************************************************************************/
{
    struct PosixCIntBase *PosixCBase =
        __cwd_runtime_base();

    return __dl_takeerror(PosixCBase);
}
