/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
   Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef VIRTIO_VSOCK_TRANSPORT_H
#define VIRTIO_VSOCK_TRANSPORT_H
#include <exec/types.h>
#include <exec/libraries.h>
#include <oop/oop.h>
/* OASIS Virtio 1.2 section 5.10. All fields are little endian. */
struct VirtioVsockHeader {
    UQUAD src_cid,dst_cid;
    ULONG src_port,dst_port,len;
    UWORD type,op;
    ULONG flags,buf_alloc,fwd_cnt;
} __attribute__((packed));
_Static_assert(sizeof(struct VirtioVsockHeader)==44,"vsock wire header");
struct VirtioVsock;
struct Interrupt;
/* Single serialized task owner. Coherent PCI DMA; caller retains OOP/PCI.
 * This is a packet transport, not a socket API: caller owns stream state,
 * peer credit checks, pending control replies and receive buffering. */
BOOL VirtioVsockOpen(struct Library *,OOP_Object *,struct VirtioVsock **);
UQUAD VirtioVsockCID(struct VirtioVsock *);
BOOL VirtioVsockEnableInterrupt(struct VirtioVsock *,struct Interrupt *);
UBYTE VirtioVsockInterruptStatus(struct VirtioVsock *);
/* Nonblocking: 1 queued/idle, 0 busy, -1 fault. Receive returns bytes or
 * 0 pending / -1 fault. One packet <=4096 bytes, including the header.
 * Receive capacity >=4096. RX may be drained while TX is pending.
 * Transport reset fails the context: close/reopen before reconnecting. */
LONG VirtioVsockSend(struct VirtioVsock *,const UBYTE *,ULONG);
LONG VirtioVsockPollTX(struct VirtioVsock *);
LONG VirtioVsockReceive(struct VirtioVsock *,UBYTE *,ULONG);
/* Failed Close retains DMA, interrupt callback and context in quarantine;
 * caller must remain resident. Successful Close consumes the context. */
BOOL VirtioVsockClose(struct VirtioVsock *);
#endif
