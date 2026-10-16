/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_CPU_ACCOUNTING_H
#define AROS_CPU_ACCOUNTING_H
#include <aros/apple/cpustats.h>
void AROSCPUAccount(struct AROSCPUAccounting *, uint32_t state, uint64_t now);
int AROSCPUSnapshot(const struct AROSCPUAccounting *, uint64_t now, struct AROSCPUSample *);
/* Delta between samples, per mille; -1 means unavailable/reset/no interval. */
int AROSCPULoad(const struct AROSCPUSample *, const struct AROSCPUSample *);
#endif
