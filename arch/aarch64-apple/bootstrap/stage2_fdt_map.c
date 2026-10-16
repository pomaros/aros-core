/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Derive a bounded identity-mapped Apple MMIO plan from the published
 * flattened-device-tree format without importing a third-party FDT parser.
 */

#include "stage2_fdt_map.h"
#include "early_fdt.h"
#include <aros/apple/vmconsole.h>
#include "pci_ecam.h"

#include <stddef.h>

#define FDT_MAGIC              UINT32_C(0xd00dfeed)
#define FDT_BEGIN_NODE         UINT32_C(1)
#define FDT_END_NODE           UINT32_C(2)
#define FDT_PROP               UINT32_C(3)
#define FDT_NOP                UINT32_C(4)
#define FDT_END                UINT32_C(9)
#define FDT_HEADER_SIZE        40U
#define FDT_MAX_SIZE           (2U * 1024U * 1024U)
#define FDT_MAX_DEPTH          32
#define FDT_MAX_NODES          4096U
#define FDT_MAX_PROPERTIES     32768U
#define FDT_MAX_RAW_RANGES     AROS_APPLE_DEVICE_MAP_MAX_RANGES
#define FDT_MAX_NODE_RANGES    16U
#define FDT_MAX_PATH           256U
#define FDT_MAX_NODE_NAME      128U
#define DEVICE_PAGE_SIZE       UINT64_C(4096)
#define DEVICE_ADDRESS_LIMIT   (UINT64_C(1) << 48)
#define EVIDENCE_PROPERTY      "aros,h2-evidence-channel"
#define EVIDENCE_PREFIX        "dockchannel:"
#define EVIDENCE_TEXT_SIZE     110U
#define EVIDENCE_MIN_SIZE      UINT64_C(0x4030)

struct FDTView {
    const uint8_t *data;
    const uint8_t *structure;
    const uint8_t *strings;
    uint32_t total_size;
    uint32_t structure_size;
    uint32_t strings_size;
};

struct FDTPlatform {
    uint32_t address_cells;
    uint32_t size_cells;
    uint32_t node_count;
    uint32_t property_count;
    int apple;
    int saw_soc;
    int saw_address_cells;
    int saw_size_cells;
    int saw_ranges;
    int saw_serial_alias;
    int saw_evidence_channel;
    uint32_t soc_name_size;
    uint32_t serial_path_size;
    uint64_t evidence_start;
    uint64_t evidence_end;
    uint8_t soc_name[FDT_MAX_NODE_NAME];
    uint8_t serial_path[FDT_MAX_PATH];
};

struct RawRange {
    uint64_t start;
    uint64_t end;
};

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
        ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint64_t read_be64(const uint8_t *data)
{
    return ((uint64_t)read_be32(data) << 32) | read_be32(data + 4U);
}

static int add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right > UINT64_MAX - left)
        return 0;
    *result = left + right;
    return 1;
}

static int region_valid(uint32_t start, uint32_t size, uint32_t total)
{
    return start <= total && size <= total - start;
}

static int regions_overlap(
    uint32_t left_start, uint32_t left_end,
    uint32_t right_start, uint32_t right_end)
{
    return left_start < right_end && right_start < left_end;
}

static int bounded_cstring(
    const uint8_t *data, uint32_t start, uint32_t end,
    uint32_t *length)
{
    uint32_t cursor;

    if (start >= end)
        return 0;
    for (cursor = start; cursor < end; ++cursor) {
        if (data[cursor] == 0) {
            *length = cursor - start;
            return 1;
        }
    }
    return 0;
}

static int bytes_equal(
    const uint8_t *data, uint32_t size, const char *wanted)
{
    uint32_t index = 0;

    while (wanted[index] != '\0') {
        if (index >= size || data[index] != (uint8_t)wanted[index])
            return 0;
        ++index;
    }
    return index == size;
}

static int soc_node_name(const uint8_t *data, uint32_t size)
{
    return bytes_equal(data, size, "soc") ||
        (size > 4U && bytes_equal(data, 4U, "soc@"));
}

static int string_list_has_apple(const uint8_t *data, uint32_t size)
{
    uint32_t cursor = 0;
    uint32_t start;

    if (size == 0 || data[size - 1U] != 0)
        return -1;
    while (cursor < size) {
        start = cursor;
        while (cursor < size && data[cursor] != 0)
            ++cursor;
        if (cursor == start || cursor >= size)
            return -1;
        if (cursor - start > 6U &&
            bytes_equal(data + start, 6U, "apple,"))
            return 1;
        ++cursor;
    }
    return 0;
}

static int string_list_has(
    const uint8_t *data, uint32_t size, const char *wanted)
{
    uint32_t cursor = 0;
    uint32_t start;

    if (size == 0 || data[size - 1U] != 0)
        return -1;
    while (cursor < size) {
        start = cursor;
        while (cursor < size && data[cursor] != 0)
            ++cursor;
        if (cursor == start || cursor >= size)
            return -1;
        if (bytes_equal(data + start, cursor - start, wanted))
            return 1;
        ++cursor;
    }
    return 0;
}

static int copy_cstring(
    const uint8_t *value, uint32_t size,
    uint8_t *output, uint32_t capacity, uint32_t *output_size)
{
    uint32_t index;

    if (size < 2U || size > capacity || value[size - 1U] != 0)
        return 0;
    for (index = 0; index + 1U < size; ++index) {
        if (value[index] == 0 || value[index] == ':')
            break;
        output[index] = value[index];
    }
    if (index == 0 || index >= capacity)
        return 0;
    output[index] = 0;
    *output_size = index;
    return 1;
}

static int copy_bytes(
    uint8_t *output, uint32_t capacity,
    const uint8_t *input, uint32_t size)
{
    uint32_t index;

    if (size == 0 || size >= capacity)
        return 0;
    for (index = 0; index < size; ++index)
        output[index] = input[index];
    output[size] = 0;
    return 1;
}

static int parse_hex64(const uint8_t *text, uint64_t *value)
{
    uint64_t result = 0;
    uint32_t index;

    for (index = 0; index < 16U; ++index) {
        uint8_t digit = text[index];
        uint8_t nibble;

        if (digit >= '0' && digit <= '9')
            nibble = digit - '0';
        else if (digit >= 'a' && digit <= 'f')
            nibble = digit - 'a' + 10U;
        else
            return 0;
        result = (result << 4) | nibble;
    }
    *value = result;
    return 1;
}

static int parse_evidence_channel(
    const uint8_t *value, uint32_t size, struct FDTPlatform *platform)
{
    static const char prefix[] = EVIDENCE_PREFIX;
    uint64_t start;
    uint64_t length;
    uint64_t end;
    uint32_t index;

    if (size != EVIDENCE_TEXT_SIZE + 1U || value[EVIDENCE_TEXT_SIZE] != 0)
        return 0;
    for (index = 0; index < sizeof(prefix) - 1U; ++index) {
        if (value[index] != (uint8_t)prefix[index])
            return 0;
    }
    if (!parse_hex64(value + 12U, &start) || value[28] != ':' ||
        !parse_hex64(value + 29U, &length) || value[45] != ':')
        return 0;
    for (index = 46U; index < EVIDENCE_TEXT_SIZE; ++index) {
        uint8_t digit = value[index];

        if (!((digit >= '0' && digit <= '9') ||
              (digit >= 'a' && digit <= 'f')))
            return 0;
    }
    if (start == 0 || (start & (DEVICE_PAGE_SIZE - 1U)) != 0 ||
        length < EVIDENCE_MIN_SIZE ||
        (length & (DEVICE_PAGE_SIZE - 1U)) != 0 ||
        !add_u64(start, length, &end) || end > DEVICE_ADDRESS_LIMIT)
        return 0;
    platform->evidence_start = start;
    platform->evidence_end = end;
    return 1;
}

static int property_name(
    const struct FDTView *view, uint32_t offset,
    const uint8_t **name, uint32_t *length)
{
    if (offset >= view->strings_size ||
        !bounded_cstring(view->strings, offset, view->strings_size, length) ||
        *length == 0)
        return 0;
    *name = view->strings + offset;
    return 1;
}

static int fdt_view(
    const struct AROSBootInfo *boot_info, struct FDTView *view)
{
    const uint8_t *data;
    uint32_t total;
    uint32_t structure_offset;
    uint32_t strings_offset;
    uint32_t reserve_offset;
    uint32_t version;
    uint32_t last_compatible;
    uint32_t strings_size;
    uint32_t structure_size;
    uint32_t reserve_end;

    if (boot_info == NULL || view == NULL || boot_info->fdt_physical == 0 ||
        boot_info->fdt_size < FDT_HEADER_SIZE ||
        boot_info->fdt_size > FDT_MAX_SIZE ||
        boot_info->fdt_size > UINT32_MAX)
        return 0;
    data = (const uint8_t *)(uintptr_t)boot_info->fdt_physical;
    total = read_be32(data + 4U);
    structure_offset = read_be32(data + 8U);
    strings_offset = read_be32(data + 12U);
    reserve_offset = read_be32(data + 16U);
    version = read_be32(data + 20U);
    last_compatible = read_be32(data + 24U);
    strings_size = read_be32(data + 32U);
    structure_size = read_be32(data + 36U);
    if (read_be32(data) != FDT_MAGIC || total != boot_info->fdt_size ||
        total > FDT_MAX_SIZE || version < 17U ||
        last_compatible > 17U || last_compatible > version ||
        (structure_offset & 3U) != 0 || (reserve_offset & 7U) != 0 ||
        structure_size < 4U || strings_size == 0 ||
        !region_valid(structure_offset, structure_size, total) ||
        !region_valid(strings_offset, strings_size, total) ||
        reserve_offset < FDT_HEADER_SIZE || reserve_offset > total - 16U ||
        structure_offset < FDT_HEADER_SIZE ||
        strings_offset < FDT_HEADER_SIZE)
        return 0;

    reserve_end = reserve_offset;
    while (reserve_end <= total - 16U) {
        uint64_t address = read_be64(data + reserve_end);
        uint64_t size = read_be64(data + reserve_end + 8U);

        reserve_end += 16U;
        if (address == 0 && size == 0)
            break;
    }
    if (reserve_end > total ||
        read_be64(data + reserve_end - 16U) != 0 ||
        read_be64(data + reserve_end - 8U) != 0 ||
        regions_overlap(0, FDT_HEADER_SIZE, reserve_offset, reserve_end) ||
        regions_overlap(0, FDT_HEADER_SIZE, structure_offset,
            structure_offset + structure_size) ||
        regions_overlap(0, FDT_HEADER_SIZE, strings_offset,
            strings_offset + strings_size) ||
        regions_overlap(reserve_offset, reserve_end, structure_offset,
            structure_offset + structure_size) ||
        regions_overlap(reserve_offset, reserve_end, strings_offset,
            strings_offset + strings_size) ||
        regions_overlap(structure_offset, structure_offset + structure_size,
            strings_offset, strings_offset + strings_size))
        return 0;

    view->data = data;
    view->structure = data + structure_offset;
    view->strings = data + strings_offset;
    view->total_size = total;
    view->structure_size = structure_size;
    view->strings_size = strings_size;
    return 1;
}

static int align_property(uint32_t cursor, uint32_t size, uint32_t limit,
    uint32_t *next)
{
    uint32_t end;

    if (cursor > limit || size > limit - cursor)
        return 0;
    end = cursor + size;
    if (end > UINT32_MAX - 3U)
        return 0;
    end = (end + 3U) & ~UINT32_C(3);
    if (end > limit)
        return 0;
    *next = end;
    return 1;
}

static int read_cell_count(const uint8_t *value, uint32_t size,
    uint32_t *count)
{
    if (size != 4U)
        return 0;
    *count = read_be32(value);
    return *count == 1U || *count == 2U;
}

static int scan_platform(
    const struct FDTView *view, struct FDTPlatform *platform)
{
    uint32_t cursor = 0;
    uint32_t name_size;
    uint32_t value_size;
    uint32_t name_offset;
    uint32_t next;
    int depth = -1;
    int soc_active = 0;
    int aliases_active = 0;
    int chosen_active = 0;
    int saw_root = 0;
    int saw_chosen = 0;
    int saw_root_compatible = 0;
    int saw_end = 0;

    platform->address_cells = 0;
    platform->size_cells = 0;
    platform->node_count = 0;
    platform->property_count = 0;
    platform->apple = 0;
    platform->saw_soc = 0;
    platform->saw_address_cells = 0;
    platform->saw_size_cells = 0;
    platform->saw_ranges = 0;
    platform->saw_serial_alias = 0;
    platform->saw_evidence_channel = 0;
    platform->soc_name_size = 0;
    platform->serial_path_size = 0;
    platform->evidence_start = 0;
    platform->evidence_end = 0;

    while (cursor <= view->structure_size - 4U) {
        uint32_t token = read_be32(view->structure + cursor);
        const uint8_t *name;
        const uint8_t *value;

        cursor += 4U;
        if (token == FDT_BEGIN_NODE) {
            if (depth + 1 >= FDT_MAX_DEPTH ||
                !bounded_cstring(view->structure, cursor,
                    view->structure_size, &name_size) ||
                !align_property(cursor, name_size + 1U,
                    view->structure_size, &next))
                return 0;
            ++depth;
            ++platform->node_count;
            if (platform->node_count > FDT_MAX_NODES)
                return 0;
            name = view->structure + cursor;
            if (depth == 0) {
                if (name_size != 0 || saw_root)
                    return 0;
                saw_root = 1;
            } else if (depth == 1 && platform->apple &&
                    soc_node_name(name, name_size)) {
                if (platform->saw_soc)
                    return 0;
                if (!copy_bytes(platform->soc_name,
                        sizeof(platform->soc_name), name, name_size))
                    return 0;
                platform->soc_name_size = name_size;
                platform->saw_soc = 1;
                soc_active = 1;
            } else if (depth == 1 && bytes_equal(name, name_size, "aliases")) {
                aliases_active = 1;
            } else if (depth == 1 && bytes_equal(name, name_size, "chosen")) {
                if (saw_chosen)
                    return 0;
                saw_chosen = 1;
                chosen_active = 1;
            }
            cursor = next;
        } else if (token == FDT_END_NODE) {
            if (depth < 0)
                return 0;
            if (depth == 1 && soc_active)
                soc_active = 0;
            if (depth == 1 && aliases_active)
                aliases_active = 0;
            if (depth == 1 && chosen_active)
                chosen_active = 0;
            --depth;
        } else if (token == FDT_PROP) {
            int apple;

            if (depth < 0 || cursor > view->structure_size - 8U)
                return 0;
            value_size = read_be32(view->structure + cursor);
            name_offset = read_be32(view->structure + cursor + 4U);
            cursor += 8U;
            if (!property_name(view, name_offset, &name, &name_size) ||
                !align_property(cursor, value_size,
                    view->structure_size, &next))
                return 0;
            value = view->structure + cursor;
            ++platform->property_count;
            if (platform->property_count > FDT_MAX_PROPERTIES)
                return 0;
            if (depth == 0 && bytes_equal(name, name_size, "compatible")) {
                if (saw_root_compatible)
                    return 0;
                apple = string_list_has_apple(value, value_size);
                if (apple < 0)
                    return 0;
                platform->apple |= apple;
                saw_root_compatible = 1;
            } else if (depth == 1 && soc_active) {
                if (bytes_equal(name, name_size, "#address-cells")) {
                    if (platform->saw_address_cells ||
                        !read_cell_count(value, value_size,
                            &platform->address_cells))
                        return 0;
                    platform->saw_address_cells = 1;
                } else if (bytes_equal(name, name_size, "#size-cells")) {
                    if (platform->saw_size_cells ||
                        !read_cell_count(value, value_size,
                            &platform->size_cells))
                        return 0;
                    platform->saw_size_cells = 1;
                } else if (bytes_equal(name, name_size, "ranges")) {
                    if (platform->saw_ranges || value_size != 0)
                        return 0;
                    platform->saw_ranges = 1;
                }
            } else if (depth == 1 && aliases_active &&
                    bytes_equal(name, name_size, "serial0")) {
                if (platform->saw_serial_alias ||
                    !copy_cstring(value, value_size, platform->serial_path,
                        sizeof(platform->serial_path),
                        &platform->serial_path_size) ||
                    platform->serial_path[0] != '/')
                    return 0;
                platform->saw_serial_alias = 1;
            } else if (depth == 1 && chosen_active &&
                    bytes_equal(name, name_size, EVIDENCE_PROPERTY)) {
                if (platform->saw_evidence_channel ||
                    !parse_evidence_channel(value, value_size, platform))
                    return 0;
                platform->saw_evidence_channel = 1;
            }
            cursor = next;
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            if (depth != -1 || !saw_root)
                return 0;
            saw_end = 1;
            break;
        } else {
            return 0;
        }
    }
    if (!saw_end)
        return 0;
    if (!platform->apple)
        return saw_root_compatible;
    return platform->saw_soc && platform->saw_address_cells &&
        platform->saw_size_cells && platform->saw_ranges;
}

static int serial_path_matches(
    const struct FDTPlatform *platform,
    const uint8_t *child_name, uint32_t child_name_size)
{
    uint32_t index;
    uint32_t cursor = 0;

    if (!platform->saw_serial_alias || platform->serial_path_size !=
            platform->soc_name_size + child_name_size + 2U)
        return 0;
    if (platform->serial_path[cursor++] != '/')
        return 0;
    for (index = 0; index < platform->soc_name_size; ++index) {
        if (platform->serial_path[cursor++] != platform->soc_name[index])
            return 0;
    }
    if (platform->serial_path[cursor++] != '/')
        return 0;
    for (index = 0; index < child_name_size; ++index) {
        if (platform->serial_path[cursor++] != child_name[index])
            return 0;
    }
    return cursor == platform->serial_path_size;
}

static int append_role(
    struct AROSAppleDeviceMap *device_map,
    uint32_t kind, const struct RawRange *range)
{
    struct AROSAppleDeviceRole *role;
    uint32_t instance = 0;
    uint32_t index;

    if (device_map->role_count >= AROS_APPLE_DEVICE_MAP_MAX_ROLES)
        return 0;
    for (index = 0; index < device_map->role_count; ++index) {
        if (device_map->roles[index].kind == kind)
            ++instance;
    }
    if ((kind == AROS_APPLE_DEVICE_ROLE_CONSOLE ||
         kind == AROS_APPLE_DEVICE_ROLE_EVIDENCE) && instance != 0)
        return 0;
    role = &device_map->roles[device_map->role_count++];
    role->kind = kind;
    role->instance = instance;
    role->start = range->start;
    role->end = range->end;
    return 1;
}

static uint64_t read_cells(const uint8_t *value, uint32_t count)
{
    uint64_t result = 0;
    uint32_t index;

    for (index = 0; index < count; ++index)
        result = (result << 32) | read_be32(value + index * 4U);
    return result;
}

static int enabled_status(const uint8_t *value, uint32_t size, int *enabled)
{
    uint32_t index;

    if (size < 2U || value[size - 1U] != 0)
        return 0;
    for (index = 0; index + 1U < size; ++index) {
        if (value[index] == 0)
            return 0;
    }
    *enabled = bytes_equal(value, size - 1U, "ok") ||
        bytes_equal(value, size - 1U, "okay");
    return 1;
}

static int parse_reg(
    const uint8_t *value, uint32_t size,
    const struct FDTPlatform *platform,
    struct RawRange ranges[FDT_MAX_NODE_RANGES], uint32_t *range_count)
{
    uint32_t tuple_cells = platform->address_cells + platform->size_cells;
    uint32_t tuple_size = tuple_cells * 4U;
    uint32_t cursor;

    if (size == 0 || tuple_size == 0 || size % tuple_size != 0 ||
        size / tuple_size > FDT_MAX_NODE_RANGES)
        return 0;
    *range_count = 0;
    for (cursor = 0; cursor < size; cursor += tuple_size) {
        uint64_t start = read_cells(value + cursor, platform->address_cells);
        uint64_t length = read_cells(value + cursor +
            platform->address_cells * 4U, platform->size_cells);
        uint64_t end;
        uint64_t aligned_end;

        if (length == 0 || !add_u64(start, length, &end) ||
            end > DEVICE_ADDRESS_LIMIT || end > UINT64_MAX -
                (DEVICE_PAGE_SIZE - 1U))
            return 0;
        start &= ~(DEVICE_PAGE_SIZE - 1U);
        aligned_end = (end + DEVICE_PAGE_SIZE - 1U) &
            ~(DEVICE_PAGE_SIZE - 1U);
        if (start == 0 || aligned_end <= start)
            return 0;
        ranges[*range_count].start = start;
        ranges[*range_count].end = aligned_end;
        ++*range_count;
    }
    return 1;
}

/* PCI child addresses contain a flags cell followed by a 64-bit address.
 * Only the CPU side is mapped here; BAR translation belongs to the bus owner. */
static int append_pcie_windows(const uint8_t *value, uint32_t size,
    uint32_t address_cells, uint32_t size_cells,
    const struct FDTPlatform *platform,
    struct RawRange raw[FDT_MAX_RAW_RANGES], uint32_t *count)
{
    if (!value || !size || size % 28 || size / 28 > 4 ||
        address_cells != 3 || size_cells != 2 || platform->address_cells != 2 ||
        size / 28 > FDT_MAX_RAW_RANGES - *count)
        return 0;
    for (uint32_t i = 0; i < size; i += 28) {
        uint32_t flags = read_be32(value + i);
        uint64_t pci = read_be64(value + i + 4);
        uint64_t cpu = read_be64(value + i + 12);
        uint64_t length = read_be64(value + i + 20), end;
        if ((flags != 0x02000000 && flags != 0x43000000) ||
            !cpu || !length || (cpu | pci | length) & (DEVICE_PAGE_SIZE - 1) ||
            !add_u64(cpu, length, &end) || end > DEVICE_ADDRESS_LIMIT ||
            pci > UINT64_MAX - length ||
            (flags == 0x02000000 && (pci >= (UINT64_C(1) << 32) ||
                length > (UINT64_C(1) << 32) - pci)))
            return 0;
        /* A PCI aperture cannot alias controller registers or another
         * already collected aperture. RAM overlap is checked by the plan. */
        for (uint32_t j = 0; j < *count; ++j)
            if (cpu < raw[j].end && raw[j].start < end) return 0;
        for (uint32_t j = 0; j < i; j += 28) {
            uint64_t other = read_be64(value + j + 4);
            uint64_t extent = read_be64(value + j + 20);
            if (pci < other + extent && other < pci + length) return 0;
        }
        raw[*count].start = cpu;
        raw[*count].end = end;
        ++*count;
    }
    return 1;
}

static int collect_ranges(
    const struct FDTView *view, const struct FDTPlatform *platform,
    struct RawRange raw[FDT_MAX_RAW_RANGES], uint32_t *raw_count,
    uint32_t *enabled_nodes, uint32_t *disabled_nodes,
    struct AROSAppleDeviceMap *device_map)
{
    struct RawRange child[FDT_MAX_NODE_RANGES];
    uint32_t child_count = 0;
    uint32_t cursor = 0;
    uint32_t name_size;
    uint32_t value_size;
    uint32_t name_offset;
    uint32_t next;
    uint32_t index;
    int depth = -1;
    int soc_active = 0;
    int direct_active = 0;
    int child_enabled = 1;
    int saw_status = 0;
    int saw_reg = 0;
    int child_matches_serial = 0;
    int child_is_console = 0;
    int child_is_aic = 0;
    int child_is_pmgr = 0;
    int child_is_pcie = 0, saw_ranges = 0;
    const uint8_t *pci_ranges = NULL;
    uint32_t pci_ranges_size = 0, pci_address_cells = 0, pci_size_cells = 0;
    uint32_t pci_first = 0, pci_count = 0;
    int saw_end = 0;

    *raw_count = 0;
    *enabled_nodes = 0;
    *disabled_nodes = 0;
    while (cursor <= view->structure_size - 4U) {
        uint32_t token = read_be32(view->structure + cursor);
        const uint8_t *name;
        const uint8_t *value;

        cursor += 4U;
        if (token == FDT_BEGIN_NODE) {
            if (depth + 1 >= FDT_MAX_DEPTH ||
                !bounded_cstring(view->structure, cursor,
                    view->structure_size, &name_size) ||
                !align_property(cursor, name_size + 1U,
                    view->structure_size, &next))
                return 0;
            ++depth;
            name = view->structure + cursor;
            if (depth == 1 && soc_node_name(name, name_size))
                soc_active = 1;
            else if (depth == 2 && soc_active) {
                if (direct_active)
                    return 0;
                direct_active = 1;
                child_enabled = 1;
                saw_status = 0;
                saw_reg = 0;
                child_count = 0;
                child_matches_serial = serial_path_matches(
                    platform, name, name_size);
                child_is_console = 0;
                child_is_aic = 0;
                child_is_pmgr = 0;
                child_is_pcie = 0;
                saw_ranges = 0;
                pci_ranges = NULL;
                pci_ranges_size = pci_address_cells = pci_size_cells = 0;
            }
            cursor = next;
        } else if (token == FDT_END_NODE) {
            if (depth < 0)
                return 0;
            if (depth == 2 && direct_active) {
                if (saw_reg) {
                    if (child_enabled) {
                        if (child_count > FDT_MAX_RAW_RANGES - *raw_count)
                            return 0;
                        for (index = 0; index < child_count; ++index)
                            raw[(*raw_count)++] = child[index];
                        if (child_is_pcie) {
                            if (pci_count) return 0;
                            pci_first = *raw_count;
                            if (!append_pcie_windows(pci_ranges,
                                    pci_ranges_size, pci_address_cells, pci_size_cells,
                                    platform, raw, raw_count)) return 0;
                            pci_count = *raw_count - pci_first;
                            for (index = pci_first; index < pci_first + pci_count; ++index)
                                if (!append_role(device_map,
                                        AROS_APPLE_DEVICE_ROLE_PCI_WINDOW, &raw[index]))
                                    return 0;
                        }
                        if (child_is_console &&
                            !append_role(device_map,
                                AROS_APPLE_DEVICE_ROLE_CONSOLE, &child[0]))
                            return 0;
                        if (child_is_aic &&
                            !append_role(device_map,
                                AROS_APPLE_DEVICE_ROLE_AIC, &child[0]))
                            return 0;
                        if (child_is_pmgr &&
                            !append_role(device_map,
                                AROS_APPLE_DEVICE_ROLE_PMGR, &child[0]))
                            return 0;
                        ++*enabled_nodes;
                    } else {
                        if (AROSAppleStage2FindDeviceRole(device_map,
                                AROS_APPLE_DEVICE_ROLE_EXCLUDED, 0) == NULL &&
                            !append_role(device_map,
                                AROS_APPLE_DEVICE_ROLE_EXCLUDED, &child[0]))
                            return 0;
                        ++*disabled_nodes;
                    }
                }
                direct_active = 0;
            } else if (depth == 1 && soc_active) {
                soc_active = 0;
            }
            --depth;
        } else if (token == FDT_PROP) {
            if (depth < 0 || cursor > view->structure_size - 8U)
                return 0;
            value_size = read_be32(view->structure + cursor);
            name_offset = read_be32(view->structure + cursor + 4U);
            cursor += 8U;
            if (!property_name(view, name_offset, &name, &name_size) ||
                !align_property(cursor, value_size,
                    view->structure_size, &next))
                return 0;
            value = view->structure + cursor;
            if (depth == 2 && direct_active) {
                if (bytes_equal(name, name_size, "status")) {
                    if (saw_status ||
                        !enabled_status(value, value_size, &child_enabled))
                        return 0;
                    saw_status = 1;
                } else if (bytes_equal(name, name_size, "reg")) {
                    if (saw_reg || !parse_reg(value, value_size, platform,
                            child, &child_count))
                        return 0;
                    saw_reg = 1;
                } else if (bytes_equal(name, name_size, "compatible")) {
                    int uart = string_list_has(value, value_size,
                        "apple,s5l-uart");
                    int aic = string_list_has(value, value_size,
                        "apple,aic");
                    int pmgr = string_list_has(value, value_size,
                        "apple,pmgr");
                    int pcie = string_list_has(value, value_size,
                        "apple,t8103-pcie");

                    if (uart < 0 || aic < 0 || pmgr < 0 || pcie < 0)
                        return 0;
                    child_is_console = child_matches_serial && uart;
                    child_is_aic |= aic;
                    child_is_pmgr |= pmgr;
                    child_is_pcie |= pcie;
                } else if (bytes_equal(name, name_size, "ranges")) {
                    if (saw_ranges) return 0;
                    saw_ranges = 1;
                    pci_ranges = value;
                    pci_ranges_size = value_size;
                } else if (bytes_equal(name, name_size, "#address-cells")) {
                    if (pci_address_cells || value_size != 4) return 0;
                    pci_address_cells = read_be32(value);
                } else if (bytes_equal(name, name_size, "#size-cells")) {
                    if (pci_size_cells || value_size != 4) return 0;
                    pci_size_cells = read_be32(value);
                }
            }
            cursor = next;
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            if (depth != -1 || direct_active || soc_active)
                return 0;
            saw_end = 1;
            break;
        } else {
            return 0;
        }
    }
    /* Also reject aliases with devices encountered after the PCI host. */
    for (uint32_t i = pci_first; i < pci_first + pci_count; ++i)
        for (uint32_t j = 0; j < *raw_count; ++j)
            if (i != j && raw[i].start < raw[j].end &&
                raw[j].start < raw[i].end) return 0;
    return saw_end && *raw_count != 0 && *enabled_nodes != 0;
}

const struct AROSAppleDeviceRole *AROSAppleStage2FindDeviceRole(
    const struct AROSAppleDeviceMap *device_map,
    uint32_t kind,
    uint32_t instance)
{
    uint32_t index;

    if (device_map == NULL ||
        device_map->version != AROS_APPLE_DEVICE_MAP_VERSION ||
        device_map->size != sizeof(*device_map) ||
        device_map->role_count > AROS_APPLE_DEVICE_MAP_MAX_ROLES)
        return NULL;
    for (index = 0; index < device_map->role_count; ++index) {
        if (device_map->roles[index].kind == kind &&
            device_map->roles[index].instance == instance)
            return &device_map->roles[index];
    }
    return NULL;
}

static void sort_ranges(struct RawRange *ranges, uint32_t count)
{
    struct RawRange value;
    uint32_t index;
    uint32_t position;

    for (index = 1; index < count; ++index) {
        value = ranges[index];
        position = index;
        while (position != 0 &&
                (ranges[position - 1U].start > value.start ||
                 (ranges[position - 1U].start == value.start &&
                  ranges[position - 1U].end > value.end))) {
            ranges[position] = ranges[position - 1U];
            --position;
        }
        ranges[position] = value;
    }
}

static const struct AROSAppleMemoryPlanRange *plan_range_at(
    const struct AROSAppleMemoryPlan *plan, uint32_t index)
{
    return (const struct AROSAppleMemoryPlanRange *)(
        (const uint8_t *)plan + sizeof(*plan) +
        (uint64_t)index * plan->entry_size);
}

int AROSAppleStage2DeviceMapValidate(
    const struct AROSAppleDeviceMap *device_map,
    const struct AROSAppleMemoryPlan *memory_plan)
{
    uint64_t mapped_bytes = 0;
    uint32_t index;
    uint32_t memory_index;
    uint32_t role_index;

    if (device_map == NULL || memory_plan == NULL ||
        !AROSAppleMemoryPlanValidate(memory_plan) ||
        device_map->version != AROS_APPLE_DEVICE_MAP_VERSION ||
        device_map->size != sizeof(*device_map) ||
        device_map->entry_count > AROS_APPLE_DEVICE_MAP_MAX_RANGES ||
        device_map->role_count > AROS_APPLE_DEVICE_MAP_MAX_ROLES ||
        device_map->source_count > FDT_MAX_RAW_RANGES)
        return 0;
    for (index = device_map->entry_count;
            index < AROS_APPLE_DEVICE_MAP_MAX_RANGES; ++index) {
        if (device_map->ranges[index].start != 0 ||
            device_map->ranges[index].end != 0)
            return 0;
    }
    for (index = device_map->role_count;
            index < AROS_APPLE_DEVICE_MAP_MAX_ROLES; ++index) {
        if (device_map->roles[index].kind != 0 ||
            device_map->roles[index].instance != 0 ||
            device_map->roles[index].start != 0 ||
            device_map->roles[index].end != 0)
            return 0;
    }
    if (device_map->profile == AROS_APPLE_DEVICE_MAP_PROFILE_NONE)
        return device_map->entry_count == 0 &&
            device_map->source_count == 0 &&
            device_map->enabled_node_count == 0 &&
            device_map->disabled_node_count == 0 &&
            device_map->role_count == 0 &&
            device_map->mapped_bytes == 0;
    if ((device_map->profile != AROS_APPLE_DEVICE_MAP_PROFILE_APPLE &&
         device_map->profile != AROS_APPLE_DEVICE_MAP_PROFILE_VIRT) ||
        device_map->entry_count == 0 ||
        device_map->source_count < device_map->entry_count ||
        device_map->source_count < device_map->enabled_node_count ||
        device_map->enabled_node_count == 0 ||
        device_map->enabled_node_count > FDT_MAX_NODES ||
        device_map->disabled_node_count > FDT_MAX_NODES ||
        device_map->enabled_node_count > FDT_MAX_NODES -
            device_map->disabled_node_count)
        return 0;
    for (index = 0; index < device_map->entry_count; ++index) {
        const struct AROSAppleDeviceRange *range = &device_map->ranges[index];

        if (range->start == 0 || range->end <= range->start ||
            range->end > DEVICE_ADDRESS_LIMIT ||
            (range->start & (DEVICE_PAGE_SIZE - 1U)) != 0 ||
            (range->end & (DEVICE_PAGE_SIZE - 1U)) != 0 ||
            (index != 0 && device_map->ranges[index - 1U].end >=
                range->start) ||
            !add_u64(mapped_bytes, range->end - range->start, &mapped_bytes))
            return 0;
        for (memory_index = 0;
                memory_index < memory_plan->entry_count; ++memory_index) {
            const struct AROSAppleMemoryPlanRange *memory =
                plan_range_at(memory_plan, memory_index);

            if (range->start < memory->end && memory->start < range->end)
                return 0;
        }
    }
    for (index = 0; index < device_map->role_count; ++index) {
        const struct AROSAppleDeviceRole *role = &device_map->roles[index];
        uint32_t expected_instance = 0;
        int contained = 0;

        if ((role->kind != AROS_APPLE_DEVICE_ROLE_CONSOLE &&
             role->kind != AROS_APPLE_DEVICE_ROLE_AIC &&
             role->kind != AROS_APPLE_DEVICE_ROLE_PMGR &&
             role->kind != AROS_APPLE_DEVICE_ROLE_EXCLUDED &&
             role->kind != AROS_APPLE_DEVICE_ROLE_EVIDENCE &&
             role->kind != AROS_APPLE_DEVICE_ROLE_PCI_WINDOW &&
             role->kind != AROS_APPLE_DEVICE_ROLE_GIC &&
             role->kind != AROS_APPLE_DEVICE_ROLE_MSI_FRAME &&
             role->kind != AROS_APPLE_DEVICE_ROLE_PCI_CONFIG) ||
            role->start == 0 || role->end <= role->start ||
            (role->start & (DEVICE_PAGE_SIZE - 1U)) != 0 ||
            (role->end & (DEVICE_PAGE_SIZE - 1U)) != 0)
            return 0;
        for (role_index = 0; role_index < index; ++role_index) {
            if (device_map->roles[role_index].kind == role->kind)
                ++expected_instance;
        }
        if (role->instance != expected_instance ||
            ((role->kind == AROS_APPLE_DEVICE_ROLE_CONSOLE ||
              role->kind == AROS_APPLE_DEVICE_ROLE_EVIDENCE) &&
             role->instance != 0))
            return 0;
        for (role_index = 0; role_index < device_map->entry_count;
                ++role_index) {
            if (device_map->ranges[role_index].start <= role->start &&
                device_map->ranges[role_index].end >= role->end) {
                contained = 1;
                break;
            }
        }
        if ((role->kind == AROS_APPLE_DEVICE_ROLE_EXCLUDED) == contained)
            return 0;
    }
    return mapped_bytes == device_map->mapped_bytes;
}

/* QEMU virt publishes its PL011 as a direct root child. Only map a
 * complete, enabled binding with the root's documented 64-bit reg cells. */
uint64_t AROSVMPL011Base;
static struct VMConsole *vm_console;
void AROSVMConsoleWrite0(const char *text)
{
    if (vm_console) {
        size_t n=0;while(text[n]) ++n;
        (void)vm_console_write(vm_console,text,n);
    } else if (AROSVMPL011Base) {
        volatile uint32_t *data=(volatile uint32_t *)(uintptr_t)AROSVMPL011Base;
        volatile uint32_t *flags=(volatile uint32_t *)(uintptr_t)(AROSVMPL011Base+0x18);
        while(*text) {
            unsigned count=0;
            while((*flags&(1U<<5)) && ++count<1000000) {}
            if(count==1000000) return;
            *data=(uint8_t)*text++;
        }
    }
}
/* Resolve only the bootstrap-owned register windows. Device drivers will
 * acquire their own resources later; do not map every reg in an untrusted tree. */
static int vm_gic_map(const struct AROSEarlyFDT *tree,
    struct AROSAppleDeviceMap *map)
{
    struct RawRange raw[3];
    int node = -1, depth = -1, controller = -1;
    uint32_t count = 1, index;

    raw[0].start = map->ranges[0].start;
    raw[0].end = map->ranges[0].end;
    while ((node = AROSEarlyFDTNext(tree, node, &depth)) >= 0 && depth >= 0) {
        if (!AROSEarlyFDTEnabled(tree, node) ||
            (!AROSEarlyFDTCompatible(tree, node, "arm,gic-v3") &&
             !AROSEarlyFDTCompatible(tree, node, "arm,gic-400") &&
             !AROSEarlyFDTCompatible(tree, node, "arm,cortex-a15-gic")))
            continue;
        if (controller >= 0)
            return 0;
        controller = node;
    }
    if (controller < 0)
        return 1; /* Console-only firmware remains valid, without IRQ service. */
    for (index = 0; index < 2; ++index) {
        uint64_t base, span;
        if (!AROSEarlyFDTReg(tree, controller, index, &base, &span) ||
            !base || span < DEVICE_PAGE_SIZE || span > DEVICE_ADDRESS_LIMIT ||
            base > DEVICE_ADDRESS_LIMIT - span ||
            (base & (DEVICE_PAGE_SIZE - 1)) || (span & (DEVICE_PAGE_SIZE - 1)))
            return 0;
        for (uint32_t previous = 0; previous < count; ++previous)
            if (base < raw[previous].end && raw[previous].start < base + span)
                return 0;
        raw[count].start = base;
        raw[count].end = base + span;
        if (!append_role(map, AROS_APPLE_DEVICE_ROLE_GIC, &raw[count]))
            return 0;
        ++count;
    }
    sort_ranges(raw, count);
    map->entry_count = 0;
    map->source_count = count;
    ++map->enabled_node_count;
    map->mapped_bytes = 0;
    for (index = 0; index < count; ++index) {
        struct AROSAppleDeviceRange *range;
        map->mapped_bytes += raw[index].end - raw[index].start;
        if (map->entry_count &&
            map->ranges[map->entry_count - 1].end == raw[index].start) {
            map->ranges[map->entry_count - 1].end = raw[index].end;
            continue;
        }
        range = &map->ranges[map->entry_count++];
        range->start = raw[index].start;
        range->end = raw[index].end;
    }
    return 1;
}

static int vm_pci_map(const struct AROSEarlyFDT *tree, struct AROSAppleDeviceMap *map)
{
    struct AROSFDTPCI pci;
    struct RawRange raw[AROS_APPLE_DEVICE_MAP_MAX_RANGES];
    uint32_t count = map->entry_count;
    int status = AROSFDTDiscoverPCI(tree, &pci);
    if (status < 0) return 1;
    if (!status || count + pci.window_count + 1 > AROS_APPLE_DEVICE_MAP_MAX_RANGES) return 0;
    for (uint32_t i = 0; i < count; ++i) {
        raw[i].start = map->ranges[i].start;
        raw[i].end = map->ranges[i].end;
    }
    for (uint32_t i = 0; i <= pci.window_count; ++i) {
        struct RawRange range;
        range.start = i ? pci.windows[i-1].cpu : pci.ecam;
        range.end = range.start + (i ? pci.windows[i-1].bytes : pci.ecam_bytes);
        /* A console BAR may be contained in its PCI aperture. Controllers
         * outside PCI must never acquire a different MMIO memory type. */
        for (uint32_t j = 0; j < map->role_count; ++j) {
            const struct AROSAppleDeviceRole *role = &map->roles[j];
            if (range.start < role->end && role->start < range.end &&
                !(i && role->kind == AROS_APPLE_DEVICE_ROLE_CONSOLE &&
                  range.start <= role->start && role->end <= range.end)) return 0;
        }
        if (!append_role(map, i ? AROS_APPLE_DEVICE_ROLE_PCI_WINDOW :
            AROS_APPLE_DEVICE_ROLE_PCI_CONFIG, &range)) return 0;
        raw[count++] = range;
    }
    struct AROSFDTMSIFrame msi;
    status = AROSFDTDiscoverMSIFrame(tree, pci.node, &msi);
    if (!status) return 0;
    if (status > 0) {
        if (count == AROS_APPLE_DEVICE_MAP_MAX_RANGES) return 0;
        struct RawRange range = {msi.base, msi.base + msi.bytes};
        for (uint32_t i = 0; i < count; ++i)
            if (range.start < raw[i].end && raw[i].start < range.end) return 0;
        if (!append_role(map, AROS_APPLE_DEVICE_ROLE_MSI_FRAME, &range)) return 0;
        raw[count++] = range;
        ++map->source_count;
        ++map->enabled_node_count;
    }
    sort_ranges(raw, count);
    map->source_count += pci.window_count + 1;
    ++map->enabled_node_count;
    map->entry_count = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (map->entry_count && map->ranges[map->entry_count-1].end >= raw[i].start) {
            if (raw[i].end > map->ranges[map->entry_count-1].end)
                map->ranges[map->entry_count-1].end = raw[i].end;
        } else {
            map->ranges[map->entry_count].start = raw[i].start;
            map->ranges[map->entry_count++].end = raw[i].end;
        }
    }
    map->mapped_bytes = 0;
    for (uint32_t i = 0; i < map->entry_count; ++i)
        map->mapped_bytes += map->ranges[i].end - map->ranges[i].start;
    return 1;
}

static int vm_console_map(const struct FDTView *view,
    struct AROSAppleDeviceMap *map, uint64_t *console_state)
{
    struct AROSEarlyFDT tree;
    uint64_t base, span;
    int selected, node = -1, depth = -1, length;
    const uint8_t *state;
    struct RawRange range;

    if (!AROSEarlyFDTOpen(&tree, view->data, view->total_size))
        return 0;
    if (!AROSEarlyFDTCompatible(&tree, 0, "linux,dummy-virt") &&
        !AROSEarlyFDTCompatible(&tree, 0, "aros,virtualization"))
        return 1;
    state = AROSEarlyFDTProperty(&tree, 0, "aros,vm-console-state", &length);
    if (state) {
        if (length != 8) return 0;
        *console_state = read_be64(state);
    }
    selected = AROSEarlyFDTStdout(&tree);
    if (selected < 0) {
        /* Older qualification trees have no chosen node. A unique compatible
         * console is an unambiguous fallback; an invalid stdout-path is not. */
        int chosen = AROSEarlyFDTFindPath(&tree, "/chosen");
        if (chosen >= 0 && AROSEarlyFDTProperty(&tree, chosen, "stdout-path", &length))
            return 0;
        while ((node = AROSEarlyFDTNext(&tree, node, &depth)) >= 0 && depth >= 0) {
            if (AROSEarlyFDTEnabled(&tree, node) &&
                (AROSEarlyFDTCompatible(&tree, node, "arm,pl011") ||
                 AROSEarlyFDTCompatible(&tree, node, "aros,virtio-console"))) {
                if (selected >= 0) return 0;
                selected = node;
            }
        }
    }
    if (selected < 0 ||
        (!AROSEarlyFDTCompatible(&tree, selected, "arm,pl011") &&
         !AROSEarlyFDTCompatible(&tree, selected, "aros,virtio-console")) ||
        !AROSEarlyFDTReg(&tree, selected, 0, &base, &span) ||
        !base || span < DEVICE_PAGE_SIZE || span > DEVICE_ADDRESS_LIMIT ||
        base > DEVICE_ADDRESS_LIMIT - span ||
        (base & (DEVICE_PAGE_SIZE - 1)) || (span & (DEVICE_PAGE_SIZE - 1)))
        return 0;
    map->profile = AROS_APPLE_DEVICE_MAP_PROFILE_VIRT;
    map->entry_count = map->source_count = map->enabled_node_count = 1;
    map->ranges[0].start = base; map->ranges[0].end = base + span;
    map->mapped_bytes = span;
    range.start = base; range.end = base + span;
    return append_role(map, AROS_APPLE_DEVICE_ROLE_CONSOLE, &range) &&
        vm_gic_map(&tree, map) && vm_pci_map(&tree, map);
}

int AROSAppleStage2BuildDeviceMap(
    const struct AROSBootInfo *boot_info,
    const struct AROSAppleMemoryPlan *memory_plan,
    struct AROSAppleDeviceMap *device_map)
{
    struct FDTView view;
    struct FDTPlatform platform;
    struct RawRange raw[FDT_MAX_RAW_RANGES];
    uint32_t raw_count;
    uint32_t enabled_nodes;
    uint32_t disabled_nodes;
    uint32_t index;

    if (device_map == NULL || memory_plan == NULL ||
        !AROSAppleMemoryPlanValidate(memory_plan) ||
        !fdt_view(boot_info, &view) || !scan_platform(&view, &platform))
        return 0;
    device_map->version = AROS_APPLE_DEVICE_MAP_VERSION;
    device_map->size = sizeof(*device_map);
    device_map->profile = AROS_APPLE_DEVICE_MAP_PROFILE_NONE;
    device_map->entry_count = 0;
    device_map->source_count = 0;
    device_map->enabled_node_count = 0;
    device_map->disabled_node_count = 0;
    device_map->role_count = 0;
    device_map->mapped_bytes = 0;
    for (index = 0; index < AROS_APPLE_DEVICE_MAP_MAX_ROLES; ++index) {
        device_map->roles[index].kind = 0;
        device_map->roles[index].instance = 0;
        device_map->roles[index].start = 0;
        device_map->roles[index].end = 0;
    }
    for (index = 0; index < AROS_APPLE_DEVICE_MAP_MAX_RANGES; ++index) {
        device_map->ranges[index].start = 0;
        device_map->ranges[index].end = 0;
    }
    AROSVMPL011Base = 0;
    vm_console = NULL;
    if (!platform.apple) {
        uint64_t console_state=0;
        if (!vm_console_map(&view, device_map, &console_state) ||
            !AROSAppleStage2DeviceMapValidate(device_map, memory_plan)) return 0;
        const struct AROSAppleDeviceRole *console =
            AROSAppleStage2FindDeviceRole(device_map,
                AROS_APPLE_DEVICE_ROLE_CONSOLE, 0);
        if (device_map->profile == AROS_APPLE_DEVICE_MAP_PROFILE_VIRT && console)
            AROSVMPL011Base = console->start;
        if (console_state) {
            int retained=0;
            if (console_state & 4095) return 0;
            for (uint32_t i=0;i<memory_plan->entry_count;++i) {
                const struct AROSAppleMemoryPlanRange *r=plan_range_at(memory_plan,i);
                if (console_state>=r->start && console_state<=r->end && r->end-console_state>=4096) retained=1;
            }
            if (!retained) return 0;
            vm_console=(struct VMConsole *)(uintptr_t)console_state;
            if (vm_console->magic!=VM_CONSOLE_MAGIC ||
                !console || vm_console->region_start!=console->start ||
                vm_console->region_end!=console->end) return 0;
        }
        return 1;
    }
    if (!collect_ranges(&view, &platform, raw, &raw_count,
            &enabled_nodes, &disabled_nodes, device_map))
        return 0;
    if (platform.saw_evidence_channel) {
        struct RawRange evidence;

        if (raw_count >= FDT_MAX_RAW_RANGES)
            return 0;
        evidence.start = platform.evidence_start;
        evidence.end = platform.evidence_end;
        raw[raw_count++] = evidence;
        if (!append_role(device_map,
                AROS_APPLE_DEVICE_ROLE_EVIDENCE, &evidence))
            return 0;
    }
    sort_ranges(raw, raw_count);
    device_map->profile = AROS_APPLE_DEVICE_MAP_PROFILE_APPLE;
    device_map->source_count = raw_count;
    device_map->enabled_node_count = enabled_nodes;
    device_map->disabled_node_count = disabled_nodes;
    for (index = 0; index < raw_count; ++index) {
        struct AROSAppleDeviceRange *range;

        if (device_map->entry_count != 0) {
            range = &device_map->ranges[device_map->entry_count - 1U];
            if (raw[index].start <= range->end) {
                if (raw[index].end > range->end)
                    range->end = raw[index].end;
                continue;
            }
        }
        if (device_map->entry_count >= AROS_APPLE_DEVICE_MAP_MAX_RANGES)
            return 0;
        range = &device_map->ranges[device_map->entry_count++];
        range->start = raw[index].start;
        range->end = raw[index].end;
    }
    for (index = 0; index < device_map->entry_count; ++index) {
        if (!add_u64(device_map->mapped_bytes,
                device_map->ranges[index].end -
                    device_map->ranges[index].start,
                &device_map->mapped_bytes))
            return 0;
    }
    return AROSAppleStage2DeviceMapValidate(device_map, memory_plan);
}
