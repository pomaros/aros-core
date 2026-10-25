/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: The ELF64 records stage two reads from boot-bundle modules.
 */

#ifndef AROS_AARCH64_APPLE_STAGE2_ELF_H
#define AROS_AARCH64_APPLE_STAGE2_ELF_H

#include <stdint.h>

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

_Static_assert(sizeof(struct Stage2ELFHeader) == 64, "ELF64 header size");
_Static_assert(sizeof(struct Stage2Section) == 64, "ELF64 section size");
_Static_assert(sizeof(struct Stage2Symbol) == 24, "ELF64 symbol size");

#endif /* AROS_AARCH64_APPLE_STAGE2_ELF_H */
