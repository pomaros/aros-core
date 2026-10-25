/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_PLATFORM_CPU_H
#define AROS_PLATFORM_CPU_H
#include <stdint.h>
int AROSPlatformJitChoose(uint64_t requested);
void AROSPlatformJitInitCPU(int mode);
void AROSPlatformJitRestore(int mode, int writable);
uint32_t AROSPlatformDefaultIdleMode(void);
uintptr_t AROSPlatformIdleEntry(uint32_t mode);
#endif
