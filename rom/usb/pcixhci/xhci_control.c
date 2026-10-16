/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include <proto/exec.h>
#include "uhwcmd.h"
#include "xhci_schedule.h"

/* Called under the outer ring lock, with all correlation/DMA metadata ready.
 * Complete list membership and ring the doorbell before permitting any guest
 * completion IRQ. After unlocking, the IO and private record may be freed by
 * the completion task: neither this helper's caller nor the scheduler may
 * dereference them on the success path.
 */
static void xhciPublishRequest(struct PCIController *hc, struct IOUsbHWReq *ioreq,
                               struct pciusbXHCIIODevPrivate *priv)
{
    struct List *active = &hc->hc_TDQueue;
    if(ioreq->iouh_Req.io_Command == UHCMD_INTXFER || ioreq->iouh_Req.io_Command == UHCMD_ISOXFER)
        active = &hc->hc_PeriodicTDQueue;
    if(ioreq->iouh_Req.io_Command == UHCMD_ISOXFER && ioreq->iouh_DriverPrivate2) {
        struct PTDNode *ptd = ioreq->iouh_DriverPrivate2;
        ptd->ptd_Flags |= PTDF_ACTIVE;
        ptd->ptd_Flags &= ~PTDF_BUFFER_VALID;
    }
    ioreq->iouh_Req.io_Error = 0;
    ioreq->iouh_Actual = 0;
    priv->dpCC = TRB_CC_INVALID;
    priv->dpRemaining = 0;
    priv->dpCompletionRetired = FALSE;
    AddTail(active, &ioreq->iouh_Req.io_Message.mn_Node);
    xhciRingDoorbell(hc, priv->dpDevice->dc_SlotID, priv->dpEPID);
}

BOOL xhciQueueControlStages(struct PCIController *hc, struct IOUsbHWReq *ioreq,
                            struct pciusbXHCIIODevPrivate *priv,
                            volatile struct pcisusbXHCIRing *ring, ULONG flags)
{
    APTR bounce = NULL, cpu = NULL;
    ULONG bytes = ioreq->iouh_Length;
    UWORD direction = xhciEffectiveDataDir(ioreq);
    struct xhciControlTRBs indices;
    UQUAD setup;
    /* Pack the eight USB setup bytes explicitly, independent of CPU endian. */
    const UBYTE *raw = (const UBYTE *)&ioreq->iouh_SetupData;
    setup = 0;
    for(unsigned i = 0; i < 8; ++i)
        setup |= (UQUAD)raw[i] << (8 * i);

    /* Preparation can allocate or call the PCI owner. It must precede both
     * SETUP publication and the ring's interrupt-masked admission section. */
    if(bytes) {
        if(!ioreq->iouh_Data)
            goto invalid;
        cpu = xhciPrepareDMABuffer(hc, ioreq, &bytes, direction, &bounce);
        if(!cpu)
            goto invalid;
        if(bytes != ioreq->iouh_Length) {
            xhciDiscardPreparedDMABuffer(hc, ioreq, cpu, bytes, direction, bounce);
            goto invalid;
        }
    }
    /* Keep completion interrupts out until the correlation indices and
     * prepared-buffer ownership are installed together. */
    xhciRingLock();
    if(!xhciQueueControl_IO(hc, ring, setup, (UQUAD)(IPTR)cpu, bytes, flags,
                            &ioreq->iouh_Req, &indices)) {
        xhciRingUnlock();
        if(cpu)
            xhciDiscardPreparedDMABuffer(hc, ioreq, cpu, bytes, direction, bounce);
        return FALSE;
    }
    priv->dpSTRB = indices.setup;
    priv->dpTxSTRB = indices.dataFirst;
    priv->dpTxETRB = indices.dataLast;
    priv->dpSttTRB = indices.status;
    priv->dpBounceBuf = bounce;
    priv->dpBounceData = bounce ? ioreq->iouh_Data : NULL;
    priv->dpBounceLen = bytes;
    priv->dpBounceDir = direction;
    xhciPublishRequest(hc, ioreq, priv);
    xhciRingUnlock();
    return TRUE;
invalid:
    ioreq->iouh_Req.io_Error = UHIOERR_HOSTERROR;
    return FALSE;
}

WORD xhciQueuePayloadTRBs(struct PCIController *hc, struct IOUsbHWReq *ioreq,
                          struct pciusbXHCIIODevPrivate *priv, volatile struct pcisusbXHCIRing *ring,
                          ULONG flags, BOOL iocOnLast)
{
    ULONG bytes = ioreq->iouh_Length;
    UWORD direction = xhciEffectiveDataDir(ioreq);
    APTR bounce = NULL;
    APTR cpu = xhciPrepareDMABuffer(hc, ioreq, &bytes, direction, &bounce);
    if((!cpu && bytes) || bytes != ioreq->iouh_Length) {
        if(cpu)
            xhciDiscardPreparedDMABuffer(hc, ioreq, cpu, bytes, direction, bounce);
        ioreq->iouh_Req.io_Error = UHIOERR_HOSTERROR;
        return -1;
    }
    xhciRingLock();
    WORD queued = xhciQueueData_IO(hc, ring, (UQUAD)(IPTR)cpu, bytes, flags,
                                   iocOnLast, &ioreq->iouh_Req);
    if(queued != -1) {
        priv->dpTxSTRB = queued;
        priv->dpTxETRB = ring->next - 1;
        priv->dpBounceBuf = bounce;
        priv->dpBounceData = bounce ? ioreq->iouh_Data : NULL;
        priv->dpBounceLen = bytes;
        priv->dpBounceDir = direction;
        xhciPublishRequest(hc, ioreq, priv);
    }
    xhciRingUnlock();
    if(queued == -1)
        xhciDiscardPreparedDMABuffer(hc, ioreq, cpu, bytes, direction, bounce);
    return queued;
}
