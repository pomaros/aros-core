/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: Venus shared memory for a renderer and clients in one address space.

    A region is ordinary memory; its handle indexes a table. The region
    stays while a handle or a mapping of it does.
*/

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <venus/avs.h>

struct avs_region
{
    void   *ptr;
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
    free(r->ptr);
    r->ptr = NULL;
}

static void *local_create(size_t size, size_t alignment, int *handle)
{
    void *ptr = NULL;
    int i;

    if (alignment < sizeof(void *))
        alignment = sizeof(void *);
    if (posix_memalign(&ptr, alignment, size))
        return NULL;
    memset(ptr, 0, size);

    pthread_mutex_lock(&region_lock);
    for (i = 0; i < region_count && regions[i].ptr; i++)
        ;
    if (i == region_count)
    {
        struct avs_region *grown = realloc(regions, (region_count + 64) * sizeof(*regions));

        if (!grown)
        {
            pthread_mutex_unlock(&region_lock);
            free(ptr);
            return NULL;
        }
        memset(grown + region_count, 0, 64 * sizeof(*regions));
        regions = grown;
        region_count += 64;
    }
    regions[i] = (struct avs_region){ ptr, size, 1, 1 };
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
