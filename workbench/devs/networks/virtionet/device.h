/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
   Licensed under the AROS Public License (APL) Version 1.1.
   SANA-II/Exec layout follows the existing AROS APL tap and dwmac devices. */
#ifndef VIRTIONET_DEVICE_H
#define VIRTIONET_DEVICE_H
#include <exec/devices.h>
#include <exec/semaphores.h>
#include <exec/interrupts.h>
#include <devices/sana2.h>
#include <oop/oop.h>
#include <utility/hooks.h>
struct VirtioNet;
struct NetOpener {
    struct MinNode node;
    BOOL (*rx)(APTR,APTR,ULONG);
    BOOL (*tx)(APTR,APTR,ULONG);
    struct Hook *filter;
};
struct NetControl {
    struct Message message;
    struct NetOpener *opener;
    BOOL stop,okay;
};
struct NetBase {
    struct Device device;
    struct Unit unit;
    struct Library *oop,*utility;
    struct SignalSemaphore lifecycle;
    struct MinList openers;
    struct List reads,writes,events;
    struct MsgPort *input,*control;
    struct Task *worker;
    struct VirtioNet *net;
    OOP_Object *pci,*pci_device;
    OOP_AttrBase pci_attr;
    struct Interrupt interrupt;
    ULONG irq_mask,open_count,mtu;
    UBYTE mac[6];
    BOOL configured,online,failed,tx_pending,active_abort;
    struct IOSana2Req *active,*tx_request;
    struct Sana2DeviceStats stats;
    UBYTE rx_frame[1518],tx_frame[1518];
};
#endif
