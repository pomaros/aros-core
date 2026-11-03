/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: the Venus renderer's client interface, served by vkr.

    Each client is a vkr context. Syncs are 64-bit counters a client waits
    on; a submission's syncs are signalled either right after its command
    stream is processed or, on a timeline, when vkr retires the fence that
    follows the batch on its ring. A retired fence also signals every batch
    before it on that ring, as fences are mergeable.
*/

#include "config.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vulkan/vulkan.h"
#include "util/macros.h"
#include "virgl_resource.h"
#include "virglrenderer.h"
#include "vkr_aros.h"
#include "vkr_renderer.h"
#include "virtgpu_drm.h"

#include <venus/avs.h>

#define AVS_MAX_TIMELINES 64

struct avs_sync
{
    struct avs_sync *next;
    uint32_t         id;
    uint32_t         refs;
    uint64_t         value;
};

struct avs_submit
{
    struct avs_submit *next;
    uint32_t           count;
    struct avs_sync  **syncs;
    uint64_t          *values;
};

struct avs_blob
{
    struct avs_blob *next;
    uint32_t         res_id;
    int              handle;
    uint64_t         size;
    void            *addr;
};

struct avs_client
{
    struct avs_client *next;
    uint32_t           ctx_id;
    uint32_t           next_sync_id;
    uint32_t           next_res_id;
    struct avs_sync   *syncs;
    struct avs_blob   *blobs;
    struct {
        struct avs_submit *head, **tail;
    } timelines[AVS_MAX_TIMELINES];
};

static pthread_mutex_t avs_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t avs_signalled = PTHREAD_COND_INITIALIZER;
static struct avs_client *avs_clients;
static uint32_t avs_next_ctx_id = 1;
static const struct avs_memory_ops *avs_memory;
static avs_get_instance_proc_addr avs_gipa;
static bool avs_running;

/* vkr reaches the native driver through this symbol */
PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *name)
{
    return (PFN_vkVoidFunction)avs_gipa(instance, name);
}

static struct avs_vkr_shm
{
    struct vkr_aros_shm_ops ops;
} avs_vkr_shm;

static struct avs_client *avs_find_client(uint32_t ctx_id)
{
    struct avs_client *c;

    for (c = avs_clients; c; c = c->next)
        if (c->ctx_id == ctx_id)
            return c;
    return NULL;
}

static struct avs_sync *avs_find_sync(struct avs_client *c, uint32_t id)
{
    struct avs_sync *s;

    for (s = c->syncs; s; s = s->next)
        if (s->id == id)
            return s;
    return NULL;
}

static void avs_unref_sync(struct avs_client *c, struct avs_sync *sync)
{
    struct avs_sync **p;

    if (--sync->refs)
        return;
    for (p = &c->syncs; *p; p = &(*p)->next)
        if (*p == sync)
        {
            *p = sync->next;
            break;
        }
    free(sync);
}

static void avs_signal(struct avs_sync *sync, uint64_t value)
{
    sync->value = value;
    pthread_cond_broadcast(&avs_signalled);
}

static void avs_free_submit(struct avs_client *c, struct avs_submit *submit)
{
    uint32_t i;

    for (i = 0; i < submit->count; i++)
        avs_unref_sync(c, submit->syncs[i]);
    free(submit);
}

/* Called by vkr from its own threads */
static void avs_retire_fence(uint32_t ctx_id, uint32_t ring_idx, uint64_t fence_id)
{
    struct avs_client *c;

    pthread_mutex_lock(&avs_lock);
    c = avs_find_client(ctx_id);
    if (c && ring_idx < AVS_MAX_TIMELINES)
    {
        struct avs_submit *target = (struct avs_submit *)(uintptr_t)fence_id, *submit;

        while ((submit = c->timelines[ring_idx].head))
        {
            uint32_t i;

            c->timelines[ring_idx].head = submit->next;
            if (!submit->next)
                c->timelines[ring_idx].tail = &c->timelines[ring_idx].head;
            bool last = submit == target;

            for (i = 0; i < submit->count; i++)
                avs_signal(submit->syncs[i], submit->values[i]);
            avs_free_submit(c, submit);
            if (last)
                break;
        }
    }
    pthread_mutex_unlock(&avs_lock);
}

static void avs_log(UNUSED enum virgl_log_level_flags level, const char *message,
                    UNUSED void *data)
{
    fprintf(stderr, "[venus] %s", message);
}

static const struct vkr_renderer_callbacks avs_callbacks =
{
    .debug_logger = avs_log,
    .retire_fence = avs_retire_fence,
};

bool avs_server_init(const struct avs_memory_ops *memory, avs_get_instance_proc_addr gipa)
{
    if (avs_running)
        return true;
    avs_memory = memory;
    avs_gipa = gipa;
    avs_vkr_shm.ops = (struct vkr_aros_shm_ops){
        .create = memory->create,
        .import = memory->import,
        .share  = memory->share,
        .close  = memory->close,
        .unmap  = memory->unmap,
    };
    vkr_aros_set_shm_ops(&avs_vkr_shm.ops);
    avs_running = vkr_renderer_init(VKR_RENDERER_THREAD_SYNC | VKR_RENDERER_ASYNC_FENCE_CB,
                                    &avs_callbacks);
    return avs_running;
}

void avs_server_fini(void)
{
    if (!avs_running)
        return;
    while (avs_clients)
        avs_disconnect(avs_clients);
    vkr_renderer_fini();
    avs_running = false;
}

struct avs_client *avs_connect(const char *name)
{
    struct avs_client *c = calloc(1, sizeof(*c));
    uint32_t i;

    if (!c || !avs_running)
    {
        free(c);
        return NULL;
    }
    c->next_sync_id = 1;
    c->next_res_id = 1;
    for (i = 0; i < AVS_MAX_TIMELINES; i++)
        c->timelines[i].tail = &c->timelines[i].head;

    pthread_mutex_lock(&avs_lock);
    c->ctx_id = avs_next_ctx_id++;
    pthread_mutex_unlock(&avs_lock);
    if (!vkr_renderer_create_context(c->ctx_id, VIRTGPU_DRM_CAPSET_VENUS,
                                     strlen(name), name))
    {
        free(c);
        return NULL;
    }
    pthread_mutex_lock(&avs_lock);
    c->next = avs_clients;
    avs_clients = c;
    pthread_mutex_unlock(&avs_lock);
    return c;
}

void avs_disconnect(struct avs_client *c)
{
    struct avs_client **p;
    uint32_t i;

    /* The context goes first: no fence retires for it afterwards */
    vkr_renderer_destroy_context(c->ctx_id);

    pthread_mutex_lock(&avs_lock);
    for (p = &avs_clients; *p; p = &(*p)->next)
        if (*p == c)
        {
            *p = c->next;
            break;
        }
    for (i = 0; i < AVS_MAX_TIMELINES; i++)
    {
        struct avs_submit *submit;

        while ((submit = c->timelines[i].head))
        {
            c->timelines[i].head = submit->next;
            avs_free_submit(c, submit);
        }
    }
    while (c->blobs)
    {
        struct avs_blob *blob = c->blobs;

        c->blobs = blob->next;
        if (blob->addr)
            avs_memory->client_unmap(blob->handle, blob->addr, blob->size);
        avs_memory->close(blob->handle);
        free(blob);
    }
    while (c->syncs)
    {
        struct avs_sync *sync = c->syncs;

        c->syncs = sync->next;
        free(sync);
    }
    pthread_cond_broadcast(&avs_signalled);
    pthread_mutex_unlock(&avs_lock);
    free(c);
}

bool avs_get_capset(struct avs_client *c, void *capset, size_t size)
{
    uint8_t buffer[1024];
    size_t full = vkr_get_capset(NULL, 0);

    if (full > sizeof(buffer))
        return false;
    vkr_get_capset(buffer, 0);
    memcpy(capset, buffer, size < full ? size : full);
    if (size > full)
        memset((uint8_t *)capset + full, 0, size - full);
    return true;
}

uint32_t avs_max_timelines(struct avs_client *c)
{
    return AVS_MAX_TIMELINES;
}

uint32_t avs_blob_create(struct avs_client *c, uint64_t size, uint64_t blob_id,
                         uint32_t flags, void **addr)
{
    struct avs_blob *blob = calloc(1, sizeof(*blob));
    enum virgl_resource_fd_type type;
    uint32_t map_info;
    int handle;

    if (!blob)
        return 0;
    pthread_mutex_lock(&avs_lock);
    blob->res_id = c->next_res_id++;
    pthread_mutex_unlock(&avs_lock);
    if (!vkr_renderer_create_resource(c->ctx_id, blob->res_id, blob_id, size, flags,
                                      &type, &handle, &map_info, NULL) ||
        type != VIRGL_RESOURCE_FD_SHM)
    {
        free(blob);
        return 0;
    }
    blob->handle = handle;
    blob->size = size;
    if (flags & AVS_BLOB_MAPPABLE)
    {
        blob->addr = avs_memory->client_map(handle, size);
        if (!blob->addr)
        {
            avs_memory->close(handle);
            vkr_renderer_destroy_resource(c->ctx_id, blob->res_id);
            free(blob);
            return 0;
        }
    }
    if (addr)
        *addr = blob->addr;

    pthread_mutex_lock(&avs_lock);
    blob->next = c->blobs;
    c->blobs = blob;
    pthread_mutex_unlock(&avs_lock);
    return blob->res_id;
}

void avs_blob_unref(struct avs_client *c, uint32_t res_id)
{
    struct avs_blob **p, *blob = NULL;

    pthread_mutex_lock(&avs_lock);
    for (p = &c->blobs; *p; p = &(*p)->next)
        if ((*p)->res_id == res_id)
        {
            blob = *p;
            *p = blob->next;
            break;
        }
    pthread_mutex_unlock(&avs_lock);
    if (!blob)
        return;
    if (blob->addr)
        avs_memory->client_unmap(blob->handle, blob->addr, blob->size);
    avs_memory->close(blob->handle);
    vkr_renderer_destroy_resource(c->ctx_id, res_id);
    free(blob);
}

uint32_t avs_sync_create(struct avs_client *c, uint64_t initial)
{
    struct avs_sync *sync = calloc(1, sizeof(*sync));
    uint32_t id;

    if (!sync)
        return 0;
    pthread_mutex_lock(&avs_lock);
    sync->id = id = c->next_sync_id++;
    sync->refs = 1;
    sync->value = initial;
    sync->next = c->syncs;
    c->syncs = sync;
    pthread_mutex_unlock(&avs_lock);
    return id;
}

void avs_sync_unref(struct avs_client *c, uint32_t id)
{
    struct avs_sync *sync;

    pthread_mutex_lock(&avs_lock);
    if ((sync = avs_find_sync(c, id)))
        avs_unref_sync(c, sync);
    pthread_mutex_unlock(&avs_lock);
}

uint64_t avs_sync_read(struct avs_client *c, uint32_t id)
{
    struct avs_sync *sync;
    uint64_t value = 0;

    pthread_mutex_lock(&avs_lock);
    if ((sync = avs_find_sync(c, id)))
        value = sync->value;
    pthread_mutex_unlock(&avs_lock);
    return value;
}

void avs_sync_write(struct avs_client *c, uint32_t id, uint64_t value)
{
    struct avs_sync *sync;

    pthread_mutex_lock(&avs_lock);
    if ((sync = avs_find_sync(c, id)))
        avs_signal(sync, value);
    pthread_mutex_unlock(&avs_lock);
}

static bool avs_wait_done(struct avs_client *c, bool any, const uint32_t *ids,
                          const uint64_t *values, uint32_t count)
{
    uint32_t i, reached = 0;

    for (i = 0; i < count; i++)
    {
        struct avs_sync *sync = avs_find_sync(c, ids[i]);

        if (sync && sync->value >= values[i])
        {
            if (any)
                return true;
            reached++;
        }
    }
    return reached == count;
}

int avs_sync_wait(struct avs_client *c, bool any, uint64_t timeout_ns,
                  const uint32_t *ids, const uint64_t *values, uint32_t count)
{
    struct timespec deadline;
    int result = 0;

    clock_gettime(CLOCK_REALTIME, &deadline);
    if (timeout_ns > (uint64_t)365 * 24 * 3600 * 1000000000ULL)
        timeout_ns = (uint64_t)365 * 24 * 3600 * 1000000000ULL;
    deadline.tv_sec += timeout_ns / 1000000000ULL;
    deadline.tv_nsec += timeout_ns % 1000000000ULL;
    if (deadline.tv_nsec >= 1000000000L)
    {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&avs_lock);
    while (!avs_wait_done(c, any, ids, values, count))
    {
        if (!avs_running)
        {
            result = -1;
            break;
        }
        if (pthread_cond_timedwait(&avs_signalled, &avs_lock, &deadline) == ETIMEDOUT)
        {
            result = avs_wait_done(c, any, ids, values, count) ? 0 : 1;
            break;
        }
    }
    pthread_mutex_unlock(&avs_lock);
    return result;
}

bool avs_sync_ready(struct avs_client *c, bool any, const uint32_t *ids,
                    const uint64_t *values, uint32_t count)
{
    bool ready;

    pthread_mutex_lock(&avs_lock);
    ready = avs_wait_done(c, any, ids, values, count);
    pthread_mutex_unlock(&avs_lock);
    return ready;
}

static bool avs_submit_batch(struct avs_client *c, const struct avs_batch *b)
{
    struct avs_submit *submit = NULL;
    uint32_t i;

    if (b->cs_size && !vkr_renderer_submit_cmd(c->ctx_id, (void *)b->cs, b->cs_size))
        return false;
    if (!b->sync_count)
        return true;
    if (b->timeline && b->ring_idx >= AVS_MAX_TIMELINES)
        return false;

    pthread_mutex_lock(&avs_lock);
    if (b->timeline)
    {
        submit = malloc(sizeof(*submit) + b->sync_count * (sizeof(*submit->syncs) + sizeof(*submit->values)));
        if (!submit)
        {
            pthread_mutex_unlock(&avs_lock);
            return false;
        }
        submit->next = NULL;
        submit->count = 0;
        submit->syncs = (struct avs_sync **)&submit[1];
        submit->values = (uint64_t *)&submit->syncs[b->sync_count];
    }
    for (i = 0; i < b->sync_count; i++)
    {
        struct avs_sync *sync = avs_find_sync(c, b->sync_ids[i]);

        if (!sync)
            break;
        if (submit)
        {
            sync->refs++;
            submit->syncs[submit->count] = sync;
            submit->values[submit->count++] = b->sync_values[i];
        }
        else
            avs_signal(sync, b->sync_values[i]);
    }
    if (i < b->sync_count)
    {
        if (submit)
            avs_free_submit(c, submit);
        pthread_mutex_unlock(&avs_lock);
        return false;
    }
    if (submit)
    {
        *c->timelines[b->ring_idx].tail = submit;
        c->timelines[b->ring_idx].tail = &submit->next;
    }
    pthread_mutex_unlock(&avs_lock);

    /* The fence follows the batch on its ring; retiring it signals the syncs */
    if (submit && !vkr_renderer_submit_fence(c->ctx_id, VIRGL_RENDERER_FENCE_FLAG_MERGEABLE,
                                             b->ring_idx, (uintptr_t)submit))
        return false;
    return true;
}

bool avs_submit(struct avs_client *c, const struct avs_batch *batches, uint32_t count)
{
    uint32_t i;

    for (i = 0; i < count; i++)
        if (!avs_submit_batch(c, &batches[i]))
            return false;
    return true;
}
