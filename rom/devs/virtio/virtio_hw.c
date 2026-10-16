/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.
*/

#include <proto/exec.h>
#include <proto/oop.h>

#include <hidd/pci.h>

#include <string.h>

#include "virtio_debug.h"
#include "virtio_intern.h"
#include "virtio_hw.h"
#include "status.h"

static uint32_t pci_read(void *context,unsigned reg,unsigned width)
{
    device_t dev=context;
    struct VirtIOBase *VirtIOBase=dev->dev_VirtIOBase;
    return width==1 ? HIDD_PCIDevice_ReadConfigByte(dev->dev_Object,reg) :
        width==2 ? HIDD_PCIDevice_ReadConfigWord(dev->dev_Object,reg) :
        HIDD_PCIDevice_ReadConfigLong(dev->dev_Object,reg);
}
static int pci_bar(void *context,unsigned bar,uint64_t *base,uint64_t *bytes)
{
    device_t dev=context;
    struct VirtIOBase *VirtIOBase=dev->dev_VirtIOBase;
    IPTR address=0,size=0;
    /* Reject port I/O and upper halves of 64-bit BARs. */
    for(unsigned i=0;i<=bar;++i) {
        ULONG raw=HIDD_PCIDevice_ReadConfigLong(dev->dev_Object,0x10+4*i);
        if(i==bar && (raw&1)) return 0;
        if(!(raw&1) && (raw&6)==4) { if(i+1==bar) return 0; ++i; }
    }
    OOP_GetAttr(dev->dev_Object,HiddPCIDeviceAttrBase+aoHidd_PCIDevice_Base0+3*bar,&address);
    OOP_GetAttr(dev->dev_Object,HiddPCIDeviceAttrBase+aoHidd_PCIDevice_Size0+3*bar,&size);
    *base=address&~(IPTR)15;*bytes=size;
    return *base && *bytes;
}
static void *pci_map(void *context,uint64_t bus,uint32_t bytes)
{
    device_t dev=context;
    struct VirtIOBase *VirtIOBase=dev->dev_VirtIOBase;
    if(bus>UINTPTR_MAX) return NULL;
    return HIDD_PCIDriver_MapPCI(dev->dev_PCIDriverObject,(APTR)(IPTR)bus,bytes);
}
static void pci_unmap(void *context,void *cpu,uint32_t bytes)
{
    device_t dev=context;
    struct VirtIOBase *VirtIOBase=dev->dev_VirtIOBase;
    HIDD_PCIDriver_UnmapPCI(dev->dev_PCIDriverObject,cpu,bytes);
}
void virtio_hw_unmap(device_t dev)
{
    if(!dev || !dev->dev_VirtIOBase || !dev->dev_PCIDriverObject) return;
    const struct VirtioPCIAccess io={dev,pci_read,pci_bar,pci_map,pci_unmap};
    VirtioPCIUnmap(&io,&dev->dev_PCIMapping);
    dev->dev_CommonCfg=NULL;dev->dev_NotifyBase=NULL;dev->dev_ISR=NULL;
    dev->dev_DeviceCfg=NULL;dev->dev_NotifyOffMultiplier=0;dev->dev_IsModern=0;
}
BOOL virtio_hw_map(device_t dev)
{
    if(!dev || !dev->dev_VirtIOBase || !dev->dev_Object || !dev->dev_PCIDriverObject) return FALSE;
    const struct VirtioPCIAccess io={dev,pci_read,pci_bar,pci_map,pci_unmap};
    if(!VirtioPCIMap(&io,&dev->dev_PCIMapping,15)) return FALSE;
    dev->dev_CommonCfg=dev->dev_PCIMapping.regions[0];
    dev->dev_NotifyBase=dev->dev_PCIMapping.regions[1];
    dev->dev_ISR=dev->dev_PCIMapping.regions[2];
    dev->dev_DeviceCfg=dev->dev_PCIMapping.regions[3];
    dev->dev_NotifyOffMultiplier=dev->dev_PCIMapping.notify_multiplier;
    dev->dev_IsModern=1;
    return TRUE;
}

static void status_barrier(void)
{
#if defined(__aarch64__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}
static uint8_t status_read(void *context)
{
    device_t dev = context;
    UBYTE value = dev->dev_CommonCfg->device_status;
    status_barrier();
    return value;
}
static void status_write(void *context, uint8_t value)
{
    device_t dev = context;
    status_barrier();
    dev->dev_CommonCfg->device_status = value;
    status_barrier();
}
BOOL virtio_hw_stop(device_t dev)
{
    if (!dev || !dev->dev_CommonCfg) return FALSE;
    const struct VirtioStatusAccess io = {dev, status_read, status_write};
    return VirtioStatusReset(&io, 1000000);
}
BOOL virtio_hw_reset(device_t dev)
{
    if (!dev || !dev->dev_CommonCfg) return FALSE;
    const struct VirtioStatusAccess io = {dev, status_read, status_write};
    return VirtioStatusBegin(&io, 1000000);
}

UQUAD virtio_hw_get_device_features(device_t dev)
{
    UQUAD lo, hi;
    if (!dev->dev_CommonCfg) return 0;

    dev->dev_CommonCfg->device_feature_select = 0;
    lo = dev->dev_CommonCfg->device_feature;

    dev->dev_CommonCfg->device_feature_select = 1;
    hi = dev->dev_CommonCfg->device_feature;

    return lo | (hi << 32);
}

void virtio_hw_set_guest_features(device_t dev, UQUAD features)
{
    if (!dev->dev_CommonCfg) return;

    dev->dev_CommonCfg->guest_feature_select = 0;
    dev->dev_CommonCfg->guest_feature        = (ULONG)(features & 0xFFFFFFFFUL);

    dev->dev_CommonCfg->guest_feature_select = 1;
    dev->dev_CommonCfg->guest_feature        = (ULONG)(features >> 32);
}

BOOL virtio_hw_negotiate_features(device_t dev, UQUAD wanted)
{
    UQUAD have;
    UBYTE st;

    if (!dev->dev_CommonCfg) return FALSE;

    have = virtio_hw_get_device_features(dev);
    dev->dev_Features = have & wanted;

    /* VERSION_1 is mandatory for modern devices. */
    if (dev->dev_IsModern) {
        if (!(have & ((UQUAD)1 << VIRTIO_F_VERSION_1))) {
            D(bug("[VIRTIO:HW] %s: device does not advertise VERSION_1\n", __func__);)
            return FALSE;
        }
        dev->dev_Features |= ((UQUAD)1 << VIRTIO_F_VERSION_1);
    }

    virtio_hw_set_guest_features(dev, dev->dev_Features);

    /* Set FEATURES_OK */
    st = dev->dev_CommonCfg->device_status;
    dev->dev_CommonCfg->device_status = st | VIRTIO_CONFIG_S_FEATURES_OK;

    /* Re-read */
    st = dev->dev_CommonCfg->device_status;
    if (!(st & VIRTIO_CONFIG_S_FEATURES_OK)) {
        D(bug("[VIRTIO:HW] %s: device rejected feature negotiation (status %02x)\n", __func__, st);)
        return FALSE;
    }
    return TRUE;
}

BOOL virtio_hw_setup_queue(device_t dev, UWORD qid, struct virtio_queue *vq)
{
    UWORD qsize;
    UWORD notify_off;

    if (!dev->dev_CommonCfg) return FALSE;

    dev->dev_CommonCfg->queue_select = qid;
    qsize = dev->dev_CommonCfg->queue_size;
    if (qsize == 0) {
        D(bug("[VIRTIO:HW] %s: queue %u is not present\n", __func__, qid);)
        return FALSE;
    }
    if (qsize < vq->q_Size) {
        D(bug("[VIRTIO:HW] %s: device caps queue size at %u, requested %u\n",
              __func__, qsize, vq->q_Size);)
        return FALSE;
    }
    /* Tell the device our chosen size */
    dev->dev_CommonCfg->queue_size = vq->q_Size;

    dev->dev_CommonCfg->queue_desc   = vq->q_DescPhys;
    dev->dev_CommonCfg->queue_driver = vq->q_AvailPhys;
    dev->dev_CommonCfg->queue_device = vq->q_UsedPhys;

    notify_off = dev->dev_CommonCfg->queue_notify_off;
    vq->q_Notify = VirtioPCINotify(&dev->dev_PCIMapping,notify_off);
    if(!vq->q_Notify) return FALSE;

    /* Keep the queue unassigned until PCI interrupt registration succeeds. */
    dev->dev_CommonCfg->queue_msix_vector = 0xFFFF;

    /* Enable */
    dev->dev_CommonCfg->queue_enable = 1;
    return TRUE;
}

/* The PCI AddInterrupt contract supplies
 * one shared MSI-X table entry (index zero), or the platform's legacy route.
 * Route the queue only after the interrupt handler has been installed. */
BOOL virtio_hw_route_queue_irq(device_t dev, UWORD queue)
{
    if (!dev || !dev->dev_CommonCfg || queue >= dev->dev_CommonCfg->num_queues) return FALSE;
    struct VirtIOBase *VirtIOBase=dev->dev_VirtIOBase;
    IPTR cap=0;
    OOP_GetAttr(dev->dev_Object,aHidd_PCIDevice_CapabilityMSIX,&cap);
    UWORD vector=0xffff;
    if (cap) {
        if (cap<0x40 || cap>0xf4 || (cap&3) ||
            HIDD_PCIDevice_ReadConfigByte(dev->dev_Object,cap)!=0x11) return FALSE;
        UWORD control=HIDD_PCIDevice_ReadConfigWord(dev->dev_Object,cap+2);
        if (control&0x8000) {
            if (control&0x4000) return FALSE;
            vector=0;
        }
    }
    dev->dev_CommonCfg->queue_select=queue;
    status_barrier();
    dev->dev_CommonCfg->queue_msix_vector=vector;
    __sync_synchronize();
    return dev->dev_CommonCfg->queue_msix_vector==vector;
}

BOOL virtio_hw_route_irq(device_t dev)
{
    return dev && dev->dev_VQ && virtio_hw_route_queue_irq(dev, 0);
}

BOOL virtio_hw_driver_ok(device_t dev)
{
    UBYTE st;
    if (!dev->dev_CommonCfg) return FALSE;
    st = dev->dev_CommonCfg->device_status;
    dev->dev_CommonCfg->device_status = st | VIRTIO_CONFIG_S_DRIVER_OK;

    st = dev->dev_CommonCfg->device_status;
    if (st & VIRTIO_CONFIG_S_FAILED) return FALSE;
    return TRUE;
}

void virtio_hw_read_blk_config(device_t dev)
{
    volatile struct virtio_blk_config *cfg;

    if (!dev->dev_DeviceCfg) {
        /* Use defaults */
        dev->dev_Capacity = 0;
        dev->dev_BlkSize  = VIRTIO_BLK_SECTOR_SIZE;
        dev->dev_Heads    = 16;
        dev->dev_Sectors  = 63;
        dev->dev_Cylinders = 0;
        return;
    }

    cfg = (volatile struct virtio_blk_config *)dev->dev_DeviceCfg;
    dev->dev_Capacity = cfg->capacity;     /* in 512-byte sectors */

    if (dev->dev_Features & ((UQUAD)1 << VIRTIO_BLK_F_BLK_SIZE)) {
        dev->dev_BlkSize = cfg->blk_size ? cfg->blk_size : VIRTIO_BLK_SECTOR_SIZE;
    } else {
        dev->dev_BlkSize = VIRTIO_BLK_SECTOR_SIZE;
    }

    if (dev->dev_Features & ((UQUAD)1 << VIRTIO_BLK_F_GEOMETRY)) {
        dev->dev_Cylinders = cfg->geometry.cylinders;
        dev->dev_Heads     = cfg->geometry.heads;
        dev->dev_Sectors   = cfg->geometry.sectors;
    } else {
        dev->dev_Heads     = 16;
        dev->dev_Sectors   = 63;
        dev->dev_Cylinders = 0;
    }

    dev->dev_ReadOnly = (dev->dev_Features & ((UQUAD)1 << VIRTIO_BLK_F_RO)) ? 1 : 0;

    D(bug("[VIRTIO:HW] %s: capacity=%llu sectors, blk_size=%u, geom=%u/%u/%u, RO=%u\n",
          __func__, (unsigned long long)dev->dev_Capacity, dev->dev_BlkSize,
          dev->dev_Cylinders, dev->dev_Heads, dev->dev_Sectors, dev->dev_ReadOnly);)
}
