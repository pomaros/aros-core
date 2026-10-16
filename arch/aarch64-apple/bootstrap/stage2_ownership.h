/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#ifndef AROS_AARCH64_APPLE_STAGE2_OWNERSHIP_H
#define AROS_AARCH64_APPLE_STAGE2_OWNERSHIP_H

#include <stdint.h>

#include <aros/apple/bootinfo.h>

struct AROSAppleStage2OwnershipProof {
    uint64_t retained_bytes;
    uint64_t release_after_stage2_bytes;
    uint32_t allocation_count;
    uint32_t retained_count;
    uint32_t release_after_stage2_count;
    uint32_t reserved;
};

int AROSAppleStage2ValidateOwnership(
    const struct AROSBootInfo *boot_info,
    uintptr_t stage2_image_start,
    uintptr_t stage2_image_end,
    struct AROSAppleStage2OwnershipProof *proof);

#endif /* AROS_AARCH64_APPLE_STAGE2_OWNERSHIP_H */
