/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Prove explicit ownership and lifetime for every live loader allocation.
 * This runs before normal memory attributes exist and is built strict-aligned.
 */

#include "stage2_ownership.h"
#include "boot_memory_map.h"

#define EFI_PAGE_SIZE UINT64_C(4096)
#define STAGE2_MAX_OWNED_ALLOCATIONS 2080U
#define STAGE2_MAX_BOOT_MODULES 258U
#define STAGE2_HANDOFF_STACK_MIN UINT64_C(65536)

static int add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right > UINT64_MAX - left)
        return 0;
    *result = left + right;
    return 1;
}

static int range_contains(
    uint64_t outer_start, uint64_t outer_size,
    uint64_t inner_start, uint64_t inner_size)
{
    uint64_t outer_end;
    uint64_t inner_end;

    return inner_size != 0 &&
        add_u64(outer_start, outer_size, &outer_end) &&
        add_u64(inner_start, inner_size, &inner_end) &&
        inner_start >= outer_start && inner_end <= outer_end;
}

static int ranges_overlap(
    uint64_t first_start, uint64_t first_size,
    uint64_t second_start, uint64_t second_size)
{
    uint64_t first_end;
    uint64_t second_end;

    if (!add_u64(first_start, first_size, &first_end) ||
        !add_u64(second_start, second_size, &second_end))
        return 1;
    return first_start < second_end && second_start < first_end;
}

static const struct AROSBootAllocation *allocation_at(
    const struct AROSBootInfo *boot_info, uint32_t index)
{
    return (const struct AROSBootAllocation *)(uintptr_t)(
        boot_info->allocations_physical +
        (uint64_t)index * boot_info->allocation_entry_size);
}

static const struct AROSBootModule *module_at(
    const struct AROSBootInfo *boot_info, uint32_t index)
{
    return (const struct AROSBootModule *)(uintptr_t)(
        boot_info->modules_physical +
        (uint64_t)index * boot_info->module_entry_size);
}

static int allocation_has_loader_range(
    const struct AROSBootInfo *boot_info,
    const struct AROSBootAllocation *allocation,
    uint32_t range_count)
{
    struct AROSAppleBootMemoryView range;
    uint32_t index;
    uint32_t matches = 0;

    for (index = 0; index < range_count; ++index) {
        if (!AROSAppleBootMemoryMapRangeAt(boot_info, index, &range) ||
            range.kind != AROS_APPLE_BOOT_MEMORY_LOADER_DATA)
            continue;
        if (range_contains(range.start, range.end - range.start,
                allocation->physical_address, allocation->size))
            ++matches;
    }
    return matches == 1;
}

static int exact_lifetime(uint32_t flags, uint32_t expected)
{
    const uint32_t known =
        AROS_BOOT_ALLOCATION_FLAG_OWNED |
        AROS_BOOT_ALLOCATION_FLAG_RETAIN |
        AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2;

    return (flags & ~known) == 0 &&
        flags == (AROS_BOOT_ALLOCATION_FLAG_OWNED | expected);
}

static int used_matches(
    const struct AROSBootAllocation *allocation,
    uint64_t address, uint64_t size, int permit_larger)
{
    return allocation->used_physical == address &&
        (permit_larger ? allocation->used_size >= size :
            allocation->used_size == size);
}

int AROSAppleStage2ValidateOwnership(
    const struct AROSBootInfo *boot_info,
    uintptr_t stage2_image_start,
    uintptr_t stage2_image_end,
    struct AROSAppleStage2OwnershipProof *proof)
{
    uint8_t module_data[STAGE2_MAX_BOOT_MODULES];
    uint8_t module_names[STAGE2_MAX_BOOT_MODULES];
    const struct AROSBootAllocation *allocation;
    const struct AROSBootAllocation *other;
    const struct AROSBootModule *module;
    uint64_t table_bytes;
    uint64_t expected_size;
    uint64_t page_bytes;
    uint32_t range_count;
    uint32_t bootinfo_count = 0;
    uint32_t table_count = 0;
    uint32_t stack_count = 0;
    uint32_t fdt_count = 0;
    uint32_t early_console_count = 0;
    uint32_t map_match_count = 0;
    uint32_t log_count = 0;
    uint32_t module_table_count = 0;
    uint32_t index;
    uint32_t other_index;

    if (boot_info == NULL || proof == NULL ||
        boot_info->allocation_count == 0 ||
        boot_info->allocation_count > STAGE2_MAX_OWNED_ALLOCATIONS ||
        boot_info->module_count == 0 ||
        boot_info->module_count > STAGE2_MAX_BOOT_MODULES ||
        !AROSAppleBootMemoryMapValidate(boot_info, &range_count))
        return 0;
    for (index = 0; index < STAGE2_MAX_BOOT_MODULES; ++index) {
        module_data[index] = 0;
        module_names[index] = 0;
    }
    table_bytes = (uint64_t)boot_info->allocation_count *
        boot_info->allocation_entry_size;
    proof->retained_bytes = 0;
    proof->release_after_stage2_bytes = 0;
    proof->allocation_count = boot_info->allocation_count;
    proof->retained_count = 0;
    proof->release_after_stage2_count = 0;
    proof->reserved = 0;

    for (index = 0; index < boot_info->allocation_count; ++index) {
        allocation = allocation_at(boot_info, index);
        if (allocation->physical_address == 0 || allocation->size == 0 ||
            (allocation->physical_address & (EFI_PAGE_SIZE - 1U)) != 0 ||
            (allocation->size & (EFI_PAGE_SIZE - 1U)) != 0 ||
            allocation->physical_address > UINTPTR_MAX ||
            !range_contains(allocation->physical_address, allocation->size,
                allocation->used_physical, allocation->used_size) ||
            allocation->reserved != 0 ||
            !allocation_has_loader_range(
                boot_info, allocation, range_count))
            return 0;
        for (other_index = 0; other_index < index; ++other_index) {
            other = allocation_at(boot_info, other_index);
            if (ranges_overlap(allocation->physical_address, allocation->size,
                    other->physical_address, other->size))
                return 0;
        }

        if (exact_lifetime(allocation->flags,
                AROS_BOOT_ALLOCATION_FLAG_RETAIN)) {
            if (!add_u64(proof->retained_bytes, allocation->size,
                    &proof->retained_bytes))
                return 0;
            ++proof->retained_count;
        } else if (exact_lifetime(allocation->flags,
                AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2)) {
            if (!add_u64(proof->release_after_stage2_bytes,
                    allocation->size,
                    &proof->release_after_stage2_bytes))
                return 0;
            ++proof->release_after_stage2_count;
        } else {
            return 0;
        }

        switch (allocation->type) {
        case AROS_BOOT_ALLOCATION_BOOTINFO:
            if (!exact_lifetime(allocation->flags,
                    AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2) ||
                allocation->module_index != AROS_BOOT_ALLOCATION_NO_MODULE ||
                !used_matches(allocation, (uintptr_t)boot_info,
                    sizeof(*boot_info), 0))
                return 0;
            ++bootinfo_count;
            break;
        case AROS_BOOT_ALLOCATION_TABLE:
            if (!exact_lifetime(allocation->flags,
                    AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2) ||
                allocation->module_index != AROS_BOOT_ALLOCATION_NO_MODULE ||
                !used_matches(allocation, boot_info->allocations_physical,
                    table_bytes, 1))
                return 0;
            ++table_count;
            break;
        case AROS_BOOT_ALLOCATION_HANDOFF_STACK:
            if (!exact_lifetime(allocation->flags,
                    AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2) ||
                allocation->module_index != AROS_BOOT_ALLOCATION_NO_MODULE ||
                (allocation->used_physical & 15U) != 0 ||
                allocation->used_size < STAGE2_HANDOFF_STACK_MIN)
                return 0;
            ++stack_count;
            break;
        case AROS_BOOT_ALLOCATION_EARLY_CONSOLE:
            if (++early_console_count > 1 ||
                !exact_lifetime(allocation->flags, AROS_BOOT_ALLOCATION_FLAG_RETAIN) ||
                allocation->module_index != AROS_BOOT_ALLOCATION_NO_MODULE ||
                allocation->used_size != 4096 || (allocation->used_physical & 4095))
                return 0;
            break;
        case AROS_BOOT_ALLOCATION_FDT:
            if (!exact_lifetime(allocation->flags,
                    AROS_BOOT_ALLOCATION_FLAG_RETAIN) ||
                allocation->module_index != AROS_BOOT_ALLOCATION_NO_MODULE ||
                !used_matches(allocation, boot_info->fdt_physical,
                    boot_info->fdt_size, 0))
                return 0;
            ++fdt_count;
            break;
        case AROS_BOOT_ALLOCATION_MEMORY_MAP:
            if (!exact_lifetime(allocation->flags,
                    AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2) ||
                allocation->module_index != AROS_BOOT_ALLOCATION_NO_MODULE)
                return 0;
            if (used_matches(allocation, boot_info->memory_map_physical,
                    boot_info->memory_map_size, 1))
                ++map_match_count;
            break;
        case AROS_BOOT_ALLOCATION_LOADER_LOG:
            if (!exact_lifetime(allocation->flags,
                    AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2) ||
                allocation->module_index != AROS_BOOT_ALLOCATION_NO_MODULE ||
                !used_matches(allocation, boot_info->loader_log_physical,
                    boot_info->loader_log_size, 1))
                return 0;
            ++log_count;
            break;
        case AROS_BOOT_ALLOCATION_MODULE_TABLE:
            expected_size = (uint64_t)boot_info->module_count *
                boot_info->module_entry_size;
            if (!exact_lifetime(allocation->flags,
                    AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2) ||
                allocation->module_index != AROS_BOOT_ALLOCATION_NO_MODULE ||
                !used_matches(allocation, boot_info->modules_physical,
                    expected_size, 0))
                return 0;
            ++module_table_count;
            break;
        case AROS_BOOT_ALLOCATION_MODULE_DATA:
            if (allocation->module_index >= boot_info->module_count ||
                module_data[allocation->module_index] != 0)
                return 0;
            module = module_at(boot_info, allocation->module_index);
            if (!used_matches(allocation, module->physical_address,
                    module->size, 0))
                return 0;
            if (module->type == AROS_BOOT_MODULE_COMPONENT) {
                if (!exact_lifetime(allocation->flags,
                        AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2))
                    return 0;
            } else if ((module->type == AROS_BOOT_MODULE_KERNEL ||
                        module->type == AROS_BOOT_MODULE_RAMDISK) &&
                    !exact_lifetime(allocation->flags,
                        AROS_BOOT_ALLOCATION_FLAG_RETAIN)) {
                return 0;
            }
            if (allocation->module_index == 0 &&
                (stage2_image_start != module->physical_address ||
                 stage2_image_end <= stage2_image_start ||
                 !range_contains(module->physical_address, module->size,
                    stage2_image_start, stage2_image_end - stage2_image_start)))
                return 0;
            module_data[allocation->module_index] = 1;
            break;
        case AROS_BOOT_ALLOCATION_MODULE_NAME:
            if (allocation->module_index >= boot_info->module_count ||
                module_names[allocation->module_index] != 0 ||
                !exact_lifetime(allocation->flags,
                    AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2))
                return 0;
            module = module_at(boot_info, allocation->module_index);
            if (!used_matches(allocation, module->name_physical,
                    module->name_size, 0))
                return 0;
            module_names[allocation->module_index] = 1;
            break;
        default:
            return 0;
        }
    }

    for (index = 0; index < boot_info->module_count; ++index) {
        if (module_data[index] != 1 || module_names[index] != 1)
            return 0;
    }
    page_bytes = proof->retained_bytes +
        proof->release_after_stage2_bytes;
    if (page_bytes < proof->retained_bytes || bootinfo_count != 1 ||
        table_count != 1 || stack_count != 1 || fdt_count != 1 ||
        map_match_count != 1 || module_table_count != 1 ||
        (((boot_info->flags & AROS_BOOTINFO_FLAG_LOADER_LOG) != 0) ?
            log_count != 1 : log_count != 0))
        return 0;
    return proof->retained_count + proof->release_after_stage2_count ==
        proof->allocation_count;
}
