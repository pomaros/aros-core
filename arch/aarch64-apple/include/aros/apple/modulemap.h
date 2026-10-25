/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: Where stage two placed the code of every boot-bundle module, and
          the function symbols of that code, for the kernel's crash report.

    Stage two writes the map into memory it keeps protected from Exec and
    stores its address in the kernel's AROSAppleKernelModuleMap. The map is
    read-only after the handoff and is never freed.

    code[] has one entry per allocated executable ELF section of a module,
    in ascending address order (the order stage two placed them). The
    symbols of code[i] are symbols[code[i].symbol_first ..
    code[i].symbol_first + code[i].symbol_count - 1], sorted by offset; an
    offset is relative to the start of that section, as `llvm-nm` prints it
    for the module's ELF file. strings[] holds NUL-terminated names;
    symbols[n].name is an offset into it.

    flags AROS_APPLE_MODULE_MAP_TRUNCATED: a capacity was reached; the
    entries present are exact, some symbols or sections are missing.
*/
#ifndef AROS_APPLE_MODULEMAP_H
#define AROS_APPLE_MODULEMAP_H

#include <stdint.h>

#define AROS_APPLE_MODULE_MAP_VERSION UINT32_C(1)
#define AROS_APPLE_MODULE_MAP_TRUNCATED UINT32_C(1)
#define AROS_APPLE_MODULE_NAME_BYTES 32U
#define AROS_APPLE_MODULE_SECTION_BYTES 16U
/* Capacities stage two reserves: a 2026 desktop bundle has 65 modules,
 * 65 executable sections, 13250 function symbols and 203 KB of names. */
#define AROS_APPLE_MODULE_MAP_MAX_CODE 1024U
#define AROS_APPLE_MODULE_MAP_MAX_SYMBOLS 65536U
#define AROS_APPLE_MODULE_MAP_MAX_STRINGS (UINT32_C(2) * 1024U * 1024U)

struct AROSAppleModuleCode {
    uint64_t start;
    uint64_t size;
    uint32_t symbol_first;
    uint32_t symbol_count;
    uint16_t module;            /* bundle module index, kernel.resource 0 */
    uint16_t section;           /* ELF section index */
    uint32_t reserved;
    char name[AROS_APPLE_MODULE_NAME_BYTES];
    char section_name[AROS_APPLE_MODULE_SECTION_BYTES];
};

struct AROSAppleModuleSymbol {
    uint32_t offset;
    uint32_t size;              /* 0 when the ELF symbol carries none */
    uint32_t name;
};

struct AROSAppleModuleMap {
    uint32_t version;
    uint32_t size;
    uint32_t code_count;
    uint32_t symbol_count;
    uint32_t string_bytes;
    uint32_t flags;
    const struct AROSAppleModuleCode *code;
    const struct AROSAppleModuleSymbol *symbols;
    const char *strings;
};

#endif
