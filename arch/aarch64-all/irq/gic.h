/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#ifndef AROS_AARCH64_GIC_H
#define AROS_AARCH64_GIC_H
#include "early_fdt.h"
#include <stdint.h>
struct AROSGIC
{
    uintptr_t distributor, cpu, private;
    uint64_t affinity;
    uint32_t version, limit;
    int node;
};
/* Caller maps the FDT register ranges and masks CPU interrupts first.
 * Init owns shared distributor setup and must run once on the boot CPU.
 * InitCPU preserves the distributor and initializes only the executing CPU. */
int AROSGICInit(struct AROSGIC *, const struct AROSEarlyFDT *, int controller);
int AROSGICInitCPU(struct AROSGIC *, const struct AROSEarlyFDT *, int controller);
/* Target full GICv3 affinity; caller owns/enables the destination SGI. */
int AROSGICSendIPI(struct AROSGIC *, uint32_t id, uint64_t affinity);
int AROSGICClearPending(struct AROSGIC *, uint32_t id);
int AROSGICResolve(const struct AROSFDTIRQ *, uint32_t *id, uint32_t *trigger);
int AROSGICEnable(struct AROSGIC *, uint32_t id, uint32_t trigger);
int AROSGICDisable(struct AROSGIC *, uint32_t id);
uint32_t AROSGICAcknowledge(struct AROSGIC *);
void AROSGICEnd(struct AROSGIC *, uint32_t token);
/* Self-target wrapper also supports the GICv2 banked CPU target interface. */
int AROSGICSendSelfIPI(struct AROSGIC *, uint32_t id);
#endif
