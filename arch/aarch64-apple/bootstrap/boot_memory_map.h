/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Producer-neutral view of the loader-supplied physical memory map.
 */

#ifndef AROS_APPLE_BOOT_MEMORY_MAP_H
#define AROS_APPLE_BOOT_MEMORY_MAP_H

#include <stdint.h>

#include <aros/apple/bootinfo.h>

#define AROS_APPLE_BOOT_MEMORY_MAX_RANGES 512U

enum AROSAppleBootMemoryKind {
    AROS_APPLE_BOOT_MEMORY_AVAILABLE = 1,
    AROS_APPLE_BOOT_MEMORY_LOADER_DATA = 2,
    AROS_APPLE_BOOT_MEMORY_RESERVED = 3
};

struct AROSAppleBootMemoryView {
    uint64_t start;
    uint64_t end;
    uint32_t source_index;
    uint32_t kind;
};

int AROSAppleBootMemoryMapValidate(
    const struct AROSBootInfo *boot_info, uint32_t *range_count);

int AROSAppleBootMemoryMapRangeAt(
    const struct AROSBootInfo *boot_info, uint32_t index,
    struct AROSAppleBootMemoryView *view);

#endif /* AROS_APPLE_BOOT_MEMORY_MAP_H */
