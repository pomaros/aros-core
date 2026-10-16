/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
   Licensed under the AROS Public License (APL) Version 1.1.

   Modern virtio-vsock packet transport. Stream state/credits belong to caller.
   Wire contract: OASIS Virtio 1.2, section 5.10.
   Reuses the PCI mapping, acknowledged reset and DMA queue allocator.
*/
#define __OOP_NOLIBBASE__
#define __OOP_NOATTRBASES__
#define __OOP_NOMETHODBASES__
#include <proto/oop.h>
#include <hidd/pci.h>
#include <string.h>
#include "vsock_transport.h"
#include "virtio_debug.h"
#include "virtio_intern.h"
#include "virtio_hw.h"
#include "virtio_queue.h"
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error virtio-vsock transport requires little-endian descriptor access
#endif
#define VSOCK_SLOTS 8
#define VSOCK_BUFFER 4096
#define VSOCK_HEADER 44
struct VirtioVsock {
    struct VirtIOBase base;
    __typeof__(*(device_t)0) device;
    struct virtio_queue *rx,*tx,*event;
    struct Interrupt *interrupt;
    UBYTE *memory;
    UQUAD cid;
    UQUAD bus;
    UWORD saved_command;
    BOOL mapped,ready,failed,tx_pending;
};
static void vsock_barrier(void)
{
#if defined(__aarch64__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}
static void vsock_publish(struct virtio_queue *q,UWORD id)
{
    UWORD index=q->q_Avail->idx;
    q->q_Avail->ring[index % VSOCK_SLOTS]=id;
    vsock_barrier();q->q_Avail->idx=(UWORD)(index+1);vsock_barrier();
}
static void vsock_notify(struct virtio_queue *q)
{ *q->q_Notify=q->q_Index;vsock_barrier(); }
UQUAD VirtioVsockCID(struct VirtioVsock *net)
{ return net && net->ready && !net->failed ? net->cid : 0; }
/* Transport reset invalidates every caller-owned connection. The owner closes
 * and reopens this context, fetching the current CID before reconnecting. */
static BOOL vsock_event(struct VirtioVsock *net)
{
    if(!net || !net->ready || net->failed)return FALSE;
    UWORD pending=(UWORD)(net->event->q_Used->idx-net->event->q_LastUsedIdx);
    vsock_barrier();
    if(pending) { net->failed=TRUE;return FALSE; }
    return TRUE;
}
LONG VirtioVsockPollTX(struct VirtioVsock *net)
{
    if(!vsock_event(net))return -1;
    struct virtio_queue *q=net->tx;
    UWORD pending=(UWORD)(q->q_Used->idx-q->q_LastUsedIdx);vsock_barrier();
    if(!pending)return net->tx_pending?0:1;
    if(pending!=1 || !net->tx_pending ||
       q->q_Used->ring[q->q_LastUsedIdx % VSOCK_SLOTS].id!=0 ||
       q->q_Used->ring[q->q_LastUsedIdx % VSOCK_SLOTS].len!=0) {
        net->failed=TRUE;return -1;
    }
    ++q->q_LastUsedIdx;net->tx_pending=FALSE;return 1;
}
LONG VirtioVsockSend(struct VirtioVsock *net,const UBYTE *packet,ULONG length)
{
    if(!packet || length<VSOCK_HEADER || length>VSOCK_BUFFER)return -1;
    struct VirtioVsockHeader header;memcpy(&header,packet,sizeof(header));
    if(!net || header.src_cid!=net->cid || header.len!=length-VSOCK_HEADER)return -1;
    LONG ready=VirtioVsockPollTX(net);if(ready!=1)return ready;
    UBYTE *buffer=net->memory+VSOCK_SLOTS*VSOCK_BUFFER;
    memcpy(buffer,packet,length);
    struct virtio_queue *q=net->tx;
    q->q_Desc[0].addr=net->bus+VSOCK_SLOTS*VSOCK_BUFFER;
    q->q_Desc[0].len=length;q->q_Desc[0].flags=0;q->q_Desc[0].next=0;
    net->tx_pending=TRUE;vsock_publish(q,0);vsock_notify(q);return 1;
}
LONG VirtioVsockReceive(struct VirtioVsock *net,UBYTE *packet,ULONG capacity)
{
    if(!vsock_event(net) || !packet || capacity<VSOCK_BUFFER)return -1;
    struct virtio_queue *q=net->rx;
    UWORD pending=(UWORD)(q->q_Used->idx-q->q_LastUsedIdx);vsock_barrier();
    if(!pending)return 0;
    if(pending>VSOCK_SLOTS)goto fault;
    unsigned seen=0;
    for(unsigned i=0;i<pending;++i) {
        ULONG id=q->q_Used->ring[(UWORD)(q->q_LastUsedIdx+i)%VSOCK_SLOTS].id;
        if(id>=VSOCK_SLOTS || (seen&(1u<<id)))goto fault;
        seen|=1u<<id;
    }
    ULONG id=q->q_Used->ring[q->q_LastUsedIdx%VSOCK_SLOTS].id;
    ULONG length=q->q_Used->ring[q->q_LastUsedIdx%VSOCK_SLOTS].len;
    if(length<VSOCK_HEADER || length>VSOCK_BUFFER)goto fault;
    UBYTE *buffer=net->memory+id*VSOCK_BUFFER;
    struct VirtioVsockHeader header;memcpy(&header,buffer,sizeof(header));
    if(header.len!=length-VSOCK_HEADER || header.dst_cid!=net->cid || header.src_cid>>32)goto fault;
    memcpy(packet,buffer,length);
    ++q->q_LastUsedIdx;
    vsock_publish(q,(UWORD)id);vsock_notify(q);return length;
fault:
    net->failed=TRUE;return -1;
}
UBYTE VirtioVsockInterruptStatus(struct VirtioVsock *net)
{
    if(!net || !net->mapped || !net->device.dev_ISR)return 0;
    UBYTE status=*net->device.dev_ISR;vsock_barrier();return status;
}
BOOL VirtioVsockEnableInterrupt(struct VirtioVsock *net,struct Interrupt *interrupt)
{
    if(!net || !net->ready || net->failed || net->interrupt ||
       !interrupt || !interrupt->is_Code)return FALSE;
    struct VirtIOBase *VirtIOBase=&net->base;device_t dev=&net->device;
    if(!HIDD_PCIDriver_AddInterrupt(dev->dev_PCIDriverObject,dev->dev_Object,interrupt)) {
        net->failed=TRUE;return FALSE;
    }
    net->interrupt=interrupt;
    if(!virtio_hw_route_queue_irq(dev,0) || !virtio_hw_route_queue_irq(dev,1) || !virtio_hw_route_queue_irq(dev,2)) {
        net->failed=TRUE;return FALSE;
    }
    net->rx->q_Avail->flags=0;net->tx->q_Avail->flags=0;net->event->q_Avail->flags=0;vsock_barrier();
    vsock_notify(net->rx);return TRUE;
}
BOOL VirtioVsockClose(struct VirtioVsock *net)
{
    if(!net)return TRUE;
    struct VirtIOBase *VirtIOBase=&net->base;device_t dev=&net->device;
    net->ready=FALSE;
    if(net->mapped) {
        if(!virtio_hw_stop(dev)) { net->failed=TRUE;return FALSE; }
        if(net->interrupt) {
            HIDD_PCIDriver_RemoveInterrupt(dev->dev_PCIDriverObject,dev->dev_Object,net->interrupt);
            net->interrupt=NULL;
        }
        if(net->memory)HIDD_PCIDriver_FreePCIMem(dev->dev_PCIDriverObject,net->memory);
        if(net->rx)virtio_free_queue(net->rx);
        if(net->tx)virtio_free_queue(net->tx);
        if(net->event)virtio_free_queue(net->event);
        virtio_hw_unmap(dev);
        HIDD_PCIDevice_WriteConfigWord(dev->dev_Object,4,net->saved_command);
    }
    if(net->base.virtio_HiddPCIDeviceAttrBase)OOP_ReleaseAttrBase((CONST_STRPTR)IID_Hidd_PCIDevice);
    FreeMem(net,sizeof(*net));return TRUE;
}
BOOL VirtioVsockOpen(struct Library *oop,OOP_Object *object,struct VirtioVsock **out)
{
    if(!out)return FALSE;
    *out=NULL;if(!oop || !object)return FALSE;
    struct VirtioVsock *net=AllocMem(sizeof(*net),MEMF_CLEAR|MEMF_PUBLIC);
    if(!net)return FALSE;
    *out=net;
    const char *stage="attributes";
    struct VirtIOBase *VirtIOBase=&net->base;device_t dev=&net->device;
    net->base.virtio_OOPBase=oop;
    net->base.virtio_HiddPCIDeviceAttrBase=OOP_ObtainAttrBase((CONST_STRPTR)IID_Hidd_PCIDevice);
    if(!net->base.virtio_HiddPCIDeviceAttrBase)goto fail;
    net->base.virtio_HiddPCIDeviceMethodBase=OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCIDevice,0);
    net->base.virtio_HiddPCIDriverMethodBase=OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCIDriver,0);
    dev->dev_VirtIOBase=VirtIOBase;dev->dev_Object=object;
    IPTR vendor=0,product=0;
    OOP_GetAttr(object,aHidd_PCIDevice_VendorID,&vendor);
    OOP_GetAttr(object,aHidd_PCIDevice_ProductID,&product);
    if(vendor!=0x1af4 || product!=0x1053)goto fail;
    OOP_GetAttr(object,aHidd_PCIDevice_Driver,(IPTR *)&dev->dev_PCIDriverObject);
    stage="map";
    if(!virtio_hw_map(dev))goto fail;
    net->mapped=TRUE;net->saved_command=HIDD_PCIDevice_ReadConfigWord(object,4);
    HIDD_PCIDevice_WriteConfigWord(object,4,net->saved_command|6);
    if(dev->dev_PCIMapping.bytes[3]<8)goto fail;
    stage="reset";
    if(!virtio_hw_reset(dev))goto fail;
    stage="features";
    if(!virtio_hw_negotiate_features(dev,((UQUAD)1<<VIRTIO_F_VERSION_1)|1))goto fail;
    stage="CID";
    BOOL consistent=FALSE;
    for(unsigned attempt=0;attempt<8;++attempt) {
        volatile UBYTE *common=(void *)dev->dev_CommonCfg,*cfg=dev->dev_DeviceCfg;
        UBYTE generation=common[21];vsock_barrier();
        UQUAD cid=0;for(unsigned i=0;i<8;++i)cid|=(UQUAD)cfg[i]<<(i*8);
        vsock_barrier();if(generation==common[21]) { net->cid=cid;consistent=TRUE;break; }
    }
    if(!consistent || net->cid<=2 || net->cid>=0xffffffffULL)goto fail;
    stage="queues";
    net->rx=virtio_alloc_queue(dev,0,VSOCK_SLOTS);
    net->tx=virtio_alloc_queue(dev,1,VSOCK_SLOTS);
    net->event=virtio_alloc_queue(dev,2,VSOCK_SLOTS);
    if(!net->rx || !net->tx || !net->event || !virtio_hw_setup_queue(dev,0,net->rx) ||
       !virtio_hw_setup_queue(dev,1,net->tx) || !virtio_hw_setup_queue(dev,2,net->event))goto fail;
    net->rx->q_Avail->flags=1;net->tx->q_Avail->flags=1;net->event->q_Avail->flags=1;
    stage="DMA buffers";
    const ULONG bytes=(VSOCK_SLOTS+1)*VSOCK_BUFFER+4;
    net->memory=HIDD_PCIDriver_AllocPCIMem(dev->dev_PCIDriverObject,bytes);
    if(!net->memory || (IPTR)net->memory>UINTPTR_MAX-(bytes-1))goto fail;
    IPTR first=(IPTR)HIDD_PCIDriver_CPUtoPCI(dev->dev_PCIDriverObject,net->memory);
    IPTR last=(IPTR)HIDD_PCIDriver_CPUtoPCI(dev->dev_PCIDriverObject,net->memory+bytes-1);
    if(first==UINTPTR_MAX || last==UINTPTR_MAX || first>UINTPTR_MAX-(bytes-1) || last!=first+bytes-1)goto fail;
    net->bus=first;memset(net->memory,0,bytes);
    for(unsigned i=0;i<VSOCK_SLOTS;++i) {
        net->rx->q_Desc[i].addr=net->bus+i*VSOCK_BUFFER;
        net->rx->q_Desc[i].len=VSOCK_BUFFER;
        net->rx->q_Desc[i].flags=VIRTQ_DESC_F_WRITE;
        net->rx->q_Desc[i].next=0;vsock_publish(net->rx,i);
    }
    net->event->q_Desc[0].addr=net->bus+(VSOCK_SLOTS+1)*VSOCK_BUFFER;
    net->event->q_Desc[0].len=4;net->event->q_Desc[0].flags=VIRTQ_DESC_F_WRITE;
    vsock_publish(net->event,0);
    stage="DRIVER_OK";
    if(!virtio_hw_driver_ok(dev))goto fail;
    net->ready=TRUE;vsock_notify(net->rx);vsock_notify(net->event);return TRUE;
fail:
    bug("[VirtioVsock] open failed at %s status=%02x features=%llx\n",stage,
        dev->dev_CommonCfg?dev->dev_CommonCfg->device_status:0,
        (unsigned long long)dev->dev_Features);
    net->failed=TRUE;
    if(VirtioVsockClose(net))*out=NULL;
    return FALSE;
}
