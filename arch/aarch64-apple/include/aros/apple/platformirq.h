/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_PLATFORMIRQ_H
#define AROS_PLATFORMIRQ_H
#include <stdint.h>
/* Kernel capability: task-visible IRQ exclusion saves/restores both masks. */
#define AROS_APPLE_KATTR_IRQ_MASK UINT32_C(0x83f10011)
#endif
