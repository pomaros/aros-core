/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Bounded AArch64 ET_REL loader for the Apple boot handoff.
 */

#include "stage2_loader.h"
#include "boot_memory_map.h"
#include "stage2_memory_plan.h"
#include "stage2_tags.h"

#include "../bootinfo_validate.h"

#include <aros/apple/startup.h>
#include <aros/apple/page_arena.h>

#include <limits.h>

#define ELF_MAGIC_0 0x7fU
#define ELF_CLASS_64 2U
#define ELF_DATA_LITTLE 1U
#define ELF_VERSION_CURRENT 1U
#define ELF_OSABI_AROS 15U
#define ELF_ABI_VERSION_AROS 1U
#define ELF_TYPE_REL 1U
#define ELF_MACHINE_AARCH64 183U

#define SHT_PROGBITS 1U
#define SHT_SYMTAB 2U
#define SHT_STRTAB 3U
#define SHT_RELA 4U
#define SHT_NOBITS 8U
#define SHT_REL 9U

#define SHF_WRITE UINT64_C(1)
#define STAGE2_PAGE_SIZE UINT64_C(4096)
#define STAGE2_PAGE_ARENA_MIN (UINT64_C(64) * 1024U)
#define STAGE2_PAGE_ARENA_MAX AROS_APPLE_PAGE_ARENA_MAX_BYTES
#define SHF_ALLOC UINT64_C(2)

#define SHN_UNDEF 0U
#define SHN_LORESERVE 0xff00U
#define SHN_ABS 0xfff1U
#define SHN_COMMON 0xfff2U

#define STB_GLOBAL 1U

#define R_AARCH64_NONE 0U
#define R_AARCH64_ABS64 257U
#define R_AARCH64_ABS32 258U
#define R_AARCH64_PREL64 260U
#define R_AARCH64_PREL32 261U
#define R_AARCH64_MOVW_UABS_G0 263U
#define R_AARCH64_MOVW_UABS_G0_NC 264U
#define R_AARCH64_MOVW_UABS_G1 265U
#define R_AARCH64_MOVW_UABS_G1_NC 266U
#define R_AARCH64_MOVW_UABS_G2 267U
#define R_AARCH64_MOVW_UABS_G2_NC 268U
#define R_AARCH64_MOVW_UABS_G3 269U
#define R_AARCH64_ADR_PREL_PG_HI21 275U
#define R_AARCH64_ADD_ABS_LO12_NC 277U
#define R_AARCH64_LDST8_ABS_LO12_NC 278U
#define R_AARCH64_JUMP26 282U
#define R_AARCH64_CALL26 283U
#define R_AARCH64_LDST16_ABS_LO12_NC 284U
#define R_AARCH64_LDST32_ABS_LO12_NC 285U
#define R_AARCH64_LDST64_ABS_LO12_NC 286U
#define R_AARCH64_LDST128_ABS_LO12_NC 299U

#define STAGE2_PAGE_SIZE UINT64_C(4096)
#define STAGE2_MAX_SECTION_ALIGNMENT UINT64_C(2097152)

struct Stage2ELFHeader {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_offset;
    uint64_t section_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_entry_size;
    uint16_t program_count;
    uint16_t section_entry_size;
    uint16_t section_count;
    uint16_t section_name_index;
};

struct Stage2Section {
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t address;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t alignment;
    uint64_t entry_size;
};

struct Stage2Symbol {
    uint32_t name;
    uint8_t info;
    uint8_t other;
    uint16_t section_index;
    uint64_t value;
    uint64_t size;
};

struct Stage2Rela {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
};

struct Stage2Rel {
    uint64_t offset;
    uint64_t info;
};

_Static_assert(sizeof(struct Stage2ELFHeader) == 64, "ELF64 header size");
_Static_assert(sizeof(struct Stage2Section) == 64, "ELF64 section size");
_Static_assert(sizeof(struct Stage2Symbol) == 24, "ELF64 symbol size");
_Static_assert(sizeof(struct Stage2Rela) == 24, "ELF64 rela size");
_Static_assert(sizeof(struct Stage2Rel) == 16, "ELF64 rel size");

static void bytes_zero(void *destination, size_t size)
{
    uint8_t *out = (uint8_t *)destination;
    size_t index;

    for (index = 0; index < size; ++index)
        out[index] = 0;
}

static void bytes_copy(void *destination, const void *source, size_t size)
{
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    size_t index;

    for (index = 0; index < size; ++index)
        out[index] = in[index];
}

static int name_equal(const char *value, size_t size, const char *expected)
{
    size_t index = 0;

    while (expected[index] != '\0') {
        if (index >= size || value[index] != expected[index])
            return 0;
        ++index;
    }
    return index == size;
}

static int cstring_equal(const char *left, const char *right)
{
    size_t index = 0;

    for (;;) {
        if (left[index] != right[index])
            return 0;
        if (left[index] == '\0')
            return 1;
        ++index;
    }
}

static int add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right > UINT64_MAX - left)
        return 0;
    *result = left + right;
    return 1;
}

static int multiply_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (left != 0 && right > UINT64_MAX / left)
        return 0;
    *result = left * right;
    return 1;
}

static int range_end(uint64_t start, uint64_t size, uint64_t *end)
{
    return size != 0 && add_u64(start, size, end);
}

static int range_inside(uint64_t offset, uint64_t size, uint64_t total)
{
    return offset <= total && size <= total - offset;
}

static int ranges_overlap(
    uint64_t first_start, uint64_t first_size,
    uint64_t second_start, uint64_t second_size)
{
    uint64_t first_end;
    uint64_t second_end;

    if (!range_end(first_start, first_size, &first_end) ||
        !range_end(second_start, second_size, &second_end))
        return 1;
    return first_start < second_end && second_start < first_end;
}

static int power_of_two(uint64_t value)
{
    return value != 0 && (value & (value - 1U)) == 0;
}

static int align_up(uint64_t value, uint64_t alignment, uint64_t *result)
{
    uint64_t mask;

    if (!power_of_two(alignment))
        return 0;
    mask = alignment - 1U;
    if (value > UINT64_MAX - mask)
        return 0;
    *result = (value + mask) & ~mask;
    return 1;
}

static const struct AROSBootModule *boot_module_at(
    const struct AROSBootInfo *boot_info, uint32_t index)
{
    uintptr_t address = (uintptr_t)boot_info->modules_physical;
    uint64_t offset = (uint64_t)index * boot_info->module_entry_size;

    return (const struct AROSBootModule *)(address + (uintptr_t)offset);
}

static int module_name_equal(
    const struct AROSBootModule *module, const char *expected)
{
    uint64_t end;

    if (module->name_physical == 0 || module->name_size == 0 ||
        !range_end(module->name_physical, module->name_size, &end) ||
        end > UINTPTR_MAX)
        return 0;
    return name_equal(
        (const char *)(uintptr_t)module->name_physical,
        module->name_size, expected);
}

static int module_names_equal(
    const struct AROSBootModule *left,
    const struct AROSBootModule *right)
{
    const uint8_t *left_name;
    const uint8_t *right_name;
    uint32_t index;

    if (left->name_size != right->name_size)
        return 0;
    left_name = (const uint8_t *)(uintptr_t)left->name_physical;
    right_name = (const uint8_t *)(uintptr_t)right->name_physical;
    for (index = 0; index < left->name_size; ++index) {
        if (left_name[index] != right_name[index])
            return 0;
    }
    return 1;
}

static const struct Stage2ELFHeader *module_header(
    const struct AROSAppleStage2ModuleState *module)
{
    return (const struct Stage2ELFHeader *)module->image;
}

static const struct Stage2Section *module_section(
    const struct AROSAppleStage2ModuleState *module, uint32_t index)
{
    const struct Stage2ELFHeader *header = module_header(module);

    return (const struct Stage2Section *)(
        module->image + header->section_offset +
        (uint64_t)index * sizeof(struct Stage2Section));
}

static uintptr_t runtime_section(
    const struct AROSAppleStage2Context *context,
    uint32_t module_index, uint32_t section_index)
{
    const struct AROSAppleStage2ModuleState *module =
        &context->modules[module_index];

    return context->section_addresses[
        module->section_address_base + section_index];
}

static enum AROSAppleStage2Status validate_boot_module_table(
    const struct AROSBootInfo *boot_info,
    uintptr_t stage2_image_start,
    uintptr_t stage2_image_end,
    struct AROSAppleStage2Result *result)
{
    const struct AROSBootModule *module;
    const struct AROSBootModule *other;
    uint64_t table_size;
    uint64_t table_end;
    uint64_t stage2_end;
    uint32_t index;
    uint32_t other_index;

    if (boot_info->module_count < 4U ||
        boot_info->module_count > AROS_APPLE_STAGE2_MAX_MODULES + 2U ||
        boot_info->module_entry_size < sizeof(struct AROSBootModule) ||
        !multiply_u64(boot_info->module_count,
            boot_info->module_entry_size, &table_size) ||
        !range_end(boot_info->modules_physical, table_size, &table_end) ||
        table_end > UINTPTR_MAX)
        return AROS_APPLE_STAGE2_INVALID_MODULE_TABLE;

    for (index = 0; index < boot_info->module_count; ++index) {
        module = boot_module_at(boot_info, index);
        if (module->physical_address == 0 || module->size == 0 ||
            module->physical_address > UINTPTR_MAX ||
            !range_end(module->physical_address, module->size, &stage2_end) ||
            stage2_end > UINTPTR_MAX || module->name_physical == 0 ||
            module->name_size == 0 || module->flags != 0 ||
            module->reserved[0] != 0 || module->reserved[1] != 0 ||
            module->reserved[2] != 0)
            return AROS_APPLE_STAGE2_INVALID_MODULE_TABLE;

        if (index == 0) {
            if (module->type != AROS_BOOT_MODULE_KERNEL ||
                module->physical_address != stage2_image_start ||
                stage2_image_end <= stage2_image_start ||
                !range_end(module->physical_address, module->size,
                    &stage2_end) || stage2_image_end > stage2_end)
                return AROS_APPLE_STAGE2_MODULE_ORDER;
        } else if (index + 1U == boot_info->module_count) {
            if (module->type != AROS_BOOT_MODULE_RAMDISK)
                return AROS_APPLE_STAGE2_MODULE_ORDER;
            result->ramdisk_address = (uintptr_t)module->physical_address;
            result->ramdisk_size = module->size;
        } else if (module->type != AROS_BOOT_MODULE_COMPONENT) {
            return AROS_APPLE_STAGE2_MODULE_ORDER;
        }

        for (other_index = 0; other_index < index; ++other_index) {
            other = boot_module_at(boot_info, other_index);
            if (ranges_overlap(module->physical_address, module->size,
                    other->physical_address, other->size))
                return AROS_APPLE_STAGE2_SOURCE_OVERLAP;
            if (module_names_equal(module, other))
                return AROS_APPLE_STAGE2_DUPLICATE_MODULE;
        }
    }

    if (!module_name_equal(boot_module_at(boot_info, 1U),
            "kernel.resource") ||
        !module_name_equal(boot_module_at(boot_info, 2U), "exec.library"))
        return AROS_APPLE_STAGE2_MODULE_ORDER;

    return AROS_APPLE_STAGE2_OK;
}

static enum AROSAppleStage2Status validate_elf_module(
    const struct AROSBootModule *boot_module,
    struct AROSAppleStage2Context *context,
    uint32_t module_index)
{
    const struct Stage2ELFHeader *header;
    const struct Stage2Section *section;
    struct AROSAppleStage2ModuleState *module;
    uint64_t section_table_size;
    uint32_t index;

    if (boot_module->size < sizeof(struct Stage2ELFHeader))
        return AROS_APPLE_STAGE2_INVALID_ELF;
    header = (const struct Stage2ELFHeader *)(uintptr_t)
        boot_module->physical_address;
    if (header->ident[0] != ELF_MAGIC_0 || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != ELF_CLASS_64 ||
        header->ident[5] != ELF_DATA_LITTLE ||
        header->ident[6] != ELF_VERSION_CURRENT ||
        header->ident[7] != ELF_OSABI_AROS ||
        header->ident[8] != ELF_ABI_VERSION_AROS ||
        header->type != ELF_TYPE_REL ||
        header->machine != ELF_MACHINE_AARCH64 ||
        header->version != ELF_VERSION_CURRENT ||
        header->header_size != sizeof(struct Stage2ELFHeader) ||
        header->section_entry_size != sizeof(struct Stage2Section) ||
        header->section_count == 0 || header->section_offset % 8U != 0)
        return AROS_APPLE_STAGE2_INVALID_ELF;
    if (!multiply_u64(header->section_count, sizeof(struct Stage2Section),
            &section_table_size) ||
        !range_inside(header->section_offset, section_table_size,
            boot_module->size))
        return AROS_APPLE_STAGE2_INVALID_ELF;
    if (context->section_count > AROS_APPLE_STAGE2_MAX_SECTIONS -
            header->section_count)
        return AROS_APPLE_STAGE2_TOO_MANY_SECTIONS;

    module = &context->modules[module_index];
    module->image = (uintptr_t)boot_module->physical_address;
    module->image_size = boot_module->size;
    module->boot_module_index = module_index + 1U;
    module->section_address_base = context->section_count;
    module->section_count = header->section_count;
    context->section_count += header->section_count;

    for (index = 0; index < header->section_count; ++index) {
        section = module_section(module, index);
        if (section->alignment != 0 &&
            (!power_of_two(section->alignment) ||
             section->alignment > STAGE2_MAX_SECTION_ALIGNMENT))
            return AROS_APPLE_STAGE2_INVALID_SECTION;
        if (section->type != SHT_NOBITS && section->size != 0 &&
            !range_inside(section->offset, section->size, module->image_size))
            return AROS_APPLE_STAGE2_INVALID_SECTION;
        if (section->type == SHT_SYMTAB) {
            if (section->entry_size != sizeof(struct Stage2Symbol) ||
                section->size % sizeof(struct Stage2Symbol) != 0 ||
                section->link >= header->section_count ||
                module_section(module, section->link)->type != SHT_STRTAB)
                return AROS_APPLE_STAGE2_INVALID_SYMBOL_TABLE;
        } else if (section->type == SHT_RELA) {
            if (section->entry_size != sizeof(struct Stage2Rela) ||
                section->size % sizeof(struct Stage2Rela) != 0 ||
                section->link >= header->section_count ||
                section->info >= header->section_count ||
                module_section(module, section->link)->type != SHT_SYMTAB)
                return AROS_APPLE_STAGE2_INVALID_RELOCATION;
        } else if (section->type == SHT_REL) {
            if (section->entry_size != sizeof(struct Stage2Rel) ||
                section->size % sizeof(struct Stage2Rel) != 0 ||
                section->link >= header->section_count ||
                section->info >= header->section_count ||
                module_section(module, section->link)->type != SHT_SYMTAB)
                return AROS_APPLE_STAGE2_INVALID_RELOCATION;
            if (module_section(module, section->info)->flags & SHF_ALLOC)
                return AROS_APPLE_STAGE2_UNSUPPORTED_RELOCATION;
        }
    }
    return AROS_APPLE_STAGE2_OK;
}

static enum AROSAppleStage2Status allocate_section_class(
    struct AROSAppleStage2Context *context,
    uint64_t arena_end,
    int writable,
    uint64_t *cursor)
{
    const struct AROSAppleStage2ModuleState *module;
    const struct Stage2Section *section;
    uint64_t aligned;
    uint64_t end;
    uint64_t alignment;
    uint32_t module_index;
    uint32_t section_index;

    for (module_index = 0; module_index < context->module_count;
         ++module_index) {
        module = &context->modules[module_index];
        for (section_index = 0; section_index < module->section_count;
             ++section_index) {
            section = module_section(module, section_index);
            if ((section->flags & SHF_ALLOC) == 0 || section->size == 0 ||
                (((section->flags & SHF_WRITE) != 0) != writable))
                continue;
            alignment = section->alignment == 0 ? 1U : section->alignment;
            if (!align_up(*cursor, alignment, &aligned) ||
                !add_u64(aligned, section->size, &end) || end > arena_end)
                return AROS_APPLE_STAGE2_ARENA_EXHAUSTED;
            context->section_addresses[
                module->section_address_base + section_index] =
                (uintptr_t)aligned;
            if (section->type == SHT_NOBITS) {
                if (context->bss_count >= AROS_APPLE_STAGE2_MAX_BSS_RANGES)
                    return AROS_APPLE_STAGE2_TOO_MANY_BSS_RANGES;
                bytes_zero((void *)(uintptr_t)aligned, (size_t)section->size);
                context->bss[context->bss_count].address = (uintptr_t)aligned;
                context->bss[context->bss_count].length =
                    (uintptr_t)section->size;
                ++context->bss_count;
            } else {
                bytes_copy((void *)(uintptr_t)aligned,
                    (const void *)(module->image + section->offset),
                    (size_t)section->size);
            }
            *cursor = end;
        }
    }
    return AROS_APPLE_STAGE2_OK;
}

static enum AROSAppleStage2Status symbol_name(
    const struct AROSAppleStage2ModuleState *module,
    const struct Stage2Section *symbol_table,
    const struct Stage2Symbol *symbol,
    const char **name)
{
    const struct Stage2Section *strings =
        module_section(module, symbol_table->link);
    const char *table;
    uint64_t index;

    if (symbol->name >= strings->size)
        return AROS_APPLE_STAGE2_INVALID_SYMBOL_TABLE;
    table = (const char *)(module->image + strings->offset);
    for (index = symbol->name; index < strings->size; ++index) {
        if (table[index] == '\0') {
            *name = table + symbol->name;
            return AROS_APPLE_STAGE2_OK;
        }
    }
    return AROS_APPLE_STAGE2_INVALID_SYMBOL_TABLE;
}

static enum AROSAppleStage2Status defined_symbol_address(
    const struct AROSAppleStage2Context *context,
    uint32_t module_index,
    const struct Stage2Symbol *symbol,
    uintptr_t *address)
{
    const struct AROSAppleStage2ModuleState *module =
        &context->modules[module_index];
    const struct Stage2Section *section;
    uintptr_t section_address;
    uint64_t value;

    if (symbol->section_index == SHN_UNDEF)
        return AROS_APPLE_STAGE2_UNDEFINED_SYMBOL;
    if (symbol->section_index == SHN_COMMON)
        return AROS_APPLE_STAGE2_COMMON_SYMBOL;
    if (symbol->section_index == SHN_ABS) {
        if (symbol->value > UINTPTR_MAX)
            return AROS_APPLE_STAGE2_RANGE_OVERFLOW;
        *address = (uintptr_t)symbol->value;
        return AROS_APPLE_STAGE2_OK;
    }
    if (symbol->section_index >= SHN_LORESERVE ||
        symbol->section_index >= module->section_count)
        return AROS_APPLE_STAGE2_INVALID_SYMBOL_TABLE;
    section = module_section(module, symbol->section_index);
    section_address = runtime_section(context, module_index,
        symbol->section_index);
    if (section_address == 0 || symbol->value > section->size ||
        symbol->value > UINTPTR_MAX - section_address)
        return AROS_APPLE_STAGE2_INVALID_SYMBOL_TABLE;
    value = section_address + symbol->value;
    *address = (uintptr_t)value;
    return AROS_APPLE_STAGE2_OK;
}

static enum AROSAppleStage2Status find_defined_symbol(
    const struct AROSAppleStage2Context *context,
    uint32_t module_index,
    const char *wanted,
    uintptr_t *address)
{
    const struct AROSAppleStage2ModuleState *module =
        &context->modules[module_index];
    const struct Stage2ELFHeader *header = module_header(module);
    const struct Stage2Section *section;
    const struct Stage2Symbol *symbols;
    const char *name;
    enum AROSAppleStage2Status status;
    uint64_t count;
    uint64_t symbol_index;
    unsigned int matches = 0;
    uint32_t section_index;

    for (section_index = 0; section_index < header->section_count;
         ++section_index) {
        section = module_section(module, section_index);
        if (section->type != SHT_SYMTAB)
            continue;
        symbols = (const struct Stage2Symbol *)(
            module->image + section->offset);
        count = section->size / sizeof(*symbols);
        for (symbol_index = 0; symbol_index < count; ++symbol_index) {
            if ((symbols[symbol_index].info >> 4) != STB_GLOBAL)
                continue;
            status = symbol_name(module, section,
                &symbols[symbol_index], &name);
            if (status != AROS_APPLE_STAGE2_OK)
                return status;
            if (!cstring_equal(name, wanted))
                continue;
            status = defined_symbol_address(context, module_index,
                &symbols[symbol_index], address);
            if (status != AROS_APPLE_STAGE2_OK)
                return status;
            ++matches;
        }
    }
    if (matches == 0)
        return AROS_APPLE_STAGE2_MISSING_SYMBOL;
    if (matches != 1)
        return AROS_APPLE_STAGE2_DUPLICATE_SYMBOL;
    return AROS_APPLE_STAGE2_OK;
}

static enum AROSAppleStage2Status resolve_symbol(
    const struct AROSAppleStage2Context *context,
    uint32_t module_index,
    const struct Stage2Section *symbol_table,
    const struct Stage2Symbol *symbol,
    uint64_t *value)
{
    const struct AROSAppleStage2ModuleState *module =
        &context->modules[module_index];
    const char *name;
    uintptr_t address;
    enum AROSAppleStage2Status status =
        symbol_name(module, symbol_table, symbol, &name);

    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    if (cstring_equal(name, "SysBase")) {
        if (context->sysbase_storage == 0)
            return AROS_APPLE_STAGE2_MISSING_SYMBOL;
        *value = context->sysbase_storage;
        return AROS_APPLE_STAGE2_OK;
    }
    status = defined_symbol_address(context, module_index, symbol, &address);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    *value = address;
    return AROS_APPLE_STAGE2_OK;
}

static int add_signed_u64(uint64_t value, int64_t addend, uint64_t *result)
{
    uint64_t magnitude;

    if (addend >= 0)
        return add_u64(value, (uint64_t)addend, result);
    magnitude = (uint64_t)(-(addend + 1)) + 1U;
    if (value < magnitude)
        return 0;
    *result = value - magnitude;
    return 1;
}

static int signed_delta(uint64_t target, uint64_t place, int64_t *result)
{
    uint64_t magnitude;

    if (target >= place) {
        magnitude = target - place;
        if (magnitude > INT64_MAX)
            return 0;
        *result = (int64_t)magnitude;
    } else {
        magnitude = place - target;
        if (magnitude > (uint64_t)INT64_MAX + 1U)
            return 0;
        if (magnitude == (uint64_t)INT64_MAX + 1U)
            *result = INT64_MIN;
        else
            *result = -(int64_t)magnitude;
    }
    return 1;
}

static uint32_t load_u32_le(const uint8_t *address)
{
    return (uint32_t)address[0] |
        ((uint32_t)address[1] << 8) |
        ((uint32_t)address[2] << 16) |
        ((uint32_t)address[3] << 24);
}

static void store_u32_le(uint8_t *address, uint32_t value)
{
    address[0] = (uint8_t)value;
    address[1] = (uint8_t)(value >> 8);
    address[2] = (uint8_t)(value >> 16);
    address[3] = (uint8_t)(value >> 24);
}

static void store_u64_le(uint8_t *address, uint64_t value)
{
    unsigned int index;

    for (index = 0; index < 8U; ++index)
        address[index] = (uint8_t)(value >> (index * 8U));
}

static unsigned int relocation_width(uint32_t type)
{
    switch (type) {
    case R_AARCH64_NONE:
        return 0;
    case R_AARCH64_ABS64:
    case R_AARCH64_PREL64:
        return 8;
    case R_AARCH64_ABS32:
    case R_AARCH64_PREL32:
    case R_AARCH64_MOVW_UABS_G0:
    case R_AARCH64_MOVW_UABS_G0_NC:
    case R_AARCH64_MOVW_UABS_G1:
    case R_AARCH64_MOVW_UABS_G1_NC:
    case R_AARCH64_MOVW_UABS_G2:
    case R_AARCH64_MOVW_UABS_G2_NC:
    case R_AARCH64_MOVW_UABS_G3:
    case R_AARCH64_ADR_PREL_PG_HI21:
    case R_AARCH64_ADD_ABS_LO12_NC:
    case R_AARCH64_LDST8_ABS_LO12_NC:
    case R_AARCH64_JUMP26:
    case R_AARCH64_CALL26:
    case R_AARCH64_LDST16_ABS_LO12_NC:
    case R_AARCH64_LDST32_ABS_LO12_NC:
    case R_AARCH64_LDST64_ABS_LO12_NC:
    case R_AARCH64_LDST128_ABS_LO12_NC:
        return 4;
    default:
        return UINT_MAX;
    }
}

static enum AROSAppleStage2Status apply_relocation(
    uint8_t *place,
    uint64_t place_address,
    uint32_t type,
    uint64_t symbol,
    int64_t addend)
{
    uint64_t value;
    uint64_t page_value;
    uint64_t page_place;
    uint32_t instruction;
    uint32_t immediate;
    uint32_t shift;
    int64_t delta;

    if (type == R_AARCH64_NONE)
        return AROS_APPLE_STAGE2_OK;
    if (!add_signed_u64(symbol, addend, &value))
        return AROS_APPLE_STAGE2_RELOCATION_OVERFLOW;

    switch (type) {
    case R_AARCH64_ABS64:
        store_u64_le(place, value);
        return AROS_APPLE_STAGE2_OK;
    case R_AARCH64_ABS32:
        if (value > UINT32_MAX)
            return AROS_APPLE_STAGE2_RELOCATION_OVERFLOW;
        store_u32_le(place, (uint32_t)value);
        return AROS_APPLE_STAGE2_OK;
    case R_AARCH64_PREL64:
        store_u64_le(place, value - place_address);
        return AROS_APPLE_STAGE2_OK;
    case R_AARCH64_PREL32:
        if (!signed_delta(value, place_address, &delta) ||
            delta < INT32_MIN || delta > INT32_MAX)
            return AROS_APPLE_STAGE2_RELOCATION_OVERFLOW;
        store_u32_le(place, (uint32_t)(int32_t)delta);
        return AROS_APPLE_STAGE2_OK;
    case R_AARCH64_MOVW_UABS_G0:
        if (value > UINT16_MAX)
            return AROS_APPLE_STAGE2_RELOCATION_OVERFLOW;
        shift = 0;
        break;
    case R_AARCH64_MOVW_UABS_G0_NC:
        shift = 0;
        break;
    case R_AARCH64_MOVW_UABS_G1:
        if (value > UINT32_MAX)
            return AROS_APPLE_STAGE2_RELOCATION_OVERFLOW;
        shift = 16;
        break;
    case R_AARCH64_MOVW_UABS_G1_NC:
        shift = 16;
        break;
    case R_AARCH64_MOVW_UABS_G2:
        if (value > UINT64_C(0xffffffffffff))
            return AROS_APPLE_STAGE2_RELOCATION_OVERFLOW;
        shift = 32;
        break;
    case R_AARCH64_MOVW_UABS_G2_NC:
        shift = 32;
        break;
    case R_AARCH64_MOVW_UABS_G3:
        shift = 48;
        break;
    case R_AARCH64_JUMP26:
    case R_AARCH64_CALL26:
        if (!signed_delta(value, place_address, &delta) || (delta & 3) != 0 ||
            delta < -INT64_C(134217728) || delta > INT64_C(134217724))
            return AROS_APPLE_STAGE2_RELOCATION_OVERFLOW;
        instruction = load_u32_le(place);
        instruction = (instruction & UINT32_C(0xfc000000)) |
            ((uint32_t)(delta >> 2) & UINT32_C(0x03ffffff));
        store_u32_le(place, instruction);
        return AROS_APPLE_STAGE2_OK;
    case R_AARCH64_ADR_PREL_PG_HI21:
        page_value = value & ~UINT64_C(0xfff);
        page_place = place_address & ~UINT64_C(0xfff);
        if (!signed_delta(page_value, page_place, &delta))
            return AROS_APPLE_STAGE2_RELOCATION_OVERFLOW;
        delta >>= 12;
        if (delta < -INT64_C(1048576) || delta > INT64_C(1048575))
            return AROS_APPLE_STAGE2_RELOCATION_OVERFLOW;
        instruction = load_u32_le(place);
        instruction = (instruction & UINT32_C(0x9f00001f)) |
            (((uint32_t)delta & 3U) << 29) |
            ((((uint32_t)delta >> 2) & UINT32_C(0x7ffff)) << 5);
        store_u32_le(place, instruction);
        return AROS_APPLE_STAGE2_OK;
    case R_AARCH64_ADD_ABS_LO12_NC:
        immediate = (uint32_t)value & UINT32_C(0xfff);
        instruction = load_u32_le(place);
        instruction = (instruction & UINT32_C(0xffc003ff)) |
            (immediate << 10);
        store_u32_le(place, instruction);
        return AROS_APPLE_STAGE2_OK;
    case R_AARCH64_LDST8_ABS_LO12_NC:
        shift = 0;
        goto scaled_load_store;
    case R_AARCH64_LDST16_ABS_LO12_NC:
        shift = 1;
        goto scaled_load_store;
    case R_AARCH64_LDST32_ABS_LO12_NC:
        shift = 2;
        goto scaled_load_store;
    case R_AARCH64_LDST64_ABS_LO12_NC:
        shift = 3;
        goto scaled_load_store;
    case R_AARCH64_LDST128_ABS_LO12_NC:
        shift = 4;
scaled_load_store:
        immediate = (uint32_t)value & UINT32_C(0xfff);
        if ((immediate & ((1U << shift) - 1U)) != 0)
            return AROS_APPLE_STAGE2_RELOCATION_OVERFLOW;
        instruction = load_u32_le(place);
        instruction = (instruction & UINT32_C(0xffc003ff)) |
            ((immediate >> shift) << 10);
        store_u32_le(place, instruction);
        return AROS_APPLE_STAGE2_OK;
    default:
        return AROS_APPLE_STAGE2_UNSUPPORTED_RELOCATION;
    }

    instruction = load_u32_le(place);
    instruction = (instruction & UINT32_C(0xffe0001f)) |
        (((uint32_t)(value >> shift) & UINT32_C(0xffff)) << 5);
    store_u32_le(place, instruction);
    return AROS_APPLE_STAGE2_OK;
}

static enum AROSAppleStage2Status relocate_module(
    struct AROSAppleStage2Context *context,
    uint32_t module_index,
    struct AROSAppleStage2Result *result)
{
    const struct AROSAppleStage2ModuleState *module =
        &context->modules[module_index];
    const struct Stage2ELFHeader *header = module_header(module);
    const struct Stage2Section *relocation_section;
    const struct Stage2Section *target_section;
    const struct Stage2Section *symbol_table;
    const struct Stage2Symbol *symbols;
    const struct Stage2Rela *relocations;
    enum AROSAppleStage2Status status;
    uint64_t relocation_count;
    uint64_t symbol_count;
    uint64_t relocation_index;
    uint64_t symbol_index;
    uint64_t symbol_value;
    uintptr_t target_address;
    uint8_t *place;
    uint32_t type;
    unsigned int width;
    uint32_t section_index;

    for (section_index = 0; section_index < header->section_count;
         ++section_index) {
        relocation_section = module_section(module, section_index);
        if (relocation_section->type != SHT_RELA)
            continue;
        target_section = module_section(module, relocation_section->info);
        if ((target_section->flags & SHF_ALLOC) == 0)
            continue;
        target_address = runtime_section(context, module_index,
            relocation_section->info);
        if (target_address == 0)
            return AROS_APPLE_STAGE2_INVALID_RELOCATION;
        symbol_table = module_section(module, relocation_section->link);
        symbols = (const struct Stage2Symbol *)(
            module->image + symbol_table->offset);
        relocations = (const struct Stage2Rela *)(
            module->image + relocation_section->offset);
        symbol_count = symbol_table->size / sizeof(*symbols);
        relocation_count = relocation_section->size / sizeof(*relocations);

        for (relocation_index = 0; relocation_index < relocation_count;
             ++relocation_index) {
            type = (uint32_t)relocations[relocation_index].info;
            symbol_index = relocations[relocation_index].info >> 32;
            width = relocation_width(type);
            if (width == UINT_MAX)
                return AROS_APPLE_STAGE2_UNSUPPORTED_RELOCATION;
            if (symbol_index >= symbol_count ||
                relocations[relocation_index].offset > target_section->size ||
                width > target_section->size -
                    relocations[relocation_index].offset)
                return AROS_APPLE_STAGE2_INVALID_RELOCATION;
            status = resolve_symbol(context, module_index, symbol_table,
                &symbols[symbol_index], &symbol_value);
            if (status != AROS_APPLE_STAGE2_OK)
                return status;
            place = (uint8_t *)(target_address +
                (uintptr_t)relocations[relocation_index].offset);
            status = apply_relocation(place, (uintptr_t)place, type,
                symbol_value, relocations[relocation_index].addend);
            if (status != AROS_APPLE_STAGE2_OK)
                return status;
            if (result->relocation_count == UINT32_MAX)
                return AROS_APPLE_STAGE2_RANGE_OVERFLOW;
            ++result->relocation_count;
        }
    }
    return AROS_APPLE_STAGE2_OK;
}

enum AROSAppleStage2Status AROSAppleStage2Load(
    const struct AROSBootInfo *boot_info,
    uintptr_t stage2_image_start,
    uintptr_t stage2_image_end,
    struct AROSAppleStage2Context *context,
    struct AROSAppleStage2Result *result)
{
    const struct AROSBootModule *stage2_module;
    const struct AROSBootModule *boot_module;
    enum AROSAppleStage2Status status;
    uint64_t arena_start;
    uint64_t arena_end;
    uint64_t cursor;
    uint32_t memory_range_count;
    uint32_t index;

    if (boot_info == NULL || context == NULL || result == NULL)
        return AROS_APPLE_STAGE2_INVALID_ARGUMENT;
    bytes_zero(context, sizeof(*context));
    bytes_zero(result, sizeof(*result));

    if (AROSAppleBootInfoValidate(boot_info) != AROS_APPLE_BOOTINFO_OK)
        return AROS_APPLE_STAGE2_INVALID_BOOTINFO;
    if (!AROSAppleBootMemoryMapValidate(boot_info, &memory_range_count))
        return AROS_APPLE_STAGE2_INVALID_MEMORY_MAP;
    status = validate_boot_module_table(boot_info, stage2_image_start,
        stage2_image_end, result);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    if (!AROSAppleStage2ValidateOwnership(boot_info, stage2_image_start,
            stage2_image_end, &result->ownership))
        return AROS_APPLE_STAGE2_INVALID_OWNERSHIP;

    stage2_module = boot_module_at(boot_info, 0);
    if (!align_up(stage2_image_end, STAGE2_PAGE_SIZE, &arena_start) ||
        !range_end(stage2_module->physical_address, stage2_module->size,
            &arena_end) || arena_start >= arena_end)
        return AROS_APPLE_STAGE2_ARENA_EXHAUSTED;
    result->arena_start = (uintptr_t)arena_start;
    result->arena_end = (uintptr_t)arena_end;

    context->module_count = boot_info->module_count - 2U;
    for (index = 0; index < context->module_count; ++index) {
        boot_module = boot_module_at(boot_info, index + 1U);
        status = validate_elf_module(boot_module, context, index);
        if (status != AROS_APPLE_STAGE2_OK)
            return status;
    }

    cursor = arena_start;
    result->readonly_start = (uintptr_t)cursor;
    status = allocate_section_class(context, arena_end, 0, &cursor);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    result->readonly_end = (uintptr_t)cursor;
    if (!align_up(cursor, STAGE2_PAGE_SIZE, &cursor) || cursor > arena_end)
        return AROS_APPLE_STAGE2_ARENA_EXHAUSTED;
    result->writable_start = (uintptr_t)cursor;
    status = allocate_section_class(context, arena_end, 1, &cursor);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    result->writable_end = (uintptr_t)cursor;
    result->arena_used_end = (uintptr_t)cursor;

    context->bss[context->bss_count].address = 0;
    context->bss[context->bss_count].length = 0;
    result->bss = context->bss;
    result->bss_count = context->bss_count;
    result->loaded_module_count = context->module_count;

    status = find_defined_symbol(context, 0, "SysBase",
        &context->sysbase_storage);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    result->sysbase_storage = context->sysbase_storage;
    status = find_defined_symbol(context, 0, "Kernel_resident",
        &result->kernel_resident);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    status = find_defined_symbol(context, 1, "Exec_resident",
        &result->exec_resident);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;

    for (index = 0; index < context->module_count; ++index) {
        status = relocate_module(context, index, result);
        if (status != AROS_APPLE_STAGE2_OK)
            return status;
    }

    status = find_defined_symbol(context, 0,
        "AROSAppleKernelStartupContract", &result->kernel_contract);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    status = find_defined_symbol(context, 0,
        "AROSAppleKernelPrepareMemory", &result->kernel_prepare_memory);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    status = find_defined_symbol(context, 0,
        "AROSAppleKernelInitializeExec", &result->kernel_initialize_exec);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    status = find_defined_symbol(context, 0,
        "AROSAppleKernelInitializeSingleTask",
        &result->kernel_initialize_single_task);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    status = find_defined_symbol(context, 0,
        "AROSAppleKernelImportMemory", &result->kernel_import_memory);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    status = find_defined_symbol(context, 0,
        "AROSAppleKernelProveRuntime", &result->kernel_prove_runtime);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    status = find_defined_symbol(context, 0,
        "AROSAppleKernelProveDispatch", &result->kernel_prove_dispatch);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    status = find_defined_symbol(context, 0,
        "AROSAppleKernelProveCoreColdStart",
        &result->kernel_prove_core_coldstart);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    {
        uint64_t page_start;
        uint64_t page_size;

        if (!align_up(cursor, STAGE2_PAGE_SIZE, &page_start) ||
            page_start >= arena_end)
            return AROS_APPLE_STAGE2_ARENA_EXHAUSTED;
        page_size = ((arena_end - page_start) / 2U) &
            ~(STAGE2_PAGE_SIZE - 1U);
        if (page_size > STAGE2_PAGE_ARENA_MAX)
            page_size = STAGE2_PAGE_ARENA_MAX;
        if (page_size < STAGE2_PAGE_ARENA_MIN)
            return AROS_APPLE_STAGE2_ARENA_EXHAUSTED;
        result->page_arena_start = (uintptr_t)page_start;
        result->page_arena_size = page_size;
        cursor = page_start + page_size;
        result->arena_used_end = (uintptr_t)cursor;
    }
    status = AROSAppleStage2BuildMemoryPlan(boot_info, result,
        arena_end, &cursor);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    status = AROSAppleStage2BuildTags(boot_info, stage2_image_start,
        result, arena_end, &cursor);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    return AROS_APPLE_STAGE2_OK;
}

const char *AROSAppleStage2StatusName(enum AROSAppleStage2Status status)
{
    static const char *const names[] = {
        "OK",
        "INVALID_ARGUMENT",
        "INVALID_BOOTINFO",
        "INVALID_MODULE_TABLE",
        "MODULE_ORDER",
        "DUPLICATE_MODULE",
        "RANGE_OVERFLOW",
        "SOURCE_OVERLAP",
        "TOO_MANY_MODULES",
        "TOO_MANY_SECTIONS",
        "TOO_MANY_BSS_RANGES",
        "INVALID_ELF",
        "INVALID_SECTION",
        "INVALID_SYMBOL_TABLE",
        "MISSING_SYMBOL",
        "DUPLICATE_SYMBOL",
        "UNDEFINED_SYMBOL",
        "COMMON_SYMBOL",
        "INVALID_RELOCATION",
        "UNSUPPORTED_RELOCATION",
        "RELOCATION_OVERFLOW",
        "ARENA_EXHAUSTED",
        "INVALID_OWNERSHIP",
        "INVALID_MEMORY_MAP",
        "INVALID_MEMORY_PLAN",
        "NO_USABLE_MEMORY",
        "TOO_MANY_BOOT_TAGS",
        "KERNEL_CONTRACT",
        "MEMORY_PROBE",
        "KERNEL_MEMORY",
        "EXEC_INIT",
        "SYSBASE",
        "SINGLE_TASK_INIT",
        "KERNEL_BASE",
        "MEMORY_IMPORT",
        "RUNTIME",
        "DISPATCH",
        "COLDSTART",
        "INVALID_DEVICE_MAP",
        "MMU",
        "OBSERVER"
    };

    if ((unsigned int)status >= sizeof(names) / sizeof(names[0]))
        return "UNKNOWN";
    return names[status];
}
