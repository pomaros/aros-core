/*
 * Copyright (C) 2023-2026, The AROS Development Team. All rights reserved.
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Ring publication extracted from xhci_hcd.c; whole-payload admission added.
 */
#include <proto/exec.h>
#include "uhwcmd.h"
#include "xhciproto.h"
#include "xhci_hcd.h"
#undef base
#define base (hc->hc_Device)
#if defined(AROS_USE_LOGRES)
#undef LogHandle
#undef LogResBase
#define LogHandle (hc->hc_LogRHandle)
#define LogResBase (base->hd_LogResBase)
#endif

static int xhciRingEntriesFree(volatile struct pcisusbXHCIRing *ring)
{
    /* Caller must hold the ring lock. */
    ULONG last = (ring->end & ~RINGENDCFLAG);
    ULONG idx = ring->next;

    return (last > idx) ? last - idx
           : (XHCI_EVENT_RING_TRBS - 1) - idx + last;
}

static void xhciInsertTRB(struct PCIController *hc,
                          volatile struct pcisusbXHCIRing *ring,
                          UQUAD payload, ULONG trflags, ULONG plen)
{
    volatile struct xhci_trb *dst;
    ULONG trbflags;

    /* Start from caller flags, but never copy the cycle bit verbatim */
    trbflags = (trflags & ~TRBF_FLAG_C);

    /* Apply the current producer cycle state */
    if(ring->end & RINGENDCFLAG)
        trbflags |= TRBF_FLAG_C;

    /* Next available TRB slot */
    dst = &ring->ring[ring->next];

    /*
     * Parameter field handling:
     *  - IDT set: payload is immediate data (8 bytes), not an address.
     *  - IDT clear: payload is a CPU pointer that must become a bus address.
     */
    if(trflags & TRBF_FLAG_IDT) {
        dst->dbp.addr_lo = AROS_LONG2LE((ULONG)(payload & 0xffffffffUL));
        dst->dbp.addr_hi = AROS_LONG2LE((ULONG)((payload >> 32) & 0xffffffffUL));
    } else {
        UQUAD dma_payload = payload;
        const ULONG trbtype = (trflags >> TRBS_FLAG_TYPE) & TRB_FLAG_TYPE_SMASK;
        const BOOL raw_dma  = (trbtype == TRBB_FLAG_CRTYPE_SET_TR_DEQUEUE_PTR);

#if !defined(PCIUSB_NO_CPUTOPCI)
        if(!raw_dma && payload != 0) {
            dma_payload = (UQUAD)(IPTR)xhciDeviceAddress(hc, (APTR)(IPTR)payload);
        }
#endif
        /*
         * Set TR Dequeue Pointer uses a DMA address with embedded flag bits
         * (e.g. DCS in bit 0). Do not treat that value as a CPU pointer.
         */
        xhciSetPointer(hc, dst->dbp, dma_payload);
    }

    dst->tparams = AROS_LONG2LE(plen & (TRB_TPARAMS_DS_TRBLEN_SMASK | TRB_TPARAMS_DS_TDSIZE_MASK));
    dst->flags   = AROS_LONG2LE(trbflags);

    /* Ensure the controller observes the freshly written TRB */
    CacheClearE((APTR)dst, sizeof(*dst), CACRF_ClearD);
}

WORD xhciQueueTRB(struct PCIController *hc, volatile struct pcisusbXHCIRing *ring, UQUAD payload,
                  ULONG plen, ULONG trbflags)
{
    WORD queued = -1;

    if(trbflags & TRBF_FLAG_IDT) {
        pciusbXHCIDebugTRBV("xHCI", DEBUGFUNCCOLOR_SET "(0x%p, $%02x %02x %02x%02x %02x%02x %02x%02x, %u, $%08lx)"
                            DEBUGCOLOR_RESET" \n", ring,
                            ((UBYTE *)&payload)[0], ((UBYTE *)&payload)[1], ((UBYTE *)&payload)[3], ((UBYTE *)&payload)[2],
                            ((UBYTE *)&payload)[5], ((UBYTE *)&payload)[4], ((UBYTE *)&payload)[7], ((UBYTE *)&payload)[6],
                            plen, trbflags);
    } else
        pciusbXHCIDebugTRBV("xHCI", DEBUGFUNCCOLOR_SET "(0x%p, 0x%p, %u, $%08lx)" DEBUGCOLOR_RESET" \n", ring, payload, plen,
                            trbflags);

    if(!ring) {
        pciusbError("xHCI", DEBUGWARNCOLOR_SET "NO RINGSPECIFIED!!" DEBUGCOLOR_RESET" \n");
        return -1;
    }

    if(!ring->ringio) {
        pciusbError("xHCI",
                    DEBUGWARNCOLOR_SET "xHCI: ring %p has no ringio bookkeeping" DEBUGCOLOR_RESET" \n",
                    ring);
        return -1;
    }

    xhciRingLock();
    if(xhciRingEntriesFree(ring) > 1) {
        if(ring->next >= XHCI_EVENT_RING_TRBS - 1) {
            UQUAD link_dma = (UQUAD)(IPTR)&ring->ring[0];

            /*
             * If this is the last ring element, insert a link
             * back to the ring start - and update the cycle bit
             */
            pciusbXHCIDebugRIO("xHCI",
                               DEBUGWARNCOLOR_SET "xHCI: link <ringio %p>[%d] 0x%p -> 0x0000000000000000" DEBUGCOLOR_RESET "\n",
                               ring->ringio, ring->next, ring->ringio[ring->next]);
            ring->ringio[ring->next] = NULL;
            xhciInsertTRB(hc, ring,
                          link_dma,
                          TRBF_FLAG_TRTYPE_LINK | TRBF_FLAG_TC,
                          0);
            ring->next = 0;
            if(ring->end & RINGENDCFLAG)
                ring->end &= ~RINGENDCFLAG;
            else
                ring->end |= RINGENDCFLAG;
            pciusbXHCIDebugTRBV("xHCI", DEBUGCOLOR_SET "Ring Re-Linked!!" DEBUGCOLOR_RESET" \n");
        }

        queued = ring->next;
        pciusbXHCIDebugRIO("xHCI",
                           DEBUGWARNCOLOR_SET "xHCI: setting <ringio %p>[%d] 0x%p -> 0x0000000000000000" DEBUGCOLOR_RESET "\n",
                           ring->ringio, queued, ring->ringio[queued]);
        ring->ringio[queued] = NULL;
        xhciInsertTRB(hc, ring, payload, trbflags, plen);
        pciusbXHCIDebugTRBV("xHCI", DEBUGCOLOR_SET "ring %p <idx %d, %dbytes>" DEBUGCOLOR_RESET" \n", ring, ring->next, plen);
        ring->next++;
    } else {
        pciusbError("xHCI",
                    DEBUGWARNCOLOR_SET "NO SPACE ON RING!! <next = %u, last = %u>" DEBUGCOLOR_RESET" \n",
                    ring->next, (ring->end & ~RINGENDCFLAG));
    }
    xhciRingUnlock();

    return queued;
}

WORD xhciQueueTRB_IO(struct PCIController *hc, volatile struct pcisusbXHCIRing *ring, UQUAD payload,
                     ULONG plen, ULONG trbflags, struct IORequest *ioreq)
{
    WORD queued = -1;

    if(!ring) {
        pciusbError("xHCI", DEBUGWARNCOLOR_SET "NO RINGSPECIFIED!!" DEBUGCOLOR_RESET" \n");
        return -1;
    }

    if(!ring->ringio) {
        pciusbError("xHCI",
                    DEBUGWARNCOLOR_SET "xHCI: ring %p has no ringio bookkeeping" DEBUGCOLOR_RESET" \n",
                    ring);
        return -1;
    }

    xhciRingLock();
    if(xhciRingEntriesFree(ring) > 1) {
        if(ring->next >= XHCI_EVENT_RING_TRBS - 1) {
            UQUAD link_dma = (UQUAD)(IPTR)&ring->ring[0];

            pciusbXHCIDebugRIO("xHCI",
                               DEBUGWARNCOLOR_SET "xHCI: <ringio %p>[%d]  0x%p -> 0x0000000000000000" DEBUGCOLOR_RESET "\n",
                               ring->ringio, ring->next, ring->ringio[ring->next]);
            ring->ringio[ring->next] = NULL;
            xhciInsertTRB(hc, ring,
                          link_dma,
                          TRBF_FLAG_TRTYPE_LINK | TRBF_FLAG_TC,
                          0);
            ring->next = 0;
            if(ring->end & RINGENDCFLAG)
                ring->end &= ~RINGENDCFLAG;
            else
                ring->end |= RINGENDCFLAG;
            pciusbXHCIDebugTRBV("xHCI", DEBUGCOLOR_SET "Ring Re-Linked!!" DEBUGCOLOR_RESET" \n");
        }

        queued = ring->next;
        pciusbXHCIDebugRIO("xHCI",
                           DEBUGWARNCOLOR_SET "xHCI: <ringio %p>[%d] 0x%p -> 0x%p" DEBUGCOLOR_RESET "\n",
                           ring->ringio, queued, ring->ringio[queued], ioreq);
        ring->ringio[queued] = ioreq;
        xhciInsertTRB(hc, ring, payload, trbflags, plen);
        pciusbXHCIDebugTRBV("xHCI", DEBUGCOLOR_SET "ring %p <idx %d, %dbytes>" DEBUGCOLOR_RESET" \n", ring, ring->next, plen);
        ring->next++;
    } else {
        pciusbError("xHCI",
                    DEBUGWARNCOLOR_SET "NO SPACE ON RING!! <next = %u, last = %u>" DEBUGCOLOR_RESET" \n",
                    ring->next, (ring->end & ~RINGENDCFLAG));
    }
    xhciRingUnlock();

    return queued;
}

/* A buffer TRB must stay within one 64 KiB boundary in device address
 * space. The preparation path guarantees a contiguous DMA range. */
static ULONG xhciDataTRBCount(IPTR dma, ULONG bytes)
{
    if (!bytes)
        return 1;
    return 1 + (((UQUAD)(dma & 0xffff) + bytes - 1) >> 16);
}

static WORD xhciQueueDataSegments(struct PCIController *hc, volatile struct pcisusbXHCIRing *ring,
                                  UQUAD payload, IPTR dma, ULONG bytes, ULONG flags,
                                  BOOL iocOnLast, struct IORequest *ioreq)
{
    ULONG offset = 0;
    WORD first = -1;
    ULONG mps = ioreq ? ((struct IOUsbHWReq *)ioreq)->iouh_MaxPktSize : 0;
    do {
        ULONG remaining = bytes - offset;
        ULONG boundary = TRB_TPARAMS_TRBLEN_MAX - ((dma + offset) & 0xffff);
        ULONG length = remaining < boundary ? remaining : boundary;
        ULONG params = length;
        ULONG control = flags & ~TRBF_FLAG_IOC;
        if (offset && (flags & TRB_FLAG_TYPE_MASK) == TRBF_FLAG_TRTYPE_DATA)
            control = (control & ~(TRB_FLAG_TYPE_MASK | TRBF_FLAG_DS_DIR)) | TRBF_FLAG_TRTYPE_NORMAL;
        if (length < remaining) {
            ULONG tdSize = 0;
            if (!hc->hc_HCIVersionMajor)
                tdSize = remaining >> 10;
            else if (mps)
                tdSize = bytes / mps + (bytes % mps != 0) - (offset + length) / mps;
            if (tdSize > TRB_TPARAMS_DS_TDSIZE_SMASK)
                tdSize = TRB_TPARAMS_DS_TDSIZE_SMASK;
            params |= tdSize << TRBS_TPARAMS_DS_TDSIZE;
            control |= TRBF_FLAG_CH;
        } else if (iocOnLast)
            control |= TRBF_FLAG_IOC;
        WORD index = ioreq ? xhciQueueTRB_IO(hc, ring, payload + offset, params, control, ioreq)
                           : xhciQueueTRB(hc, ring, payload + offset, params, control);
        if (index < 0)
            return index;
        if (first < 0)
            first = index;
        offset += length;
    } while (offset < bytes);
    return first;
}

/*
 * Reserve the entire payload before publishing any TRB. QueueTRB also locks,
 * so retain this outer lock across admission and every segment. This uses the
 * driver's existing single-CPU Disable/Enable contract; it is not an SMP lock.
 * Simulate the primitive's guard slot and Link insertion exactly, including
 * producers positioned on the Link slot. The controller may consume entries
 * meanwhile, but software dequeue bookkeeping cannot change under this lock.
 */
static BOOL xhciEntriesFit(volatile struct pcisusbXHCIRing *ring, ULONG count)
{
    if(!ring || !ring->ringio)
        return FALSE;
    ULONG idx = ring->next;
    const ULONG last = ring->end & ~RINGENDCFLAG;
    if(idx >= XHCI_EVENT_RING_TRBS || last >= XHCI_EVENT_RING_TRBS ||
       count >= XHCI_EVENT_RING_TRBS)
        return FALSE;
    while(count--) {
        ULONG free = last > idx ? last - idx : (XHCI_EVENT_RING_TRBS - 1) - idx + last;
        if(free <= 1)
            return FALSE;
        if(idx == XHCI_EVENT_RING_TRBS - 1)
            idx = 0;
        ++idx;
    }
    return TRUE;
}

static WORD xhciQueueWholePayload(struct PCIController *hc,
                                 volatile struct pcisusbXHCIRing *ring,
                                 UQUAD payload, ULONG bytes, ULONG flags,
                                 BOOL iocOnLast, struct IORequest *ioreq)
{
    WORD result = -1;
    if(bytes && payload > ~(UQUAD)0 - (bytes - 1))
        return -1;
    xhciRingLock();
    IPTR dma = bytes ? (IPTR)xhciDeviceAddress(hc, (APTR)(IPTR)payload) : 0;
    if (xhciEntriesFit(ring, xhciDataTRBCount(dma, bytes)))
        result = xhciQueueDataSegments(hc, ring, payload, dma, bytes, flags, iocOnLast, ioreq);
    xhciRingUnlock();
    return result;
}

WORD xhciQueueData(struct PCIController *hc, volatile struct pcisusbXHCIRing *ring,
                   UQUAD payload, ULONG bytes, ULONG flags, BOOL iocOnLast)
{
    return xhciQueueWholePayload(hc, ring, payload, bytes, flags, iocOnLast, NULL);
}

WORD xhciQueueData_IO(struct PCIController *hc, volatile struct pcisusbXHCIRing *ring,
                      UQUAD payload, ULONG bytes, ULONG flags, BOOL iocOnLast,
                      struct IORequest *ioreq)
{
    return xhciQueueWholePayload(hc, ring, payload, bytes, flags, iocOnLast, ioreq);
}

/* Caller has already prepared the payload. No allocation or command wait is
 * allowed while publishing: admission covers SETUP, all DATA and STATUS.
 * All scalar queue failure conditions are checked before the first write.
 */
BOOL xhciQueueControl_IO(struct PCIController *hc, volatile struct pcisusbXHCIRing *ring,
                         UQUAD setup, UQUAD payload, ULONG bytes, ULONG flags,
                         struct IORequest *ioreq, struct xhciControlTRBs *indices)
{
    if(!ioreq || !indices || (bytes && (!payload || payload > ~(UQUAD)0 - (bytes - 1))))
        return FALSE;
    xhciRingLock();
    IPTR dma = bytes ? (IPTR)xhciDeviceAddress(hc, (APTR)(IPTR)payload) : 0;
    ULONG dataCount = bytes ? xhciDataTRBCount(dma, bytes) : 0;
    if(!xhciEntriesFit(ring, dataCount + 2)) {
        xhciRingUnlock();
        return FALSE;
    }
    /* SETUP uses only BEI from caller flags, plus IDT and the TRT field. */
    ULONG setupFlags = (flags & TRBF_FLAG_BEI) | TRBF_FLAG_TRTYPE_SETUP | TRBF_FLAG_IDT;
    if(bytes)
        setupFlags |= (flags & TRBF_FLAG_DS_DIR) ? (3UL << 16) : (2UL << 16);
    indices->setup = xhciQueueTRB_IO(hc, ring, setup, 8, setupFlags, ioreq);
    if(bytes) {
        ULONG dataFlags = (flags & ~(TRB_FLAG_TYPE_MASK | TRBF_FLAG_CH)) | TRBF_FLAG_TRTYPE_DATA;
        indices->dataFirst = xhciQueueDataSegments(hc, ring, payload, dma, bytes, dataFlags, FALSE, ioreq);
        indices->dataLast = ring->next - 1;
    } else {
        indices->dataFirst = indices->dataLast = indices->setup;
    }
    ULONG statusFlags = TRBF_FLAG_TRTYPE_STATUS | TRBF_FLAG_IOC;
    if(!bytes || !(flags & TRBF_FLAG_DS_DIR))
        statusFlags |= TRBF_FLAG_DS_DIR;
    indices->status = xhciQueueTRB_IO(hc, ring, 0, 0, statusFlags, ioreq);
    xhciRingUnlock();
    return TRUE;
}

/* This helper is only legal after Stop Endpoint acknowledgement. The command
 * owner excludes all producers/recovery on this endpoint until commit/resume. */
BOOL xhciPrepareStoppedCancellation(struct PCIController *hc, volatile struct pcisusbXHCIRing *ring,
                                    struct IORequest *ioreq, UWORD first, UWORD last,
                                    UWORD stopped, BOOL dcs, struct xhciCancelPlan *plan)
{
    const ULONG slots = XHCI_EVENT_RING_TRBS - 1; /* final TRB is Link */
    if(!ring || !ring->ringio || !ioreq || !plan || first >= slots || last >= slots || stopped > slots)
        return FALSE;
    ULONG count = (last + slots - first) % slots + 1;
    if(count >= slots) /* preserve the queue primitive's guard slot */
        return FALSE;
    ULONG current = stopped;
    BOOL cycle = !!dcs;
    if(current == slots) {
        /* Stop on Link: the next data position uses the toggled cycle. */
        current = 0;
        cycle = !cycle;
    }
    BOOL containsCurrent = FALSE;
    for(ULONG n = 0, idx = first; n < count; ++n, idx = (idx + 1) % slots) {
        if(ring->ringio[idx] != ioreq)
            return FALSE;
        if(idx == current) {
            if(!!(AROS_LE2LONG(ring->ring[idx].flags) & TRBF_FLAG_C) != cycle)
                return FALSE;
            containsCurrent = TRUE;
        }
    }
    struct xhciCancelPlan prepared = { .first = first, .last = last,
        .next = (last + 1) % slots, .moveDequeue = containsCurrent, .cycle = cycle };
    if(containsCurrent && prepared.next <= current)
        prepared.cycle = !cycle;
    for(ULONG n = 0, idx = first; n < count; ++n, idx = (idx + 1) % slots) {
        volatile struct xhci_trb *trb = &ring->ring[idx];
        ULONG flags = (AROS_LE2LONG(trb->flags) & TRBF_FLAG_C) | TRBF_FLAG_TRTYPE_NOOP;
        /* If the controller walks these No-Ops, a final event reclaims their
         * ring slots even when there is no later client transfer queued. */
        if(idx == last)
            flags |= TRBF_FLAG_IOC;
        trb->dbp.addr_lo = trb->dbp.addr_hi = 0;
        trb->tparams = 0;
        trb->flags = AROS_LONG2LE(flags);
        CacheClearE((APTR)trb, sizeof(*trb), CACRF_ClearD);
    }
    *plan = prepared;
    (void)hc;
    return TRUE;
}

BOOL xhciCommitStoppedCancellation(volatile struct pcisusbXHCIRing *ring, struct IORequest *ioreq,
                                   const struct xhciCancelPlan *plan, BOOL dequeueAcknowledged)
{
    const ULONG slots = XHCI_EVENT_RING_TRBS - 1;
    if(!ring || !ring->ringio || !ioreq || !plan || plan->first >= slots || plan->last >= slots ||
       plan->next != (plan->last + 1) % slots || (plan->moveDequeue && !dequeueAcknowledged))
        return FALSE;
    ULONG count = (plan->last + slots - plan->first) % slots + 1;
    if(count >= slots)
        return FALSE;
    for(ULONG n = 0, idx = plan->first; n < count; ++n, idx = (idx + 1) % slots) {
        if(ring->ringio[idx] != ioreq ||
           (AROS_LE2LONG(ring->ring[idx].flags) & TRB_FLAG_TYPE_MASK) != TRBF_FLAG_TRTYPE_NOOP)
            return FALSE;
    }
    for(ULONG n = 0, idx = plan->first; n < count; ++n, idx = (idx + 1) % slots)
        ring->ringio[idx] = NULL;
    if(plan->moveDequeue)
        ring->end = (ring->end & RINGENDCFLAG) | plan->next;
    return TRUE;
}
