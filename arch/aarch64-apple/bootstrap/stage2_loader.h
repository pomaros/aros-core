/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#ifndef AROS_AARCH64_APPLE_STAGE2_LOADER_H
#define AROS_AARCH64_APPLE_STAGE2_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include <aros/apple/bootinfo.h>
#include "stage2_fdt_map.h"
#include "stage2_ownership.h"

#define AROS_APPLE_STAGE2_MAX_MODULES 256U
#define AROS_APPLE_STAGE2_MAX_SECTIONS 8192U
#define AROS_APPLE_STAGE2_MAX_BSS_RANGES 2048U

enum AROSAppleStage2Status {
    AROS_APPLE_STAGE2_OK = 0,
    AROS_APPLE_STAGE2_INVALID_ARGUMENT,
    AROS_APPLE_STAGE2_INVALID_BOOTINFO,
    AROS_APPLE_STAGE2_INVALID_MODULE_TABLE,
    AROS_APPLE_STAGE2_MODULE_ORDER,
    AROS_APPLE_STAGE2_DUPLICATE_MODULE,
    AROS_APPLE_STAGE2_RANGE_OVERFLOW,
    AROS_APPLE_STAGE2_SOURCE_OVERLAP,
    AROS_APPLE_STAGE2_TOO_MANY_MODULES,
    AROS_APPLE_STAGE2_TOO_MANY_SECTIONS,
    AROS_APPLE_STAGE2_TOO_MANY_BSS_RANGES,
    AROS_APPLE_STAGE2_INVALID_ELF,
    AROS_APPLE_STAGE2_INVALID_SECTION,
    AROS_APPLE_STAGE2_INVALID_SYMBOL_TABLE,
    AROS_APPLE_STAGE2_MISSING_SYMBOL,
    AROS_APPLE_STAGE2_DUPLICATE_SYMBOL,
    AROS_APPLE_STAGE2_UNDEFINED_SYMBOL,
    AROS_APPLE_STAGE2_COMMON_SYMBOL,
    AROS_APPLE_STAGE2_INVALID_RELOCATION,
    AROS_APPLE_STAGE2_UNSUPPORTED_RELOCATION,
    AROS_APPLE_STAGE2_RELOCATION_OVERFLOW,
    AROS_APPLE_STAGE2_ARENA_EXHAUSTED,
    AROS_APPLE_STAGE2_INVALID_OWNERSHIP,
    AROS_APPLE_STAGE2_INVALID_MEMORY_MAP,
    AROS_APPLE_STAGE2_INVALID_MEMORY_PLAN,
    AROS_APPLE_STAGE2_NO_USABLE_MEMORY,
    AROS_APPLE_STAGE2_TOO_MANY_BOOT_TAGS,
    AROS_APPLE_STAGE2_KERNEL_CONTRACT,
    AROS_APPLE_STAGE2_MEMORY_PROBE,
    AROS_APPLE_STAGE2_KERNEL_MEMORY,
    AROS_APPLE_STAGE2_EXEC_INIT,
    AROS_APPLE_STAGE2_SYSBASE,
    AROS_APPLE_STAGE2_SINGLE_TASK_INIT,
    AROS_APPLE_STAGE2_KERNEL_BASE,
    AROS_APPLE_STAGE2_MEMORY_IMPORT,
    AROS_APPLE_STAGE2_RUNTIME,
    AROS_APPLE_STAGE2_DISPATCH,
    AROS_APPLE_STAGE2_COLDSTART,
    AROS_APPLE_STAGE2_INVALID_DEVICE_MAP,
    AROS_APPLE_STAGE2_MMU,
    AROS_APPLE_STAGE2_OBSERVER
};

struct AROSAppleKernelBSS {
    uintptr_t address;
    uintptr_t length;
};

struct AROSAppleStage2ModuleState {
    uintptr_t image;
    uint64_t image_size;
    uint32_t boot_module_index;
    uint32_t section_address_base;
    uint16_t section_count;
    uint16_t reserved;
};

struct AROSAppleStage2Context {
    struct AROSAppleStage2ModuleState
        modules[AROS_APPLE_STAGE2_MAX_MODULES];
    uintptr_t section_addresses[AROS_APPLE_STAGE2_MAX_SECTIONS];
    struct AROSAppleKernelBSS
        bss[AROS_APPLE_STAGE2_MAX_BSS_RANGES + 1U];
    uint32_t module_count;
    uint32_t section_count;
    uint32_t bss_count;
    uint32_t reserved;
    uintptr_t sysbase_storage;
};

struct AROSAppleStage2Result {
    uintptr_t arena_start;
    uintptr_t arena_end;
    uintptr_t arena_used_end;
    uintptr_t readonly_start;
    uintptr_t readonly_end;
    uintptr_t writable_start;
    uintptr_t writable_end;
    uintptr_t kernel_resident;
    uintptr_t exec_resident;
    uintptr_t sysbase_storage;
    uintptr_t ramdisk_address;
    uintptr_t page_arena_start;
    uintptr_t boot_tags;
    uintptr_t memory_plan;
    uintptr_t kernel_contract;
    uintptr_t kernel_prepare_memory;
    uintptr_t kernel_initialize_exec;
    uintptr_t kernel_initialize_single_task;
    uintptr_t kernel_import_memory;
    uintptr_t kernel_prove_runtime;
    uintptr_t kernel_prove_dispatch;
    uintptr_t kernel_prove_core_coldstart;
    uint64_t ramdisk_size;
    uint64_t page_arena_size;
    struct AROSAppleKernelBSS *bss;
    uint32_t bss_count;
    uint32_t loaded_module_count;
    uint32_t relocation_count;
    uint32_t boot_tag_count;
    uint32_t memory_range_count;
    uint32_t memory_range_drop_count;
    uint32_t memory_plan_size;
    uint32_t reserved;
    struct AROSAppleStage2OwnershipProof ownership;
    uintptr_t translation_table;
    uint64_t translation_mapped_bytes;
    uint32_t translation_table_count;
    uint32_t translation_reserved;
    uint64_t translation_normal_bytes;
    uint64_t translation_device_bytes;
    uint64_t translation_framebuffer_bytes;
    struct AROSAppleDeviceMap device_map;
};

enum AROSAppleStage2Status AROSAppleStage2Load(
    const struct AROSBootInfo *boot_info,
    uintptr_t stage2_image_start,
    uintptr_t stage2_image_end,
    struct AROSAppleStage2Context *context,
    struct AROSAppleStage2Result *result);

const char *AROSAppleStage2StatusName(enum AROSAppleStage2Status status);

#endif /* AROS_AARCH64_APPLE_STAGE2_LOADER_H */
