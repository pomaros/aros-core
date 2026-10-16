/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include <proto/exec.h>
#include "uhwcmd.h"
#include "xhci_schedule.h"

/* Activation only acquires software ownership. The caller already removed
 * the request from its pending queue and owns all reply/requeue decisions. */
BOOL xhciActivateEndpointTransfer(struct PCIController *hc, struct PCIUnit *unit,
                                  struct IOUsbHWReq *ioreq, struct pciusbXHCIIODevPrivate *priv,
                                  UWORD key, volatile struct pcisusbXHCIRing **ringOut)
{
    (void)hc;
    *ringOut = NULL;
    Disable();
    if(key < USB_DEVEP_CNT && priv && priv->dpDevice && priv->dpEPID < MAX_DEVENDPOINTS)
        *ringOut = priv->dpDevice->dc_EPAllocs[priv->dpEPID].dmaa_Ptr;
    if(*ringOut) {
        ioreq->iouh_DriverPrivate1 = priv;
        unit->hu_DevBusyReq[key] = ioreq;
    }
    Enable();
    if(!*ringOut)
        ioreq->iouh_Req.io_Error = UHIOERR_HOSTERROR;
    return *ringOut != NULL;
}

/* ONLY for all-or-none submission rejection, before any referencing TRB was
 * published. Never use this as an abort of an active DMA transfer. */
void xhciRollbackUnsubmitted(struct PCIUnit *unit, struct IOUsbHWReq *ioreq,
                             struct pciusbXHCIIODevPrivate *priv, UWORD key)
{
    Disable();
    if(key < USB_DEVEP_CNT && (!unit->hu_DevBusyReq[key] || unit->hu_DevBusyReq[key] == ioreq)) {
        unit->hu_DevBusyReq[key] = NULL;
        unit->hu_NakTimeoutFrame[key] = 0;
    }
    if(priv) {
        priv->dpSTRB = priv->dpTxSTRB = priv->dpTxETRB = priv->dpSttTRB = (UWORD)-1;
    }
    Enable();
}
