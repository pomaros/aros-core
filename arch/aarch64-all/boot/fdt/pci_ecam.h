/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_FDT_PCI_ECAM_H
#define AROS_FDT_PCI_ECAM_H
#include "early_fdt.h"
#define AROS_FDT_PCI_MAX_WINDOWS 8
struct AROSFDTPCIWindow { uint64_t bus, cpu, bytes; uint32_t flags; };
struct AROSFDTPCI {
    uint64_t ecam, ecam_bytes;
    uint32_t first_bus, last_bus, window_count;
    int node;
    struct AROSFDTPCIWindow windows[AROS_FDT_PCI_MAX_WINDOWS];
};
/* One enabled, direct-root generic ECAM bridge, 3/2 PCI and 2/2 root cells.
 * Returns 1 on success, -1 when absent, 0 for malformed/unsupported bindings.
 * Memory translations are retained; port I/O is deliberately not mapped.
 * This discovers CPU apertures only, not DMA or interrupt routing. */
int AROSFDTDiscoverPCI(const struct AROSEarlyFDT *, struct AROSFDTPCI *);
struct AROSFDTMSIFrame {
    uint64_t base, bytes;
    uint32_t spi_base, spi_count; /* Both zero: read MSI_TYPER, no override. */
    int node, controller;
};
/* Resolve a PCI msi-parent to a single enabled GICv2m frame. No MMIO access.
 * 1 = frame, -1 = no MSI binding or unsupported controller, 0 = malformed.
 * ITS/device-ID routing is a separate binding, never guessed here. */
int AROSFDTDiscoverMSIFrame(const struct AROSEarlyFDT *, int pci_node,
    struct AROSFDTMSIFrame *);
#endif
