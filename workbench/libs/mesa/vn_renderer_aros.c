/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: Venus renderer transport for AROS.

    The driver talks to the renderer through the avs interface: directly
    when the renderer runs in the same process, through a forwarding
    implementation from an isolated domain (a Windows process on AROS
    builds this file with VN_AROS_TRANSPORT). Shared memory comes mapped;
    syncs are the renderer's 64-bit counters. Modelled on the vtest
    transport, without its socket.
*/

#include <stdlib.h>
#include <string.h>

#ifdef __AROS__
#include <proto/exec.h>
#endif

#include "virtio/virtio-gpu/venus_hw.h"
#include "util/sparse_array.h"
#include "util/u_process.h"

#include "vn_renderer_internal.h"

#include <venus/avs.h>

/* The renderer's identity in VkPhysicalDevicePCIBusInfo and friends */
#define AROS_VENUS_PCI_VENDOR_ID 0x1af4
#define AROS_VENUS_PCI_DEVICE_ID 0x1050

#ifdef __AROS__
/* A thread's identity for the watchdog: its task */
long
vn_aros_gettid(void)
{
   return (long)(uintptr_t)FindTask(NULL);
}
#endif

struct aros_shmem {
   struct vn_renderer_shmem base;
};

struct aros_bo {
   struct vn_renderer_bo base;
};

struct aros_sync {
   struct vn_renderer_sync base;
};

struct aros_venus {
   struct vn_renderer base;

   struct vn_instance *instance;
   struct avs_client *client;
   struct virgl_renderer_capset_venus capset;

   struct util_sparse_array shmem_array;
   struct util_sparse_array bo_array;
   struct vn_renderer_shmem_cache shmem_cache;
   bool shmem_cache_ready;
};

static VkResult
aros_sync_write(struct vn_renderer *renderer, struct vn_renderer_sync *sync,
                uint64_t val)
{
   struct aros_venus *av = (struct aros_venus *)renderer;

   avs_sync_write(av->client, sync->sync_id, val);
   return VK_SUCCESS;
}

static VkResult
aros_sync_read(struct vn_renderer *renderer, struct vn_renderer_sync *sync,
               uint64_t *val)
{
   struct aros_venus *av = (struct aros_venus *)renderer;

   *val = avs_sync_read(av->client, sync->sync_id);
   return VK_SUCCESS;
}

static VkResult
aros_sync_reset(struct vn_renderer *renderer, struct vn_renderer_sync *sync)
{
   return aros_sync_write(renderer, sync, 0);
}

static void
aros_sync_destroy(struct vn_renderer *renderer, struct vn_renderer_sync *_sync)
{
   struct aros_venus *av = (struct aros_venus *)renderer;
   struct aros_sync *sync = (struct aros_sync *)_sync;

   avs_sync_unref(av->client, sync->base.sync_id);
   free(sync);
}

static VkResult
aros_sync_create(struct vn_renderer *renderer, uint64_t initial_val,
                 struct vn_renderer_sync **out_sync)
{
   struct aros_venus *av = (struct aros_venus *)renderer;
   struct aros_sync *sync = calloc(1, sizeof(*sync));

   if (!sync)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   sync->base.sync_id = avs_sync_create(av->client, initial_val);
   if (!sync->base.sync_id) {
      free(sync);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   *out_sync = &sync->base;
   return VK_SUCCESS;
}

static void
aros_bo_flush(struct vn_renderer *renderer, struct vn_renderer_bo *bo,
              VkDeviceSize offset, VkDeviceSize size)
{
   /* coherent */
}

static void
aros_bo_invalidate(struct vn_renderer *renderer, struct vn_renderer_bo *bo,
                   VkDeviceSize offset, VkDeviceSize size)
{
   /* coherent */
}

static void *
aros_bo_map(struct vn_renderer *renderer, struct vn_renderer_bo *bo,
            void *placed_addr)
{
   /* Mapped at creation; a placed mapping is not possible */
   if (placed_addr && placed_addr != bo->mmap_ptr)
      return NULL;
   return bo->mmap_ptr;
}

static int
aros_bo_export_dma_buf(struct vn_renderer *renderer, struct vn_renderer_bo *bo)
{
   return -1;
}

static bool
aros_bo_destroy(struct vn_renderer *renderer, struct vn_renderer_bo *bo)
{
   struct aros_venus *av = (struct aros_venus *)renderer;

   avs_blob_unref(av->client, bo->res_id);
   return true;
}

static uint32_t
aros_bo_blob_flags(VkMemoryPropertyFlags flags,
                   VkExternalMemoryHandleTypeFlags external_handles)
{
   uint32_t blob_flags = 0;

   if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
      blob_flags |= AVS_BLOB_MAPPABLE;
   if (external_handles)
      blob_flags |= AVS_BLOB_SHAREABLE;
   if (external_handles & VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT)
      blob_flags |= AVS_BLOB_CROSS_DEVICE;
   return blob_flags;
}

static VkResult
aros_submit(struct vn_renderer *renderer, const struct vn_renderer_submit_batch *b)
{
   struct aros_venus *av = (struct aros_venus *)renderer;
   uint32_t ids_buffer[16], *ids = ids_buffer;
   uint32_t i;
   bool ok;

   if (b->sync_count > ARRAY_SIZE(ids_buffer) &&
       !(ids = malloc(b->sync_count * sizeof(*ids))))
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   for (i = 0; i < b->sync_count; i++)
      ids[i] = b->syncs[i]->sync_id;

   const struct avs_batch batch = {
      .cs = b->cs_data,
      .cs_size = b->cs_size,
      .ring_idx = b->ring_idx,
      /* ring 0 is the CPU timeline: signalled once processed */
      .timeline = b->ring_idx != 0,
      .sync_count = b->sync_count,
      .sync_ids = ids,
      .sync_values = b->sync_values,
   };

   ok = avs_submit(av->client, &batch, 1);
   if (ids != ids_buffer)
      free(ids);
   return ok ? VK_SUCCESS : VK_ERROR_DEVICE_LOST;
}

static VkResult
aros_bo_create_from_device_memory(struct vn_renderer *renderer,
                                  struct vn_renderer_submit_batch *batch,
                                  VkDeviceSize size, vn_object_id mem_id,
                                  VkMemoryPropertyFlags flags,
                                  VkExternalMemoryHandleTypeFlags external_handles,
                                  struct vn_renderer_bo **out_bo)
{
   struct aros_venus *av = (struct aros_venus *)renderer;
   void *addr = NULL;
   uint32_t res_id;

   /* The batch creates the device memory this blob exports */
   if (batch) {
      VkResult result = aros_submit(renderer, batch);

      if (result != VK_SUCCESS)
         return result;
   }

   res_id = avs_blob_create(av->client, size, mem_id,
                            aros_bo_blob_flags(flags, external_handles),
                            &addr);
   if (!res_id)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct aros_bo *bo = util_sparse_array_get(&av->bo_array, res_id);
   *bo = (struct aros_bo){
      .base = {
         .refcount = VN_REFCOUNT_INIT(1),
         .res_id = res_id,
         .mmap_size = size,
         .mmap_ptr = addr,
      },
   };
   *out_bo = &bo->base;
   return VK_SUCCESS;
}

static void
aros_shmem_destroy_now(struct vn_renderer *renderer,
                       struct vn_renderer_shmem *shmem)
{
   struct aros_venus *av = (struct aros_venus *)renderer;

   avs_blob_unref(av->client, shmem->res_id);
}

static void
aros_shmem_destroy(struct vn_renderer *renderer, struct vn_renderer_shmem *shmem)
{
   struct aros_venus *av = (struct aros_venus *)renderer;

   if (vn_renderer_shmem_cache_add(&av->shmem_cache, shmem))
      return;
   aros_shmem_destroy_now(renderer, shmem);
}

static struct vn_renderer_shmem *
aros_shmem_create(struct vn_renderer *renderer, size_t size)
{
   struct aros_venus *av = (struct aros_venus *)renderer;
   struct vn_renderer_shmem *cached =
      vn_renderer_shmem_cache_get(&av->shmem_cache, size);
   void *addr = NULL;
   uint32_t res_id;

   if (cached) {
      cached->refcount = VN_REFCOUNT_INIT(1);
      return cached;
   }

   res_id = avs_blob_create(av->client, size, 0, AVS_BLOB_MAPPABLE, &addr);
   if (!res_id)
      return NULL;

   struct aros_shmem *shmem = util_sparse_array_get(&av->shmem_array, res_id);
   *shmem = (struct aros_shmem){
      .base = {
         .refcount = VN_REFCOUNT_INIT(1),
         .res_id = res_id,
         .mmap_size = size,
         .mmap_ptr = addr,
      },
   };
   return &shmem->base;
}

static VkResult
aros_wait(struct vn_renderer *renderer, const struct vn_renderer_wait *wait)
{
   struct aros_venus *av = (struct aros_venus *)renderer;
   uint32_t ids_buffer[16], *ids = ids_buffer;
   uint32_t i;
   int ret;

   if (wait->sync_count > ARRAY_SIZE(ids_buffer) &&
       !(ids = malloc(wait->sync_count * sizeof(*ids))))
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   for (i = 0; i < wait->sync_count; i++)
      ids[i] = wait->syncs[i]->sync_id;
   ret = avs_sync_wait(av->client, wait->wait_any, wait->timeout, ids,
                       wait->sync_values, wait->sync_count);
   if (ids != ids_buffer)
      free(ids);
   return ret == 0 ? VK_SUCCESS : ret > 0 ? VK_TIMEOUT : VK_ERROR_DEVICE_LOST;
}

static void
aros_destroy(struct vn_renderer *renderer, const VkAllocationCallbacks *alloc)
{
   struct aros_venus *av = (struct aros_venus *)renderer;

   if (av->shmem_cache_ready)
      vn_renderer_shmem_cache_fini(&av->shmem_cache);
   if (av->client)
      avs_disconnect(av->client);
   util_sparse_array_finish(&av->shmem_array);
   util_sparse_array_finish(&av->bo_array);
   vk_free(alloc, av);
}

static void
aros_init_renderer_info(struct aros_venus *av)
{
   struct vn_renderer_info *info = &av->base.info;
   const struct virgl_renderer_capset_venus *capset = &av->capset;

   info->pci.vendor_id = AROS_VENUS_PCI_VENDOR_ID;
   info->pci.device_id = AROS_VENUS_PCI_DEVICE_ID;

   info->has_dma_buf_import = false;
   info->has_external_sync = false;
   /* the renderer's syncs are 64-bit counters */
   info->has_timeline_sync = true;
   info->has_implicit_fencing = false;

   info->wire_format_version = capset->wire_format_version;
   info->vk_xml_version = capset->vk_xml_version;
   info->vk_ext_command_serialization_spec_version =
      capset->vk_ext_command_serialization_spec_version;
   info->vk_mesa_venus_protocol_spec_version =
      capset->vk_mesa_venus_protocol_spec_version;
   STATIC_ASSERT(sizeof(info->vk_extension_mask) >=
                 sizeof(capset->vk_extension_mask1));
   memcpy(info->vk_extension_mask, capset->vk_extension_mask1,
          sizeof(capset->vk_extension_mask1));
   info->max_timeline_count = avs_max_timelines(av->client);
}

static VkResult
aros_init(struct aros_venus *av)
{
   const char *name = util_get_process_name();

   util_sparse_array_init(&av->shmem_array, sizeof(struct aros_shmem), 1024);
   util_sparse_array_init(&av->bo_array, sizeof(struct aros_bo), 1024);

   av->client = avs_connect(name ? name : "venus");
   if (!av->client) {
      vn_log(av->instance, "no Venus renderer");
      return VK_ERROR_INITIALIZATION_FAILED;
   }
   if (!avs_get_capset(av->client, &av->capset, sizeof(av->capset)) ||
       !av->capset.supports_blob_id_0 || !av->capset.allow_vk_wait_syncs ||
       !av->capset.supports_multiple_timelines) {
      vn_log(av->instance, "unusable venus capset");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   vn_renderer_shmem_cache_init(&av->shmem_cache, &av->base, aros_shmem_destroy_now);
   av->shmem_cache_ready = true;
   aros_init_renderer_info(av);

   av->base.ops.destroy = aros_destroy;
   av->base.ops.submit = aros_submit;
   av->base.ops.wait = aros_wait;

   av->base.shmem_ops.create = aros_shmem_create;
   av->base.shmem_ops.destroy = aros_shmem_destroy;

   av->base.bo_ops.create_from_device_memory = aros_bo_create_from_device_memory;
   av->base.bo_ops.create_from_dma_buf = NULL;
   av->base.bo_ops.destroy = aros_bo_destroy;
   av->base.bo_ops.export_dma_buf = aros_bo_export_dma_buf;
   av->base.bo_ops.export_sync_file = vn_renderer_bo_export_sync_file_internal;
   av->base.bo_ops.map = aros_bo_map;
   av->base.bo_ops.flush = aros_bo_flush;
   av->base.bo_ops.invalidate = aros_bo_invalidate;

   av->base.sync_ops.create = aros_sync_create;
   av->base.sync_ops.create_from_syncobj = NULL;
   av->base.sync_ops.destroy = aros_sync_destroy;
   av->base.sync_ops.export_syncobj = NULL;
   av->base.sync_ops.reset = aros_sync_reset;
   av->base.sync_ops.read = aros_sync_read;
   av->base.sync_ops.write = aros_sync_write;

   return VK_SUCCESS;
}

VkResult
vn_renderer_create_aros(struct vn_instance *instance,
                        const VkAllocationCallbacks *alloc,
                        struct vn_renderer **renderer)
{
   struct aros_venus *av = vk_zalloc(alloc, sizeof(*av), VN_DEFAULT_ALIGN,
                                     VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

   if (!av)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   av->instance = instance;

   VkResult result = aros_init(av);
   if (result != VK_SUCCESS) {
      aros_destroy(&av->base, alloc);
      return result;
   }
   *renderer = &av->base;
   return VK_SUCCESS;
}
