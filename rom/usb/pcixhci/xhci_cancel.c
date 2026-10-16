/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include <proto/exec.h>
#include "uhwcmd.h"
#include "xhciproto.h"
#include "xhci_hcd.h"

/* Task context only. The caller pins the request, device and ring and excludes
 * endpoint producers, recovery, teardown and completion retirement throughout
 * this operation, INCLUDING command waits. Interrupts must remain enabled
 * while waiting. Transfer events may be recorded, but must not clear ringio or
 * release payloads until this owner returns. These are lifetime requirements,
 * not provided by the short ring lock below.
 *
 * TRUE means this request no longer has executable descriptors on the ring.
 * FALSE requires quarantine: retain ownership and DMA, and do not resume the
 * endpoint. A timed-out command can still execute later. No path here frees,
 * replies, rings an endpoint doorbell or changes the client's IO result.
 */
BOOL xhciRetireCancelledRequest(struct PCIController *hc, struct IOUsbHWReq *io,
                                struct timerequest *timerreq)
{
    if(!hc || !io || !timerreq || !io->iouh_DriverPrivate1)
        return FALSE;
    struct pciusbXHCIIODevPrivate *priv = io->iouh_DriverPrivate1;
    struct pciusbXHCIDevice *dev = priv->dpDevice;
    ULONG epid = priv->dpEPID;
    if(!dev || !dev->dc_SlotID || !epid || epid >= MAX_DEVENDPOINTS ||
       !dev->dc_SlotCtx.dmaa_Ptr)
        return FALSE;
    volatile struct pcisusbXHCIRing *ring = dev->dc_EPAllocs[epid].dmaa_Ptr;
    IPTR dma = (IPTR)dev->dc_EPAllocs[epid].dmaa_DMA;
    const IPTR ringBytes = sizeof(ring->ring);
    if(!ring || !ring->ringio || !dma || (dma & 15) ||
       dma > (IPTR)-1 - (ringBytes - 1))
        return FALSE;
    UWORD first = io->iouh_Req.io_Command == UHCMD_CONTROLXFER ? priv->dpSTRB : priv->dpTxSTRB;
    UWORD last = io->iouh_Req.io_Command == UHCMD_CONTROLXFER ? priv->dpSttTRB : priv->dpTxETRB;
    if(first >= XHCI_EVENT_RING_TRBS - 1 || last >= XHCI_EVENT_RING_TRBS - 1)
        return FALSE;

    const ULONG ctxBytes = (hc->hc_Flags & HCF_CTX64) ? 64 : 32;
    volatile struct xhci_ep *ctx = (volatile struct xhci_ep *)
        ((UBYTE *)dev->dc_SlotCtx.dmaa_Ptr + epid * ctxBytes);
    CacheClearE((APTR)ctx, ctxBytes, CACRF_InvalidateD);
    ULONG state = AROS_LE2LONG(ctx->ctx[0]) & 7;
    LONG cc;
    if(state == 1 || state == 3) /* Running / Stopped: obtain command acknowledgement. */
        cc = xhciCmdEndpointStop(hc, dev->dc_SlotID, epid, 0, timerreq);
    else if(state == 2) /* Halted: Reset Endpoint transfers it to Stopped. */
        cc = xhciCmdEndpointReset(hc, dev->dc_SlotID, epid, 0, timerreq);
    else
        return FALSE;
    if(cc != TRB_CC_SUCCESS)
        return FALSE;

    /* Read the hardware's stopped position, not the software producer or the
     * last transfer event (which may precede a partially executed TRB). */
    CacheClearE((APTR)ctx, ctxBytes, CACRF_InvalidateD);
    if((AROS_LE2LONG(ctx->ctx[0]) & 7) != 3)
        return FALSE;
    UQUAD dequeue = ((UQUAD)AROS_LE2LONG(ctx->deq.addr_hi) << 32) |
                    AROS_LE2LONG(ctx->deq.addr_lo);
    /* Streams are not supported by this driver's single-ring cancellation. */
    if(dequeue & 14)
        return FALSE;
    UQUAD pointer = dequeue & ~(UQUAD)15;
    if(pointer < dma || pointer - dma >= ringBytes)
        return FALSE;
    UWORD stopped = (pointer - dma) / sizeof(struct xhci_trb);
    struct xhciCancelPlan plan;
    xhciRingLock();
    BOOL prepared = xhciPrepareStoppedCancellation(hc, ring, &io->iouh_Req,
                                                 first, last, stopped, dequeue & 1, &plan);
    xhciRingUnlock();
    if(!prepared)
        return FALSE;

    if(plan.moveDequeue) {
        /* dmaa_DMA is already a bus address. Set TR Dequeue command insertion
         * accepts raw DMA; do not pass it through the CPU-pointer wrapper. */
        IPTR nextDMA = dma + plan.next * sizeof(struct xhci_trb);
        ULONG flags = ((ULONG)dev->dc_SlotID << 24) | (epid << 16) |
                      TRBF_FLAG_CRTYPE_SET_TR_DEQUEUE_PTR;
        cc = xhciCmdSubmit(hc, (APTR)(nextDMA | !!plan.cycle), flags, NULL, timerreq);
        if(cc != TRB_CC_SUCCESS)
            return FALSE;
    }
    xhciRingLock();
    BOOL committed = xhciCommitStoppedCancellation(ring, &io->iouh_Req, &plan, TRUE);
    xhciRingUnlock();
    return committed;
}
