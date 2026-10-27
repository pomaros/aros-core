/*
    Copyright (C) 1995-2013, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER <code@jkn.me>

    POSIX.1-2008 function chdir().
*/

#include "__posixc_intbase.h"

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <aros/symbolsets.h>
#include <errno.h>
#include "__upath.h"
#include "__cwd_runtime.h"

/*****************************************************************************

    NAME */
#include <unistd.h>

        int chdir(

/*  SYNOPSIS */
        const char *path )

/*  FUNCTION
        Change the current working directory to the one specified by path.

    INPUTS
        path - Path of the directory to change to.
        
    RESULT
        If the current directory was changed successfully, zero is returned.
        Otherwise, -1 is returned and errno set appropriately.
        
    NOTES
        At program exit, the current working directory will be changed back
        to the one that was current when the program first started. If you
        do not desire this behavior, use dos.library/CurrentDir() instead.
        The path given to chdir can be translated so that getcwd gives back
        a string that is not the same but points to the same directory. For
        example, assigns are replaced by the path where the assign points to
        and device names (like DH0:) are replaced with the volume name
        (e.g. Workbench:).

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS

******************************************************************************/
{
    struct __cwd_directory *directory;
    BPTR lock;
    LONG error;
    struct FileInfoBlock information;
    if (!path) { errno = EFAULT; return -1; }
    path = __path_u2a(path);
    if (!path) return -1;
    lock = Lock(path, SHARED_LOCK);
    if (!lock) { errno = __stdc_ioerr2errno(IoErr()); return -1; }
    if (!Examine(lock, &information)) {
        error = IoErr(); UnLock(lock);
        errno = __stdc_ioerr2errno(error); return -1;
    }
    if (information.fib_DirEntryType <= 0) {
        UnLock(lock); errno = ENOTDIR; return -1;
    }
    directory = __cwd_directory_capture(lock, &error);
    UnLock(lock);
    if (!directory) { errno = __stdc_ioerr2errno(error); return -1; }
    BOOL changed = __cwd_runtime_set(directory);
    __cwd_directory_release(directory);
    if (!changed) { errno = ENOMEM; return -1; }
    return 0;
}
