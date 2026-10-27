/*
    Copyright (C) 1995-2012, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER <code@jkn.me>

    POSIX.1-2008 function getcwd().
*/

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <exec/memory.h>
#include "__upath.h"
#include "__cwd_runtime.h"
#include <proto/exec.h>
#include <proto/dos.h>

/*****************************************************************************

    NAME */
#include <unistd.h>

        char *getcwd (

/*  SYNOPSIS */
        char *buf,
        size_t size)

/*  FUNCTION
        Get the current working directory.

    INPUTS
        buf - Pointer of the buffer where the path is to be stored
        size - The size of the above buffer

    RESULT
        Copies the absolute pathname of the current working directory
        to the buffer. If the pathname is longer than the buffer
        (with lenght "size") NULL is returned and errno set to ERANGE.
        Otherwise the pointer to the buffer is returned.

    NOTES
        If buf is NULL this function will allocate the buffer itself
        using malloc() and the specified size "size". If size is
        0, too, the buffer is allocated to hold the whole path.
        It is possible and recommended to free() this buffer yourself!
        The path returned does not have to be literally the same as the
        one given to chdir. See NOTES from chdir for more explanation.

    EXAMPLE

    BUGS

    SEE ALSO
        chdir()

    INTERNALS

******************************************************************************/
{
    if (buf && !size) { errno = EINVAL; return NULL; }
    POSIXC_CWD_SCOPE_OR_RETURN(NULL);
    struct __cwd_snapshot snapshot;
    char *native = NULL, *result = buf;
    const char *converted;
    size_t capacity = 256, length;
    LONG error;
    BOOL named;

    if (!__cwd_runtime_snapshot(&snapshot)) { errno = ENOMEM; return NULL; }
    for (;;) {
        native = AllocVec(capacity, MEMF_PUBLIC);
        if (!native) { error = ERROR_NO_FREE_STORE; break; }
        named = NameFromLock(__cwd_directory_lock(snapshot.cwd), native, (LONG)capacity);
        error = IoErr();
        if (named) break;
        FreeVec(native); native = NULL;
        if (error != ERROR_LINE_TOO_LONG) break;
        if (capacity > (size_t)0x7fffffffU / 2) break;
        capacity *= 2;
    }
    __cwd_snapshot_release(&snapshot);
    if (!native) { errno = __stdc_ioerr2errno(error); return NULL; }
    converted = __path_a2u(native);
    if (!converted) { FreeVec(native); return NULL; }
    length = strlen(converted);
    if (!size && !buf) size = length + 1;
    if (length >= size) {
        FreeVec(native); errno = ERANGE; return NULL;
    }
    if (!result) result = malloc(size);
    if (!result) { FreeVec(native); errno = ENOMEM; return NULL; }
    memmove(result, converted, length + 1);
    FreeVec(native);
    return result;
} /* getcwd */
