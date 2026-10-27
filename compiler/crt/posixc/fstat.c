/*
    Copyright (C) 1995-2025, The AROS Development Team. All rights reserved.

    Copyright (C) 2026 John KNIPPER <code@jkn.me>.

    POSIX.1-2008 function fstat().
*/

#include <errno.h>

#include "__stat.h"
#include "__fdesc.h"

/*****************************************************************************

    NAME */

#include <sys/stat.h>

        int fstat(

/*  SYNOPSIS */
        int fd,
        struct stat *sb)

/*  FUNCTION
        Returns information about a file specified by an open file descriptor.
        Information is stored in stat structure. Consult stat() documentation
        for detailed description of that structure.

    INPUTS
        filedes - File descriptor of the file
        sb - Pointer to stat structure that will be filled by the fstat()
        call.

    RESULT
        0 on success and -1 on error. If an error occurred, the global
        variable errno is set.

    NOTES

    EXAMPLE

    BUGS

    SEE ALSO
        __posixc_stat()

    INTERNALS
        Consult stat() documentation for details.

******************************************************************************/
{
    int admission_error, result, saved_errno;
    fcb *file = __holdfcb(fd, &admission_error);
    if (!file)
    {
        errno = admission_error ? admission_error : EBADF;
        return -1;
    }

    __fcb_lock(file);
    BPTR handle = file->handle;
    BOOL filehandle = !(file->privflags & _FCB_ISDIR);
    __fcb_unlock(file);
    /* The retained file owns its native handle. Metadata/name/time conversion
       may reenter CRT services, so do not hold its I/O semaphore here. */
    result = __stat(handle, sb, filehandle);
    saved_errno = errno;
    __releasefcb(file);
    errno = saved_errno;
    return result;
}

