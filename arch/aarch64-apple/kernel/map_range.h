/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef APPLE_MAP_RANGE_H
#define APPLE_MAP_RANGE_H

#include <stdint.h>

/* The identity walker uses four 9-bit indices and 4-KiB pages. Reject
 * high address bits before they can alias an unrelated table entry.
 * Validate the exclusive end before rounding it to a page boundary. */
static inline int AROSAppleMapRange(uintptr_t address, uint32_t length,
    uintptr_t *start, uintptr_t *end)
{
    const uint64_t limit = UINT64_C(1) << 48;
    if (length == 0 || address >= limit || length > limit - address)
        return 0;
    *start = address & ~UINT64_C(4095);
    *end = (address + length + UINT64_C(4095)) & ~UINT64_C(4095);
    return 1;
}

#endif
