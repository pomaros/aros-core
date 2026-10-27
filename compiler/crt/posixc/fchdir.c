/*
    Copyright (C) 2008-2013, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
*/

#include "__posixc_intbase.h"
#include "__fdesc.h"

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <aros/symbolsets.h>
#include <stdio.h>
#include <errno.h>
#include "__upath.h"
#include "__cwd_runtime.h"
#include <aros/debug.h>

/*****************************************************************************

    NAME */
#include <unistd.h>

        int fchdir(

/*  SYNOPSIS */
        int fd )

/*  FUNCTION
        Change the current working directory to the directory given as an open
        file descriptor.

    INPUTS
        fd - File descriptor of the directory to change to.
        
    RESULT
        If the current directory was changed successfully, zero is returned.
        Otherwise, -1 is returned and errno set appropriately.
        
    NOTES
        At program exit, the current working directory will be changed back
        to the one that was current when the program first started. If you
        do not desire this behavior, use dos.library/CurrentDir() instead.

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS

******************************************************************************/
{
    int admission_error;
    fcb *file = __holdfcb(fd, &admission_error);
    struct __cwd_directory *directory;
    LONG error;
    BOOL changed;
    BPTR handle;
    if (!file) { errno = admission_error ? admission_error : EBADF; return -1; }
    __fcb_lock(file);
    if (!(file->privflags & _FCB_ISDIR)) {
        __fcb_unlock(file); __releasefcb(file);
        errno = ENOTDIR; return -1;
    }
    handle = file->handle;
    __fcb_unlock(file);
    /* Admission keeps this immutable directory handle alive during COPY_DIR;
       no fcb semaphore is needed while waiting for the handler. */
    directory = __cwd_directory_capture(handle, &error);
    __releasefcb(file);
    if (!directory) { errno = __stdc_ioerr2errno(error); return -1; }
    changed = __cwd_runtime_set(directory);
    __cwd_directory_release(directory);
    if (!changed) { errno = ENOMEM; return -1; }
    return 0;
}
