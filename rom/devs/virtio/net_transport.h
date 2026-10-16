/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
   Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef VIRTIO_NET_TRANSPORT_H
#define VIRTIO_NET_TRANSPORT_H
#include <exec/types.h>
#include <exec/libraries.h>
#include <oop/oop.h>
struct VirtioNet;
struct Interrupt;
/* Task context, one serialized owner. The caller retains OOP/PCI providers.
 * Modern little-endian transport with coherent PCI DMA only, no offloads.
 * The resident caller supplies scheduling and an optional interrupt callback.
 * Failed Open may return a quarantined context: retain until Close succeeds. */
BOOL VirtioNetOpen(struct Library *,OOP_Object *,struct VirtioNet **);
BOOL VirtioNetMAC(struct VirtioNet *,UBYTE mac[6]);
ULONG VirtioNetMTU(struct VirtioNet *);
/* Callback/code/data must survive until successful Close. IRQ callback may
 * acknowledge via InterruptStatus and signal its owner; no transport polling
 * or close from interrupt context. Failed Enable requires Close. */
BOOL VirtioNetEnableInterrupt(struct VirtioNet *,struct Interrupt *);
UBYTE VirtioNetInterruptStatus(struct VirtioNet *);
/* Send: 1 copied/queued, 0 previous TX pending, -1 invalid/faulted.
 * PollTX: 1 idle/completed, 0 pending, -1 malformed completion/faulted.
 * Receive: frame bytes copied, 0 pending, -1 invalid/faulted.
 * Receive buffer must hold 1518 bytes. Calls never wait for the device. */
LONG VirtioNetSend(struct VirtioNet *,const UBYTE *,ULONG);
LONG VirtioNetPollTX(struct VirtioNet *);
LONG VirtioNetReceive(struct VirtioNet *,UBYTE *,ULONG);
/* FALSE: DMA quarantine retained. TRUE consumes/frees context after reset. */
BOOL VirtioNetClose(struct VirtioNet *);
#endif
