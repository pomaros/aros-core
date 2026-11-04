/* Copyright (C) 2025-2026 The AROS Development Team.
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include <proto/exec.h>
#include <exec/memory.h>
#include "uhwcmd.h"
#include "xhci_schedule.h"
#undef base
#define base (hc->hc_Device)

/* The completion cookie is CPU-only metadata, never an address given to xHC. */
struct XHCIPayload {
    struct XHCIPayload *next;   /* deferred-free list, see xhciFreeDeferredBounces */
    APTR cpu;
    APTR target;
    ULONG bytes;
    UWORD direction;
};

static void freePayload(struct PCIController *hc, struct XHCIPayload *payload)
{
    if(payload->cpu)
        FREEPCIMEM(hc, hc->hc_PCIDriverObject, payload->cpu);
    FreeMem(payload, sizeof(*payload));
}

/*
 * A transfer's payload is released where the transfer completes, in the
 * controller's interrupt, but the PCI allocator and exec's allocator may not
 * be entered from an interrupt: on SMP the task it interrupted can hold the
 * memory header's spinlock, and the CPU then spins on it for good. The
 * payload goes on a list instead and the event ring task frees it when the
 * completion wakes it (upstream's deferred bounce free, on the fork's
 * PCI-payload cookies).
 */
static void deferPayloadFree(struct PCIController *hc, struct XHCIPayload *payload)
{
    struct XhciHCPrivate *xhcic = xhciGetHCPrivate(hc);
#if defined(__AROSEXEC_SMP__)
    struct XHCIPayload *head = __atomic_load_n((struct XHCIPayload **)&xhcic->xhc_FreeBounce, __ATOMIC_RELAXED);

    do
        payload->next = head;
    while(!__atomic_compare_exchange_n((struct XHCIPayload **)&xhcic->xhc_FreeBounce, &head, payload, TRUE,
                                       __ATOMIC_RELEASE, __ATOMIC_RELAXED));
#else
    Disable();
    payload->next = xhcic->xhc_FreeBounce;
    xhcic->xhc_FreeBounce = payload;
    Enable();
#endif
}

/* Free the deferred payloads - task context only */
void xhciFreeDeferredBounces(struct PCIController *hc)
{
    struct XhciHCPrivate *xhcic = xhciGetHCPrivate(hc);
    struct XHCIPayload *payload;

    if(!xhcic)
        return;
#if defined(__AROSEXEC_SMP__)
    payload = __atomic_exchange_n((struct XHCIPayload **)&xhcic->xhc_FreeBounce, NULL, __ATOMIC_ACQUIRE);
#else
    Disable();
    payload = xhcic->xhc_FreeBounce;
    xhcic->xhc_FreeBounce = NULL;
    Enable();
#endif
    while(payload) {
        struct XHCIPayload *next = payload->next;

        freePayload(hc, payload);
        payload = next;
    }
}

static APTR payloadAddress(struct PCIController *hc, APTR cpu, ULONG bytes)
{
    IPTR bus = (IPTR)CPUTOPCI(hc, hc->hc_PCIDriverObject, cpu);
    if(bus == (IPTR)-1 || bytes - 1 > (IPTR)-1 - bus)
        return NULL;
    if(!(hc->hc_Flags & HCF_ADDR64) &&
       ((UQUAD)bus + bytes - 1) > 0xffffffffULL)
        return NULL;
    /* Verify every 4 KiB interval and the final byte, not merely the start. */
    ULONG offset = 0;
    while(bytes - offset > 4096) {
        offset += 4096;
        if((IPTR)CPUTOPCI(hc, hc->hc_PCIDriverObject, (UBYTE *)cpu + offset) != bus + offset)
            return NULL;
    }
    if((IPTR)CPUTOPCI(hc, hc->hc_PCIDriverObject, (UBYTE *)cpu + bytes - 1) != bus + bytes - 1)
        return NULL;
    return (APTR)bus;
}

APTR xhciPreparePCIPayload(struct PCIController *hc, struct IOUsbHWReq *ioreq,
                        ULONG *dmalen, UWORD effdir, APTR *cookieOut)
{
    if(cookieOut)
        *cookieOut = NULL;
    if(!hc || !ioreq || !dmalen || (effdir != UHDIR_IN && effdir != UHDIR_OUT))
        return NULL;
    if(!*dmalen || !ioreq->iouh_Data || *dmalen > ioreq->iouh_Length || !cookieOut)
        return NULL;

    struct XHCIPayload *payload = AllocMem(sizeof(*payload), MEMF_PUBLIC | MEMF_CLEAR);
    if(!payload)
        return NULL;
    payload->bytes = *dmalen;
    payload->target = ioreq->iouh_Data;
    payload->direction = effdir;
    payload->cpu = ALLOCPCIMEM(hc, hc->hc_PCIDriverObject, payload->bytes);
    if(!payload->cpu)
        goto fail;
    APTR dma = payloadAddress(hc, payload->cpu, payload->bytes);
    if(!dma)
        goto fail;
    if(effdir == UHDIR_OUT)
        CopyMem(payload->target, payload->cpu, payload->bytes);
    ULONG cached = payload->bytes;
    CachePreDMA(payload->cpu, &cached,
                effdir == UHDIR_IN ? 0 : DMA_ReadFromRAM);
    if(cached != payload->bytes) {
        CachePostDMA(payload->cpu, &cached,
                     effdir == UHDIR_IN ? (1UL << 31) : ((1UL << 31) | DMA_ReadFromRAM));
        goto fail;
    }
    *cookieOut = payload;
    return dma;
fail:
    freePayload(hc, payload);
    return NULL;
}

void xhciReleasePCIPayload(struct PCIController *hc, struct IOUsbHWReq *ioreq,
                        ULONG actual, UWORD effdir, APTR cookie)
{
    if(!cookie)
        return;
    struct XHCIPayload *payload = cookie;
    ULONG cached = payload->bytes;
    CachePostDMA(payload->cpu, &cached,
                 payload->direction == UHDIR_IN ? (1UL << 31) : ((1UL << 31) | DMA_ReadFromRAM));
    if(actual > payload->bytes || actual > ioreq->iouh_Length || effdir != payload->direction)
        ioreq->iouh_Req.io_Error = UHIOERR_HOSTERROR;
    else if(payload->direction == UHDIR_IN && actual)
        CopyMem(payload->cpu, payload->target, actual);
    /* Completion context: never free here (see deferPayloadFree) */
    deferPayloadFree(hc, payload);
}

APTR xhciPrepareDMABuffer(struct PCIController *hc, struct IOUsbHWReq *ioreq,
                          ULONG *dmalen, UWORD effdir, APTR *bounceOut)
{
    /* The ring API takes CPU addresses and translates exactly once. The
     * primitive validates bus contiguity but keeps its CPU allocation in the
     * opaque cookie; never pass its returned bus address to InsertTRB. */
    if(bounceOut) *bounceOut = NULL;
    if(dmalen && !*dmalen) return ioreq->iouh_Data;
    if(!xhciPreparePCIPayload(hc, ioreq, dmalen, effdir, bounceOut))
        return NULL;
    return ((struct XHCIPayload *)*bounceOut)->cpu;
}

void xhciReleaseDMABuffer(struct PCIController *hc, struct IOUsbHWReq *ioreq,
                          ULONG actual, UWORD effdir, APTR bounceBuf)
{
    xhciReleasePCIPayload(hc, ioreq, actual, effdir, bounceBuf);
}

/* No referencing TRB may have been published. Retire the owned PCI
 * payload without an IN copy-back. CPU and bytes describe that payload
 * to callers; the cookie owns the allocation and prepared extent. */
void xhciDiscardPreparedDMABuffer(struct PCIController *hc, struct IOUsbHWReq *ioreq,
                                   APTR cpu, ULONG bytes, UWORD direction, APTR bounce)
{
    (void)cpu;
    (void)bytes;
    (void)ioreq;
    (void)direction;
    if(!bounce)
        return;
    /* Submission path, task context: the payload can be freed directly. */
    freePayload(hc, bounce);
}
