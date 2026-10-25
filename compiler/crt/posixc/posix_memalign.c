/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.

    POSIX.1-2008 function posix_memalign().
*/

#include <errno.h>
#include <stdint.h>

/*****************************************************************************

    NAME */
#include <stdlib.h>

        int posix_memalign (

/*  SYNOPSIS */
        void **memptr,
        size_t alignment,
        size_t size)

/*  FUNCTION
        Allocate aligned memory.

    INPUTS
        memptr - Pointer to a place to store the pointer to allocated memory.
        alignment - Alignment of allocated memory. The address of the
                    allocated memory will be a multiple of this value, which
                    must be a power of two and a multiple of sizeof(void *).
        size - How much memory to allocate.

    RESULT
        Returns zero on success.
        Returns EINVAL if the alignment parameter was not a power of two, or
        was not a multiple of sizeof(void *).
        Returns ENOMEM if there was insufficient memory to fulfill the request.

    NOTES
        Memory allocated by posix_memalign() should be freed with free(). If
        not, it will be freed when the program terminates.

        errno is preserved. On failure, *memptr is unchanged.

    EXAMPLE

    BUGS

    SEE ALSO
        stdc.library/aligned_alloc(), stdc.library/calloc(),
        stdc.library/free(), stdc.library/malloc()

    INTERNALS

******************************************************************************/
{
    int old_errno = errno;
    void *allocation;
    size_t rounded_size;

    if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
        alignment % sizeof(void *) != 0)
        return EINVAL;

    /* Unlike C11 aligned_alloc(), POSIX permits any requested size. */
    if (size > SIZE_MAX - (alignment - 1))
        return ENOMEM;
    rounded_size = (size + alignment - 1) & ~(alignment - 1);
    allocation = aligned_alloc(alignment, rounded_size);
    errno = old_errno;
    if (allocation == NULL)
        return ENOMEM;
    *memptr = allocation;
    return 0;
}
