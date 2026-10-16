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
};
/* Queue ownership stays in task context. Current platforms are single CPU;
 * Disable protects the borrowed caller/signal until the callback is detached. */
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
static int gpu_command(struct VirtioGPU *gpu,unsigned request_bytes,
                       unsigned response_bytes,uint32_t response_type)
{
    if(gpu->failed || request_bytes>64 || response_bytes>448)return 0;
    BYTE bit=AllocSignal(-1);
    if(bit<0) { gpu->failed=TRUE;return 0; }
    ULONG mask=1UL<<bit;
    SetSignal(0,mask);
    gpu->timer_port.mp_SigTask=FindTask(NULL);
    gpu->timer_port.mp_SigBit=bit;gpu->timer_port.mp_Flags=PA_SIGNAL;
    Disable();gpu->waiter=FindTask(NULL);gpu->wait_mask=mask;
    ULONG before=gpu->interrupts;Enable();
    gpu->timer->tr_node.io_Command=TR_ADDREQUEST;
    gpu->timer->tr_time.tv_secs=1;gpu->timer->tr_time.tv_micro=0;
    SendIO((struct IORequest *)gpu->timer);
    device_t dev=&gpu->device;void *memory=gpu->message;UQUAD bus=gpu->message_bus;
    struct virtio_queue *q=dev->dev_VQ;
    struct GPUHeader *request=memory,*response=(void *)((UBYTE *)memory+64);
    UWORD next=q->q_Avail->idx+1;
    request->flags=1;request->fence=++gpu->fence;
    memset(response,0,response_bytes);
    q->q_Desc[0].addr=bus;q->q_Desc[0].len=request_bytes;
    q->q_Desc[0].flags=VIRTQ_DESC_F_NEXT;q->q_Desc[0].next=1;
    q->q_Desc[1].addr=bus+64;q->q_Desc[1].len=response_bytes;
    q->q_Desc[1].flags=VIRTQ_DESC_F_WRITE;q->q_Desc[1].next=0;
    q->q_Avail->ring[(UWORD)(next-1)%8]=0;__asm__ volatile("dsb sy" ::: "memory");
    q->q_Avail->idx=next;__asm__ volatile("dsb sy" ::: "memory");
    *q->q_Notify=0;__asm__ volatile("dsb sy" ::: "memory");
    while(!CheckIO((struct IORequest *)gpu->timer) &&
          (q->q_Used->idx!=(UWORD)next || gpu->interrupts==before))
        Wait(mask);
    Disable();gpu->waiter=NULL;Enable();
    if(!CheckIO((struct IORequest *)gpu->timer))AbortIO((struct IORequest *)gpu->timer);
    WaitIO((struct IORequest *)gpu->timer);
    gpu->timer_port.mp_Flags=PA_IGNORE;
    SetSignal(0,mask);FreeSignal(bit);
    __asm__ volatile("dsb sy" ::: "memory");
    int okay=gpu->interrupts!=before && q->q_Used->idx==next && q->q_Used->ring[(UWORD)(next-1)%8].id==0 &&
        q->q_Used->ring[(UWORD)(next-1)%8].len==response_bytes &&
        response->type==response_type && (response->flags&1) && response->fence==gpu->fence;
    if(!okay)gpu->failed=TRUE;
    else if(!gpu->irq_reported) {
        gpu->irq_reported=TRUE;
        bug("[VM] virtio graphics IRQ completion PASS\n");
    }
    return okay;
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
    BOOL okay=gpu->ready && !gpu->failed && gpu_present(gpu,x,y,width,height);
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
        if(!gpu->failed && gpu->scanout) { gpu_request(gpu,0x103);gpu_command(gpu,48,24,0x1100); }
        if(!gpu->failed && gpu->backing) { body=gpu_request(gpu,0x107);body[0]=1;gpu_command(gpu,32,24,0x1100); }
        if(!gpu->failed && gpu->resource) { body=gpu_request(gpu,0x102);body[0]=1;gpu_command(gpu,32,24,0x1100); }
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
