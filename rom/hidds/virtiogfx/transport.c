/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1.

    Virtio GPU 2D transport. Reuses the maintained PCI/DMA/reset primitives.
    Derived from the project's MIT GPU qualification code by John Knipper.
*/
#define __OOP_NOLIBBASE__
#ifndef __OOP_NOATTRBASES__
#define __OOP_NOATTRBASES__
#endif
#define __OOP_NOMETHODBASES__
#include "transport.h"
#include <devices/timer.h>
#include <aros/asmcall.h>
#include "../../devs/virtio/virtio_hw.c"
#include "../../devs/virtio/virtio_queue.c"
struct VirtioGPU {
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
    volatile ULONG interrupts;
    BOOL irq_reported;
    BOOL pending,repaint;
    UWORD pending_next;
    unsigned pending_bytes;
    uint32_t pending_type;
};
/* Queue ownership stays in task context. Disable protects the borrowed
 * caller/signal against local IRQs; it does not serialize a remote-CPU IRQ.
 * Cross-CPU waiter lifetime requires a separate synchronization contract. */
static AROS_INTH1(gpu_irq,struct VirtioGPU *,gpu)
{
    AROS_INTFUNC_INIT
    (void)__ufi_intmask;(void)__ufi_custom;(void)__ufi_code;(void)SysBase;
    if(gpu->device.dev_ISR)(void)*gpu->device.dev_ISR;
    __asm__ volatile("dsb sy" ::: "memory");
    ++gpu->interrupts;
    if(gpu->waiter)Signal(gpu->waiter,gpu->wait_mask);
    return TRUE;
    AROS_INTFUNC_EXIT
}
struct GPUHeader { uint32_t type,flags;uint64_t fence;uint32_t context;uint8_t ring,padding[3]; };
struct GPUDisplay { struct GPUHeader header;struct { uint32_t x,y,width,height,enabled,flags; } mode[16]; };
_Static_assert(sizeof(struct GPUHeader)==24,"GPU header wire size");
_Static_assert(sizeof(struct GPUDisplay)==408,"GPU display response wire size");
/* An interrupt is a wakeup hint, not ownership of a particular descriptor.
 * Observe the device's publication before reading its response. DMA memory on
 * the supported transport is coherent; this barrier orders those reads. */
static int gpu_reap(struct VirtioGPU *gpu)
{
    struct virtio_queue *q=gpu->device.dev_VQ;
    UWORD used=q->q_Used->idx;
    if(used!=gpu->pending_next)return 0;
    __asm__ volatile("dsb sy" ::: "memory");
    unsigned slot=(UWORD)(gpu->pending_next-1)%8;
    uint32_t id=q->q_Used->ring[slot].id,len=q->q_Used->ring[slot].len;
    volatile struct GPUHeader *response=(void *)((UBYTE *)gpu->message+64);
    uint32_t type=response->type,flags=response->flags;
    UQUAD fence=response->fence;
    gpu->pending=FALSE;
    if(id || len!=gpu->pending_bytes || type!=gpu->pending_type ||
       !(flags&1) || fence!=gpu->fence) {
        bug("[VirtioGfx] invalid completion used=%u id=%lu len=%lu/%lu type=%lx/%lx flags=%lx fence=%lu/%lu\n",
            (unsigned)used,(unsigned long)id,(unsigned long)len,(unsigned long)gpu->pending_bytes,
            (unsigned long)type,(unsigned long)gpu->pending_type,(unsigned long)flags,
            (unsigned long)fence,(unsigned long)gpu->fence);
        gpu->failed=TRUE;return -1;
    }
    return 1;
}
static int gpu_command(struct VirtioGPU *gpu,unsigned request_bytes,
                       unsigned response_bytes,uint32_t response_type)
{
    if(gpu->failed || gpu->pending || request_bytes>64 || response_bytes>448)return 0;
    BYTE bit=AllocSignal(-1);
    if(bit<0) { bug("[VirtioGfx] no signal available; present deferred\n");return 0; }
    ULONG mask=1UL<<bit;
    SetSignal(0,mask);
    gpu->timer_port.mp_SigTask=FindTask(NULL);
    gpu->timer_port.mp_SigBit=bit;gpu->timer_port.mp_Flags=PA_SIGNAL;
    Disable();gpu->waiter=FindTask(NULL);gpu->wait_mask=mask;
    ULONG before=gpu->interrupts;Enable();
    device_t dev=&gpu->device;void *memory=gpu->message;UQUAD bus=gpu->message_bus;
    struct virtio_queue *q=dev->dev_VQ;
    struct GPUHeader *request=memory,*response=(void *)((UBYTE *)memory+64);
    UWORD next=q->q_Avail->idx+1;
    request->flags=1;request->fence=++gpu->fence;
    gpu->pending=TRUE;gpu->pending_next=next;
    gpu->pending_bytes=response_bytes;gpu->pending_type=response_type;
    memset(response,0,response_bytes);
    q->q_Desc[0].addr=bus;q->q_Desc[0].len=request_bytes;
    q->q_Desc[0].flags=VIRTQ_DESC_F_NEXT;q->q_Desc[0].next=1;
    q->q_Desc[1].addr=bus+64;q->q_Desc[1].len=response_bytes;
    q->q_Desc[1].flags=VIRTQ_DESC_F_WRITE;q->q_Desc[1].next=0;
    q->q_Avail->ring[(UWORD)(next-1)%8]=0;__asm__ volatile("dsb sy" ::: "memory");
    q->q_Avail->idx=next;__asm__ volatile("dsb sy" ::: "memory");
    *q->q_Notify=0;__asm__ volatile("dsb sy" ::: "memory");
    /* Start the deadline after submission: a preempted producer must not
     * consume the entire device timeout before it has notified the device. */
    gpu->timer->tr_node.io_Command=TR_ADDREQUEST;
    gpu->timer->tr_time.tv_secs=1;gpu->timer->tr_time.tv_micro=0;
    SendIO((struct IORequest *)gpu->timer);
    while(!CheckIO((struct IORequest *)gpu->timer) &&
          q->q_Used->idx!=next)
        Wait(mask);
    Disable();gpu->waiter=NULL;Enable();
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
    else if(result>0 && !gpu->irq_reported && gpu->interrupts!=before) {
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
ULONG VirtioGPUClose(struct VirtioGPU *gpu)
{
    if(!gpu)return 1;
    struct VirtIOBase *VirtIOBase=&gpu->base;device_t dev=&gpu->device;
    if(gpu->mapped) {
        uint32_t *body;
        if(!gpu->failed && !gpu->pending && gpu->scanout) { gpu_request(gpu,0x103);gpu_command(gpu,48,24,0x1100); }
        if(!gpu->failed && !gpu->pending && gpu->backing) { body=gpu_request(gpu,0x107);body[0]=1;gpu_command(gpu,32,24,0x1100); }
        if(!gpu->failed && !gpu->pending && gpu->resource) { body=gpu_request(gpu,0x102);body[0]=1;gpu_command(gpu,32,24,0x1100); }
        if(!virtio_hw_stop(dev)) { gpu->failed=TRUE;gpu->ready=FALSE;return 0; }
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
    *out=gpu;InitSemaphore(&gpu->lock);
    struct VirtIOBase *VirtIOBase=&gpu->base;device_t dev=&gpu->device;
    const char *stage=NULL;
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
    if(!virtio_hw_reset(dev) || !virtio_hw_negotiate_features(dev,(UQUAD)1<<VIRTIO_F_VERSION_1))goto fail;
    stage="queue";
    dev->dev_VQ=virtio_alloc_queue(dev,0,8);
    if(!dev->dev_VQ || !virtio_hw_setup_queue(dev,0,dev->dev_VQ))goto fail;
    stage="timer";
    NewList(&gpu->timer_port.mp_MsgList);
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
    gpu->message=HIDD_PCIDriver_AllocPCIMem(dev->dev_PCIDriverObject,512);
    if(!gpu->message || !vq_address(dev,gpu->message,512,&gpu->message_bus))goto fail;
    memset(gpu->message,0,512);
    if(!virtio_hw_driver_ok(dev))goto fail;
    stage="display query";
    gpu_request(gpu,0x100);
    if(!gpu_command(gpu,24,408,0x1101))goto fail;
    struct GPUDisplay *display=(void *)((UBYTE *)gpu->message+64);
    if(display->mode[0].enabled!=1 || display->mode[0].width!=1024 || display->mode[0].height!=768)goto fail;
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
