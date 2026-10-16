/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Apple AArch64 kernel interrupt-controller hooks.
 */

#ifndef AROS_AARCH64_APPLE_KERNEL_ARCH_H
#define AROS_AARCH64_APPLE_KERNEL_ARCH_H

#include "vm_irq.h"

struct KernelBase;

#define ictl_valid_irq(irq, base) AROSVMKernelIRQAllowed((uint32_t)(irq), (base))

/* Stable source IDs reserve twelve IRQ bits and two die bits. */
#define HW_IRQ_COUNT 1020
#define IRQ_COUNT HW_IRQ_COUNT

#define ictl_enable_irq(irq, base) \
    AROSVMKernelEnableIRQ((uint32_t)(irq), (base))
#define ictl_disable_irq(irq, base) \
    AROSVMKernelDisableIRQ((uint32_t)(irq), (base))

#endif /* AROS_AARCH64_APPLE_KERNEL_ARCH_H */
