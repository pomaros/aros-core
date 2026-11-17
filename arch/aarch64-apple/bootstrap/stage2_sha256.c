/* Copyright (C) 2026 John KNIPPER.
 * Licensed under the AROS Public License (APL) Version 1.1. */

/* Freestanding bootstrap adapter: share the generic implementation without
 * bringing a link library or any operating-system dependency into stage two. */
#include "stage2_sha256.h"
#include "../../../compiler/alib/sha256.c"
void AROSAppleStage2SHA256(const void *data, size_t size,
                         uint8_t digest[AROS_APPLE_STAGE2_SHA256_SIZE])
{
    AROSSHA256(data, size, digest);
}
