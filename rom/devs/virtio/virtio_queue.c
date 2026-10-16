/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.
*/

#include <proto/exec.h>
#include <hidd/pci.h>

#include <exec/errors.h>
#include <string.h>

#include "virtio_debug.h"
#include "virtio_intern.h"
#include "virtio_queue.h"

#define VIRTIO_PAGE_SIZE        4096
#define VIRTIO_PAGE_ALIGN(x)    (((x) + (VIRTIO_PAGE_SIZE - 1)) & ~(VIRTIO_PAGE_SIZE - 1))

/* AllocPCIMem must provide coherent DMA memory. Native noncoherent platforms
 * need an explicit cache ownership implementation before using this transport. */
static void vq_barrier(void)
{
#if defined(__aarch64__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}
static BOOL vq_address(device_t dev, APTR cpu, ULONG bytes, UQUAD *bus)
{
    struct VirtIOBase *VirtIOBase = dev->dev_VirtIOBase;
    if (!cpu || !bytes || bytes - 1 > UINTPTR_MAX - (IPTR)cpu) return FALSE;
    APTR first = HIDD_PCIDriver_CPUtoPCI(dev->dev_PCIDriverObject, cpu);
    if (first == (APTR)-1 || bytes - 1 > UINTPTR_MAX - (IPTR)first) return FALSE;
    /* AllocPCIMem provides a contiguous allocation. Verify its advertised end
     * as well as its base; never turn the failure sentinel into a descriptor. */
    APTR last = HIDD_PCIDriver_CPUtoPCI(dev->dev_PCIDriverObject, (UBYTE *)cpu + bytes - 1);
    if ((IPTR)last != (IPTR)first + bytes - 1 || last == (APTR)-1) return FALSE;
    *bus = (IPTR)first;
    return TRUE;
}

static ULONG vq_alloc_size(int size, ULONG *avail_off, ULONG *used_off)
{
    ULONG desc_bytes  = sizeof(struct virtq_desc) * size;
    ULONG avail_bytes = sizeof(UWORD) * (3 + size);
    ULONG used_bytes  = sizeof(UWORD) * 3 + sizeof(struct virtq_used_elem) * size;
    ULONG total;

    *avail_off = VIRTIO_PAGE_ALIGN(desc_bytes);
    *used_off  = VIRTIO_PAGE_ALIGN(*avail_off + avail_bytes);
    total      = VIRTIO_PAGE_ALIGN(*used_off + used_bytes);
    return total;
}

struct virtio_queue *virtio_alloc_queue(device_t dev, int qid, int size)
{
    struct virtio_queue *vq;
    struct VirtIOBase *VirtIOBase = dev ? dev->dev_VirtIOBase : NULL;
    ULONG bytes, avail_off, used_off;
    UWORD i;

    if (!dev || !dev->dev_PCIDriverObject || !VirtIOBase)
        return NULL;
    if (size <= 0 || size > 32768 || (size & (size - 1)) != 0)
        return NULL;

    vq = AllocMem(sizeof(*vq), MEMF_CLEAR);
    if (!vq)
        return NULL;

    vq->dev    = dev;
    vq->q_Index = qid;
    vq->q_Size = size;
    InitSemaphore(&vq->q_Lock);

    bytes = vq_alloc_size(size, &avail_off, &used_off);
    vq->q_RingBytes = bytes;

    vq->q_RingBase = HIDD_PCIDriver_AllocPCIMem(dev->dev_PCIDriverObject, bytes);
    if (!vq->q_RingBase) {
        FreeMem(vq, sizeof(*vq));
        return NULL;
    }
    memset(vq->q_RingBase, 0, bytes);

    vq->q_Desc  = (volatile struct virtq_desc  *) vq->q_RingBase;
    vq->q_Avail = (volatile struct virtq_avail *)((UBYTE *)vq->q_RingBase + avail_off);
    vq->q_Used  = (volatile struct virtq_used  *)((UBYTE *)vq->q_RingBase + used_off);

    {
        UQUAD base;
        if (!vq_address(dev, vq->q_RingBase, bytes, &base)) {
            virtio_free_queue(vq);
            return NULL;
        }
        vq->q_DescPhys  = base;
        vq->q_AvailPhys = base + avail_off;
        vq->q_UsedPhys  = base + used_off;
    }

    vq->q_Reqs = HIDD_PCIDriver_AllocPCIMem(dev->dev_PCIDriverObject, sizeof(struct virtio_request) * size);
    if (!vq->q_Reqs) {
        HIDD_PCIDriver_FreePCIMem(dev->dev_PCIDriverObject, vq->q_RingBase);
        FreeMem(vq, sizeof(*vq));
        return NULL;
    }

    memset(vq->q_Reqs, 0, sizeof(struct virtio_request) * size);

    for (i = 0; i < size; i++) {
        vq->q_Desc[i].next  = (i + 1 == size) ? 0 : (i + 1);
        vq->q_Desc[i].flags = (i + 1 == size) ? 0 : VIRTQ_DESC_F_NEXT;
    }
    vq->q_FreeHead    = 0;
    vq->q_NumFree     = size;
    vq->q_LastUsedIdx = 0;

    vq->q_Avail->flags = 0;
    vq->q_Avail->idx   = 0;
    vq->q_Used->flags  = 0;
    vq->q_Used->idx    = 0;

    return vq;
}

void virtio_free_queue(struct virtio_queue *vq)
{
    struct VirtIOBase *VirtIOBase;

    if (!vq) return;
    VirtIOBase = (vq->dev) ? vq->dev->dev_VirtIOBase : NULL;
    if (vq->q_RingBase && vq->dev && vq->dev->dev_PCIDriverObject && VirtIOBase) {
        HIDD_PCIDriver_FreePCIMem(vq->dev->dev_PCIDriverObject, vq->q_RingBase);
        vq->q_RingBase = NULL;
    }
    if (vq->q_Reqs) {
        for (UWORD i = 0; i < vq->q_Size; ++i)
            if (vq->q_Reqs[i].vr_DMABuffer)
                HIDD_PCIDriver_FreePCIMem(vq->dev->dev_PCIDriverObject, vq->q_Reqs[i].vr_DMABuffer);
        HIDD_PCIDriver_FreePCIMem(vq->dev->dev_PCIDriverObject, vq->q_Reqs);
        vq->q_Reqs = NULL;
    }
    FreeMem(vq, sizeof(*vq));
}

/* Caller holds vq->q_Lock. Allocates head/middle/tail descriptor indices. */
static int vq_alloc_chain(struct virtio_queue *vq, UWORD count, UWORD *idxs)
{
    UWORD i, cur, prev = 0;
    if (vq->q_NumFree < count) return -1;

    cur = vq->q_FreeHead;
    for (i = 0; i < count; i++) {
        idxs[i] = cur;
        prev = cur;
        cur = vq->q_Desc[cur].next;
    }
    vq->q_FreeHead = cur;
    vq->q_NumFree -= count;

    /* Detach our chain (clear NEXT on tail) */
    vq->q_Desc[prev].flags &= ~VIRTQ_DESC_F_NEXT;
    vq->q_Desc[prev].next   = 0;
    return 0;
}

/* Caller holds vq->q_Lock. */
static void vq_free_chain(struct virtio_queue *vq, UWORD head)
{
    UWORD idx = head;
    UWORD count = 0;
    UWORD last;

    for (;;) {
        count++;
        if (!(vq->q_Desc[idx].flags & VIRTQ_DESC_F_NEXT)) break;
        idx = vq->q_Desc[idx].next;
    }
    last = idx;

    vq->q_Desc[last].flags = VIRTQ_DESC_F_NEXT;
    vq->q_Desc[last].next  = vq->q_FreeHead;
    vq->q_FreeHead = head;
    vq->q_NumFree += count;
}

int virtio_submit_blk_request(struct virtio_queue *vq, struct virtio_request *templ)
{
    if (!vq || !templ) return -1;
    device_t dev = vq->dev;
    struct VirtIOBase *VirtIOBase = dev ? dev->dev_VirtIOBase : NULL;
    struct virtio_request *slot;
    UWORD descs_needed = (templ->vr_DataLength > 0) ? 3 : 2;
    UWORD idxs[3];
    UQUAD hdr_phys, status_phys, data_phys = 0;
    UWORD avail_idx;

    if (!dev || !dev->dev_PCIDriverObject || !VirtIOBase || !vq->q_Notify ||
        (templ->vr_DataLength && !templ->vr_DataAddr) || templ->vr_DataLength == UINT32_MAX)
        return -1;

    ObtainSemaphore(&vq->q_Lock);
    if (vq->q_Broken || vq_alloc_chain(vq, descs_needed, idxs) != 0) {
        ReleaseSemaphore(&vq->q_Lock);
        return -1;
    }

    slot = &vq->q_Reqs[idxs[0]];
    *slot = *templ;
    slot->vr_HeadDesc  = idxs[0];
    slot->vr_DescCount = descs_needed;
    slot->vr_InUse     = 1;
    slot->vr_Status    = 0xff;
    slot->vr_DMABuffer = NULL;

    if (!vq_address(dev, &slot->vr_Header, sizeof(slot->vr_Header), &hdr_phys) ||
        !vq_address(dev, &slot->vr_Status, 1, &status_phys)) goto fail;
    if (slot->vr_DataLength) {
        slot->vr_DMABuffer = HIDD_PCIDriver_AllocPCIMem(dev->dev_PCIDriverObject, slot->vr_DataLength);
        if (!slot->vr_DMABuffer ||
            !vq_address(dev, slot->vr_DMABuffer, slot->vr_DataLength, &data_phys)) goto fail;
        if (slot->vr_IsWrite) memcpy(slot->vr_DMABuffer, slot->vr_DataAddr, slot->vr_DataLength);
        else memset(slot->vr_DMABuffer, 0, slot->vr_DataLength);
    }

    /* Build descriptors. */
    vq->q_Desc[idxs[0]].addr  = hdr_phys;
    vq->q_Desc[idxs[0]].len   = sizeof(slot->vr_Header);
    vq->q_Desc[idxs[0]].flags = VIRTQ_DESC_F_NEXT;
    vq->q_Desc[idxs[0]].next  = idxs[1];

    if (templ->vr_DataLength > 0) {
        UWORD data_flags = VIRTQ_DESC_F_NEXT;
        if (!templ->vr_IsWrite) data_flags |= VIRTQ_DESC_F_WRITE; /* device writes => guest reads */
        vq->q_Desc[idxs[1]].addr  = data_phys;
        vq->q_Desc[idxs[1]].len   = templ->vr_DataLength;
        vq->q_Desc[idxs[1]].flags = data_flags;
        vq->q_Desc[idxs[1]].next  = idxs[2];

        vq->q_Desc[idxs[2]].addr  = status_phys;
        vq->q_Desc[idxs[2]].len   = 1;
        vq->q_Desc[idxs[2]].flags = VIRTQ_DESC_F_WRITE;
        vq->q_Desc[idxs[2]].next  = 0;
    } else {
        vq->q_Desc[idxs[1]].addr  = status_phys;
        vq->q_Desc[idxs[1]].len   = 1;
        vq->q_Desc[idxs[1]].flags = VIRTQ_DESC_F_WRITE;
        vq->q_Desc[idxs[1]].next  = 0;
    }

    /* Place head index in available ring. */
    avail_idx = vq->q_Avail->idx;
    vq->q_Avail->ring[avail_idx % vq->q_Size] = idxs[0];
    /* Memory barrier ensuring descriptors are visible before idx update */
    vq_barrier();
    vq->q_Avail->idx = avail_idx + 1;
    vq_barrier();

    ReleaseSemaphore(&vq->q_Lock);

    /* Notify the device. */
    if (vq->q_Notify) {
        *vq->q_Notify = vq->q_Index;
    }

    return idxs[0];
fail:
    if (slot->vr_DMABuffer) {
        HIDD_PCIDriver_FreePCIMem(dev->dev_PCIDriverObject, slot->vr_DMABuffer);
        slot->vr_DMABuffer = NULL;
    }
    slot->vr_InUse = 0;
    vq_free_chain(vq, idxs[0]);
    ReleaseSemaphore(&vq->q_Lock);
    return -1;
}

ULONG virtio_process_used(struct virtio_queue *vq)
{
    ULONG handled = 0;
    struct VirtIOBase *VirtIOBase = vq->dev->dev_VirtIOBase;
    ObtainSemaphore(&vq->q_Lock);
    UWORD used_idx = vq->q_Used->idx;
    vq_barrier(); /* Observe device writes before consuming status/data. */
    if ((UWORD)(used_idx - vq->q_LastUsedIdx) > vq->q_Size) vq->q_Broken = TRUE;
    while (!vq->q_Broken && vq->q_LastUsedIdx != used_idx) {
        UWORD ring_slot = vq->q_LastUsedIdx % vq->q_Size;
        ULONG head = vq->q_Used->ring[ring_slot].id; /* id is 32 bits, never truncate. */
        ULONG length = vq->q_Used->ring[ring_slot].len;
        if (head >= vq->q_Size || !vq->q_Reqs[head].vr_InUse) {
            /* Unknown ownership: stop submission, retain DMA until reset. */
            vq->q_Broken = TRUE;
            break;
        }
        struct virtio_request *req = &vq->q_Reqs[head];
        struct IORequest *io = req->vr_IORequest;
        ULONG expected = 1 + (req->vr_IsWrite ? 0 : req->vr_DataLength);
        UBYTE status = *(volatile UBYTE *)&req->vr_Status;
        BOOL success = length == expected && status == VIRTIO_BLK_S_OK;
        if (success && !req->vr_IsWrite && req->vr_DataLength)
            memcpy(req->vr_DataAddr, req->vr_DMABuffer, req->vr_DataLength);
        if (io) {
            IOStdReq(io)->io_Actual = success ? req->vr_DataLength : 0;
            io->io_Error = success ? 0 :
                (length && length <= expected && status == VIRTIO_BLK_S_UNSUPP ? IOERR_NOCMD : IOERR_ABORTED);
        }
        if (req->vr_DMABuffer) {
            HIDD_PCIDriver_FreePCIMem(vq->dev->dev_PCIDriverObject, req->vr_DMABuffer);
            req->vr_DMABuffer = NULL;
        }
        req->vr_InUse = 0;
        vq_free_chain(vq, (UWORD)head);
        ++vq->q_LastUsedIdx;
        ++handled;
        if (io) ReplyMsg(&io->io_Message);
    }
    ReleaseSemaphore(&vq->q_Lock);
    return handled;
}
