/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Copyright (C) 1995-2011, The AROS Development Team. All rights reserved.

    Desc:
*/
#include <aros/debug.h>

#include <proto/exec.h>
#include "dos_intern.h"
#include "taskpathresolver.h"

/*****************************************************************************

    NAME */
#include <proto/dos.h>

        AROS_LH0(SIPTR, IoErr,

/*  SYNOPSIS */

/*  LOCATION */
        struct DosLibrary *, DOSBase, 22, Dos)

/*  FUNCTION
        Get the dos error code for the current process.

    INPUTS

    RESULT
        Error code.

    NOTES

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    /* Get pointer to process structure */
    struct Process *me=(struct Process *)FindTask(NULL);

    if (!__is_process(me)) {
        struct DosPathResolver *resolver = __dos_task_path_resolver();
        return resolver ? resolver->error : 0;
    }

    /* Nothing spectacular */
    return me->pr_Result2;
    AROS_LIBFUNC_EXIT
} /* IoErr */
