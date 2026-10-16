/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#ifndef AROS_AARCH64_APPLE_STAGE2_MEMORY_PLAN_H
#define AROS_AARCH64_APPLE_STAGE2_MEMORY_PLAN_H

#include "stage2_loader.h"

enum AROSAppleStage2Status AROSAppleStage2BuildMemoryPlan(
    const struct AROSBootInfo *boot_info,
    struct AROSAppleStage2Result *result,
    uint64_t arena_end,
    uint64_t *cursor);

#endif /* AROS_AARCH64_APPLE_STAGE2_MEMORY_PLAN_H */
