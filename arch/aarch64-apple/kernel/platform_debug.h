/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_PLATFORM_DEBUG_H
#define AROS_PLATFORM_DEBUG_H
#include <stdint.h>
void AROSPlatformDebugInitialize(uintptr_t base, uint64_t size);
int AROSPlatformDebugSelectEvidenceChannel(const void *fdt, uint64_t size);
int AROSPlatformDebugPutRaw(int chr);
void AROSPlatformDebugTouchSink(void);
int AROSPlatformIdentity(uint32_t id, intptr_t *value);
#endif
