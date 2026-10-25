/*
    Copyright (C) 1995-2014, The AROS Development Team. All rights reserved.
*/

#include "__malloc.h"
#include <proto/exec.h>

/*****************************************************************************

    NAME */
#include <stdlib.h>

        void * realloc_nocopy (

/*  SYNOPSIS */
        void * oldmem,
        size_t size)

/*  FUNCTION
        Change the size of an allocated part of memory. The memory must
        have been allocated by malloc(), calloc(), realloc() or realloc_nocopy().
        
        The reallocated buffer, unlike with realloc(), is not guaranteed to hold
        a copy of the old one.

    INPUTS
        oldmem - What you got from malloc(), calloc(), realloc() or realloc_nocopy().
                 If NULL, the function will behave exactly like malloc().
        size   - The new size. If 0, the buffer will be freed.

    RESULT
        A pointer to the allocated memory or NULL. If you don't need the
        memory anymore, you can pass this pointer to free(). If you don't,
        the memory will be freed for you when the application exits.

    NOTES
        If you get NULL, the memory at oldmem will not have been freed and
        can still be used.

        This function is AROS specific.

    EXAMPLE

    BUGS

    SEE ALSO
        free(), malloc(), calloc(), realloc()

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
        free(oldmem);
    }
    return newmem;
} /* realloc_nocopy */
