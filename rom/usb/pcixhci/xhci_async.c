/*
    Copyright (C) 2023-2026, The AROS Development Team. All rights reserved

    Desc: xHCI chipset driver async transfer support functions
*/

#include <proto/exec.h>
#include <proto/poseidon.h>
#include <proto/oop.h>
#include <hidd/pci.h>
#include <aros/cpu.h>

#include <devices/usb_hub.h>

#include <string.h>

#include "uhwcmd.h"
#include "xhciproto.h"
#include "xhci_schedule.h"

#if defined(DEBUG) && defined(XHCI_LONGDEBUGNAK)
#define XHCI_NAKTOSHIFT         (8)
#else
#define XHCI_NAKTOSHIFT         (3)
#endif

#ifdef base
#undef base
#endif
#define base (hc->hc_Device)
#if defined(AROS_USE_LOGRES)
#ifdef LogHandle
#undef LogHandle
#endif
#ifdef LogResBase
#undef LogResBase
#endif
#define LogHandle (hc->hc_LogRHandle)
#define LogResBase (base->hd_LogResBase)
#endif

void xhciFreeAsyncContext(struct PCIController *hc, struct PCIUnit *unit, struct IOUsbHWReq *ioreq)
{
    pciusbXHCIDebug("xHCI", DEBUGFUNCCOLOR_SET "%s()" DEBUGCOLOR_RESET" \n", __func__);

    /* Deactivate the endpoint */
    xhciFinishRequest(hc, unit, ioreq);
}

static BOOL isStandardTRBTransfer(struct IOUsbHWReq *ioreq, ULONG txtype)
{
    const UBYTE bm = ioreq->iouh_SetupData.bmRequestType;
    const UBYTE br = ioreq->iouh_SetupData.bRequest;

    if(txtype != UHCMD_CONTROLXFER)
        return TRUE;

    /* Special-case: Standard Device request: SET_ADDRESS */
    if(bm == (URTF_STANDARD | URTF_DEVICE) &&
            br == USR_SET_ADDRESS) {
        return FALSE;
    }

    return TRUE;
}

void xhciScheduleAsyncTDs(struct PCIController *hc, struct List *txlist, ULONG txtype)
{
    struct PCIUnit *unit = hc->hc_Unit;
    struct IOUsbHWReq *ioreq, *ionext;
    UWORD devadrep;

    pciusbXHCIDebug("xHCI", DEBUGFUNCCOLOR_SET "%s()" DEBUGCOLOR_RESET" txtype=%lx, txlist=%p\n",
                    __func__, txtype, txlist);

#if defined(DEBUG) && (DEBUG > 1)
    {
        struct Task *thisTask = FindTask(NULL);
        pciusbXHCIDebugV("xHCI",
                         DEBUGCOLOR_SET "Task @ 0x%p, IDnest %d TDNest %d" DEBUGCOLOR_RESET" \n",
                         thisTask, thisTask->tc_IDNestCnt, thisTask->tc_TDNestCnt);
    }
#endif

    /* *** Schedule Transfers *** */
    pciusbXHCIDebug("xHCI",
                    DEBUGCOLOR_SET "Scheduling new Async transfers (Type %lx)..." DEBUGCOLOR_RESET" \n",
                    txtype);

    ForeachNodeSafe(txlist, ioreq, ionext) {
        pciusbXHCIDebugV("xHCI",
                         DEBUGCOLOR_SET "---- IOReq start: %p ----" DEBUGCOLOR_RESET" \n",
                         ioreq);

        UWORD effdir = xhciEffectiveDataDir(ioreq);
        devadrep = xhciDevEPKey(ioreq);

        pciusbXHCIDebug("xHCI",
                        DEBUGCOLOR_SET "New Async transfer to dev=%u ep=%u (DevEP=%02lx): len=%lu, dir=%s, cmd=%u" DEBUGCOLOR_RESET" \n",
                        ioreq->iouh_DevAddr,
                        ioreq->iouh_Endpoint,
                        devadrep,
                        (ULONG)ioreq->iouh_Length,
                        (effdir == UHDIR_IN) ? "IN" : "OUT",
                        ioreq->iouh_Req.io_Command);

        pciusbXHCIDebugV("xHCI",
                         "    Flags=0x%08lx, NakTO=%u, MaxPkt=%u, Interval=%u\n",
                         ioreq->iouh_Flags,
                         ioreq->iouh_NakTimeout,
                         ioreq->iouh_MaxPktSize,
                         ioreq->iouh_Interval);

        if(ioreq->iouh_Req.io_Command == UHCMD_CONTROLXFER) {
            pciusbXHCIDebugV("xHCI",
                             "    SETUP: bmReqType=0x%02x bReq=0x%02x wValue=0x%04x wIndex=0x%04x wLength=%u\n",
                             ioreq->iouh_SetupData.bmRequestType,
                             ioreq->iouh_SetupData.bRequest,
                             (unsigned)AROS_LE2WORD(ioreq->iouh_SetupData.wValue),
                             (unsigned)AROS_LE2WORD(ioreq->iouh_SetupData.wIndex),
                             (unsigned)AROS_LE2WORD(ioreq->iouh_SetupData.wLength));
        }

        /* is endpoint already in use or do we have to wait for next transaction */
        if(unit->hu_DevBusyReq[devadrep]) {
            pciusbWarn("xHCI",
                       DEBUGWARNCOLOR_SET "DevEP %02lx in use, IOReq=%p requeued/left in list" DEBUGCOLOR_RESET" \n",
                       devadrep, ioreq);
            continue;
        }

        volatile struct pcisusbXHCIRing *epring = NULL;
        struct pciusbXHCIIODevPrivate *driprivate = (struct pciusbXHCIIODevPrivate *)ioreq->iouh_DriverPrivate1;
        struct pciusbXHCIDevice *devCtx = NULL;
        ULONG trbflags = 0;
        WORD queued = -1;
        BOOL txdone = FALSE;

        if(!driprivate || !driprivate->dpDevice) {
            pciusbError("xHCI",
                        DEBUGWARNCOLOR_SET "xHCI: Missing prepared transfer context for Dev=%u EP=%u" DEBUGCOLOR_RESET" \n",
                        ioreq->iouh_DevAddr, ioreq->iouh_Endpoint);
            Remove(&ioreq->iouh_Req.io_Message.mn_Node);
            ioreq->iouh_Req.io_Error = UHIOERR_HOSTERROR;
            xhciRollbackUnsubmitted(unit, ioreq, driprivate, devadrep);
            ioreq->iouh_DriverPrivate1 = NULL;
            if(driprivate)
                FreeMem(driprivate, sizeof(*driprivate));
            ReplyMsg(&ioreq->iouh_Req.io_Message);
            continue;
        }

        devCtx = driprivate->dpDevice;

        if(isStandardTRBTransfer(ioreq, txtype)) {
            pciusbXHCIDebugV("xHCI",
                             "Normal transfer path: txtype=%lx, DevAddr=%u, EP=%u\n",
                             txtype, ioreq->iouh_DevAddr, ioreq->iouh_Endpoint);

            trbflags |= xhciBuildDataTRBFlags(ioreq, txtype);

            Remove(&ioreq->iouh_Req.io_Message.mn_Node);
            unit->hu_NakTimeoutFrame[devadrep] =
                (ioreq->iouh_Flags & UHFF_NAKTIMEOUT) ? hc->hc_FrameCounter + (ioreq->iouh_NakTimeout << XHCI_NAKTOSHIFT) : 0;
            pciusbXHCIDebugV("xHCI",
                             DEBUGCOLOR_SET "Frame=%u: Nak timeout for DevEP=%02lx set to %u" DEBUGCOLOR_RESET" \n",
                             hc->hc_FrameCounter, devadrep,
                             unit->hu_NakTimeoutFrame[devadrep]);

            if(txtype != UHCMD_CONTROLXFER) {
                driprivate->dpSTRB = (UWORD)-1;
                driprivate->dpSttTRB = (UWORD)-1;
            }

            if(xhciActivateEndpointTransfer(hc, unit, ioreq, driprivate, devadrep, &epring)) {
                if(txtype == UHCMD_CONTROLXFER) {
                    txdone = xhciQueueControlStages(hc, ioreq, driprivate, epring, trbflags);
                } else {
                    queued = xhciQueuePayloadTRBs(hc, ioreq, driprivate, epring, trbflags, TRUE);
                    if(queued != -1)
                        txdone = TRUE;
                }
            }

            if(!txdone) {
                pciusbError("xHCI",
                            DEBUGWARNCOLOR_SET "xHCI: txdone=FALSE, failed to submit transaction (Dev=%u EP=%u, txtype=%lx)" DEBUGCOLOR_RESET" \n",
                            ioreq->iouh_DevAddr, ioreq->iouh_Endpoint, txtype);

                xhciRollbackUnsubmitted(unit, ioreq, driprivate, devadrep);

                if(ioreq->iouh_Req.io_Error) {
                    ioreq->iouh_DriverPrivate1 = NULL;
                    FreeMem(driprivate, sizeof(*driprivate));
                    ReplyMsg(&ioreq->iouh_Req.io_Message);
                } else {
                    Disable();
                    AddHead(txlist, (struct Node *) ioreq);
                    Enable();
                }
            }
        }

        pciusbXHCIDebugV("xHCI",
                         "---- IOReq done: %p ----\n", ioreq);
    }

    pciusbXHCIDebugV("xHCI", "%s: exit\n", __func__);
}
