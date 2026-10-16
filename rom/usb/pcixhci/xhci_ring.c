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
            dma_payload = (UQUAD)(IPTR)CPUTOPCI(hc, hc->hc_PCIDriverObject, (APTR)(IPTR)payload);
        }
#endif
        /*
         * Set TR Dequeue Pointer uses a DMA address with embedded flag bits
         * (e.g. DCS in bit 0). Do not treat that value as a CPU pointer.
         */
        xhciSetPointer(hc, dst->dbp, dma_payload);
    }

    dst->tparams = AROS_LONG2LE(plen & TRB_TPARAMS_DS_TRBLEN_SMASK);
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

static WORD xhciQueueDataUnchecked(struct PCIController *hc,
                   volatile struct pcisusbXHCIRing *ring,
                   UQUAD payload,
                   ULONG plen,
                   ULONG trbflags,
                   BOOL  ioconlast)
{
    ULONG remaining = plen;
    WORD  queued, firstqueued = -1;

    /* Caller-selected TRB type (NORMAL vs. DATA stage, etc.). */
    const ULONG base_type = (trbflags & TRB_FLAG_TYPE_MASK);

    /*
     * The caller may supply TRBF_FLAG_CH to indicate that the final TRB
     * generated by this helper must remain chained to a subsequent TRB that
     * is queued by the caller (e.g. Control DATA stage chained into STATUS).
     */
    ULONG base_flags = (trbflags & ~TRB_FLAG_TYPE_MASK);
    const BOOL chain_beyond = (base_flags & TRBF_FLAG_CH) != 0;

    /*
     * CH/IOC are managed locally per segment.  Do not inherit IOC directly
     * from the caller; use ioconlast for that policy.
     */
    base_flags &= ~(TRBF_FLAG_CH | TRBF_FLAG_IOC);

    /*
     * xHCI limits the per-TRB transfer length field to 17 bits (and 64K of
     * max size). Do not split by MaxPacketSize; xHCI TRBs are not packet-sized.
     */
    const ULONG segmax = TRB_TPARAMS_TRBLEN_MAX;

    /*
     * If this is a Control DATA Stage TRB, only the first segment is a DATA
     * TRB; any additional segments must be NORMAL TRBs chained to it.
     */
    const BOOL is_ctl_data_stage = (base_type == TRBF_FLAG_TRTYPE_DATA);

    /* Zero-length payload: queue a single TRB with len=0. */
    if(remaining == 0) {
        ULONG txflags = base_flags | base_type;

        if(chain_beyond)
            txflags |= TRBF_FLAG_CH;

        if(ioconlast)
            txflags |= TRBF_FLAG_IOC;

        return xhciQueueTRB(hc, ring, payload, 0, txflags);
    }

    while(remaining > 0) {
        const ULONG offset = (plen - remaining);
        ULONG trblen = remaining;
        ULONG txflags;

        if(trblen > segmax)
            trblen = segmax;

        /* Select TRB type for this segment. */
        if(is_ctl_data_stage && offset != 0) {
            /*
             * Subsequent segments of a Control DATA stage use NORMAL TRBs.
             * Clear the DATA-stage-specific direction bit if present.
             */
            txflags = (base_flags & ~TRBF_FLAG_DS_DIR) | TRBF_FLAG_TRTYPE_NORMAL;
        } else {
            txflags = base_flags | base_type;
        }

        /* Chain all but the last segment; optionally chain the final segment too. */
        if(remaining > trblen)
            txflags |= TRBF_FLAG_CH;
        else if(chain_beyond)
            txflags |= TRBF_FLAG_CH;

        /* IOC only on the last segment if requested. */
        if(ioconlast && (remaining == trblen))
            txflags |= TRBF_FLAG_IOC;

        queued = xhciQueueTRB(hc, ring, payload + offset, trblen, txflags);
        if(queued == -1)
            return queued;

        if(firstqueued == -1)
            firstqueued = queued;

        remaining -= trblen;
    }

    return firstqueued;
}

static WORD xhciQueueDataIOUnchecked(struct PCIController *hc,
                      volatile struct pcisusbXHCIRing *ring,
                      UQUAD payload,
                      ULONG plen,
                      ULONG trbflags,
                      BOOL  ioconlast,
                      struct IORequest *ioreq)
{
    ULONG remaining = plen;
    WORD  queued, firstqueued = -1;

    const ULONG base_type = (trbflags & TRB_FLAG_TYPE_MASK);
    ULONG base_flags = (trbflags & ~TRB_FLAG_TYPE_MASK);
    const BOOL chain_beyond = (base_flags & TRBF_FLAG_CH) != 0;

    base_flags &= ~(TRBF_FLAG_CH | TRBF_FLAG_IOC);

    const ULONG segmax = TRB_TPARAMS_TRBLEN_MAX;

    const BOOL is_ctl_data_stage = (base_type == TRBF_FLAG_TRTYPE_DATA);

    if(remaining == 0) {
        ULONG txflags = base_flags | base_type;

        if(chain_beyond)
            txflags |= TRBF_FLAG_CH;

        if(ioconlast)
            txflags |= TRBF_FLAG_IOC;

        return xhciQueueTRB_IO(hc, ring, payload, 0, txflags, ioreq);
    }

    while(remaining > 0) {
        const ULONG offset = (plen - remaining);
        ULONG trblen = remaining;
        ULONG txflags;

        if(trblen > segmax)
            trblen = segmax;

        if(is_ctl_data_stage && offset != 0) {
            txflags = (base_flags & ~TRBF_FLAG_DS_DIR) | TRBF_FLAG_TRTYPE_NORMAL;
        } else {
            txflags = base_flags | base_type;
        }

        if(remaining > trblen)
            txflags |= TRBF_FLAG_CH;
        else if(chain_beyond)
            txflags |= TRBF_FLAG_CH;

        if(ioconlast && (remaining == trblen))
            txflags |= TRBF_FLAG_IOC;

        queued = xhciQueueTRB_IO(hc, ring, payload + offset, trblen, txflags, ioreq);
        if(queued == -1)
            return queued;

        if(firstqueued == -1)
            firstqueued = queued;

        remaining -= trblen;
    }

    return firstqueued;
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
    const ULONG max = TRB_TPARAMS_TRBLEN_MAX;
    const ULONG count = bytes ? bytes / max + ((bytes % max) != 0) : 1;
    if(xhciEntriesFit(ring, count)) {
        if(ioreq)
            result = xhciQueueDataIOUnchecked(hc, ring, payload, bytes, flags, iocOnLast, ioreq);
        else
            result = xhciQueueDataUnchecked(hc, ring, payload, bytes, flags, iocOnLast);
    }
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
    const ULONG max = TRB_TPARAMS_TRBLEN_MAX;
    const ULONG dataCount = bytes / max + ((bytes % max) != 0);
    if(!ioreq || !indices || (bytes && (!payload || payload > ~(UQUAD)0 - (bytes - 1))))
        return FALSE;
    xhciRingLock();
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
        indices->dataFirst = xhciQueueDataIOUnchecked(hc, ring, payload, bytes, dataFlags, FALSE, ioreq);
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
