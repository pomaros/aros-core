/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Derive a bounded identity-mapped Apple MMIO plan from the published
 * flattened-device-tree format without importing a third-party FDT parser.
 */

#ifndef STAGE2_FDT_INTERNAL_H
#define STAGE2_FDT_INTERNAL_H
#include "stage2_fdt_map.h"
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
    int native;
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

#define read_be32 AROSStage2FDT_read_be32
uint32_t read_be32(const uint8_t *data);
#define read_be64 AROSStage2FDT_read_be64
uint64_t read_be64(const uint8_t *data);
#define add_u64 AROSStage2FDT_add_u64
int add_u64(uint64_t left, uint64_t right, uint64_t *result);
#define bounded_cstring AROSStage2FDT_bounded_cstring
int bounded_cstring(
    const uint8_t *data, uint32_t start, uint32_t end,
    uint32_t *length);
#define bytes_equal AROSStage2FDT_bytes_equal
int bytes_equal(
    const uint8_t *data, uint32_t size, const char *wanted);
#define string_list_has AROSStage2FDT_string_list_has
int string_list_has(
    const uint8_t *data, uint32_t size, const char *wanted);
#define copy_cstring AROSStage2FDT_copy_cstring
int copy_cstring(
    const uint8_t *value, uint32_t size,
    uint8_t *output, uint32_t capacity, uint32_t *output_size);
#define copy_bytes AROSStage2FDT_copy_bytes
int copy_bytes(
    uint8_t *output, uint32_t capacity,
    const uint8_t *input, uint32_t size);
#define property_name AROSStage2FDT_property_name
int property_name(
    const struct FDTView *view, uint32_t offset,
    const uint8_t **name, uint32_t *length);
#define align_property AROSStage2FDT_align_property
int align_property(uint32_t cursor, uint32_t size, uint32_t limit,
    uint32_t *next);
#define append_role AROSStage2FDT_append_role
int append_role(
    struct AROSAppleDeviceMap *device_map,
    uint32_t kind, const struct RawRange *range);
#define sort_ranges AROSStage2FDT_sort_ranges
void sort_ranges(struct RawRange *ranges, uint32_t count);

#define soc_node_name AROSStage2FDT_soc_node_name
int soc_node_name(const uint8_t *data, uint32_t size);
#endif
