/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <aros/apple/jit.h>
#include "platform_cpu.h"
/* Guest CPU identity never permits access to native implementation registers. */
int AROSPlatformJitChoose(uint64_t requested)
{ (void)requested; return AROS_APPLE_JIT_RWX; }
void AROSPlatformJitInitCPU(int mode) { (void)mode; }
void AROSPlatformJitRestore(int mode, int writable)
{ (void)mode; (void)writable; }
uint32_t AROSPlatformDefaultIdleMode(void) { return 1; }
extern void AROSAppleKernelIdleLoop(void);
uintptr_t AROSPlatformIdleEntry(uint32_t mode)
{ (void)mode; return (uintptr_t)AROSAppleKernelIdleLoop; }
