/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#ifndef AROS_AARCH64_APPLE_STAGE2_MMU_H
#define AROS_AARCH64_APPLE_STAGE2_MMU_H

#include "stage2_loader.h"

/* Index 0 Device-nGnRnE (Apple SoC MMIO is non-posted), 1 normal WB,
 * 2 normal non-cacheable, 3 Device-nGnRE for PCI apertures: PCI memory
 * writes are posted, and a non-posted mapping of a BAR faults on write. */
#define AROS_APPLE_STAGE2_MAIR_VALUE \
    ((UINT64_C(0x04) << 24) | (UINT64_C(0x44) << 16) | \
    (UINT64_C(0xff) << 8) | UINT64_C(0x00))
#define AROS_APPLE_STAGE2_DEVICE_ATTRIBUTES \
    ((UINT64_C(2) << 8) | (UINT64_C(1) << 10) | \
    (UINT64_C(1) << 53) | (UINT64_C(1) << 54))
#define AROS_APPLE_STAGE2_POSTED_DEVICE_ATTRIBUTES \
    (AROS_APPLE_STAGE2_DEVICE_ATTRIBUTES | (UINT64_C(3) << 2))
#define AROS_APPLE_STAGE2_FRAMEBUFFER_ATTRIBUTES \
    ((UINT64_C(2) << 2) | (UINT64_C(1) << 6) | \
    (UINT64_C(2) << 8) | (UINT64_C(1) << 10) | \
    (UINT64_C(1) << 53) | (UINT64_C(1) << 54))

_Static_assert((AROS_APPLE_STAGE2_DEVICE_ATTRIBUTES &
    (UINT64_C(7) << 2)) == 0, "device map uses MAIR index zero");
_Static_assert((AROS_APPLE_STAGE2_POSTED_DEVICE_ATTRIBUTES &
    (UINT64_C(7) << 2)) == (UINT64_C(3) << 2),
    "PCI apertures use the posted Device MAIR index");
_Static_assert((AROS_APPLE_STAGE2_FRAMEBUFFER_ATTRIBUTES &
    (UINT64_C(7) << 2)) == (UINT64_C(2) << 2),
    "framebuffer uses non-cacheable normal-memory MAIR index");
_Static_assert((AROS_APPLE_STAGE2_DEVICE_ATTRIBUTES &
    (UINT64_C(3) << 6)) == 0, "device map is inaccessible from EL0");

struct AROSAppleStage2Translation {
    uint64_t descriptor;
    uint32_t level;
    uint32_t valid;
};

_Static_assert(sizeof(struct AROSAppleStage2Translation) == 16,
    "stage-two translation observation size");

enum AROSAppleStage2Status AROSAppleStage2BuildIdentityMap(
    struct AROSAppleStage2Result *result,
    uintptr_t stage2_image_start,
    uintptr_t stage2_writable_start);

enum AROSAppleStage2Status AROSAppleStage2EnableIdentityMMU(
    struct AROSAppleStage2Result *result,
    uintptr_t stage2_image_start,
    uintptr_t stage2_writable_start);

int AROSAppleStage2InspectTranslation(
    const struct AROSAppleStage2Result *result,
    uint64_t virtual_address,
    struct AROSAppleStage2Translation *translation);

#endif /* AROS_AARCH64_APPLE_STAGE2_MMU_H */
