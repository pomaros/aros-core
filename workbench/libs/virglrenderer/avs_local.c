/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: Venus shared memory for a renderer and clients in one address space.

    A region is ordinary memory; its handle indexes a table. The region
    stays while a handle or a mapping of it does.
*/

#include <stdint.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include <venus/avs.h>

struct avs_region
{
    void   *ptr;
    void   *allocation;
    size_t  size;
    int     handles;
    int     maps;
};

static pthread_mutex_t region_lock = PTHREAD_MUTEX_INITIALIZER;
static struct avs_region *regions;
static int region_count;

static void region_release(int handle)
{
    struct avs_region *r = &regions[handle];

    if (r->handles || r->maps)
        return;
    FreeVec(r->allocation);
    r->ptr = NULL;
}

static void *local_create(size_t size, size_t alignment, int *handle)
{
    void *ptr, *allocation;
    int i;

    if (alignment < sizeof(void *))
        alignment = sizeof(void *);
    /* The native backing can be allocated by a renderer worker. Its
       lifetime does not depend on that worker having a POSIX context. */
    if (!size || (alignment & (alignment - 1)) ||
        size > UINTPTR_MAX - (alignment - 1))
        return NULL;
    allocation = AllocVec(size + alignment - 1, MEMF_PUBLIC | MEMF_CLEAR);
    if (!allocation)
        return NULL;
    ptr = (void *)(((uintptr_t)allocation + alignment - 1) & ~(uintptr_t)(alignment - 1));

    pthread_mutex_lock(&region_lock);
    for (i = 0; i < region_count && regions[i].ptr; i++)
        ;
    if (i == region_count)
    {
        struct avs_region *grown = NULL;

        if (region_count <= INT_MAX - 64 &&
            (size_t)region_count + 64 <= SIZE_MAX / sizeof(*regions))
            grown = realloc(regions, ((size_t)region_count + 64) * sizeof(*regions));

        if (!grown)
        {
            pthread_mutex_unlock(&region_lock);
            FreeVec(allocation);
            return NULL;
        }
        memset(grown + region_count, 0, 64 * sizeof(*regions));
        regions = grown;
        region_count += 64;
    }
    regions[i] = (struct avs_region){ ptr, allocation, size, 1, 1 };
    pthread_mutex_unlock(&region_lock);
    *handle = i;
    return ptr;
}

static struct avs_region *region_of(int handle)
{
    return handle >= 0 && handle < region_count && regions[handle].ptr ? &regions[handle] : NULL;
}

static void *local_map(int handle, size_t size)
{
    struct avs_region *r;
    void *ptr = NULL;

    pthread_mutex_lock(&region_lock);
    if ((r = region_of(handle)) && size <= r->size)
    {
        r->maps++;
        ptr = r->ptr;
    }
    pthread_mutex_unlock(&region_lock);
    return ptr;
}

static int local_share(int handle)
{
    struct avs_region *r;

    pthread_mutex_lock(&region_lock);
    if ((r = region_of(handle)))
        r->handles++;
    pthread_mutex_unlock(&region_lock);
    return r ? handle : -1;
}

static void local_close(int handle)
{
    struct avs_region *r;

    pthread_mutex_lock(&region_lock);
    if ((r = region_of(handle)) && r->handles)
    {
        r->handles--;
        region_release(handle);
    }
    pthread_mutex_unlock(&region_lock);
}

static void local_unmap(void *ptr, size_t size)
{
    int i;

    pthread_mutex_lock(&region_lock);
    for (i = 0; i < region_count; i++)
        if (regions[i].ptr == ptr && regions[i].maps)
        {
            regions[i].maps--;
            region_release(i);
            break;
        }
    pthread_mutex_unlock(&region_lock);
}

static void local_client_unmap(int handle, void *addr, size_t size)
{
    local_unmap(addr, size);
}

static const struct avs_memory_ops local_memory =
{
    .create       = local_create,
    .import       = local_map,
    .share        = local_share,
    .close        = local_close,
    .unmap        = local_unmap,
    .client_map   = local_map,
    .client_unmap = local_client_unmap,
};

const struct avs_memory_ops *avs_local_memory(void)
{
    return &local_memory;
}
