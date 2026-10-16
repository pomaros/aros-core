/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Validate UEFI or normalized BootInfo memory records and expose one
 * common, bounded view to stage two.
 */

#include "boot_memory_map.h"

#include <stddef.h>

#define EFI_LOADER_DATA UINT32_C(2)
#define EFI_CONVENTIONAL_MEMORY UINT32_C(7)
#define EFI_MEMORY_RUNTIME (UINT64_C(1) << 63)
#define BOOT_MEMORY_PAGE_SIZE UINT64_C(4096)

struct BootMemoryEFIDescriptor {
    uint32_t type;
    uint32_t padding;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
};

_Static_assert(sizeof(struct BootMemoryEFIDescriptor) == 40,
    "UEFI memory descriptor prefix size");

static int add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right > UINT64_MAX - left)
        return 0;
    *result = left + right;
    return 1;
}

static const void *record_at(
    const struct AROSBootInfo *boot_info, uint32_t index)
{
    return (const void *)(uintptr_t)(boot_info->memory_map_physical +
        (uint64_t)index * boot_info->memory_map_entry_size);
}

int AROSAppleBootMemoryMapRangeAt(
    const struct AROSBootInfo *boot_info, uint32_t index,
    struct AROSAppleBootMemoryView *view)
{
    const struct BootMemoryEFIDescriptor *efi;
    const struct AROSBootMemoryRange *normalized;
    uint64_t count;
    uint64_t size;

    if (boot_info == NULL || view == NULL ||
        boot_info->memory_map_physical == 0 ||
        boot_info->memory_map_physical > UINTPTR_MAX ||
        boot_info->memory_map_entry_size == 0 ||
        boot_info->memory_map_size % boot_info->memory_map_entry_size != 0 ||
        boot_info->memory_map_size >
            UINTPTR_MAX - boot_info->memory_map_physical)
        return 0;
    count = boot_info->memory_map_size / boot_info->memory_map_entry_size;
    if (index >= count)
        return 0;

    view->source_index = index;
    if (boot_info->memory_map_format == AROS_BOOT_MEMORY_MAP_UEFI) {
        if (boot_info->memory_map_entry_size < sizeof(*efi))
            return 0;
        if ((boot_info->memory_map_physical & 7U) != 0 ||
            (boot_info->memory_map_entry_size & 7U) != 0)
            return 0;
        efi = (const struct BootMemoryEFIDescriptor *)record_at(
            boot_info, index);
        if (efi->number_of_pages == 0 ||
            efi->number_of_pages > UINT64_MAX / BOOT_MEMORY_PAGE_SIZE ||
            (efi->physical_start & (BOOT_MEMORY_PAGE_SIZE - 1U)) != 0)
            return 0;
        size = efi->number_of_pages * BOOT_MEMORY_PAGE_SIZE;
        view->start = efi->physical_start;
        if ((efi->attribute & EFI_MEMORY_RUNTIME) != 0)
            view->kind = AROS_APPLE_BOOT_MEMORY_RESERVED;
        else if (efi->type == EFI_CONVENTIONAL_MEMORY)
            view->kind = AROS_APPLE_BOOT_MEMORY_AVAILABLE;
        else if (efi->type == EFI_LOADER_DATA)
            view->kind = AROS_APPLE_BOOT_MEMORY_LOADER_DATA;
        else
            view->kind = AROS_APPLE_BOOT_MEMORY_RESERVED;
    } else if (boot_info->memory_map_format ==
            AROS_BOOT_MEMORY_MAP_NORMALIZED) {
        if (boot_info->memory_map_entry_size < sizeof(*normalized))
            return 0;
        if ((boot_info->memory_map_physical & 15U) != 0 ||
            (boot_info->memory_map_entry_size & 15U) != 0)
            return 0;
        normalized = (const struct AROSBootMemoryRange *)record_at(
            boot_info, index);
        if (normalized->size == 0 ||
            (normalized->physical_start &
                (BOOT_MEMORY_PAGE_SIZE - 1U)) != 0 ||
            (normalized->size & (BOOT_MEMORY_PAGE_SIZE - 1U)) != 0 ||
            normalized->flags != 0 || normalized->reserved != 0)
            return 0;
        view->start = normalized->physical_start;
        size = normalized->size;
        switch (normalized->type) {
        case AROS_BOOT_MEMORY_AVAILABLE:
            view->kind = AROS_APPLE_BOOT_MEMORY_AVAILABLE;
            break;
        case AROS_BOOT_MEMORY_LOADER_DATA:
            view->kind = AROS_APPLE_BOOT_MEMORY_LOADER_DATA;
            break;
        case AROS_BOOT_MEMORY_RESERVED:
            view->kind = AROS_APPLE_BOOT_MEMORY_RESERVED;
            break;
        default:
            return 0;
        }
    } else {
        return 0;
    }
    return add_u64(view->start, size, &view->end) &&
        view->end > view->start && view->end <= UINTPTR_MAX;
}

int AROSAppleBootMemoryMapValidate(
    const struct AROSBootInfo *boot_info, uint32_t *range_count)
{
    struct AROSAppleBootMemoryView first;
    struct AROSAppleBootMemoryView second;
    uint64_t count64;
    uint32_t index;
    uint32_t other;

    if (boot_info == NULL || range_count == NULL ||
        boot_info->memory_map_version != AROS_BOOT_MEMORY_MAP_VERSION_1 ||
        boot_info->memory_map_physical == 0 ||
        boot_info->memory_map_physical > UINTPTR_MAX ||
        boot_info->memory_map_entry_size == 0 ||
        boot_info->memory_map_size < boot_info->memory_map_entry_size ||
        boot_info->memory_map_size % boot_info->memory_map_entry_size != 0 ||
        boot_info->memory_map_size >
            UINTPTR_MAX - boot_info->memory_map_physical)
        return 0;
    count64 = boot_info->memory_map_size /
        boot_info->memory_map_entry_size;
    if (count64 == 0 || count64 > AROS_APPLE_BOOT_MEMORY_MAX_RANGES)
        return 0;

    for (index = 0; index < (uint32_t)count64; ++index) {
        if (!AROSAppleBootMemoryMapRangeAt(boot_info, index, &first))
            return 0;
        for (other = 0; other < index; ++other) {
            if (!AROSAppleBootMemoryMapRangeAt(boot_info, other, &second) ||
                (first.start < second.end && second.start < first.end))
                return 0;
        }
    }
    *range_count = (uint32_t)count64;
    return 1;
}
