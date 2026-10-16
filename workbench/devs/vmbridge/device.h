/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
   Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef VMBRIDGE_DEVICE_H
#define VMBRIDGE_DEVICE_H
#include <exec/devices.h>
#include <exec/semaphores.h>
#include <exec/interrupts.h>
#include <oop/oop.h>
#include "vsock_stream.h"
struct VirtioVsock;
struct BridgeControl { struct Message message;BOOL okay; };
struct BridgeBase {
    struct Device device;struct Unit unit;
    struct Library *oop;struct SignalSemaphore lifecycle;
    struct MsgPort *input,*control;struct Task *worker;
    struct List reads,writes;struct IOStdReq *active;
    BOOL active_abort,opened,failed,closing;
    struct VirtioVsock *socket;struct VsockStream stream;
    OOP_Object *pci,*pci_device;OOP_AttrBase pci_attr;
    struct Interrupt interrupt;ULONG irq_mask,irq_hits,port,next_port;
    UBYTE packet[4096],reject[44];BOOL reject_pending;
};
#endif
