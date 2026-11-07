/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1.

    Single-owner Virtio GPU transport. Reuses the maintained PCI/DMA/reset primitives.
    Derived from the project's MIT GPU qualification code by John Knipper.
*/
#define __OOP_NOLIBBASE__
#ifndef __OOP_NOATTRBASES__
#define __OOP_NOATTRBASES__
#endif
#define __OOP_NOMETHODBASES__
#include "transport.h"
#include <exec/lists.h>
#include <devices/timer.h>
#include <aros/asmcall.h>
#if defined(__AROSEXEC_SMP__)
#define __EXECLOCK_NOLIBBASE__
#define __aros_getbase_ExecLockBase() (gpu->irq_lock_base)
#include <proto/execlock.h>
#include <aros/types/spinlock_s.h>
#endif
#include "../../devs/virtio/virtio_hw.c"
#include "../../devs/virtio/virtio_queue.c"
/* One owner stores every published command until completion. Keep response
 * storage disjoint from variable-size VirGL submissions. */
#define GPU_REQUEST_BYTES VIRTIOGPU_MAX_REQUEST_BYTES
#define GPU_RESPONSE_BYTES VIRTIOGPU_MAX_RESPONSE_BYTES
#define GPU_MESSAGE_BYTES (GPU_REQUEST_BYTES + GPU_RESPONSE_BYTES)
#define gpu_barrier() vq_barrier()

/* VirtIO GPU control queue command and response types (virtio-gpu 1.x). */
#define GPU_CMD_GET_DISPLAY_INFO        0x100
#define GPU_CMD_RESOURCE_UNREF          0x102
#define GPU_CMD_RESOURCE_FLUSH          0x104
#define GPU_CMD_TRANSFER_TO_HOST_2D     0x105
#define GPU_CMD_RESOURCE_ATTACH_BACKING 0x106
#define GPU_CMD_RESOURCE_DETACH_BACKING 0x107
#define GPU_CMD_GET_CAPSET_INFO         0x108
#define GPU_CMD_GET_CAPSET              0x109
#define GPU_CMD_CTX_CREATE              0x200
#define GPU_CMD_CTX_DESTROY             0x201
#define GPU_CMD_CTX_ATTACH_RESOURCE     0x202
#define GPU_CMD_CTX_DETACH_RESOURCE     0x203
#define GPU_CMD_RESOURCE_CREATE_3D      0x204
#define GPU_CMD_TRANSFER_TO_HOST_3D     0x205
#define GPU_CMD_TRANSFER_FROM_HOST_3D   0x206
#define GPU_CMD_SUBMIT_3D               0x207
#define GPU_RESP_OK_NODATA              0x1100
#define GPU_RESP_OK_DISPLAY_INFO        0x1101
#define GPU_RESP_OK_CAPSET_INFO         0x1102
#define GPU_RESP_OK_CAPSET              0x1103
#define GPU_RESP_ERR_FIRST              0x1200
#define GPU_RESP_ERR_LAST               0x1205
#define GPU_FLAG_FENCE                  1

/* Wire sizes of the requests the owner validates for clients. */
#define GPU_HEADER_BYTES                24
#define GPU_DISPLAY_INFO_RESPONSE_BYTES 408
#define GPU_CAPSET_INFO_REQUEST_BYTES   32
#define GPU_CAPSET_INFO_RESPONSE_BYTES  40
#define GPU_CAPSET_REQUEST_BYTES        32
#define GPU_CTX_CREATE_BYTES            96
#define GPU_CTX_CREATE_MAX_NAME         64
#define GPU_RESOURCE_CREATE_3D_BYTES    72
#define GPU_RESOURCE_REQUEST_BYTES      32
#define GPU_ATTACH_BACKING_BYTES        48
#define GPU_TRANSFER_3D_BYTES           72
#define GPU_SUBMIT_3D_HEADER_BYTES      32
#define GPU_MAX_CAPSETS                 16
#define GPU_MAX_TEXTURE_TARGET          8
#define GPU_MAX_MIP_LEVELS              32

/* Field offsets inside the copied request, after the 24-byte header. */
#define GPU_FIELD_RESOURCE              24
#define GPU_FIELD_CTX_NAME_LENGTH       24
#define GPU_FIELD_CTX_INIT              28
#define GPU_FIELD_CAPSET_ID             24
#define GPU_FIELD_CAPSET_VERSION        28
#define GPU_FIELD_BACKING_COUNT         28
#define GPU_FIELD_BACKING_ADDRESS       32
#define GPU_FIELD_BACKING_LENGTH        40
#define GPU_FIELD_CREATE_TARGET         28
#define GPU_FIELD_CREATE_WIDTH          40
#define GPU_FIELD_CREATE_HEIGHT         44
#define GPU_FIELD_CREATE_DEPTH          48
#define GPU_FIELD_CREATE_ARRAY_SIZE     52
#define GPU_FIELD_CREATE_LAST_LEVEL     56
#define GPU_FIELD_BOX_START             24
#define GPU_FIELD_BOX_EXTENT            36
#define GPU_FIELD_TRANSFER_OFFSET       48
#define GPU_FIELD_TRANSFER_RESOURCE     56
#define GPU_FIELD_TRANSFER_LEVEL        60
#define GPU_FIELD_TRANSFER_STRIDE       64
#define GPU_FIELD_TRANSFER_LAYER_STRIDE 68
#define GPU_FIELD_SUBMIT_SIZE           24
#define GPU_FIELD_CAPSET_INFO_ID        24
#define GPU_FIELD_CAPSET_INFO_VERSION   28
#define GPU_FIELD_CAPSET_INFO_SIZE      32

/* One service-owned, contiguous PCI allocation. host_refs counts attached
 * backing registrations; dma_active marks an open CachePreDMA interval. */
struct GPUAllocation {
    struct GPUAllocation *next;
    APTR owner, pointer;
    UQUAD bus;
    ULONG bytes, dma_bytes, flags, host_refs;
    BOOL dma_active;
};
/* A context or resource identity handed out by AllocateID. live means the
 * host acknowledged its creation; uncertain means a command that changes
 * its state is published and not yet completed. */
struct GPUObject {
    struct GPUObject *next;
    APTR owner;
    ULONG id;
    BOOL context, live, uncertain;
    struct GPUAllocation *backing;
    ULONG backing_bytes, target, width, height, depth, array_size, last_level;
};
struct VirtioGPU {
    struct GPUAllocation *allocations;
    struct GPUObject *objects,*owned_pending;
    ULONG owned_pending_type;
    struct { ULONG id, version, bytes; BOOL valid; } caps[GPU_MAX_CAPSETS];
    struct VirtIOBase base;
    __typeof__(*(device_t)0) device;
    struct SignalSemaphore lock;
    APTR message,pixels;
    UQUAD message_bus,fence;
    UWORD saved_command;
    BOOL mapped,ready,failed,resource,backing,scanout,interrupt_installed,timer_open;
    struct Interrupt interrupt;
    struct MsgPort timer_port;
    struct timerequest *timer;
    struct Task *waiter;
    ULONG wait_mask;
    ULONG interrupts,irq_users;
#if defined(__AROSEXEC_SMP__)
    APTR irq_lock_base,irq_lock;
#endif
    BOOL irq_reported;
    BOOL pending,repaint;
    UWORD pending_next;
    unsigned pending_bytes;
    uint32_t pending_type;
    ULONG clients,next_context,next_resource,contexts,resources;
};
/* The public Exec lock resource serializes publication with remote IRQs.
 * Never hold this lock across Signal: claiming IPI space may drain callbacks.
 * Each IRQ instead retains a producer reference until Signal has returned. */
static void gpu_wait_lock(struct VirtioGPU *gpu)
{
#if defined(__AROSEXEC_SMP__)
    ObtainLock(gpu->irq_lock,SPINLOCK_MODE_WRITE,LOCKF_DISABLE);
#else
    (void)gpu;Disable();
#endif
}
static void gpu_wait_unlock(struct VirtioGPU *gpu)
{
#if defined(__AROSEXEC_SMP__)
    ReleaseLock(gpu->irq_lock,LOCKF_DISABLE);
#else
    (void)gpu;Enable();
#endif
}
static ULONG gpu_wait_begin(struct VirtioGPU *gpu,ULONG mask)
{
    gpu_wait_lock(gpu);
    gpu->waiter=FindTask(NULL);gpu->wait_mask=mask;
    ULONG before=__atomic_load_n(&gpu->interrupts,__ATOMIC_RELAXED);
    gpu_wait_unlock(gpu);
    return before;
}
static void gpu_wait_end(struct VirtioGPU *gpu)
{
    gpu_wait_lock(gpu);
    gpu->waiter=NULL;gpu->wait_mask=0;
    gpu_wait_unlock(gpu);
    /* Interrupts remain enabled while joining remote producers, so an IPI
     * sender waiting for queue space cannot deadlock with this consumer.
     * Signal publishes bits synchronously; a deferred wakeup carries no bits.
     * Joining therefore also ends every producer's use of this signal bit. */
    while(__atomic_load_n(&gpu->irq_users,__ATOMIC_ACQUIRE)) {}
}
static AROS_INTH1(gpu_irq,struct VirtioGPU *,gpu)
{
    AROS_INTFUNC_INIT
    (void)__ufi_intmask;(void)__ufi_custom;(void)__ufi_code;(void)SysBase;
    if(gpu->device.dev_ISR)(void)*gpu->device.dev_ISR;
    gpu_barrier();
    __atomic_fetch_add(&gpu->interrupts,1,__ATOMIC_RELAXED);
    gpu_wait_lock(gpu);
    struct Task *waiter=gpu->waiter;
    ULONG mask=gpu->wait_mask;
    if(waiter)__atomic_fetch_add(&gpu->irq_users,1,__ATOMIC_RELAXED);
    gpu_wait_unlock(gpu);
    if(waiter) {
        Signal(waiter,mask);
        __atomic_fetch_sub(&gpu->irq_users,1,__ATOMIC_RELEASE);
    }
    return TRUE;
    AROS_INTFUNC_EXIT
}
struct GPUHeader { uint32_t type,flags;uint64_t fence;uint32_t context;uint8_t ring,padding[3]; };
struct GPUDisplay { struct GPUHeader header;struct { uint32_t x,y,width,height,enabled,flags; } mode[16]; };
_Static_assert(sizeof(struct GPUHeader)==24,"GPU header wire size");
_Static_assert(sizeof(struct GPUDisplay)==408,"GPU display response wire size");
static void gpu_remove_object(struct VirtioGPU *gpu, struct GPUObject *object)
{
    struct GPUObject **link = &gpu->objects;
    while (*link && *link != object)
        link = &(*link)->next;
    if (*link) {
        *link = object->next;
        FreeMem(object, sizeof(*object));
    }
}
/* Apply the same ownership transition for an immediate or a late reply.
 * success > 0: the host acknowledged the command; success == 0: the host
 * rejected it; success < 0: the completion was invalid, so the provisional
 * references stay in quarantine until a safe physical stop. */
static void gpu_owned_complete(struct VirtioGPU *gpu, int success)
{
    struct GPUObject *object = gpu->owned_pending;
    ULONG type = gpu->owned_pending_type;
    if (!object || success < 0)
        return;
    gpu->owned_pending = NULL;
    gpu->owned_pending_type = 0;
    object->uncertain = FALSE;
    if (!success) {
        /* A rejected attach never published the backing to the host. */
        if (type == GPU_CMD_RESOURCE_ATTACH_BACKING && object->backing) {
            object->backing->host_refs--;
            object->backing = NULL;
            object->backing_bytes = 0;
        }
        return;
    }
    switch (type) {
    case GPU_CMD_CTX_CREATE:
        object->live = TRUE;
        gpu->contexts++;
        break;
    case GPU_CMD_RESOURCE_CREATE_3D:
        object->live = TRUE;
        gpu->resources++;
        break;
    case GPU_CMD_CTX_DESTROY:
        if (object->live)
            gpu->contexts--;
        gpu_remove_object(gpu, object);
        break;
    case GPU_CMD_RESOURCE_UNREF:
        /* The device releases attached backing together with the resource. */
        if (object->backing)
            object->backing->host_refs--;
        if (object->live)
            gpu->resources--;
        gpu_remove_object(gpu, object);
        break;
    case GPU_CMD_RESOURCE_DETACH_BACKING:
        if (object->backing)
            object->backing->host_refs--;
        object->backing = NULL;
        object->backing_bytes = 0;
        break;
    }
}
/* An interrupt is a wakeup hint, not ownership of a particular descriptor.
 * Observe the device's publication before reading its response. DMA memory on
 * the supported transport is coherent; this barrier orders those reads. */
static int gpu_reap(struct VirtioGPU *gpu)
{
    struct virtio_queue *q=gpu->device.dev_VQ;
    UWORD used=q->q_Used->idx;
    if(used!=gpu->pending_next)return 0;
    gpu_barrier();
    unsigned slot=(UWORD)(gpu->pending_next-1)%8;
    uint32_t id=q->q_Used->ring[slot].id,len=q->q_Used->ring[slot].len;
    volatile struct GPUHeader *response=(void *)((UBYTE *)gpu->message+GPU_REQUEST_BYTES);
    uint32_t type=response->type,flags=response->flags;
    UQUAD fence=response->fence;
    gpu->pending=FALSE;
    if(id || len<sizeof(struct GPUHeader) || len>gpu->pending_bytes ||
       !(flags&GPU_FLAG_FENCE) || fence!=gpu->fence) {
        bug("[VirtioGfx] invalid completion used=%u id=%lu len=%lu/%lu type=%lx/%lx flags=%lx fence=%lu/%lu\n",
            (unsigned)used,(unsigned long)id,(unsigned long)len,(unsigned long)gpu->pending_bytes,
            (unsigned long)type,(unsigned long)gpu->pending_type,(unsigned long)flags,
            (unsigned long)fence,(unsigned long)gpu->fence);
        gpu->failed=TRUE;gpu_owned_complete(gpu,-1);return -1;
    }
    /* A complete fenced protocol rejection is an operation failure, not
     * evidence that the queue or the desktop's scanout has been lost. */
    if(type>=GPU_RESP_ERR_FIRST && type<=GPU_RESP_ERR_LAST) {
        gpu_owned_complete(gpu,0);
        return -2;
    }
    if(len!=gpu->pending_bytes || type!=gpu->pending_type) {
        gpu->failed=TRUE;
        gpu_owned_complete(gpu,-1);
        return -1;
    }
    gpu_owned_complete(gpu,1);
    return 1;
}
static int gpu_command(struct VirtioGPU *gpu,unsigned request_bytes,
                       unsigned response_bytes,uint32_t response_type)
{
    if(gpu->failed || gpu->pending || request_bytes<sizeof(struct GPUHeader) ||
       request_bytes>GPU_REQUEST_BYTES || response_bytes<sizeof(struct GPUHeader) ||
       response_bytes>GPU_RESPONSE_BYTES || gpu->fence==~(UQUAD)0)return 0;
    BYTE bit=AllocSignal(-1);
    if(bit<0) { bug("[VirtioGfx] no signal available; present deferred\n");return 0; }
    ULONG mask=1UL<<bit;
    SetSignal(0,mask);
    gpu->timer_port.mp_SigTask=FindTask(NULL);
    gpu->timer_port.mp_SigBit=bit;gpu->timer_port.mp_Flags=PA_SIGNAL;
    ULONG before=gpu_wait_begin(gpu,mask);
    device_t dev=&gpu->device;void *memory=gpu->message;UQUAD bus=gpu->message_bus;
    struct virtio_queue *q=dev->dev_VQ;
    struct GPUHeader *request=memory,*response=(void *)((UBYTE *)memory+GPU_REQUEST_BYTES);
    UWORD next=q->q_Avail->idx+1;
    request->flags=1;request->fence=++gpu->fence;
    gpu->pending=TRUE;gpu->pending_next=next;
    gpu->pending_bytes=response_bytes;gpu->pending_type=response_type;
    memset(response,0,response_bytes);
    q->q_Desc[0].addr=bus;q->q_Desc[0].len=request_bytes;
    q->q_Desc[0].flags=VIRTQ_DESC_F_NEXT;q->q_Desc[0].next=1;
    q->q_Desc[1].addr=bus+GPU_REQUEST_BYTES;q->q_Desc[1].len=response_bytes;
    q->q_Desc[1].flags=VIRTQ_DESC_F_WRITE;q->q_Desc[1].next=0;
    q->q_Avail->ring[(UWORD)(next-1)%8]=0;gpu_barrier();
    q->q_Avail->idx=next;gpu_barrier();
    *q->q_Notify=0;gpu_barrier();
    /* Start the deadline after submission: a preempted producer must not
     * consume the entire device timeout before it has notified the device. */
    gpu->timer->tr_node.io_Command=TR_ADDREQUEST;
    gpu->timer->tr_time.tv_secs=1;gpu->timer->tr_time.tv_micro=0;
    SendIO((struct IORequest *)gpu->timer);
    while(!CheckIO((struct IORequest *)gpu->timer) &&
          q->q_Used->idx!=next)
        Wait(mask);
    gpu_wait_end(gpu);
    if(!CheckIO((struct IORequest *)gpu->timer))AbortIO((struct IORequest *)gpu->timer);
    WaitIO((struct IORequest *)gpu->timer);
    gpu->timer_port.mp_Flags=PA_IGNORE;
    SetSignal(0,mask);FreeSignal(bit);
    int result=gpu_reap(gpu);
    if(!result) {
        /* Keep the one in-flight chain and all its DMA storage. A later
         * present may reap it, but must not overwrite it before completion. */
        bug("[VirtioGfx] completion delayed; retaining command fence=%lu\n",(unsigned long)gpu->fence);
    }
    else if(result>0 && !gpu->irq_reported &&
            __atomic_load_n(&gpu->interrupts,__ATOMIC_RELAXED)!=before) {
        gpu->irq_reported=TRUE;
        bug("[VM] virtio graphics IRQ completion PASS\n");
    }
    return result>0;
}

static uint32_t *gpu_request(struct VirtioGPU *gpu,uint32_t type)
{
    memset(gpu->message,0,64);
    ((struct GPUHeader *)gpu->message)->type=type;
    return (void *)((UBYTE *)gpu->message+24);
}
static int gpu_present(struct VirtioGPU *gpu,ULONG x,ULONG y,ULONG width,ULONG height)
{
    if(!width || !height || x>=1024 || y>=768 || width>1024-x || height>768-y)return 0;
    uint32_t *body=gpu_request(gpu,0x105);
    body[0]=x;body[1]=y;body[2]=width;body[3]=height;
    *(uint64_t *)((UBYTE *)gpu->message+40)=((uint64_t)y*1024+x)*4;
    body[6]=1;
    if(!gpu_command(gpu,56,24,0x1100))return 0;
    body=gpu_request(gpu,0x104);
    body[0]=x;body[1]=y;body[2]=width;body[3]=height;body[4]=1;
    return gpu_command(gpu,48,24,0x1100);
}
BOOL VirtioGPUPresent(struct VirtioGPU *gpu,ULONG x,ULONG y,ULONG width,ULONG height)
{
    if(!gpu)return FALSE;
    ObtainSemaphore(&gpu->lock);
    if(gpu->pending) {
        int result=gpu_reap(gpu);
        if(!result){gpu->repaint=TRUE;ReleaseSemaphore(&gpu->lock);return FALSE;}
        if(result>0) {
            gpu->repaint=TRUE;
            bug("[VirtioGfx] late completion recovered; repainting screen\n");
        }
    }
    /* Keep damage across every deferred presentation, including a transient
     * signal shortage after reaping. Clear it only after transfer AND flush. */
    if(gpu->repaint){x=y=0;width=1024;height=768;}
    BOOL okay=gpu->ready && !gpu->failed && gpu_present(gpu,x,y,width,height);
    gpu->repaint=!okay;
    ReleaseSemaphore(&gpu->lock);return okay;
}
BOOL VirtioGPUGetSurface(struct VirtioGPU *gpu,struct VirtioGPUSurface *surface)
{
    if(!gpu || !surface || !gpu->ready || gpu->failed)return FALSE;
    *surface=(struct VirtioGPUSurface){gpu->pixels,1024,768,4096};return TRUE;
}
BOOL VirtioGPUAcquire(struct VirtioGPU *gpu)
{
    BOOL okay = FALSE;
    if (!gpu)
        return FALSE;
    ObtainSemaphore(&gpu->lock);
    if (gpu->ready && !gpu->failed && !gpu->pending && gpu->clients != ~(ULONG)0) {
        gpu->clients++;
        okay = TRUE;
    }
    ReleaseSemaphore(&gpu->lock);
    return okay;
}
BOOL VirtioGPUHasVirGL(struct VirtioGPU *gpu)
{
    BOOL okay = FALSE;
    if (!gpu)
        return FALSE;
    ObtainSemaphore(&gpu->lock);
    okay = gpu->ready && !gpu->failed && !gpu->pending && (gpu->device.dev_Features & 1);
    ReleaseSemaphore(&gpu->lock);
    return okay;
}
static struct GPUAllocation *gpu_allocation(struct VirtioGPU *gpu, APTR owner, APTR pointer)
{
    struct GPUAllocation *a;
    for (a = gpu->allocations; a; a = a->next)
        if (a->owner == owner && a->pointer == pointer)
            return a;
    return NULL;
}
APTR VirtioGPUAlloc(struct VirtioGPU *gpu, APTR owner, ULONG bytes)
{
    struct GPUAllocation *a = NULL;
    APTR result = NULL;
    if (!gpu || !owner || !bytes || bytes > VIRTIOGPU_MAX_BACKING_BYTES)
        return NULL;
    struct VirtIOBase *VirtIOBase = &gpu->base;
    ObtainSemaphore(&gpu->lock);
    if (!gpu->ready || gpu->failed || gpu->pending)
        goto done;
    a = AllocMem(sizeof(*a), MEMF_PUBLIC | MEMF_CLEAR);
    if (!a)
        goto done;
    a->pointer = HIDD_PCIDriver_AllocPCIMem(gpu->device.dev_PCIDriverObject, bytes);
    if (!a->pointer || !vq_address(&gpu->device, a->pointer, bytes, &a->bus)) {
        if (a->pointer)
            HIDD_PCIDriver_FreePCIMem(gpu->device.dev_PCIDriverObject, a->pointer);
        FreeMem(a, sizeof(*a));
        goto done;
    }
    memset(a->pointer, 0, bytes);
    a->owner = owner;
    a->bytes = bytes;
    a->next = gpu->allocations;
    gpu->allocations = a;
    result = a->pointer;
done:
    ReleaseSemaphore(&gpu->lock);
    return result;
}
/* Free is refused while the device may still read or write the memory:
 * an open DMA interval, an attached backing, a pending command or a failed
 * owner all keep the allocation registered. */
BOOL VirtioGPUFree(struct VirtioGPU *gpu, APTR owner, APTR pointer)
{
    BOOL okay = FALSE;
    if (!gpu || !owner || !pointer)
        return FALSE;
    struct VirtIOBase *VirtIOBase = &gpu->base;
    ObtainSemaphore(&gpu->lock);
    struct GPUAllocation **link = &gpu->allocations;
    while (*link && ((*link)->owner != owner || (*link)->pointer != pointer))
        link = &(*link)->next;
    if (*link && !gpu->failed && !gpu->pending && !(*link)->dma_active && !(*link)->host_refs) {
        struct GPUAllocation *a = *link;
        *link = a->next;
        HIDD_PCIDriver_FreePCIMem(gpu->device.dev_PCIDriverObject, a->pointer);
        FreeMem(a, sizeof(*a));
        okay = TRUE;
    }
    ReleaseSemaphore(&gpu->lock);
    return okay;
}
BOOL VirtioGPUAddress(struct VirtioGPU *gpu, APTR owner, APTR pointer, UQUAD *address)
{
    BOOL okay = FALSE;
    if (!gpu || !owner || !pointer || !address)
        return FALSE;
    ObtainSemaphore(&gpu->lock);
    struct GPUAllocation *a = gpu_allocation(gpu, owner, pointer);
    if (a && !gpu->failed) {
        *address = a->bus;
        okay = TRUE;
    }
    ReleaseSemaphore(&gpu->lock);
    return okay;
}
BOOL VirtioGPUBackingBegin(struct VirtioGPU *gpu, APTR owner, APTR pointer, ULONG flags)
{
    BOOL okay = FALSE;
    if (!gpu || !owner || !pointer || (flags & ~DMA_ReadFromRAM))
        return FALSE;
    ObtainSemaphore(&gpu->lock);
    struct GPUAllocation *a = gpu_allocation(gpu, owner, pointer);
    if (a && !gpu->failed && !gpu->pending && !a->dma_active) {
        a->dma_bytes = a->bytes;
        a->flags = flags;
        if (CachePreDMA(a->pointer, &a->dma_bytes, flags)) {
            a->dma_active = TRUE;
            /* The whole allocation must map as one contiguous DMA range. */
            if (a->dma_bytes >= a->bytes) {
                okay = TRUE;
            } else {
                CachePostDMA(a->pointer, &a->dma_bytes, flags);
                a->dma_active = FALSE;
            }
        }
    }
    ReleaseSemaphore(&gpu->lock);
    return okay;
}
void VirtioGPUBackingEnd(struct VirtioGPU *gpu, APTR owner, APTR pointer)
{
    if (!gpu || !owner || !pointer)
        return;
    ObtainSemaphore(&gpu->lock);
    struct GPUAllocation *a = gpu_allocation(gpu, owner, pointer);
    /* Uncertain completion retains both registration and backing. */
    if (a && a->dma_active && !gpu->failed && !gpu->pending) {
        CachePostDMA(a->pointer, &a->dma_bytes, a->flags);
        a->dma_active = FALSE;
    }
    ReleaseSemaphore(&gpu->lock);
}
void VirtioGPURelease(struct VirtioGPU *gpu)
{
    if (!gpu)
        return;
    ObtainSemaphore(&gpu->lock);
    if (gpu->clients)
        gpu->clients--;
    /* When the last client leaves, report what the owner still holds. With
     * every client gone this must be the scanout alone; anything else is
     * quarantined state that only a physical stop will release. */
    if (!gpu->clients) {
        ULONG allocations = 0, bytes = 0;
        for (struct GPUAllocation *a = gpu->allocations; a; a = a->next) {
            allocations++;
            bytes += a->bytes;
        }
        bug("[VirtioGfx] last client released: scanout=%lu contexts=%lu resources3d=%lu allocations=%lu bytes=%lu pending=%lu failed=%lu\n",
            (unsigned long)gpu->scanout, (unsigned long)gpu->contexts,
            (unsigned long)gpu->resources, (unsigned long)allocations,
            (unsigned long)bytes, (unsigned long)gpu->pending,
            (unsigned long)gpu->failed);
    }
    ReleaseSemaphore(&gpu->lock);
}
static struct GPUObject *gpu_object(struct VirtioGPU *gpu, APTR owner, ULONG id, BOOL context)
{
    for (struct GPUObject *o = gpu->objects; o; o = o->next)
        if (o->owner == owner && o->id == id && o->context == context)
            return o;
    return NULL;
}
/* Context IDs start at 1. Resource IDs start at 2: resource 1 is the
 * scanout and resource 0 is reserved by the protocol. */
ULONG VirtioGPUAllocateID(struct VirtioGPU *gpu, APTR owner, BOOL context)
{
    ULONG result = 0;
    if (!gpu || !owner)
        return 0;
    context = !!context;
    ObtainSemaphore(&gpu->lock);
    ULONG *next = context ? &gpu->next_context : &gpu->next_resource;
    if (gpu->ready && !gpu->failed && !gpu->pending && *next != ~(ULONG)0) {
        struct GPUObject *o = AllocMem(sizeof(*o), MEMF_PUBLIC | MEMF_CLEAR);
        if (o) {
            result = ++*next;
            o->id = result;
            o->owner = owner;
            o->context = context;
            o->next = gpu->objects;
            gpu->objects = o;
        }
    }
    ReleaseSemaphore(&gpu->lock);
    return result;
}
/* Object addresses may be reused by OOP. Orphans lose the old identity and
 * retain any device-visible state until physical stop; peers cannot adopt it. */
void VirtioGPUForgetOwner(struct VirtioGPU *gpu, APTR owner)
{
    if (!gpu || !owner)
        return;
    struct VirtIOBase *VirtIOBase = &gpu->base;
    ObtainSemaphore(&gpu->lock);
    struct GPUObject *o = gpu->objects;
    while (o) {
        struct GPUObject *next = o->next;
        if (o->owner == owner) {
            if (o->live || o->uncertain)
                o->owner = NULL;
            else
                gpu_remove_object(gpu, o);
        }
        o = next;
    }
    struct GPUAllocation **link = &gpu->allocations;
    while (*link) {
        struct GPUAllocation *a = *link;
        if (a->owner != owner) {
            link = &a->next;
            continue;
        }
        if (!a->host_refs && !a->dma_active && !gpu->pending && !gpu->failed) {
            *link = a->next;
            HIDD_PCIDriver_FreePCIMem(gpu->device.dev_PCIDriverObject, a->pointer);
            FreeMem(a, sizeof(*a));
        } else {
            a->owner = NULL;
            link = &a->next;
        }
    }
    ReleaseSemaphore(&gpu->lock);
}
static ULONG gpu_u32(const void *message, ULONG offset)
{
    ULONG value;
    CopyMem((const UBYTE *)message + offset, &value, sizeof(value));
    return value;
}
static UQUAD gpu_u64(const void *message, ULONG offset)
{
    UQUAD value;
    CopyMem((const UBYTE *)message + offset, &value, sizeof(value));
    return value;
}
/* Check a 3D transfer box against the resource geometry the owner recorded
 * at creation and against the attached backing. Returns FALSE on any
 * out-of-range coordinate, level or offset. */
static BOOL gpu_transfer_in_bounds(const UBYTE *m, const struct GPUObject *object)
{
    ULONG level = gpu_u32(m, GPU_FIELD_TRANSFER_LEVEL);
    if (level > object->last_level)
        return FALSE;
    ULONG dims[3] = { object->width >> level, object->height >> level, object->depth >> level };
    for (unsigned i = 0; i < 3; i++)
        if (!dims[i])
            dims[i] = 1;
    if (dims[2] < object->array_size)
        dims[2] = object->array_size;
    for (unsigned i = 0; i < 3; i++) {
        ULONG start = gpu_u32(m, GPU_FIELD_BOX_START + i * 4);
        ULONG extent = gpu_u32(m, GPU_FIELD_BOX_EXTENT + i * 4);
        if (!extent || start >= dims[i] || extent > dims[i] - start)
            return FALSE;
    }
    UQUAD offset = gpu_u64(m, GPU_FIELD_TRANSFER_OFFSET);
    if (offset >= object->backing_bytes)
        return FALSE;
    ULONG height = gpu_u32(m, GPU_FIELD_BOX_EXTENT + 4);
    ULONG depth = gpu_u32(m, GPU_FIELD_BOX_EXTENT + 8);
    ULONG remaining = object->backing_bytes - offset;
    if (height > 1 && gpu_u32(m, GPU_FIELD_TRANSFER_STRIDE) > remaining)
        return FALSE;
    if (depth > 1 && gpu_u32(m, GPU_FIELD_TRANSFER_LAYER_STRIDE) > remaining)
        return FALSE;
    /* A buffer (target 0) transfers width bytes from offset. */
    if (!object->target && gpu_u32(m, GPU_FIELD_BOX_EXTENT) > remaining)
        return FALSE;
    return TRUE;
}
/* The service validates the VirtIO envelope and owned DMA extents. VirGL
 * retains format/block-layout and command-stream interpretation on the host.
 * The request is copied once before inspection, so caller mutation cannot
 * change an authorized request before it is published. */
BOOL VirtioGPUCommand(struct VirtioGPU *gpu, APTR owner, const void *request, ULONG bytes,
                      void *response, ULONG response_bytes, ULONG response_type)
{
    BOOL okay = FALSE;
    ULONG type = 0, expected = GPU_RESP_OK_NODATA, info_index = 0;
    struct GPUObject *object = NULL, *context = NULL;
    struct GPUAllocation *backing = NULL;
    if (!gpu || !owner || !request || !response || bytes < sizeof(struct GPUHeader) ||
        bytes > GPU_REQUEST_BYTES || response_bytes < sizeof(struct GPUHeader) ||
        response_bytes > GPU_RESPONSE_BYTES)
        return FALSE;
    ObtainSemaphore(&gpu->lock);
    if (!gpu->ready || gpu->failed)
        goto done;
    if (gpu->pending && gpu_reap(gpu) <= 0)
        goto done;
    CopyMem(request, gpu->message, bytes);
    const UBYTE *m = gpu->message;
    const struct GPUHeader *header = (const void *)m;
    type = header->type;
    if (header->flags || header->ring)
        goto done;
    switch (type) {
    case GPU_CMD_GET_DISPLAY_INFO:
        if (bytes != GPU_HEADER_BYTES || header->context ||
            response_bytes != GPU_DISPLAY_INFO_RESPONSE_BYTES)
            goto done;
        expected = GPU_RESP_OK_DISPLAY_INFO;
        break;
    case GPU_CMD_GET_CAPSET_INFO:
        if (bytes != GPU_CAPSET_INFO_REQUEST_BYTES || header->context ||
            response_bytes != GPU_CAPSET_INFO_RESPONSE_BYTES || gpu_u32(m, 28))
            goto done;
        info_index = gpu_u32(m, 24);
        if (info_index >= GPU_MAX_CAPSETS)
            goto done;
        expected = GPU_RESP_OK_CAPSET_INFO;
        break;
    case GPU_CMD_GET_CAPSET: {
        /* Only a capset whose size this owner learned through
         * GET_CAPSET_INFO may be fetched, with exactly that response size. */
        if (bytes != GPU_CAPSET_REQUEST_BYTES || header->context)
            goto done;
        BOOL found = FALSE;
        for (unsigned i = 0; i < GPU_MAX_CAPSETS; i++)
            if (gpu->caps[i].valid &&
                gpu->caps[i].id == gpu_u32(m, GPU_FIELD_CAPSET_ID) &&
                gpu->caps[i].version == gpu_u32(m, GPU_FIELD_CAPSET_VERSION) &&
                response_bytes == GPU_HEADER_BYTES + gpu->caps[i].bytes)
                found = TRUE;
        if (!found)
            goto done;
        expected = GPU_RESP_OK_CAPSET;
        break;
    }
    case GPU_CMD_CTX_CREATE:
    case GPU_CMD_CTX_DESTROY:
        object = gpu_object(gpu, owner, header->context, TRUE);
        if (!object || object->uncertain)
            goto done;
        if (type == GPU_CMD_CTX_CREATE) {
            if (bytes != GPU_CTX_CREATE_BYTES || object->live ||
                gpu_u32(m, GPU_FIELD_CTX_NAME_LENGTH) > GPU_CTX_CREATE_MAX_NAME ||
                gpu_u32(m, GPU_FIELD_CTX_INIT))
                goto done;
        } else if (bytes != GPU_HEADER_BYTES || !object->live) {
            goto done;
        }
        break;
    case GPU_CMD_RESOURCE_CREATE_3D:
        if (bytes != GPU_RESOURCE_CREATE_3D_BYTES || header->context)
            goto done;
        object = gpu_object(gpu, owner, gpu_u32(m, GPU_FIELD_RESOURCE), FALSE);
        if (!object || object->live || object->uncertain ||
            gpu_u32(m, GPU_FIELD_CREATE_TARGET) > GPU_MAX_TEXTURE_TARGET ||
            !gpu_u32(m, GPU_FIELD_CREATE_WIDTH) || !gpu_u32(m, GPU_FIELD_CREATE_HEIGHT) ||
            !gpu_u32(m, GPU_FIELD_CREATE_DEPTH) || !gpu_u32(m, GPU_FIELD_CREATE_ARRAY_SIZE) ||
            gpu_u32(m, GPU_FIELD_CREATE_LAST_LEVEL) >= GPU_MAX_MIP_LEVELS)
            goto done;
        break;
    case GPU_CMD_RESOURCE_UNREF:
    case GPU_CMD_RESOURCE_ATTACH_BACKING:
    case GPU_CMD_RESOURCE_DETACH_BACKING:
    case GPU_CMD_CTX_ATTACH_RESOURCE:
    case GPU_CMD_CTX_DETACH_RESOURCE:
        if (bytes < GPU_RESOURCE_REQUEST_BYTES)
            goto done;
        object = gpu_object(gpu, owner, gpu_u32(m, GPU_FIELD_RESOURCE), FALSE);
        if (!object || !object->live || object->uncertain)
            goto done;
        if (type == GPU_CMD_CTX_ATTACH_RESOURCE || type == GPU_CMD_CTX_DETACH_RESOURCE) {
            context = gpu_object(gpu, owner, header->context, TRUE);
            if (bytes != GPU_RESOURCE_REQUEST_BYTES || !context || !context->live || context->uncertain)
                goto done;
        } else if (header->context) {
            goto done;
        }
        if (type == GPU_CMD_RESOURCE_ATTACH_BACKING) {
            /* This client currently uses one whole PCI allocation per
             * resource. Arbitrary/foreign bus addresses are never sent. */
            if (bytes != GPU_ATTACH_BACKING_BYTES ||
                gpu_u32(m, GPU_FIELD_BACKING_COUNT) != 1 || object->backing)
                goto done;
            for (backing = gpu->allocations; backing; backing = backing->next)
                if (backing->owner == owner &&
                    backing->bus == gpu_u64(m, GPU_FIELD_BACKING_ADDRESS) &&
                    backing->bytes == gpu_u32(m, GPU_FIELD_BACKING_LENGTH))
                    break;
            if (!backing || backing->host_refs == ~(ULONG)0)
                goto done;
        } else if (bytes != GPU_RESOURCE_REQUEST_BYTES) {
            goto done;
        }
        if (type == GPU_CMD_RESOURCE_DETACH_BACKING && !object->backing)
            goto done;
        break;
    case GPU_CMD_TRANSFER_TO_HOST_3D:
    case GPU_CMD_TRANSFER_FROM_HOST_3D:
        if (bytes != GPU_TRANSFER_3D_BYTES)
            goto done;
        context = gpu_object(gpu, owner, header->context, TRUE);
        object = gpu_object(gpu, owner, gpu_u32(m, GPU_FIELD_TRANSFER_RESOURCE), FALSE);
        if (!context || !context->live || context->uncertain || !object ||
            !object->live || object->uncertain || !object->backing ||
            !object->backing->dma_active)
            goto done;
        if (!gpu_transfer_in_bounds(m, object))
            goto done;
        break;
    case GPU_CMD_SUBMIT_3D: {
        ULONG size = gpu_u32(m, GPU_FIELD_SUBMIT_SIZE);
        if (bytes < GPU_SUBMIT_3D_HEADER_BYTES || (size & 3) ||
            size != bytes - GPU_SUBMIT_3D_HEADER_BYTES)
            goto done;
        context = gpu_object(gpu, owner, header->context, TRUE);
        if (!context || !context->live || context->uncertain)
            goto done;
        break;
    }
    default:
        goto done;
    }
    if (response_type != expected ||
        (expected == GPU_RESP_OK_NODATA && response_bytes != GPU_HEADER_BYTES))
        goto done;
    /* Commands that change owned state publish provisionally; the matching
     * completion, immediate or late, applies the transition in gpu_reap. */
    if (type == GPU_CMD_CTX_CREATE || type == GPU_CMD_CTX_DESTROY ||
        type == GPU_CMD_RESOURCE_CREATE_3D || type == GPU_CMD_RESOURCE_UNREF ||
        type == GPU_CMD_RESOURCE_ATTACH_BACKING || type == GPU_CMD_RESOURCE_DETACH_BACKING) {
        gpu->owned_pending = object;
        gpu->owned_pending_type = type;
        object->uncertain = TRUE;
        if (type == GPU_CMD_RESOURCE_CREATE_3D) {
            object->target = gpu_u32(m, GPU_FIELD_CREATE_TARGET);
            object->width = gpu_u32(m, GPU_FIELD_CREATE_WIDTH);
            object->height = gpu_u32(m, GPU_FIELD_CREATE_HEIGHT);
            object->depth = gpu_u32(m, GPU_FIELD_CREATE_DEPTH);
            object->array_size = gpu_u32(m, GPU_FIELD_CREATE_ARRAY_SIZE);
            object->last_level = gpu_u32(m, GPU_FIELD_CREATE_LAST_LEVEL);
        }
        if (type == GPU_CMD_RESOURCE_ATTACH_BACKING) {
            object->backing = backing;
            object->backing_bytes = backing->bytes;
            backing->host_refs++;
        }
    }
    okay = gpu_command(gpu, bytes, response_bytes, response_type);
    /* Allocation of the signal/timer may fail before publication. */
    if (!okay && !gpu->pending && !gpu->failed && gpu->owned_pending)
        gpu_owned_complete(gpu, 0);
    if (okay) {
        const UBYTE *reply = (UBYTE *)gpu->message + GPU_REQUEST_BYTES;
        if (type == GPU_CMD_GET_CAPSET_INFO) {
            ULONG size = gpu_u32(reply, GPU_FIELD_CAPSET_INFO_SIZE);
            gpu->caps[info_index].valid = size && size <= GPU_RESPONSE_BYTES - GPU_HEADER_BYTES;
            gpu->caps[info_index].id = gpu_u32(reply, GPU_FIELD_CAPSET_INFO_ID);
            gpu->caps[info_index].version = gpu_u32(reply, GPU_FIELD_CAPSET_INFO_VERSION);
            gpu->caps[info_index].bytes = size;
        }
        CopyMem(reply, response, response_bytes);
    }
done:
    ReleaseSemaphore(&gpu->lock);
    return okay;
}
ULONG VirtioGPUClose(struct VirtioGPU *gpu)
{
    if(!gpu)return 1;
    /* Physical close is permitted only after the display and its clients
     * have stopped using this owner. A client release never invokes it. */
    if(gpu->clients)return 0;
    struct VirtIOBase *VirtIOBase=&gpu->base;device_t dev=&gpu->device;
    if(gpu->mapped) {
        uint32_t *body;
        if(!gpu->failed && !gpu->pending && gpu->scanout) { gpu_request(gpu,0x103);gpu_command(gpu,48,24,0x1100); }
        if(!gpu->failed && !gpu->pending && gpu->backing) { body=gpu_request(gpu,0x107);body[0]=1;gpu_command(gpu,32,24,0x1100); }
        if(!gpu->failed && !gpu->pending && gpu->resource) { body=gpu_request(gpu,0x102);body[0]=1;gpu_command(gpu,32,24,0x1100); }
        if(!virtio_hw_stop(dev)) { gpu->failed=TRUE;gpu->ready=FALSE;return 0; }
        /* The device is stopped: quarantined client state is released now. */
        while(gpu->allocations) {
            struct GPUAllocation *a=gpu->allocations;
            gpu->allocations=a->next;
            if(a->dma_active)CachePostDMA(a->pointer,&a->dma_bytes,a->flags);
            HIDD_PCIDriver_FreePCIMem(dev->dev_PCIDriverObject,a->pointer);
            FreeMem(a,sizeof(*a));
        }
        while(gpu->objects)gpu_remove_object(gpu,gpu->objects);
        if(gpu->interrupt_installed) {
            HIDD_PCIDriver_RemoveInterrupt(dev->dev_PCIDriverObject,dev->dev_Object,&gpu->interrupt);
            gpu->interrupt_installed=FALSE;
        }
        if(gpu->pixels)HIDD_PCIDriver_FreePCIMem(dev->dev_PCIDriverObject,gpu->pixels);
        if(gpu->message)HIDD_PCIDriver_FreePCIMem(dev->dev_PCIDriverObject,gpu->message);
        if(dev->dev_VQ)virtio_free_queue(dev->dev_VQ);
        virtio_hw_unmap(dev);HIDD_PCIDevice_WriteConfigWord(dev->dev_Object,4,gpu->saved_command);
    }
    if(gpu->timer_open)CloseDevice((struct IORequest *)gpu->timer);
    if(gpu->timer)DeleteIORequest((struct IORequest *)gpu->timer);
#if defined(__AROSEXEC_SMP__)
    if(gpu->irq_lock)FreeLock(gpu->irq_lock);
#endif
    if(gpu->base.virtio_HiddPCIDeviceAttrBase)OOP_ReleaseAttrBase((CONST_STRPTR)IID_Hidd_PCIDevice);
    ULONG result=gpu->failed?2:1;
    FreeMem(gpu,sizeof(*gpu));return result;
}
BOOL VirtioGPUOpen(struct Library *oop,OOP_Object *object,struct VirtioGPU **out)
{
    if(!out)return FALSE;*out=NULL;
    if(!oop || !object)return FALSE;
    struct VirtioGPU *gpu=AllocMem(sizeof(*gpu),MEMF_CLEAR|MEMF_PUBLIC);
    if(!gpu)return FALSE;
    *out=gpu;InitSemaphore(&gpu->lock);gpu->next_resource=1;
    struct VirtIOBase *VirtIOBase=&gpu->base;device_t dev=&gpu->device;
    const char *stage=NULL;
#if defined(__AROSEXEC_SMP__)
    stage="IRQ lock";
    gpu->irq_lock_base=OpenResource("execlock.resource");
    if(!gpu->irq_lock_base || !(gpu->irq_lock=AllocLock()))goto fail;
#endif
    gpu->base.virtio_OOPBase=oop;
    gpu->base.virtio_HiddPCIDeviceAttrBase=OOP_ObtainAttrBase((CONST_STRPTR)IID_Hidd_PCIDevice);
    if(!gpu->base.virtio_HiddPCIDeviceAttrBase)goto fail;
    gpu->base.virtio_HiddPCIDeviceMethodBase=OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCIDevice,0);
    gpu->base.virtio_HiddPCIDriverMethodBase=OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCIDriver,0);
    dev->dev_VirtIOBase=VirtIOBase;dev->dev_Object=object;
    IPTR vendor=0,product=0;
    OOP_GetAttr(object,aHidd_PCIDevice_VendorID,&vendor);
    OOP_GetAttr(object,aHidd_PCIDevice_ProductID,&product);
    if(vendor!=0x1af4 || product!=0x1050)goto fail;
    OOP_GetAttr(object,aHidd_PCIDevice_Driver,(IPTR *)&dev->dev_PCIDriverObject);
    stage="map";
    if(!virtio_hw_map(dev))goto fail;
    gpu->mapped=TRUE;gpu->saved_command=HIDD_PCIDevice_ReadConfigWord(object,4);
    HIDD_PCIDevice_WriteConfigWord(object,4,gpu->saved_command|6);
    stage="reset/negotiate";
    /* VIRGL is optional: a 2D-only device must still provide the desktop.
     * virtio_hw_negotiate_features intersects wanted with host features. */
    if(!virtio_hw_reset(dev) || !virtio_hw_negotiate_features(dev,
       ((UQUAD)1<<VIRTIO_F_VERSION_1)|1))goto fail;
    stage="queue";
    dev->dev_VQ=virtio_alloc_queue(dev,0,8);
    if(!dev->dev_VQ || !virtio_hw_setup_queue(dev,0,dev->dev_VQ))goto fail;
    stage="timer";
    NEWLIST(&gpu->timer_port.mp_MsgList);
    gpu->timer_port.mp_Node.ln_Type=NT_MSGPORT;gpu->timer_port.mp_Flags=PA_IGNORE;
    gpu->timer=CreateIORequest(&gpu->timer_port,sizeof(*gpu->timer));
    if(!gpu->timer || OpenDevice("timer.device",UNIT_MICROHZ,(struct IORequest *)gpu->timer,0))goto fail;
    gpu->timer_open=TRUE;
    stage="interrupt";
    gpu->interrupt.is_Node.ln_Type=NT_INTERRUPT;
    gpu->interrupt.is_Node.ln_Name=(STRPTR)"virtio graphics";
    gpu->interrupt.is_Code=(VOID_FUNC)gpu_irq;gpu->interrupt.is_Data=gpu;
    if(!HIDD_PCIDriver_AddInterrupt(dev->dev_PCIDriverObject,object,&gpu->interrupt))goto fail;
    gpu->interrupt_installed=TRUE;
    if(!virtio_hw_route_irq(dev))goto fail;
    dev->dev_VQ->q_Avail->flags=0;
    stage="message";
    gpu->message=HIDD_PCIDriver_AllocPCIMem(dev->dev_PCIDriverObject,GPU_MESSAGE_BYTES);
    if(!gpu->message || !vq_address(dev,gpu->message,GPU_MESSAGE_BYTES,&gpu->message_bus))goto fail;
    memset(gpu->message,0,GPU_MESSAGE_BYTES);
    if(!virtio_hw_driver_ok(dev))goto fail;
    stage="display query";
    gpu_request(gpu,0x100);
    if(!gpu_command(gpu,24,408,0x1101))goto fail;
    struct GPUDisplay *display=(void *)((UBYTE *)gpu->message+GPU_REQUEST_BYTES);
    D(bug("[VirtioGfx] host scanout hint %lux%lu enabled=%lu; fixed surface 1024x768\n",
        (unsigned long)display->mode[0].width,(unsigned long)display->mode[0].height,
        (unsigned long)display->mode[0].enabled));
    /* GET_DISPLAY_INFO reports the host window's current size, which Cocoa
     * can update from the legacy VGA console before this driver starts.
     * It is a mode hint, not a requirement on RESOURCE_CREATE_2D dimensions.
     * Keep the existing fixed boot mode and negotiate it with SET_SCANOUT. */
    if(display->mode[0].enabled!=1 || !display->mode[0].width || !display->mode[0].height)goto fail;
    stage="pixels";
    gpu->pixels=HIDD_PCIDriver_AllocPCIMem(dev->dev_PCIDriverObject,1024*768*4);
    UQUAD pixel_bus;
    if(!gpu->pixels || !vq_address(dev,gpu->pixels,1024*768*4,&pixel_bus))goto fail;
    memset(gpu->pixels,0,1024*768*4);
    stage="create resource";
    uint32_t *body=gpu_request(gpu,0x101);
    body[0]=1;body[1]=2;body[2]=1024;body[3]=768;
    if(!gpu_command(gpu,40,24,0x1100))goto fail;
    gpu->resource=TRUE;
    stage="attach backing";
    body=gpu_request(gpu,0x106);body[0]=1;body[1]=1;
    *(uint64_t *)((UBYTE *)gpu->message+32)=pixel_bus;body[4]=1024*768*4;
    if(!gpu_command(gpu,48,24,0x1100))goto fail;
    gpu->backing=TRUE;
    stage="scanout";
    body=gpu_request(gpu,0x103);body[2]=1024;body[3]=768;body[5]=1;
    if(!gpu_command(gpu,48,24,0x1100))goto fail;
    gpu->scanout=TRUE;gpu->ready=TRUE;
    return TRUE;
fail:
    if(stage)bug("[VirtioGfx] open failed at %s\n",stage);
    gpu->failed=TRUE;
    if(VirtioGPUClose(gpu))*out=NULL;
    return FALSE;
}
