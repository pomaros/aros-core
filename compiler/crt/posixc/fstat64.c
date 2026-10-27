/*
    Copyright (C) 2020, The AROS Development Team. All rights reserved.

    Copyright (C) 2026 John KNIPPER <code@jkn.me>.

    POSIX.1-2008 function fstat().
*/

#include <errno.h>

#include "__stat.h"
#include "__fdesc.h"

/*****************************************************************************

    NAME */

#include <sys/stat.h>

        int fstat64(

/*  SYNOPSIS */
        int fd,
        struct stat64 *sb)

/*  FUNCTION
        Returns information about a file specified by an open file descriptor.
        Information is stored in stat64 structure. Consult stat() documentation
        for detailed description of that structure.

    INPUTS
        filedes - File descriptor of the file
        sb - Pointer to stat structure that will be filled by fstat64()
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
        Consult stat64() documentation for details.

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
    result = __stat64(file->handle, sb,
        (file->privflags & _FCB_ISDIR) ? FALSE : TRUE);
    saved_errno = errno;
    __fcb_unlock(file);
    __releasefcb(file);
    errno = saved_errno;
    return result;
}

