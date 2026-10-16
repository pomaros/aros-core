/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#ifndef AROS_AARCH64_APPLE_STAGE2_PREPARE_H
#define AROS_AARCH64_APPLE_STAGE2_PREPARE_H

#include "stage2_loader.h"

#define AROS_APPLE_STAGE2_OBSERVE_BEFORE_MMU UINT32_C(1)
#define AROS_APPLE_STAGE2_OBSERVE_AFTER_MMU  UINT32_C(2)

typedef int (*AROSAppleStage2PrepareObserver)(
    uint32_t phase,
    const struct AROSBootInfo *boot_info,
    const struct AROSAppleStage2Result *result,
    void *context);

enum AROSAppleStage2Status AROSAppleStage2PrepareMemory(
    const struct AROSBootInfo *boot_info,
    uintptr_t stage2_image_start,
    uintptr_t stage2_image_end,
    uintptr_t stage2_writable_start,
    struct AROSAppleStage2Context *context,
    struct AROSAppleStage2Result *result,
    AROSAppleStage2PrepareObserver observer,
    void *observer_context);

#endif /* AROS_AARCH64_APPLE_STAGE2_PREPARE_H */
