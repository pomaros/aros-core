/*
    Copyright (C) 2004-2012, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.

    POSIX.1-2008 function link().
*/

#include <errno.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include "__upath.h"
#include "__cwd_runtime.h"
#include "__dos_error.h"

/*****************************************************************************

    NAME */

#include <unistd.h>

        int link(

/*  SYNOPSIS */
        const char *oldpath,
        const char *newpath)

/*  FUNCTION

    INPUTS

    RESULT

    NOTES
        Uses native DOS hard links; directory links are refused.

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS

******************************************************************************/
{
    POSIXC_CWD_SCOPE_OR_RETURN(-1);
    const char *native;
    BPTR source = BNULL;
    struct DevProc *destination = NULL;
    struct FileInfoBlock info;
    LONG error;
    int result = -1, saved_errno;

    if (!oldpath || !newpath) { errno = EFAULT; return -1; }
    if (!*oldpath || !*newpath) { errno = ENOENT; return -1; }
    native = __path_u2a(oldpath);
    if (!native) return -1;
    source = Lock((CONST_STRPTR)native, SHARED_LOCK);
    if (!source) { errno = __posixc_dos_error(IoErr()); return -1; }
    if (!Examine(source, &info)) {
        errno = __posixc_dos_error(IoErr()); goto out;
    }
    if (info.fib_DirEntryType > 0) { errno = EPERM; goto out; }
    /* The owned source lock survives reuse of the path conversion buffer. */
    native = __path_u2a(newpath);
    if (!native) goto out;
    destination = GetDeviceProc((CONST_STRPTR)native, NULL);
    if (!destination) { errno = __posixc_dos_error(IoErr()); goto out; }
    if (destination->dvp_Port != ((struct FileLock *)BADDR(source))->fl_Task) {
        errno = EXDEV; goto out;
    }
    if (MakeLink((STRPTR)native, (SIPTR)source, FALSE)) result = 0;
    else {
        error = IoErr();
        errno = __posixc_dos_error(error);
    }
out:
    saved_errno = errno;
    if (destination) FreeDeviceProc(destination);
    UnLock(source);
    errno = saved_errno;
    return result;
} /* link */

