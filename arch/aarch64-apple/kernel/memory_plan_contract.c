/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Validate the immutable stage-two physical-memory import plan.
 */

#include <aros/apple/startup.h>

#include <stddef.h>
#include <stdint.h>

static int add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right > UINT64_MAX - left)
        return 0;
    *result = left + right;
    return 1;
}

static const struct AROSAppleMemoryPlanRange *range_at(
    const struct AROSAppleMemoryPlan *plan, uint32_t index)
{
    return (const struct AROSAppleMemoryPlanRange *)(
        (const uint8_t *)plan + sizeof(*plan) +
        (uint64_t)index * plan->entry_size);
}

int AROSAppleMemoryPlanValidate(const struct AROSAppleMemoryPlan *plan)
{
    const struct AROSAppleMemoryPlanRange *range;
    uint64_t conventional_bytes = 0;
    uint64_t release_bytes = 0;
    uint64_t retained_bytes = 0;
    uint64_t expected_size;
    uint64_t bytes;
    uint64_t previous_end = 0;
    uint32_t conventional_count = 0;
    uint32_t release_count = 0;
    uint32_t retained_count = 0;
    uint32_t index;

    if (plan == NULL || ((uintptr_t)plan & 15U) != 0 ||
        plan->version != AROS_APPLE_MEMORY_PLAN_VERSION ||
        plan->entry_size != sizeof(struct AROSAppleMemoryPlanRange) ||
        plan->entry_count == 0 ||
        plan->entry_count > AROS_APPLE_MEMORY_PLAN_MAX_RANGES ||
        plan->reserved != 0 || plan->reserved64[0] != 0 ||
        plan->reserved64[1] != 0 || plan->reserved64[2] != 0)
        return 0;
    expected_size = sizeof(*plan) +
        (uint64_t)plan->entry_count * plan->entry_size;
    if (expected_size > UINT32_MAX || plan->size != expected_size)
        return 0;

    for (index = 0; index < plan->entry_count; ++index) {
        range = range_at(plan, index);
        if (range->start == 0 || range->end <= range->start ||
            range->end > UINTPTR_MAX ||
            (range->start & (AROS_APPLE_MEMORY_PLAN_PAGE_SIZE - 1U)) != 0 ||
            (range->end & (AROS_APPLE_MEMORY_PLAN_PAGE_SIZE - 1U)) != 0 ||
            range->reserved != 0 ||
            (index != 0 && range->start < previous_end))
            return 0;
        bytes = range->end - range->start;
        switch (range->kind) {
        case AROS_APPLE_MEMORY_RANGE_CONVENTIONAL:
            if (!add_u64(conventional_bytes, bytes, &conventional_bytes))
                return 0;
            ++conventional_count;
            break;
        case AROS_APPLE_MEMORY_RANGE_RELEASE:
            if (!add_u64(release_bytes, bytes, &release_bytes))
                return 0;
            ++release_count;
            break;
        case AROS_APPLE_MEMORY_RANGE_RETAIN:
            if (!add_u64(retained_bytes, bytes, &retained_bytes))
                return 0;
            ++retained_count;
            break;
        default:
            return 0;
        }
        previous_end = range->end;
    }

    return conventional_count == plan->conventional_count &&
        release_count == plan->release_count &&
        retained_count == plan->retained_count &&
        conventional_count + release_count + retained_count ==
            plan->entry_count &&
        conventional_bytes == plan->conventional_bytes &&
        release_bytes == plan->release_bytes &&
        retained_bytes == plan->retained_bytes;
}
