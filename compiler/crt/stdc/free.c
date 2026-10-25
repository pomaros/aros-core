/*
    Copyright (C) 1995-2024, The AROS Development Team. All rights reserved.

    C99 function free().
*/

#include "__malloc.h"

#include <exec/memory.h>
#include <proto/exec.h>

/*****************************************************************************

    NAME */
#include <stdlib.h>

        void free (

/*  SYNOPSIS */
        void * memory)

/*  FUNCTION
        Return memory allocated with malloc() or a similar function to the
        system.

    INPUTS
        memory - The result of the previous call to malloc(), etc. or
                NULL.

    RESULT
        None.

    NOTES
        An allocation may be freed through a different stdc opener. Its
        originating opener must remain open until the allocation is freed;
        closing that opener still destroys its pool and outstanding blocks.

    EXAMPLE

    BUGS

    SEE ALSO
        malloc()

    INTERNALS

******************************************************************************/
{
    if (memory)
    {
        struct __stdc_malloc_header *header = __stdc_allocation_header(memory);
        FreePooled(header->pool, header->allocation, header->allocation_size);
    }

} /* free */

