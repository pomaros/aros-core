/*
    Copyright (C) 2025-2026, The AROS Development Team. All rights reserved.

    C11 function aligned_alloc().
*/

#include <exec/types.h>

#include "__stdc_intbase.h"
#include "__malloc.h"

#include <errno.h>
#include <stdint.h>

#define powerof2(x) ((((x)-1)&(x))==0)

/*****************************************************************************

    NAME */
#include <stdlib.h>

        void *aligned_alloc (

/*  SYNOPSIS */
        size_t alignment,
        size_t size)

/*  FUNCTION
        Allocate aligned memory.

    INPUTS
        alignment - Alignment of allocated memory. The address of the
                    allocated memory will be a multiple of this value, which
                    must be a nonzero power of two.
        size - How much memory to allocate; must be a multiple of alignment.

    RESULT
        Returns the allocated pointer on success, or NULL on failure.
        Sets errno to EINVAL for an invalid alignment, or ENOMEM for an
        overflowing request or insufficient memory.

    NOTES
        Memory allocated by aligned_alloc() should be freed with free(). If
        not, it will be freed when the program terminates.

        The originating stdc opener must remain open until the block is freed.

    EXAMPLE

    BUGS

    SEE ALSO
        calloc(), free(), malloc()

    INTERNALS

******************************************************************************/
{
    struct StdCIntBase *StdCBase =
        (struct StdCIntBase *)__aros_getbase_StdCBase();

    if (alignment == 0 || !powerof2(alignment) || size % alignment != 0)
    {
        errno = EINVAL;
        return NULL;
    }

    return __stdc_alloc_from_pool(StdCBase->mempool, size, alignment);
}
