/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.

    C99 function realloc().
*/

#include "__malloc.h"
#include <proto/exec.h>

/*****************************************************************************

    NAME */
#include <stdlib.h>

        void * realloc (

/*  SYNOPSIS */
        void * oldmem,
        size_t size)

/*  FUNCTION
        Change the size of an allocated part of memory. The memory must
        have been allocated by malloc() or calloc(). If you reduce the
        size, the old contents will be lost. If you enlarge the size,
        the new contents will be undefined.

    INPUTS
        oldmem - What you got from malloc() or calloc().
        size - The new size.

    RESULT
        A pointer to the allocated memory or NULL. If you don't need the
        memory anymore, you can pass this pointer to free(). If you don't,
        the memory will be freed for you when the application exits.

    NOTES
        If you get NULL, the memory at oldmem will not have been freed and
        can still be used.

    EXAMPLE

    BUGS

    SEE ALSO
        calloc(), free(), malloc()

    INTERNALS

******************************************************************************/
{
    struct __stdc_malloc_header *header;
    void *newmem;
    size_t oldsize;

    if (oldmem == NULL)
        return malloc(size);
    if (size == 0)
    {
        free(oldmem);
        return NULL;
    }

    header = __stdc_allocation_header(oldmem);
    oldsize = header->requested_size;
    if (size == oldsize || (size < oldsize && oldsize - size < 4096))
    {
        header->requested_size = size;
        return oldmem;
    }

    /* Resizing retains the original allocation domain, even when a different
       stdc opener calls realloc(). On failure the old block is untouched. */
    newmem = __stdc_alloc_from_pool(header->pool, size, __alignof__(max_align_t));
    if (newmem != NULL)
    {
        CopyMem(oldmem, newmem, size < oldsize ? size : oldsize);
        free(oldmem);
    }
    return newmem;
} /* realloc */
