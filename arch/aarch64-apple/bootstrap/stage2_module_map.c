/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Record where every boot module's code was placed, with its
 *       function symbols, so the kernel's crash report can name the module,
 *       the section offset and the function of an address.
 *
 * Runs once, after section placement and relocation. Two passes over the
 * already validated module images: count, then fill; each section's
 * symbols are heap-sorted by offset (bounded, no recursion, no allocation).
 */

#include "stage2_module_map.h"
#include "stage2_elf.h"

#define MAP_SHT_SYMTAB 2U
#define MAP_SHT_STRTAB 3U
#define MAP_SHT_NOBITS 8U
#define MAP_SHF_ALLOC UINT64_C(2)
#define MAP_SHF_EXECINSTR UINT64_C(4)
#define MAP_STT_FUNC 2U
#define MAP_SHN_LORESERVE 0xff00U

static const struct Stage2ELFHeader *map_header(
    const struct AROSAppleStage2ModuleState *module)
{
    return (const struct Stage2ELFHeader *)module->image;
}

static const struct Stage2Section *map_section(
    const struct AROSAppleStage2ModuleState *module, uint32_t index)
{
    return (const struct Stage2Section *)(module->image +
        map_header(module)->section_offset +
        (uint64_t)index * sizeof(struct Stage2Section));
}

static int map_is_code(const struct AROSAppleStage2Context *context,
    uint32_t module_index, uint32_t section_index)
{
    const struct AROSAppleStage2ModuleState *module =
        &context->modules[module_index];
    const struct Stage2Section *section = map_section(module, section_index);

    return (section->flags & MAP_SHF_ALLOC) != 0 &&
        (section->flags & MAP_SHF_EXECINSTR) != 0 &&
        section->type != MAP_SHT_NOBITS && section->size != 0 &&
        section->size <= UINT32_MAX &&
        context->section_addresses[module->section_address_base +
            section_index] != 0;
}

/* A NUL-terminated name inside a string table, or NULL. */
static const char *map_string(const struct AROSAppleStage2ModuleState *module,
    const struct Stage2Section *table, uint32_t offset, uint32_t *length)
{
    const char *text;
    uint64_t index;

    if (table->type != MAP_SHT_STRTAB || offset >= table->size)
        return 0;
    text = (const char *)(module->image + table->offset);
    for (index = offset; index < table->size; ++index) {
        if (text[index] == 0) {
            *length = (uint32_t)(index - offset);
            return text + offset;
        }
    }
    return 0;
}

static const char *map_section_name(
    const struct AROSAppleStage2ModuleState *module, uint32_t index)
{
    const struct Stage2ELFHeader *header = map_header(module);
    uint32_t length;

    if (header->section_name_index >= header->section_count)
        return 0;
    return map_string(module, map_section(module, header->section_name_index),
        map_section(module, index)->name, &length);
}

/* A function symbol of section_index with a usable name, or 0. */
static const char *map_function(
    const struct AROSAppleStage2ModuleState *module,
    const struct Stage2Section *symtab, const struct Stage2Symbol *symbol,
    uint32_t section_index, uint32_t *length)
{
    const struct Stage2Section *section;

    if ((symbol->info & 0xfU) != MAP_STT_FUNC ||
            symbol->section_index != section_index ||
            symbol->section_index >= MAP_SHN_LORESERVE)
        return 0;
    section = map_section(module, section_index);
    if (symbol->value >= section->size)
        return 0;
    if (symtab->link >= map_header(module)->section_count)
        return 0;
    return map_string(module, map_section(module, symtab->link), symbol->name,
        length);
}

static void map_copy(char *out, uint32_t capacity, const char *in,
    uint64_t length)
{
    uint32_t index = 0;

    if (in != 0)
        for (; index + 1U < capacity && index < length && in[index] != 0;
                ++index)
            out[index] = in[index];
    for (; index < capacity; ++index)
        out[index] = 0;
}

static void map_sift(struct AROSAppleModuleSymbol *s, uint32_t root,
    uint32_t count)
{
    for (;;) {
        uint32_t child = 2U * root + 1U;
        struct AROSAppleModuleSymbol swap;

        if (child >= count)
            return;
        if (child + 1U < count && s[child + 1U].offset > s[child].offset)
            ++child;
        if (s[root].offset >= s[child].offset)
            return;
        swap = s[root];
        s[root] = s[child];
        s[child] = swap;
        root = child;
    }
}

static void map_sort(struct AROSAppleModuleSymbol *s, uint32_t count)
{
    uint32_t index;

    if (count < 2U)
        return;
    for (index = count / 2U; index-- > 0;)
        map_sift(s, index, count);
    for (index = count - 1U; index > 0; --index) {
        struct AROSAppleModuleSymbol swap = s[0];

        s[0] = s[index];
        s[index] = swap;
        map_sift(s, 0, index);
    }
}

/*
 * Walk every function symbol of every code section, in map order. With
 * fill == 0 only counts; otherwise writes code, symbols and strings.
 */
static uint32_t map_walk(const struct AROSBootInfo *boot_info,
    const struct AROSAppleStage2Context *context,
    struct AROSAppleModuleMap *map, struct AROSAppleModuleCode *code,
    struct AROSAppleModuleSymbol *symbols, char *strings,
    uint32_t *symbol_total, uint32_t *string_total)
{
    uint32_t flags = 0;
    uint32_t code_count = 0;
    uint32_t symbol_count = 0;
    uint32_t string_bytes = 0;
    uint32_t m, i, t;

    for (m = 0; m < context->module_count; ++m) {
        const struct AROSAppleStage2ModuleState *module = &context->modules[m];
        const struct AROSBootModule *boot = (const struct AROSBootModule *)(
            (uintptr_t)boot_info->modules_physical +
            (uintptr_t)((uint64_t)module->boot_module_index *
                boot_info->module_entry_size));

        for (i = 0; i < module->section_count; ++i) {
            struct AROSAppleModuleCode *entry;
            uint32_t first = symbol_count;

            if (!map_is_code(context, m, i))
                continue;
            if (code_count >= AROS_APPLE_MODULE_MAP_MAX_CODE) {
                flags |= AROS_APPLE_MODULE_MAP_TRUNCATED;
                continue;
            }
            entry = code != 0 ? &code[code_count] : 0;
            for (t = 0; t < module->section_count; ++t) {
                const struct Stage2Section *symtab = map_section(module, t);
                const struct Stage2Symbol *table;
                uint64_t count, k;

                if (symtab->type != MAP_SHT_SYMTAB)
                    continue;
                table = (const struct Stage2Symbol *)(module->image +
                    symtab->offset);
                count = symtab->size / sizeof(*table);
                for (k = 0; k < count; ++k) {
                    uint32_t length = 0;
                    const char *name = map_function(module, symtab, &table[k],
                        i, &length);

                    if (name == 0 || length == 0)
                        continue;
                    if (symbol_count >= AROS_APPLE_MODULE_MAP_MAX_SYMBOLS ||
                            length + 1U > AROS_APPLE_MODULE_MAP_MAX_STRINGS -
                                string_bytes) {
                        flags |= AROS_APPLE_MODULE_MAP_TRUNCATED;
                        continue;
                    }
                    if (symbols != 0) {
                        uint32_t c;

                        symbols[symbol_count].offset = (uint32_t)table[k].value;
                        symbols[symbol_count].size =
                            table[k].size > UINT32_MAX ? 0 :
                            (uint32_t)table[k].size;
                        symbols[symbol_count].name = string_bytes;
                        for (c = 0; c < length; ++c)
                            strings[string_bytes + c] = name[c];
                        strings[string_bytes + length] = 0;
                    }
                    ++symbol_count;
                    string_bytes += length + 1U;
                }
            }
            if (entry != 0) {
                const char *section_name = map_section_name(module, i);

                entry->start = context->section_addresses[
                    module->section_address_base + i];
                entry->size = map_section(module, i)->size;
                entry->symbol_first = first;
                entry->symbol_count = symbol_count - first;
                entry->module = (uint16_t)m;
                entry->section = (uint16_t)i;
                entry->reserved = 0;
                map_copy(entry->name, sizeof(entry->name),
                    (const char *)(uintptr_t)boot->name_physical,
                    boot->name_size);
                map_copy(entry->section_name, sizeof(entry->section_name),
                    section_name, section_name != 0 ? 64U : 0U);
                map_sort(&symbols[first], symbol_count - first);
            }
            ++code_count;
        }
    }
    if (map != 0) {
        map->code_count = code_count;
        map->symbol_count = symbol_count;
        map->string_bytes = string_bytes;
        map->flags = flags;
    }
    *symbol_total = symbol_count;
    *string_total = string_bytes;
    return code_count;
}

static int map_align(uint64_t value, uint64_t *result)
{
    if (value > UINT64_MAX - 15U)
        return 0;
    *result = (value + 15U) & ~UINT64_C(15);
    return 1;
}

enum AROSAppleStage2Status AROSAppleStage2BuildModuleMap(
    const struct AROSBootInfo *boot_info,
    const struct AROSAppleStage2Context *context,
    uint64_t arena_end,
    uint64_t *cursor,
    struct AROSAppleModuleMap **map_out)
{
    struct AROSAppleModuleMap *map;
    uint32_t code_count, symbol_count, string_bytes;
    uint64_t at, code_at, symbols_at, strings_at, end;

    if (boot_info == 0 || context == 0 || cursor == 0 || map_out == 0)
        return AROS_APPLE_STAGE2_INVALID_ARGUMENT;
    code_count = map_walk(boot_info, context, 0, 0, 0, 0, &symbol_count,
        &string_bytes);
    if (!map_align(*cursor, &at) ||
            !map_align(at + sizeof(*map), &code_at) ||
            !map_align(code_at + (uint64_t)code_count *
                sizeof(struct AROSAppleModuleCode), &symbols_at) ||
            !map_align(symbols_at + (uint64_t)symbol_count *
                sizeof(struct AROSAppleModuleSymbol), &strings_at) ||
            !map_align(strings_at + string_bytes + 1U, &end) || end > arena_end)
        return AROS_APPLE_STAGE2_ARENA_EXHAUSTED;
    map = (struct AROSAppleModuleMap *)(uintptr_t)at;
    map->version = AROS_APPLE_MODULE_MAP_VERSION;
    map->size = sizeof(*map);
    map->code = (const struct AROSAppleModuleCode *)(uintptr_t)code_at;
    map->symbols = (const struct AROSAppleModuleSymbol *)(uintptr_t)symbols_at;
    map->strings = (const char *)(uintptr_t)strings_at;
    ((char *)(uintptr_t)strings_at)[string_bytes] = 0;
    (void)map_walk(boot_info, context, map,
        (struct AROSAppleModuleCode *)(uintptr_t)code_at,
        (struct AROSAppleModuleSymbol *)(uintptr_t)symbols_at,
        (char *)(uintptr_t)strings_at, &symbol_count, &string_bytes);
    *cursor = end;
    *map_out = map;
    return AROS_APPLE_STAGE2_OK;
}
