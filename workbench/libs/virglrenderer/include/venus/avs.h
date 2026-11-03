#ifndef VENUS_AVS_H
#define VENUS_AVS_H

/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: the Venus renderer on AROS, as its clients see it.

    Mesa's Venus driver encodes Vulkan commands; the renderer decodes them
    and runs them on a native Vulkan driver. The client side of this
    interface is what the driver's AROS transport calls. A process that
    hosts the renderer implements it directly; a client in another address
    space, such as an isolated Windows process, gets an implementation that
    forwards each call to the renderer serving it. The interface mirrors
    the vtest protocol, without sockets or file descriptors. venusrenderer.library
    hosts the renderer and exports these functions.
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct avs_client;

/* Blob flags, as virglrenderer's VIRGL_RENDERER_BLOB_FLAG_USE_* */
#define AVS_BLOB_MAPPABLE       0x1
#define AVS_BLOB_SHAREABLE      0x2
#define AVS_BLOB_CROSS_DEVICE   0x4

/* One batch of a submission: an optional command stream for the renderer,
   then the syncs to signal. With a timeline, they are signalled when the
   ring's queue has executed the batch; without one, once the renderer has
   processed the command stream. */
struct avs_batch
{
    const void      *cs;
    size_t           cs_size;
    uint32_t         ring_idx;
    bool             timeline;
    uint32_t         sync_count;
    const uint32_t  *sync_ids;
    const uint64_t  *sync_values;
};

struct avs_client *avs_connect(const char *name);
void avs_disconnect(struct avs_client *client);

/* The renderer's struct virgl_renderer_capset_venus */
bool avs_get_capset(struct avs_client *client, void *capset, size_t size);
uint32_t avs_max_timelines(struct avs_client *client);

/* A blob: device memory when blob_id names a VkDeviceMemory, otherwise
   shared memory for the driver. *addr is the client's mapping of a
   mappable blob. Returns the resource id, 0 on failure. */
uint32_t avs_blob_create(struct avs_client *client, uint64_t size,
                         uint64_t blob_id, uint32_t flags, void **addr);
void avs_blob_unref(struct avs_client *client, uint32_t res_id);

uint32_t avs_sync_create(struct avs_client *client, uint64_t initial);
void avs_sync_unref(struct avs_client *client, uint32_t sync_id);
uint64_t avs_sync_read(struct avs_client *client, uint32_t sync_id);
void avs_sync_write(struct avs_client *client, uint32_t sync_id, uint64_t value);

/* Waits until every sync (any one, with any) reaches its value. Returns 0
   when they did, 1 on timeout, -1 when the renderer is gone. */
int avs_sync_wait(struct avs_client *client, bool any, uint64_t timeout_ns,
                  const uint32_t *sync_ids, const uint64_t *values,
                  uint32_t count);

bool avs_submit(struct avs_client *client, const struct avs_batch *batches,
                uint32_t count);

/*
    The renderer's side, for the process that hosts it.

    The host supplies the memory both sides see. Each region has a handle;
    create, import, share, close and unmap serve the renderer, client_map
    and client_unmap give the client's address of a region.
*/
struct avs_memory_ops
{
    void *(*create)(size_t size, size_t alignment, int *handle);
    void *(*import)(int handle, size_t size);
    int   (*share)(int handle);
    void  (*close)(int handle);
    void  (*unmap)(void *ptr, size_t size);
    void *(*client_map)(int handle, size_t size);
    void  (*client_unmap)(int handle, void *addr, size_t size);
};

typedef void (*avs_void_function)(void);
typedef avs_void_function (*avs_get_instance_proc_addr)(void *instance, const char *name);

/* Starts the renderer over a native Vulkan driver's root entry point */
bool avs_server_init(const struct avs_memory_ops *memory,
                     avs_get_instance_proc_addr gipa);
void avs_server_fini(void);

/* Memory for a renderer and clients in one address space */
const struct avs_memory_ops *avs_local_memory(void);

#endif /* VENUS_AVS_H */
