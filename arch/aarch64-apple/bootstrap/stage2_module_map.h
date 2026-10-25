/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#ifndef AROS_AARCH64_APPLE_STAGE2_MODULE_MAP_H
#define AROS_AARCH64_APPLE_STAGE2_MODULE_MAP_H

#include <stdint.h>

#include <aros/apple/bootinfo.h>
#include <aros/apple/modulemap.h>
#include "stage2_loader.h"

/*
 * Build the kernel's module map (aros/apple/modulemap.h) at *cursor, below
 * arena_end, from the placed sections and the ELF symbol tables of the
 * loaded modules; advance *cursor past it and answer its address. Capacity
 * limits truncate (AROS_APPLE_MODULE_MAP_TRUNCATED), they never fail the
 * boot; only a map that does not fit the arena does.
 */
enum AROSAppleStage2Status AROSAppleStage2BuildModuleMap(
    const struct AROSBootInfo *boot_info,
    const struct AROSAppleStage2Context *context,
    uint64_t arena_end,
    uint64_t *cursor,
    struct AROSAppleModuleMap **map_out);

#endif /* AROS_AARCH64_APPLE_STAGE2_MODULE_MAP_H */
