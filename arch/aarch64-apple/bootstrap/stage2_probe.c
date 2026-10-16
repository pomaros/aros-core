/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Reversible proof of a firmware-described conventional-memory range.
 */

#include "stage2_probe.h"

#include <stddef.h>

#define PROBE_WORDS (AROS_APPLE_STAGE2_PROBE_BYTES / sizeof(uint64_t))

static const uint64_t probe_pattern[PROBE_WORDS] = {
    UINT64_C(0xa5505aa50ff0f00f),
    UINT64_C(0x5aafa55af00f0ff0),
    UINT64_C(0x0123456789abcdef),
    UINT64_C(0xfedcba9876543210)
};

static int select_range(
    const struct AROSAppleTagItem *tags,
    uintptr_t *range_start,
    uintptr_t *range_end)
{
    uint64_t lower = 0;
    uint32_t index;
    int have_lower = 0;

    if (tags == NULL || range_start == NULL || range_end == NULL)
        return 0;
    for (index = 0; index < AROS_APPLE_MAX_BOOT_TAGS; ++index) {
        if (tags[index].tag == AROS_APPLE_TAG_DONE)
            return 0;
        if (tags[index].tag == AROS_APPLE_KRN_MEM_LOWER) {
            if (have_lower || tags[index].data > UINTPTR_MAX)
                return 0;
            lower = tags[index].data;
            have_lower = 1;
            continue;
        }
        if (tags[index].tag != AROS_APPLE_KRN_MEM_UPPER)
            continue;
        if (!have_lower || tags[index].data > UINTPTR_MAX ||
            tags[index].data <= lower)
            return 0;
        if (tags[index].data - lower >=
                AROS_APPLE_STAGE2_PROBE_MINIMUM_RANGE) {
            *range_start = (uintptr_t)lower;
            *range_end = (uintptr_t)tags[index].data;
            return 1;
        }
        have_lower = 0;
    }
    return 0;
}

int AROSAppleStage2ProbeMemory(
    const struct AROSAppleTagItem *tags,
    struct AROSAppleStage2MemoryProof *proof)
{
    volatile uint64_t *target;
    uint64_t original[PROBE_WORDS];
    uintptr_t range_start;
    uintptr_t range_end;
    uintptr_t probe_address;
    uint32_t index;
    int matches = 1;

    if (proof == NULL ||
        !select_range(tags, &range_start, &range_end))
        return 0;
    probe_address = (range_start + 63U) & ~(uintptr_t)63U;
    if (probe_address < range_start || probe_address > range_end ||
        AROS_APPLE_STAGE2_PROBE_BYTES > range_end - probe_address)
        return 0;

    target = (volatile uint64_t *)probe_address;
    for (index = 0; index < PROBE_WORDS; ++index)
        original[index] = target[index];
    for (index = 0; index < PROBE_WORDS; ++index)
        target[index] = probe_pattern[index];
    __asm__ volatile("" ::: "memory");
    for (index = 0; index < PROBE_WORDS; ++index) {
        if (target[index] != probe_pattern[index])
            matches = 0;
    }
    for (index = 0; index < PROBE_WORDS; ++index)
        target[index] = original[index];
    __asm__ volatile("" ::: "memory");
    for (index = 0; index < PROBE_WORDS; ++index) {
        if (target[index] != original[index])
            matches = 0;
    }

    proof->range_start = range_start;
    proof->range_end = range_end;
    proof->probe_address = probe_address;
    proof->bytes = AROS_APPLE_STAGE2_PROBE_BYTES;
    proof->restored = matches != 0;
    return matches;
}
