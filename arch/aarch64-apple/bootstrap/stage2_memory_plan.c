/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Freeze every importable and retained physical range before reclaiming
 * the loader-owned BootInfo and memory-map pages.
 */

#include "stage2_memory_plan.h"
#include "boot_memory_map.h"

#include <aros/apple/startup.h>

struct Stage2MapRange {
    uint64_t start;
    uint64_t end;
    uint32_t descriptor_index;
    uint32_t kind;
};

static int add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right > UINT64_MAX - left)
        return 0;
    *result = left + right;
    return 1;
}

static int align_up(uint64_t value, uint64_t alignment, uint64_t *result)
{
    uint64_t mask = alignment - 1U;

    if ((alignment & mask) != 0 || value > UINT64_MAX - mask)
        return 0;
    *result = (value + mask) & ~mask;
    return 1;
}

static const struct AROSBootAllocation *allocation_at(
    const struct AROSBootInfo *boot_info, uint32_t index)
{
    return (const struct AROSBootAllocation *)(uintptr_t)(
        boot_info->allocations_physical +
        (uint64_t)index * boot_info->allocation_entry_size);
}

static void sort_map_ranges(struct Stage2MapRange *ranges, uint32_t count)
{
    struct Stage2MapRange value;
    uint32_t index;
    uint32_t position;

    for (index = 1; index < count; ++index) {
        value = ranges[index];
        position = index;
        while (position != 0 && ranges[position - 1U].start > value.start) {
            ranges[position] = ranges[position - 1U];
            --position;
        }
        ranges[position] = value;
    }
}

static void sort_plan_ranges(
    struct AROSAppleMemoryPlanRange *ranges, uint32_t count)
{
    struct AROSAppleMemoryPlanRange value;
    uint32_t index;
    uint32_t position;

    for (index = 1; index < count; ++index) {
        value = ranges[index];
        position = index;
        while (position != 0 && ranges[position - 1U].start > value.start) {
            ranges[position] = ranges[position - 1U];
            --position;
        }
        ranges[position] = value;
    }
}

static int add_bytes(uint64_t *total, uint64_t start, uint64_t end)
{
    return end > start && add_u64(*total, end - start, total);
}

enum AROSAppleStage2Status AROSAppleStage2BuildMemoryPlan(
    const struct AROSBootInfo *boot_info,
    struct AROSAppleStage2Result *result,
    uint64_t arena_end,
    uint64_t *cursor)
{
    struct Stage2MapRange map_ranges[AROS_APPLE_BOOT_MEMORY_MAX_RANGES];
    struct AROSAppleBootMemoryView view;
    const struct AROSBootAllocation *allocation;
    struct AROSAppleMemoryPlanRange *ranges;
    struct AROSAppleMemoryPlan *plan;
    uint64_t descriptor_bytes;
    uint64_t plan_start;
    uint64_t plan_end;
    uint32_t descriptor_count;
    uint32_t conventional_count = 0;
    uint32_t range_count = 0;
    uint32_t index;

    if (boot_info == NULL || result == NULL || cursor == NULL ||
        !AROSAppleBootMemoryMapValidate(boot_info, &descriptor_count))
        return AROS_APPLE_STAGE2_INVALID_MEMORY_MAP;

    for (index = 0; index < descriptor_count; ++index) {
        if (!AROSAppleBootMemoryMapRangeAt(boot_info, index, &view))
            return AROS_APPLE_STAGE2_INVALID_MEMORY_MAP;
        map_ranges[index].start = view.start;
        map_ranges[index].end = view.end;
        map_ranges[index].descriptor_index = index;
        map_ranges[index].kind = view.kind;
    }
    sort_map_ranges(map_ranges, descriptor_count);
    for (index = 1; index < descriptor_count; ++index) {
        if (map_ranges[index].start < map_ranges[index - 1U].end)
            return AROS_APPLE_STAGE2_INVALID_MEMORY_MAP;
    }
    for (index = 0; index < descriptor_count; ++index) {
        if (map_ranges[index].kind == AROS_APPLE_BOOT_MEMORY_AVAILABLE &&
            map_ranges[index].end > AROS_APPLE_MEMORY_PLAN_PAGE_SIZE)
            ++conventional_count;
    }
    if (conventional_count == 0 ||
        conventional_count > AROS_APPLE_MEMORY_PLAN_MAX_RANGES -
            boot_info->allocation_count)
        return AROS_APPLE_STAGE2_NO_USABLE_MEMORY;

    if (!align_up(*cursor, 16U, &plan_start) ||
        !add_u64(sizeof(*plan),
            (uint64_t)(conventional_count + boot_info->allocation_count) *
                sizeof(*ranges), &descriptor_bytes) ||
        !add_u64(plan_start, descriptor_bytes, &plan_end) ||
        plan_end > arena_end || descriptor_bytes > UINT32_MAX)
        return AROS_APPLE_STAGE2_ARENA_EXHAUSTED;
    plan = (struct AROSAppleMemoryPlan *)(uintptr_t)plan_start;
    ranges = (struct AROSAppleMemoryPlanRange *)(plan + 1);
    plan->version = AROS_APPLE_MEMORY_PLAN_VERSION;
    plan->size = (uint32_t)descriptor_bytes;
    plan->entry_size = sizeof(*ranges);
    plan->entry_count = conventional_count + boot_info->allocation_count;
    plan->conventional_count = conventional_count;
    plan->release_count = result->ownership.release_after_stage2_count;
    plan->retained_count = result->ownership.retained_count;
    plan->reserved = 0;
    plan->conventional_bytes = 0;
    plan->release_bytes = 0;
    plan->retained_bytes = 0;
    plan->reserved64[0] = 0;
    plan->reserved64[1] = 0;
    plan->reserved64[2] = 0;

    for (index = 0; index < descriptor_count; ++index) {
        uint64_t start;

        if (map_ranges[index].kind != AROS_APPLE_BOOT_MEMORY_AVAILABLE ||
            map_ranges[index].end <= AROS_APPLE_MEMORY_PLAN_PAGE_SIZE)
            continue;
        start = map_ranges[index].start == 0 ?
            AROS_APPLE_MEMORY_PLAN_PAGE_SIZE : map_ranges[index].start;
        ranges[range_count].start = start;
        ranges[range_count].end = map_ranges[index].end;
        ranges[range_count].kind = AROS_APPLE_MEMORY_RANGE_CONVENTIONAL;
        ranges[range_count].source_index =
            map_ranges[index].descriptor_index;
        ranges[range_count].reserved = 0;
        if (!add_bytes(&plan->conventional_bytes, start,
                map_ranges[index].end))
            return AROS_APPLE_STAGE2_INVALID_MEMORY_PLAN;
        ++range_count;
    }
    for (index = 0; index < boot_info->allocation_count; ++index) {
        allocation = allocation_at(boot_info, index);
        ranges[range_count].start = allocation->physical_address;
        if (!add_u64(allocation->physical_address, allocation->size,
                &ranges[range_count].end))
            return AROS_APPLE_STAGE2_INVALID_MEMORY_PLAN;
        ranges[range_count].source_index = index;
        ranges[range_count].reserved = 0;
        if ((allocation->flags &
                AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2) != 0) {
            ranges[range_count].kind = AROS_APPLE_MEMORY_RANGE_RELEASE;
            if (!add_bytes(&plan->release_bytes,
                    ranges[range_count].start, ranges[range_count].end))
                return AROS_APPLE_STAGE2_INVALID_MEMORY_PLAN;
        } else {
            ranges[range_count].kind = AROS_APPLE_MEMORY_RANGE_RETAIN;
            if (!add_bytes(&plan->retained_bytes,
                    ranges[range_count].start, ranges[range_count].end))
                return AROS_APPLE_STAGE2_INVALID_MEMORY_PLAN;
        }
        ++range_count;
    }
    sort_plan_ranges(ranges, range_count);
    if (range_count != plan->entry_count ||
        plan->release_bytes != result->ownership.release_after_stage2_bytes ||
        plan->retained_bytes != result->ownership.retained_bytes ||
        !AROSAppleMemoryPlanValidate(plan))
        return AROS_APPLE_STAGE2_INVALID_MEMORY_PLAN;

    result->memory_plan = (uintptr_t)plan;
    result->memory_plan_size = plan->size;
    result->arena_used_end = (uintptr_t)plan_end;
    *cursor = plan_end;
    return AROS_APPLE_STAGE2_OK;
}
