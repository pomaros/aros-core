/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
   Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef VSOCK_STREAM_H
#define VSOCK_STREAM_H
#include <stdint.h>
#include <stddef.h>
#define VSOCK_STREAM_BUFFER 16384u
#define VSOCK_STREAM_PACKET 4096u
/* Single outgoing stream, serialized owner. Pure wire/state engine; no OS,
 * DMA, waiting, allocations or implicit threads. Integers on wire are LE.
 * The owner multiplexes unrelated flows and handles transport reset by
 * discarding streams and reinitializing with the new CID and a fresh port. */
enum VsockStreamState { VS_CONNECTING,VS_OPEN,VS_CLOSING,VS_CLOSED,VS_FAILED };
struct VsockStream {
    uint64_t cid,peer;
    uint32_t port,peer_port,tx_sent,peer_fwd,peer_alloc,rx_fwd;
    unsigned tx_head,tx_count,rx_head,rx_count;
    enum VsockStreamState state;
    unsigned request,credit,queried,reset,shutdown,peer_shutdown;
    unsigned failed; /* sticky across reset emission; Init clears it */
    uint8_t tx[VSOCK_STREAM_BUFFER],rx[VSOCK_STREAM_BUFFER];
};
int VsockStreamInit(struct VsockStream *,uint64_t,uint32_t,uint64_t,uint32_t);
/* Feed returns 1 handled, 0 malformed/current flow fault, -1 unrelated flow.
 * A caller must retain an unrelated packet until it can send its RST reply. */
int VsockStreamFeed(struct VsockStream *,const uint8_t *,size_t);
size_t VsockStreamWrite(struct VsockStream *,const void *,size_t);
size_t VsockStreamRead(struct VsockStream *,void *,size_t);
/* Emit accepts/copies a full packet: 1 accepted, 0 busy, -1 transport fault.
 * TX state advances only on acceptance; receive can continue while busy. */
int VsockStreamPump(struct VsockStream *,int (*emit)(void *,const uint8_t *,size_t),void *);
/* Graceful send/receive shutdown after queued output drains. The owner bounds
 * the closing deadline and calls Abort if the peer never acknowledges. */
void VsockStreamShutdown(struct VsockStream *);
void VsockStreamAbort(struct VsockStream *);
#endif
