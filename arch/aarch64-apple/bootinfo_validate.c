/*
 * Copyright (C) 2026 John Knipper
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Validate the versioned loader-to-kernel handoff before dereferencing it.
 */

#include "bootinfo_validate.h"

#include <stdint.h>

static int digest_is_zero(const uint8_t digest[32])
{
    uint8_t value = 0;
    uint32_t index;

    for (index = 0; index < 32; ++index)
        value |= digest[index];
    return value == 0;
}

enum AROSAppleBootInfoStatus
AROSAppleBootInfoValidate(const struct AROSBootInfo *boot_info)
{
    const uint64_t known_flags =
        AROS_BOOTINFO_FLAG_FDT |
        AROS_BOOTINFO_FLAG_MEMORY_MAP |
        AROS_BOOTINFO_FLAG_FRAMEBUFFER |
        AROS_BOOTINFO_FLAG_MODULES |
        AROS_BOOTINFO_FLAG_LOADER_LOG |
        AROS_BOOTINFO_FLAG_ALLOCATIONS |
        AROS_BOOTINFO_FLAG_PAYLOAD_IDENTITY;
    uint64_t framebuffer_pixels;

    if (boot_info == 0)
        return AROS_APPLE_BOOTINFO_NULL;
    if (((uintptr_t)boot_info & 15U) != 0)
        return AROS_APPLE_BOOTINFO_ALIGNMENT;
    if (boot_info->magic != AROS_BOOTINFO_MAGIC)
        return AROS_APPLE_BOOTINFO_MAGIC;
    if (boot_info->version != AROS_BOOTINFO_VERSION)
        return AROS_APPLE_BOOTINFO_VERSION;
    if (boot_info->size < sizeof(*boot_info))
        return AROS_APPLE_BOOTINFO_SIZE;
    if (boot_info->reserved != 0 || boot_info->payload_reserved != 0)
        return AROS_APPLE_BOOTINFO_RESERVED;
    if ((boot_info->flags & ~known_flags) != 0)
        return AROS_APPLE_BOOTINFO_FLAGS;

    if ((boot_info->flags & AROS_BOOTINFO_FLAG_FDT) == 0 ||
        boot_info->fdt_physical == 0 || boot_info->fdt_size < 40)
        return AROS_APPLE_BOOTINFO_FDT;

    if ((boot_info->flags & AROS_BOOTINFO_FLAG_MEMORY_MAP) == 0 ||
        boot_info->memory_map_physical == 0 ||
        boot_info->memory_map_entry_size == 0 ||
        boot_info->memory_map_size < boot_info->memory_map_entry_size ||
        boot_info->memory_map_size % boot_info->memory_map_entry_size != 0)
        return AROS_APPLE_BOOTINFO_MEMORY_MAP;
    if (boot_info->memory_map_format != AROS_BOOT_MEMORY_MAP_UEFI &&
        boot_info->memory_map_format != AROS_BOOT_MEMORY_MAP_NORMALIZED)
        return AROS_APPLE_BOOTINFO_MEMORY_MAP_FORMAT;
    if (boot_info->memory_map_version != AROS_BOOT_MEMORY_MAP_VERSION_1)
        return AROS_APPLE_BOOTINFO_MEMORY_MAP_VERSION;
    if (boot_info->memory_map_format == AROS_BOOT_MEMORY_MAP_UEFI) {
        if (boot_info->memory_map_entry_size < 40 ||
            (boot_info->memory_map_entry_size & 7U) != 0 ||
            (boot_info->memory_map_physical & 7U) != 0)
            return AROS_APPLE_BOOTINFO_MEMORY_MAP;
    } else if (boot_info->memory_map_entry_size <
            sizeof(struct AROSBootMemoryRange) ||
        (boot_info->memory_map_entry_size & 15U) != 0 ||
        (boot_info->memory_map_physical & 15U) != 0) {
        return AROS_APPLE_BOOTINFO_MEMORY_MAP;
    }
    if (boot_info->producer != AROS_BOOT_PRODUCER_UEFI &&
        boot_info->producer != AROS_BOOT_PRODUCER_M1N1)
        return AROS_APPLE_BOOTINFO_PRODUCER;

    if ((boot_info->flags & AROS_BOOTINFO_FLAG_PAYLOAD_IDENTITY) == 0 ||
        boot_info->payload_size == 0 ||
        digest_is_zero(boot_info->payload_sha256) ||
        (boot_info->payload_flags & ~(AROS_BOOT_PAYLOAD_FALLBACK |
            AROS_BOOT_PAYLOAD_DEGRADED | AROS_BOOT_PAYLOAD_DIRECT)) != 0)
        return AROS_APPLE_BOOTINFO_PAYLOAD_IDENTITY;
    if (boot_info->producer == AROS_BOOT_PRODUCER_UEFI &&
        (boot_info->payload_flags & AROS_BOOT_PAYLOAD_DIRECT) == 0) {
        if (boot_info->payload_generation_sequence == 0 ||
            boot_info->payload_slot > 1 ||
            (boot_info->payload_flags & AROS_BOOT_PAYLOAD_DIRECT) != 0 ||
            ((boot_info->payload_flags & AROS_BOOT_PAYLOAD_FALLBACK) != 0 &&
             (boot_info->payload_flags & AROS_BOOT_PAYLOAD_DEGRADED) == 0))
            return AROS_APPLE_BOOTINFO_PAYLOAD_IDENTITY;
    } else if (boot_info->payload_generation_sequence != 0 ||
            boot_info->payload_slot != AROS_BOOT_PAYLOAD_SLOT_DIRECT ||
            boot_info->payload_flags != AROS_BOOT_PAYLOAD_DIRECT) {
        return AROS_APPLE_BOOTINFO_PAYLOAD_IDENTITY;
    }

    if ((boot_info->flags & AROS_BOOTINFO_FLAG_FRAMEBUFFER) != 0) {
        if (boot_info->framebuffer_physical == 0 ||
            boot_info->framebuffer_width == 0 ||
            boot_info->framebuffer_height == 0 ||
            boot_info->framebuffer_stride < boot_info->framebuffer_width ||
            boot_info->framebuffer_format == AROS_BOOTINFO_FB_NONE ||
            boot_info->framebuffer_format > AROS_BOOTINFO_FB_X2R10G10B10)
            return AROS_APPLE_BOOTINFO_FRAMEBUFFER;
        framebuffer_pixels =
            (uint64_t)boot_info->framebuffer_stride * boot_info->framebuffer_height;
        if (framebuffer_pixels > boot_info->framebuffer_size / sizeof(uint32_t))
            return AROS_APPLE_BOOTINFO_FRAMEBUFFER;
    }

    if ((boot_info->flags & AROS_BOOTINFO_FLAG_MODULES) != 0) {
        if (boot_info->modules_physical == 0 || boot_info->module_count == 0 ||
            boot_info->module_entry_size < sizeof(struct AROSBootModule) ||
            (boot_info->modules_physical & 15U) != 0)
            return AROS_APPLE_BOOTINFO_MODULES;
    } else if (boot_info->modules_physical != 0 || boot_info->module_count != 0) {
        return AROS_APPLE_BOOTINFO_MODULES;
    }

    if ((boot_info->flags & AROS_BOOTINFO_FLAG_LOADER_LOG) != 0) {
        if (boot_info->loader_log_physical == 0 || boot_info->loader_log_size == 0)
            return AROS_APPLE_BOOTINFO_LOADER_LOG;
    } else if (boot_info->loader_log_physical != 0 ||
               boot_info->loader_log_size != 0) {
        return AROS_APPLE_BOOTINFO_LOADER_LOG;
    }

    if ((boot_info->flags & AROS_BOOTINFO_FLAG_ALLOCATIONS) == 0 ||
        boot_info->allocations_physical == 0 ||
        (boot_info->allocations_physical & 15U) != 0 ||
        boot_info->allocation_count == 0 ||
        boot_info->allocation_entry_size < sizeof(struct AROSBootAllocation) ||
        (uint64_t)boot_info->allocation_count >
            (UINT64_MAX - boot_info->allocations_physical) /
                boot_info->allocation_entry_size)
        return AROS_APPLE_BOOTINFO_ALLOCATIONS;

    return AROS_APPLE_BOOTINFO_OK;
}
