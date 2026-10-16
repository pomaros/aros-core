/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_PCIECAM_H
#define AROS_PCIECAM_H
#include <exec/libraries.h>
#include <oop/oop.h>
#include <hidd/pci.h>
#include "access.h"
#define CLID_Hidd_PCIECAM "hidd.pci.ecam"
#define IID_Hidd_PCIECAM "hidd.pci.ecam"
/* Init-only attribute 0: caller retains ownership of admitted mappings.
 * Config is copied; all regions must remain valid until the instance dies.
 * Do not pass firmware addresses before platform mapping admission.
 * The current generic PCI enumerator requires a bus-zero root. */
#define aoHidd_PCIECAM_Config 0
#define PCIECAM_MAX_WINDOWS 8
struct PCIECAMWindow { uint64_t bus, cpu, bytes; };
/* Platform-owned DMA policy. All four callbacks or none; buffers must stay
 * live until the device and its software users have stopped. */
struct PCIECAMDMA {
    void *context;
    void *(*allocate)(void *, uint32_t);
    void (*release)(void *, void *);
    void *(*to_bus)(void *, void *);
    void *(*to_cpu)(void *, void *);
};
/* Platform-owned interrupt routing. Both callbacks or none. Calls are made
 * in task context; callback code/context must outlive every registration.
 * The installed handler runs in interrupt context and must not block or take
 * task-context semaphores.
 * add publishes a live route only on success; failure leaves none. remove
 * must quiesce the device route and detach the handler before returning.
 * The platform owns vector allocation, pending-state retirement and MSI/INTx
 * policy. ECAM never guesses a route from the legacy Interrupt Line byte. */
struct PCIECAMIRQ {
    void *context;
    BOOL (*add)(void *, OOP_Object *, struct Interrupt *);
    void (*remove)(void *, OOP_Object *, struct Interrupt *);
};
struct PCIECAMConfig {
    struct AROSPCIECAM config;
    /* Complete configuration stores before subsequent MMIO in another
     * region (e.g. BAR access after decode enable). Platform barrier. */
    void (*complete_config_write)(void);
    unsigned window_count;
    struct PCIECAMWindow windows[PCIECAM_MAX_WINDOWS];
    /* Platform-owned functions, e.g. the boot console, remain invisible to
     * generic PCI enumeration. This first profile reserves whole slots on
     * the root bus. No hidden function can be reached through writes either. */
    uint32_t reserved_root_slots;
    struct PCIECAMDMA dma;
    struct PCIECAMIRQ irq;
};
struct PCIECAMData { struct PCIECAMConfig host; };
struct PCIECAMBase {
    struct Library library;
    struct Library *oop, *pci, *utility;
    OOP_Class *driver;
    OOP_AttrBase config_attr, driver_attr;
};
#define BASE(cl) ((struct PCIECAMBase *)cl->UserData)
#define OOPBase (BASE(cl)->oop)
#define UtilityBase (BASE(cl)->utility)
#endif
