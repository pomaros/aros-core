/*
    Copyright (C) 2026 John Knipper.
    Licensed under the AROS Public License (APL) Version 1.1.

    Private stdc allocation metadata. Every allocation records its exact pool
    and raw allocation, so another stdc opener can release it without changing
    Exec pool ownership rules. The originating opener must remain open: its
    existing close hook still deletes the pool and all outstanding blocks.
    This is one allocation format, including aligned allocations; no legacy
    header decoding or foreign allocator detection belongs here.
*/
#ifndef __STDC_MALLOC_H
#define __STDC_MALLOC_H

#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <proto/exec.h>

struct __stdc_malloc_header
{
    void *pool;
    void *allocation;
    size_t allocation_size;
    size_t requested_size;
};

static inline struct __stdc_malloc_header *__stdc_allocation_header(void *ptr)
{
    return (struct __stdc_malloc_header *)ptr - 1;
}

static inline void *__stdc_alloc_from_pool(void *pool, size_t size,
                                         size_t alignment)
{
    const size_t minimum_alignment = __alignof__(max_align_t);
    const size_t payload = size ? size : 1;
    size_t extra, total;
    unsigned char *allocation, *user;
    uintptr_t address;
    struct __stdc_malloc_header *header;

    if (alignment < minimum_alignment)
        alignment = minimum_alignment;
    if (alignment - 1 > SIZE_MAX - sizeof(*header))
        goto overflow;
    extra = sizeof(*header) + alignment - 1;
    if (payload > SIZE_MAX - extra)
        goto overflow;
    total = payload + extra;
    allocation = AllocPooled(pool, total);
    if (allocation == NULL)
        goto overflow;

    address = (uintptr_t)allocation + sizeof(*header);
    address = (address + alignment - 1) & ~(uintptr_t)(alignment - 1);
    user = (unsigned char *)address;
    header = __stdc_allocation_header(user);
    header->pool = pool;
    header->allocation = allocation;
    header->allocation_size = total;
    header->requested_size = size;
    return user;

overflow:
    errno = ENOMEM;
    return NULL;
}

#endif
