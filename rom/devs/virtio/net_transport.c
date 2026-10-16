/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
   Licensed under the AROS Public License (APL) Version 1.1.

   Modern virtio-net, no segmentation/checksum offloads or merged RX buffers.
   Wire contract: OASIS Virtio 1.2, section 5.1.6 (12-byte modern header).
   Reuses the PCI mapping, acknowledged reset and DMA queue allocator.
*/
#define __OOP_NOLIBBASE__
#define __OOP_NOATTRBASES__
#define __OOP_NOMETHODBASES__
#include <proto/oop.h>
#include <hidd/pci.h>
#include <string.h>
#include "net_transport.h"
#include "virtio_debug.h"
#include "virtio_intern.h"
#include "virtio_hw.h"
#include "virtio_queue.h"
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error virtio-net transport requires little-endian descriptor access
#endif
#define NET_SLOTS 8
#define NET_BUFFER 2048
#define NET_HEADER 12
#define NET_FRAME 1518
struct VirtioNet {
    struct VirtIOBase base;
    __typeof__(*(device_t)0) device;
    struct virtio_queue *rx,*tx;
    struct Interrupt *interrupt;
    UBYTE *memory,mac[6];
    UQUAD bus;
    UWORD saved_command,mtu;
    BOOL mapped,ready,failed,tx_pending;
};
static void net_barrier(void)
{
#if defined(__aarch64__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}
static void net_publish(struct virtio_queue *q,UWORD id)
{
    UWORD index=q->q_Avail->idx;
    q->q_Avail->ring[index % NET_SLOTS]=id;
    net_barrier();q->q_Avail->idx=(UWORD)(index+1);net_barrier();
}
static void net_notify(struct virtio_queue *q)
{ *q->q_Notify=q->q_Index;net_barrier(); }
BOOL VirtioNetMAC(struct VirtioNet *net,UBYTE mac[6])
{
    if(!net || !mac || !net->ready || net->failed)return FALSE;
    memcpy(mac,net->mac,6);return TRUE;
}
ULONG VirtioNetMTU(struct VirtioNet *net)
{ return net && net->ready && !net->failed ? net->mtu : 0; }
LONG VirtioNetPollTX(struct VirtioNet *net)
{
    if(!net || !net->ready || net->failed)return -1;
    struct virtio_queue *q=net->tx;
    UWORD pending=(UWORD)(q->q_Used->idx-q->q_LastUsedIdx);net_barrier();
    if(!pending)return net->tx_pending?0:1;
    if(pending!=1 || !net->tx_pending ||
       q->q_Used->ring[q->q_LastUsedIdx % NET_SLOTS].id!=0 ||
       q->q_Used->ring[q->q_LastUsedIdx % NET_SLOTS].len!=0) {
        net->failed=TRUE;return -1;
    }
    ++q->q_LastUsedIdx;net->tx_pending=FALSE;return 1;
}
LONG VirtioNetSend(struct VirtioNet *net,const UBYTE *frame,ULONG length)
{
    if(!frame || length<14 || length>NET_FRAME)return -1;
    LONG ready=VirtioNetPollTX(net);if(ready!=1)return ready;
    if(length>(ULONG)net->mtu+18)return -1;
    UBYTE *buffer=net->memory+NET_SLOTS*NET_BUFFER;
    memset(buffer,0,NET_HEADER);memcpy(buffer+NET_HEADER,frame,length);
    struct virtio_queue *q=net->tx;
    q->q_Desc[0].addr=net->bus+NET_SLOTS*NET_BUFFER;
    q->q_Desc[0].len=NET_HEADER+length;q->q_Desc[0].flags=0;q->q_Desc[0].next=0;
    net->tx_pending=TRUE;net_publish(q,0);net_notify(q);return 1;
}
LONG VirtioNetReceive(struct VirtioNet *net,UBYTE *frame,ULONG capacity)
{
    if(!net || !net->ready || net->failed || !frame || capacity<NET_FRAME)return -1;
    struct virtio_queue *q=net->rx;
    UWORD pending=(UWORD)(q->q_Used->idx-q->q_LastUsedIdx);net_barrier();
    if(!pending)return 0;
    if(pending>NET_SLOTS)goto fault;
    /* Reject duplicate IDs already in the published completion batch before
     * making any of those buffers available to the device again. */
    unsigned seen=0;
    for(unsigned i=0;i<pending;++i) {
        ULONG id=q->q_Used->ring[(UWORD)(q->q_LastUsedIdx+i)%NET_SLOTS].id;
        if(id>=NET_SLOTS || (seen&(1u<<id)))goto fault;
        seen|=1u<<id;
    }
    ULONG id=q->q_Used->ring[q->q_LastUsedIdx%NET_SLOTS].id;
    ULONG length=q->q_Used->ring[q->q_LastUsedIdx%NET_SLOTS].len;
    if(length<NET_HEADER+14 || length>NET_HEADER+NET_FRAME || length>NET_HEADER+(ULONG)net->mtu+18)goto fault;
    UBYTE *buffer=net->memory+id*NET_BUFFER;
    /* Virtio 1.2 requires num_buffers=1 here; VZ writes zero without
     * MRG_RXBUF. Both represent this one complete, bounded used descriptor.
     * Never interpret zero as a count or consume a second buffer. */
    /* Ignore checksum/RSC hints: no receive offload was negotiated and the
     * caller must verify checksums. NEEDS_CSUM, however, would require us to
     * finish a checksum, so do not deliver such an incomplete packet. */
    if((buffer[0]&1) || buffer[1] || buffer[10]>1 || buffer[11])goto fault;
    memcpy(frame,buffer+NET_HEADER,length-NET_HEADER);
    ++q->q_LastUsedIdx;
    net_publish(q,(UWORD)id);net_notify(q);return length-NET_HEADER;
fault:
    net->failed=TRUE;return -1;
}
UBYTE VirtioNetInterruptStatus(struct VirtioNet *net)
{
    if(!net || !net->mapped || !net->device.dev_ISR)return 0;
    UBYTE status=*net->device.dev_ISR;net_barrier();return status;
}
BOOL VirtioNetEnableInterrupt(struct VirtioNet *net,struct Interrupt *interrupt)
{
    if(!net || !net->ready || net->failed || net->interrupt ||
       !interrupt || !interrupt->is_Code)return FALSE;
    struct VirtIOBase *VirtIOBase=&net->base;device_t dev=&net->device;
    if(!HIDD_PCIDriver_AddInterrupt(dev->dev_PCIDriverObject,dev->dev_Object,interrupt)) {
        net->failed=TRUE;return FALSE;
    }
    net->interrupt=interrupt;
    if(!virtio_hw_route_queue_irq(dev,0) || !virtio_hw_route_queue_irq(dev,1)) {
        net->failed=TRUE;return FALSE;
    }
    net->rx->q_Avail->flags=0;net->tx->q_Avail->flags=0;net_barrier();
    net_notify(net->rx);return TRUE;
}
BOOL VirtioNetClose(struct VirtioNet *net)
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
        virtio_hw_unmap(dev);
        HIDD_PCIDevice_WriteConfigWord(dev->dev_Object,4,net->saved_command);
    }
    if(net->base.virtio_HiddPCIDeviceAttrBase)OOP_ReleaseAttrBase((CONST_STRPTR)IID_Hidd_PCIDevice);
    FreeMem(net,sizeof(*net));return TRUE;
}
BOOL VirtioNetOpen(struct Library *oop,OOP_Object *object,struct VirtioNet **out)
{
    if(!out)return FALSE;
    *out=NULL;if(!oop || !object)return FALSE;
    struct VirtioNet *net=AllocMem(sizeof(*net),MEMF_CLEAR|MEMF_PUBLIC);
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
    if(vendor!=0x1af4 || product!=0x1041)goto fail;
    OOP_GetAttr(object,aHidd_PCIDevice_Driver,(IPTR *)&dev->dev_PCIDriverObject);
    stage="map";
    if(!virtio_hw_map(dev))goto fail;
    net->mapped=TRUE;net->saved_command=HIDD_PCIDevice_ReadConfigWord(object,4);
    HIDD_PCIDevice_WriteConfigWord(object,4,net->saved_command|6);
    if(dev->dev_PCIMapping.bytes[3]<6)goto fail;
    stage="reset";
    if(!virtio_hw_reset(dev))goto fail;
    stage="features";
    if(!virtio_hw_negotiate_features(dev,
        ((UQUAD)1<<VIRTIO_F_VERSION_1)|((UQUAD)1<<5)|((UQUAD)1<<3)))goto fail;
    stage="MAC feature";
    if(!(dev->dev_Features&((UQUAD)1<<5)))goto fail;
    /* MTU is a device-provided upper bound, not an offload. Read it in the
     * same stable configuration snapshot as MAC. VZ requires its acceptance. */
    if((dev->dev_Features&((UQUAD)1<<3)) && dev->dev_PCIMapping.bytes[3]<12)goto fail;
    net->mtu=1500;
    stage="MAC/MTU";
    BOOL consistent=FALSE;
    for(unsigned attempt=0;attempt<8;++attempt) {
        volatile UBYTE *common=(void *)dev->dev_CommonCfg,*cfg=dev->dev_DeviceCfg;
        UBYTE generation=common[21];net_barrier();
        for(unsigned i=0;i<6;++i)net->mac[i]=cfg[i];
        if(dev->dev_Features&((UQUAD)1<<3))net->mtu=(UWORD)cfg[10]|((UWORD)cfg[11]<<8);
        net_barrier();if(generation==common[21]) { consistent=TRUE;break; }
    }
    if(!consistent || net->mtu<68 || net->mtu>1500 || (net->mac[0]&1) ||
       !(net->mac[0]|net->mac[1]|net->mac[2]|net->mac[3]|net->mac[4]|net->mac[5]))goto fail;
    stage="queues";
    net->rx=virtio_alloc_queue(dev,0,NET_SLOTS);
    net->tx=virtio_alloc_queue(dev,1,NET_SLOTS);
    if(!net->rx || !net->tx || !virtio_hw_setup_queue(dev,0,net->rx) ||
       !virtio_hw_setup_queue(dev,1,net->tx))goto fail;
    net->rx->q_Avail->flags=1;net->tx->q_Avail->flags=1;
    stage="DMA buffers";
    const ULONG bytes=(NET_SLOTS+1)*NET_BUFFER;
    net->memory=HIDD_PCIDriver_AllocPCIMem(dev->dev_PCIDriverObject,bytes);
    if(!net->memory || (IPTR)net->memory>UINTPTR_MAX-(bytes-1))goto fail;
    IPTR first=(IPTR)HIDD_PCIDriver_CPUtoPCI(dev->dev_PCIDriverObject,net->memory);
    IPTR last=(IPTR)HIDD_PCIDriver_CPUtoPCI(dev->dev_PCIDriverObject,net->memory+bytes-1);
    if(first==UINTPTR_MAX || last==UINTPTR_MAX || first>UINTPTR_MAX-(bytes-1) || last!=first+bytes-1)goto fail;
    net->bus=first;memset(net->memory,0,bytes);
    for(unsigned i=0;i<NET_SLOTS;++i) {
        net->rx->q_Desc[i].addr=net->bus+i*NET_BUFFER;
        net->rx->q_Desc[i].len=NET_BUFFER;
        net->rx->q_Desc[i].flags=VIRTQ_DESC_F_WRITE;
        net->rx->q_Desc[i].next=0;net_publish(net->rx,i);
    }
    stage="DRIVER_OK";
    if(!virtio_hw_driver_ok(dev))goto fail;
    net->ready=TRUE;net_notify(net->rx);return TRUE;
fail:
    bug("[VirtioNet] open failed at %s status=%02x features=%llx\n",stage,
        dev->dev_CommonCfg?dev->dev_CommonCfg->device_status:0,
        (unsigned long long)dev->dev_Features);
    net->failed=TRUE;
    if(VirtioNetClose(net))*out=NULL;
    return FALSE;
}
