/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#ifndef AROS_AARCH64_APPLE_PLATFORM_TIMER_KERNEL_H
#define AROS_AARCH64_APPLE_PLATFORM_TIMER_KERNEL_H

#include <aros/apple/platformtimer.h>

struct AROSAppleRuntimeFrame;

#define AROS_APPLE_TIMER_IPI (1U << 20)
int AROSAppleKernelTimerNotify(void);

int AROSAppleKernelTimerControl(struct AROSAppleTimerControl *control);
int AROSAppleKernelTimerHandleFIQ(struct AROSAppleRuntimeFrame *frame);

#endif /* AROS_AARCH64_APPLE_PLATFORM_TIMER_KERNEL_H */
