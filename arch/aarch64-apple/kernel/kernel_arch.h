/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Apple AArch64 kernel interrupt-controller hooks.
 */

#ifndef AROS_AARCH64_APPLE_KERNEL_ARCH_H
#define AROS_AARCH64_APPLE_KERNEL_ARCH_H

#include "platform_irq.h"

/* Controller table capacity is fixed at build time; validity is checked by
 * the selected controller before handlers may be registered. */
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
#define HW_IRQ_COUNT 1020
#else
#define HW_IRQ_COUNT 16384
#endif
#define IRQ_COUNT HW_IRQ_COUNT
#define ictl_valid_irq(irq, base) AROSPlatformIRQAllowed((uint32_t)(irq), (base))
#define ictl_enable_irq(irq, base) AROSPlatformEnableIRQ((uint32_t)(irq), (base))
#define ictl_disable_irq(irq, base) AROSPlatformDisableIRQ((uint32_t)(irq), (base))
#endif
