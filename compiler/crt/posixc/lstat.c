/*
    Copyright (C) 1995-2025, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
*/

#include <aros/debug.h>

#include <proto/dos.h>

#include <errno.h>

#include "__posixc_intbase.h"
#include "__stat.h"
#include "__upath.h"
#include "__cwd_runtime.h"

/*****************************************************************************

    NAME */

#include <sys/stat.h>

        int lstat(

/*  SYNOPSIS */
        const char  *path,
        struct stat *sb)

/*  FUNCTION
        Returns information about a file like stat does except that lstat
        does not follow symbolic links. Information is stored in stat
        structure. Consult stat() documentation for detailed description
        of that structure.

    INPUTS
        path - Pathname of the file
        sb - Pointer to stat structure that will be filled by the lstat() call.

    RESULT
        0 on success and -1 on error. If an error occurred, the global
        variable errno is set.

    NOTES

    EXAMPLE

    BUGS

    SEE ALSO
        __posixc_stat(), __posixc_fstat()

    INTERNALS
        Consult stat() documentation for details.

******************************************************************************/
{
    POSIXC_CWD_SCOPE_OR_RETURN(-1);
    struct PosixCIntBase *PosixCBase =
        __cwd_runtime_base();

    /* check for empty path before potential conversion from "." to "" */
    if (PosixCBase->doupath && path && *path == '\0')
    {
        errno = ENOENT;
        return -1;
    }

    path = __path_u2a(path);
    if (path == NULL)
        return -1;

    return __posixc_lstat_path(path, sb, FALSE);
}
