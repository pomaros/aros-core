/*
 * Copyright (C) 2023-2026, The AROS Development Team. All rights reserved.
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Included by xhci_hcd.c and host tests of the actual completion function.
 */
/* Stop Endpoint preserves resumable transfer state. Its transfer event is
 * not a terminal IO completion (xHCI 1.2b, section 4.6.9). Only the command
 * owner may retire/rewrite the TD and then release its payload. */
static BOOL xhciStoppedCompletion(ULONG code)
{
    return code == TRB_CC_STOPPED || code == TRB_CC_STOPPED_LENGTH_INVALID ||
           code == TRB_CC_STOPPED_SHORT_PACKET;
}

/* Interrupt observation only. Keep DMA and ring correlation until the task
 * owner chooses normal retirement or cancellation. The first terminal event
 * wins; later observations cannot copy/free a payload twice. */
BOOL xhciIntWorkProcess(struct PCIController *hc, struct IOUsbHWReq *ioreq, ULONG remaining, ULONG ccode)
{
    (void)hc;
    if(!ioreq || ccode == TRB_CC_INVALID || xhciStoppedCompletion(ccode))
        return FALSE;
    struct pciusbXHCIIODevPrivate *priv = ioreq->iouh_DriverPrivate1;
    if(priv) {
        if(priv->dpCC != TRB_CC_INVALID)
            return FALSE;
        priv->dpRemaining = remaining;
        priv->dpCC = ccode;
        return TRUE;
    }
    /* Legacy asynchronous command requests have no transfer payload private
     * record. Synchronous commands use xhc_CmdResults, not this path. */
    ioreq->iouh_Req.io_Error = (ccode == TRB_CC_SUCCESS) ? 0 : UHIOERR_HOSTERROR;
    ioreq->iouh_Actual += ioreq->iouh_Length - remaining;
    return TRUE;
}

/* Event-task consumer. Its caller owns the live request. Cancellation will
 * defer this consumer while it owns the endpoint; recording an event above
 * never transfers that ownership or authorizes an early client reply. */
static BOOL xhciRetireCompletion(struct PCIController *hc, struct IOUsbHWReq *ioreq)
{
    /* Until every external abort/teardown path shares task ownership, retain
     * the exclusion that payload release previously had in IRQ context. */
    Disable();
    struct pciusbXHCIIODevPrivate *driprivate = ioreq->iouh_DriverPrivate1;
    if(!driprivate || driprivate->dpCC == TRB_CC_INVALID || xhciStoppedCompletion(driprivate->dpCC)) {
        Enable();
        return FALSE;
    }
    if(driprivate->dpCompletionRetired) {
        Enable();
        return TRUE;
    }
    ULONG ccode = driprivate->dpCC;
    ULONG remaining = driprivate->dpRemaining;
    driprivate->dpCompletionRetired = TRUE;
    {
        UWORD effdir = xhciEffectiveDataDir(ioreq);
        pciusbXHCIDebugTRBV("xHCI", DEBUGCOLOR_SET "IOReq TRB(s) = #%u:#%u" DEBUGCOLOR_RESET" \n", driprivate->dpTxSTRB,
                            driprivate->dpTxETRB);
        pciusbXHCIDebugTRBV("xHCI", DEBUGCOLOR_SET "          Ring    @ 0x%p" DEBUGCOLOR_RESET" \n",
                            driprivate->dpDevice->dc_EPAllocs[driprivate->dpEPID].dmaa_Ptr);

        /* Avoid log storms for expected ISO conditions and short packets */
        if((ccode != TRB_CC_SUCCESS) && (ccode != TRB_CC_SHORT_PACKET) &&
                !(ccode == TRB_CC_RING_UNDERRUN && (ioreq->iouh_Req.io_Command == UHCMD_ISOXFER))) {
            pciusbWarn("xHCI",
                       DEBUGWARNCOLOR_SET
                       "cc=%d for IOReq 0x%p"
                       DEBUGCOLOR_RESET" \n", (int)ccode, ioreq);
        }

        /*
         * On error completions, dump a concise view of the endpoint context.
         * This is particularly useful to spot endpoints that remain HALTED /
         * STOPPED while new TDs are being submitted.
         */
        if((ccode != TRB_CC_SUCCESS) && (ccode != TRB_CC_SHORT_PACKET) &&
                !(ccode == TRB_CC_RING_UNDERRUN && (ioreq->iouh_Req.io_Command == UHCMD_ISOXFER))) {
            xhciDiagDumpEndpointBrief(hc, driprivate->dpDevice, (UBYTE)driprivate->dpEPID, "completion-error");
            xhciDumpEndpointCtx(hc, driprivate->dpDevice, driprivate->dpEPID, "completion-error");
        }

        ULONG transferred = (remaining <= ioreq->iouh_Length)
                            ? (ioreq->iouh_Length - remaining)
                            : ioreq->iouh_Length;

        /*
         * For periodic ISO OUT, Ring Underrun indicates that no data was
         * serviced in time. Report 0 bytes to the client and continue.
         */
        if((ccode == TRB_CC_RING_UNDERRUN) &&
                (ioreq->iouh_Req.io_Command == UHCMD_ISOXFER) &&
                (ioreq->iouh_Dir == UHDIR_OUT)) {
            transferred = 0;
            remaining   = ioreq->iouh_Length;
        }

        /* Clear ringio bookkeeping */
        if(driprivate->dpDevice) {
            struct pciusbXHCIDevice *devCtx = driprivate->dpDevice;
            struct pcisusbXHCIRing *epRing = devCtx->dc_EPAllocs[driprivate->dpEPID].dmaa_Ptr;
            int cnt;

            if(epRing) {
                if(driprivate->dpSTRB != (UWORD) - 1) {
                    if(xhciRingioMatchesIOReq(epRing, driprivate->dpSTRB, ioreq)) {
                        pciusbXHCIDebugRIO("xHCI", DEBUGCOLOR_SET "%s: IOReq 0x%p - setup <ringio %p>[%d] 0x%p -> 0x0000000000000000"
                                           DEBUGCOLOR_RESET" \n",
                                           __func__, ioreq, epRing->ringio, driprivate->dpSTRB,
                                           epRing->ringio[driprivate->dpSTRB]);
                        epRing->ringio[driprivate->dpSTRB] = NULL;
                    }
                }

                /* dpTxSTRB/dpTxETRB can legally be -1 (no data stage). */
                if((driprivate->dpTxSTRB != (UWORD) - 1) && (driprivate->dpTxETRB != (UWORD) - 1)) {
                    if(driprivate->dpTxETRB >= driprivate->dpTxSTRB) {
                        for(cnt = driprivate->dpTxSTRB; cnt <= driprivate->dpTxETRB; cnt++) {
                            if(xhciRingioMatchesIOReq(epRing, (UWORD)cnt, ioreq)) {
                                pciusbXHCIDebugRIO("xHCI", DEBUGCOLOR_SET "%s: IOReq 0x%p - data <ringio %p>[%d] 0x%p -> 0x0000000000000000"
                                                   DEBUGCOLOR_RESET" \n",
                                                   __func__, ioreq, epRing->ringio, cnt, epRing->ringio[cnt]);
                                epRing->ringio[cnt] = NULL;
                            }
                        }
                    } else {
                        /* Wrapped range */
                        for(cnt = driprivate->dpTxSTRB; cnt < (XHCI_EVENT_RING_TRBS - 1); cnt++) {
                            if(xhciRingioMatchesIOReq(epRing, (UWORD)cnt, ioreq)) {
                                pciusbXHCIDebugRIO("xHCI", DEBUGCOLOR_SET "%s: IOReq 0x%p - data <ringio %p>[%d] 0x%p -> 0x0000000000000000"
                                                   DEBUGCOLOR_RESET" \n",
                                                   __func__, ioreq, epRing->ringio, cnt, epRing->ringio[cnt]);
                                epRing->ringio[cnt] = NULL;
                            }
                        }
                        for(cnt = 0; cnt <= driprivate->dpTxETRB; cnt++) {
                            if(xhciRingioMatchesIOReq(epRing, (UWORD)cnt, ioreq)) {
                                pciusbXHCIDebugRIO("xHCI", DEBUGCOLOR_SET "%s: IOReq 0x%p - data <ringio %p>[%d] 0x%p -> 0x0000000000000000"
                                                   DEBUGCOLOR_RESET" \n",
                                                   __func__, ioreq, epRing->ringio, cnt, epRing->ringio[cnt]);
                                epRing->ringio[cnt] = NULL;
                            }
                        }
                    }
                }

                if(driprivate->dpSttTRB != (UWORD) - 1) {
                    if(xhciRingioMatchesIOReq(epRing, driprivate->dpSttTRB, ioreq)) {
                        pciusbXHCIDebugRIO("xHCI", DEBUGCOLOR_SET "%s:  IOReq 0x%p - status <ringio %p>[%d] 0x%p -> 0x0000000000000000"
                                           DEBUGCOLOR_RESET" \n",
                                           __func__, ioreq, epRing->ringio, driprivate->dpSttTRB,
                                           epRing->ringio[driprivate->dpSttTRB]);
                        epRing->ringio[driprivate->dpSttTRB] = NULL;
                    }
                }
            }
        }

        /*
         * Release any bounce buffer regardless of completion code to avoid
         * leaks and to complete CachePostDMA on partial transfers.
         */
        if(ioreq->iouh_Data) {
            xhciReleaseDMABuffer(hc, ioreq, transferred, effdir, driprivate->dpBounceBuf);
            driprivate->dpBounceBuf = NULL;
            driprivate->dpBounceData = NULL;
            driprivate->dpBounceLen = 0;
            driprivate->dpBounceDir = 0;
        }

        ioreq->iouh_Actual = transferred;
        pciusbXHCIDebugTRBV("xHCI", DEBUGCOLOR_SET "IOReq Transfer done, %u bytes <io length %u, %u remaining>"
                            DEBUGCOLOR_RESET" \n", transferred, ioreq->iouh_Length, remaining);

        Enable();
        return TRUE;
    }

}
