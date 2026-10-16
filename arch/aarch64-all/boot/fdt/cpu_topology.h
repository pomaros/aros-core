/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_CPU_TOPOLOGY_H
#define AROS_CPU_TOPOLOGY_H
#include "early_fdt.h"
#define AROS_CPU_MAX 64
#define AROS_CPU_AFFINITY_MASK UINT64_C(0xff00ffffff)
enum AROSCPUStartMethod { AROS_CPU_START_UNKNOWN, AROS_CPU_START_PSCI, AROS_CPU_START_SPIN_TABLE };
struct AROSCPUDescription {
    uint64_t affinity, release_address;
    uint32_t enabled, method;
};
struct AROSCPUTopology {
    uint32_t count, boot_cpu, psci_hvc, psci_available;
    struct AROSCPUDescription cpu[AROS_CPU_MAX];
};
/* Returns zero on malformed/duplicate/overflowing topology or absent boot CPU.
 * Output is only usable on success. Disabled CPUs remain listed. */
int AROSCPUDiscover(const struct AROSEarlyFDT *, uint64_t boot_affinity, struct AROSCPUTopology *);
/* Validate a 4 KiB spin-table page as both declared RAM and firmware-reserved.
 * The caller may map this page; it must never add it to the free RAM arena. */
int AROSCPUReleasePage(const struct AROSEarlyFDT *, uint64_t slot, uint64_t *page);
#endif
