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

#define EFI_LOADER_CODE UINT32_C(1)
#define EFI_BOOT_SERVICES_CODE UINT32_C(3)
#define EFI_BOOT_SERVICES_DATA UINT32_C(4)
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

/*
 * Memory the device tree reserves: the /memreserve/ block and the reg ranges
 * of the children of /reserved-memory. U-Boot reports a reserved-memory node
 * without no-map as boot-services data (the OS is expected to honour the
 * tree), so boot-services memory is free only where the tree does not reserve
 * it. Parsed once per tree and kept: stage two runs this before its caches
 * are on.
 */
#define FDT_RESERVED_MAX 64U
static struct { uint64_t start, end; } fdt_reserved[FDT_RESERVED_MAX];
static uint32_t fdt_reserved_count;
static uint64_t fdt_reserved_source;
static int fdt_reserved_valid;

static uint32_t be32_at(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
        ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t cells_at(const uint8_t *p, uint32_t cells)
{
    uint64_t value = 0;
    uint32_t i;

    for (i = 0; i < cells; ++i)
        value = (value << 32) | be32_at(p + 4U * i);
    return value;
}

static void reserve_add(uint64_t start, uint64_t size)
{
    if (size == 0 || start > UINT64_MAX - size)
        return;
    if (fdt_reserved_count == FDT_RESERVED_MAX) {
        fdt_reserved_valid = 0;     /* too many: trust nothing extra */
        return;
    }
    fdt_reserved[fdt_reserved_count].start = start;
    fdt_reserved[fdt_reserved_count].end = start + size;
    ++fdt_reserved_count;
}

static int name_is(const uint8_t *name, uint32_t room, const char *wanted)
{
    uint32_t i;

    for (i = 0; wanted[i] != '\0'; ++i)
        if (i >= room || name[i] != (uint8_t)wanted[i])
            return 0;
    return i < room && (name[i] == '\0' || name[i] == '@');
}

static void fdt_reserved_scan(const struct AROSBootInfo *boot_info)
{
    const uint8_t *data = (const uint8_t *)(uintptr_t)boot_info->fdt_physical;
    uint32_t total, off_struct, size_struct, off_strings, size_strings, off_rsv;
    uint32_t pos, end, depth = 0, in_reserved = 0;
    uint32_t addr_cells = 2, size_cells = 2;

    fdt_reserved_count = 0;
    fdt_reserved_valid = 0;
    fdt_reserved_source = boot_info->fdt_physical;
    if (boot_info->fdt_physical == 0 || boot_info->fdt_size < 40U ||
        boot_info->fdt_size > UINT32_MAX || be32_at(data) != UINT32_C(0xd00dfeed))
        return;
    total = be32_at(data + 4);
    off_struct = be32_at(data + 8);
    off_strings = be32_at(data + 12);
    off_rsv = be32_at(data + 16);
    size_strings = be32_at(data + 32);
    size_struct = be32_at(data + 36);
    if (total > boot_info->fdt_size || off_struct > total ||
        size_struct > total - off_struct || off_strings > total ||
        size_strings > total - off_strings || off_rsv > total - 16U ||
        (off_struct & 3U) != 0)
        return;
    fdt_reserved_valid = 1;
    for (pos = off_rsv; pos <= total - 16U; pos += 16U) {
        uint64_t a = cells_at(data + pos, 2), n = cells_at(data + pos + 8, 2);

        if (a == 0 && n == 0)
            break;
        reserve_add(a, n);
    }
    pos = off_struct;
    end = off_struct + size_struct;
    while (pos + 4U <= end && fdt_reserved_valid) {
        uint32_t token = be32_at(data + pos);

        pos += 4U;
        if (token == 1U) {                          /* FDT_BEGIN_NODE */
            const uint8_t *name = data + pos;
            uint32_t len = 0;

            while (pos + len < end && name[len] != 0)
                ++len;
            if (pos + len >= end)
                break;
            ++depth;
            if (depth == 2 && name_is(name, len + 1U, "reserved-memory"))
                in_reserved = 1;
            pos = (pos + len + 1U + 3U) & ~3U;
        } else if (token == 2U) {                   /* FDT_END_NODE */
            if (depth == 2)
                in_reserved = 0;
            if (depth == 0)
                break;
            --depth;
        } else if (token == 3U) {                   /* FDT_PROP */
            uint32_t len, nameoff;
            const uint8_t *value, *pname;

            if (pos + 8U > end)
                break;
            len = be32_at(data + pos);
            nameoff = be32_at(data + pos + 4);
            value = data + pos + 8U;
            if (len > end - pos - 8U || nameoff >= size_strings)
                break;
            pname = data + off_strings + nameoff;
            if (in_reserved && depth == 2) {
                if (name_is(pname, size_strings - nameoff, "#address-cells") && len == 4)
                    addr_cells = be32_at(value);
                else if (name_is(pname, size_strings - nameoff, "#size-cells") && len == 4)
                    size_cells = be32_at(value);
            } else if (in_reserved && depth == 3 &&
                    name_is(pname, size_strings - nameoff, "reg") &&
                    addr_cells >= 1 && addr_cells <= 2 &&
                    size_cells >= 1 && size_cells <= 2) {
                uint32_t step = 4U * (addr_cells + size_cells), at;

                for (at = 0; at + step <= len; at += step)
                    reserve_add(cells_at(value + at, addr_cells),
                        cells_at(value + at + 4U * addr_cells, size_cells));
            }
            pos = (pos + 8U + len + 3U) & ~3U;
        } else if (token == 9U) {                   /* FDT_END */
            break;
        } else if (token != 4U) {                   /* not FDT_NOP */
            fdt_reserved_valid = 0;
            break;
        }
    }
}

/* Whether [start, end) touches memory the device tree reserves. Without a
 * readable tree, every boot-services range counts as reserved. */
static int fdt_reserves(const struct AROSBootInfo *boot_info,
    uint64_t start, uint64_t end)
{
    uint32_t i;

    if (fdt_reserved_source != boot_info->fdt_physical || boot_info->fdt_physical == 0)
        fdt_reserved_scan(boot_info);
    if (!fdt_reserved_valid)
        return 1;
    for (i = 0; i < fdt_reserved_count; ++i)
        if (start < fdt_reserved[i].end && fdt_reserved[i].start < end)
            return 1;
    return 0;
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
        else if (efi->type == EFI_CONVENTIONAL_MEMORY ||
            ((efi->type == EFI_LOADER_CODE ||
              efi->type == EFI_BOOT_SERVICES_CODE ||
              efi->type == EFI_BOOT_SERVICES_DATA) &&
             !fdt_reserves(boot_info, efi->physical_start,
                 efi->physical_start +
                     efi->number_of_pages * BOOT_MEMORY_PAGE_SIZE)))
            /* The loader has left boot services and owns everything it
             * still needs in tracked loader-data allocations (FDT, memory
             * map, handoff stack, bundle). Firmware boot-services memory and
             * the loader image itself are then free RAM (UEFI 2.10, 7.4),
             * as every operating system treats them; only runtime ranges
             * and device-tree reserved ranges stay reserved. */
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
