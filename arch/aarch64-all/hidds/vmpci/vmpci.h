/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_VMPCI_H
#define AROS_VMPCI_H
#include "ecam/pciecam.h"
#include <exec/semaphores.h>
#undef OOPBase
#undef UtilityBase
struct VMPciBase {
    struct Library library;
    struct Library *oop, *hidd, *pci, *ecam;
    OOP_Object *root, *bridge;
    struct PCIECAMConfig host;
    APTR kernel;
    struct List dma_buffers;
    struct List irq_routes;
    struct SignalSemaphore irq_lock;
    uint64_t msi_frame;
    uint32_t msi_first, msi_count, msi_next;
    OOP_AttrBase device_attr;
    OOP_MethodID device_methods;
};
void VMPciDMAInit(struct VMPciBase *base);
BOOL VMPciIRQInit(struct VMPciBase *,APTR,APTR);
#endif
