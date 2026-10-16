/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_CPU_ACCOUNTING_H
#define AROS_CPU_ACCOUNTING_H
#include <stdint.h>
enum AROSCPUState { AROS_CPU_OFFLINE, AROS_CPU_BUSY, AROS_CPU_IDLE };
/* Each CPU owns its record; transition callers mask local interrupts.
 * Snapshot readers use bounded retries and never block the widget. */
struct AROSCPUAccounting {
    uint32_t sequence, state;
    uint64_t since, busy, idle, transitions;
};
struct AROSCPUSample { uint64_t busy, idle, transitions; uint32_t state; };
void AROSCPUAccount(struct AROSCPUAccounting *, uint32_t state, uint64_t now);
int AROSCPUSnapshot(const struct AROSCPUAccounting *, uint64_t now, struct AROSCPUSample *);
/* Delta between samples, per mille; -1 means unavailable/reset/no interval. */
int AROSCPULoad(const struct AROSCPUSample *, const struct AROSCPUSample *);
#endif
