/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 *
 * SPRR-backed JIT write protection, kernel side. See <aros/apple/jit.h>.
 */
#ifndef AROS_APPLE_KERNEL_JIT_PROTECT_H
#define AROS_APPLE_KERNEL_JIT_PROTECT_H

#include <stdint.h>

struct Task;

/* Called once on every core before it runs tasks, with the MMU on. Chooses
 * the JIT mode on the boot core and programs SPRR on the calling core. */
void AROSAppleJitProtectInitCPU(void);

/* The JIT mode of this kernel: AROS_APPLE_JIT_NONE, _SPRR or _RWX. */
int AROSAppleJitMode(void);

/* Program the calling core's EL0 permissions for the task about to run. */
void AROSAppleJitRestore(struct Task *task);

/* The SVC body: switch the calling task's JIT pages. Returns the mode, or
 * UINT64_MAX when JIT pages do not exist on this kernel. */
uint64_t AROSAppleJitSetWritable(struct Task *task, int writable);

#endif
