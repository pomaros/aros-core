/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#ifndef AROS_AARCH64_APPLE_STAGE2_PROBE_H
#define AROS_AARCH64_APPLE_STAGE2_PROBE_H

#include <stdint.h>

#include <aros/apple/startup.h>

#define AROS_APPLE_STAGE2_PROBE_MINIMUM_RANGE (UINT64_C(1) << 20)
#define AROS_APPLE_STAGE2_PROBE_BYTES UINT32_C(32)

struct AROSAppleStage2MemoryProof {
    uintptr_t range_start;
    uintptr_t range_end;
    uintptr_t probe_address;
    uint32_t bytes;
    uint32_t restored;
};

int AROSAppleStage2ProbeMemory(
    const struct AROSAppleTagItem *tags,
    struct AROSAppleStage2MemoryProof *proof);

#endif /* AROS_AARCH64_APPLE_STAGE2_PROBE_H */
