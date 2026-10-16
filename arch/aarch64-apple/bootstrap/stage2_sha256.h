/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#ifndef AROS_AARCH64_APPLE_STAGE2_SHA256_H
#define AROS_AARCH64_APPLE_STAGE2_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define AROS_APPLE_STAGE2_SHA256_SIZE 32U

void AROSAppleStage2SHA256(
    const void *data, size_t size,
    uint8_t digest[AROS_APPLE_STAGE2_SHA256_SIZE]);

#endif /* AROS_AARCH64_APPLE_STAGE2_SHA256_H */
