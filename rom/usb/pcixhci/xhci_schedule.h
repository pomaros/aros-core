#ifndef XHCICHIP_SCHEDULE_H
#define XHCICHIP_SCHEDULE_H

#include "xhciproto.h"
#include "xhci_hcd.h"

UBYTE xhciEndpointIDFromIndex(UWORD wIndex);

/* Returns a CPU address; bounceOut is an opaque PCI cookie or platform bounce. */
APTR xhciPrepareDMABuffer(struct PCIController *hc, struct IOUsbHWReq *ioreq,
                          ULONG *dmalen, UWORD effdir, APTR *bounceOut);
void xhciReleaseDMABuffer(struct PCIController *hc, struct IOUsbHWReq *ioreq,
                          ULONG actual, UWORD effdir, APTR bounceBuf);

/* PCI-owned payload primitive used by the CPU-addressed preparation wrapper.
 * Abort and endpoint teardown still require hardware retirement integration.
 * Prepare returns a controller bus address and an opaque completion cookie.
 * Existing InsertTRB translates CPU addresses: do NOT feed this returned bus
 * address into that path without an explicit already-translated address mode.
 * Release requires completion of every referencing TRB, or acknowledged stop
 * AND removal of those TRBs before any endpoint restart. A software timeout
 * or ReplyMsg is not evidence that the controller has stopped accessing it.
 */
APTR xhciPreparePCIPayload(struct PCIController *hc, struct IOUsbHWReq *ioreq,
                         ULONG *dmalen, UWORD effdir, APTR *cookieOut);
void xhciReleasePCIPayload(struct PCIController *hc, struct IOUsbHWReq *ioreq,
                         ULONG actual, UWORD effdir, APTR cookie);

/* Only for prepared CPU-addressed buffers that were never published. */
void xhciDiscardPreparedDMABuffer(struct PCIController *hc, struct IOUsbHWReq *ioreq,
                                   APTR cpu, ULONG bytes, UWORD direction, APTR bounce);
/* Success publishes active-list ownership and rings the doorbell. The caller
 * must not access IO/private state afterwards: completion may already run. */
BOOL xhciQueueControlStages(struct PCIController *hc, struct IOUsbHWReq *ioreq,
                            struct pciusbXHCIIODevPrivate *priv,
                            volatile struct pcisusbXHCIRing *ring, ULONG flags);

BOOL xhciInitIOTRBTransfer(struct PCIController *hc, struct IOUsbHWReq *ioreq,
                           struct List *ownerList, ULONG txtype, BOOL allowEp0AutoCreate,
                           struct timerequest *timerreq,
                           struct pciusbXHCIIODevPrivate **outPrivate);

ULONG xhciBuildDataTRBFlags(const struct IOUsbHWReq *ioreq, ULONG txtype);

BOOL xhciActivateEndpointTransfer(struct PCIController *hc, struct PCIUnit *unit,
                                  struct IOUsbHWReq *ioreq, struct pciusbXHCIIODevPrivate *driprivate,
                                  UWORD devadrep, volatile struct pcisusbXHCIRing **epringOut);

/* Unpublished requests only: no DMA cancellation or message/list ownership. */
void xhciRollbackUnsubmitted(struct PCIUnit *unit, struct IOUsbHWReq *ioreq,
                             struct pciusbXHCIIODevPrivate *priv, UWORD key);

/* Same publication/ownership contract as xhciQueueControlStages. */
WORD xhciQueuePayloadTRBs(struct PCIController *hc, struct IOUsbHWReq *ioreq,
                          struct pciusbXHCIIODevPrivate *driprivate, volatile struct pcisusbXHCIRing *epring,
                          ULONG trbflags, BOOL iocOnLast);

#endif /* XHCICHIP_SCHEDULE_H */
