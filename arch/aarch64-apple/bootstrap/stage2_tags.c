/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Convert the owned loader handoff into bounded AROS kernel tags.
 */

#include "stage2_tags.h"
#include "boot_memory_map.h"

#include <aros/apple/startup.h>

struct Stage2MemoryRange {
    uint64_t start;
    uint64_t end;
};

_Static_assert(sizeof(struct AROSAppleTagItem) == 16,
    "AROS TagItem-compatible layout");

static const char uefi_boot_loader_name[] = "arosboot-uefi-v4";
static const char m1n1_boot_loader_name[] = "arosboot-m1n1-v4";

static uint64_t load_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    uint32_t index;

    for (index = 0; index < 8; ++index)
        value |= (uint64_t)bytes[index] << (index * 8U);
    return value;
}

static int add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right > UINT64_MAX - left)
        return 0;
    *result = left + right;
    return 1;
}

static int align_up(uint64_t value, uint64_t alignment, uint64_t *result)
{
    uint64_t mask = alignment - 1U;

    if ((alignment & mask) != 0 || value > UINT64_MAX - mask)
        return 0;
    *result = (value + mask) & ~mask;
    return 1;
}

static void sort_ranges(struct Stage2MemoryRange *ranges, uint32_t count)
{
    struct Stage2MemoryRange value;
    uint32_t index;
    uint32_t position;

    for (index = 1; index < count; ++index) {
        value = ranges[index];
        position = index;
        while (position != 0 && ranges[position - 1U].start > value.start) {
            ranges[position] = ranges[position - 1U];
            --position;
        }
        ranges[position] = value;
    }
}

static enum AROSAppleStage2Status collect_memory_ranges(
    const struct AROSBootInfo *boot_info,
    struct Stage2MemoryRange *ranges,
    uint32_t *range_count,
    uint32_t *drop_count)
{
    struct AROSAppleBootMemoryView view;
    uint32_t descriptor_count;
    uint32_t input_count = 0;
    uint32_t output_count = 0;
    uint32_t smallest;
    uint32_t index;

    if (!AROSAppleBootMemoryMapValidate(boot_info, &descriptor_count))
        return AROS_APPLE_STAGE2_INVALID_MEMORY_MAP;

    for (index = 0; index < descriptor_count; ++index) {
        if (!AROSAppleBootMemoryMapRangeAt(boot_info, index, &view))
            return AROS_APPLE_STAGE2_INVALID_MEMORY_MAP;
        if (view.kind != AROS_APPLE_BOOT_MEMORY_AVAILABLE)
            continue;
        ranges[input_count].start = view.start;
        ranges[input_count].end = view.end;
        ++input_count;
    }
    if (input_count == 0)
        return AROS_APPLE_STAGE2_NO_USABLE_MEMORY;

    sort_ranges(ranges, input_count);
    for (index = 0; index < input_count; ++index) {
        if (output_count != 0) {
            if (ranges[index].start < ranges[output_count - 1U].end)
                return AROS_APPLE_STAGE2_INVALID_MEMORY_MAP;
            if (ranges[index].start == ranges[output_count - 1U].end) {
                ranges[output_count - 1U].end = ranges[index].end;
                continue;
            }
        }
        ranges[output_count++] = ranges[index];
    }
    *drop_count = 0;
    while (output_count > AROS_APPLE_MAX_MEMORY_RANGES) {
        smallest = 0;
        for (index = 1; index < output_count; ++index) {
            if (ranges[index].end - ranges[index].start <
                    ranges[smallest].end - ranges[smallest].start)
                smallest = index;
        }
        for (index = smallest + 1U; index < output_count; ++index)
            ranges[index - 1U] = ranges[index];
        --output_count;
        ++*drop_count;
    }
    *range_count = output_count;
    return AROS_APPLE_STAGE2_OK;
}

static enum AROSAppleStage2Status append_tag(
    struct AROSAppleTagItem *tags, uint32_t *count,
    uint32_t tag, uint64_t data)
{
    if (*count >= AROS_APPLE_MAX_BOOT_TAGS)
        return AROS_APPLE_STAGE2_TOO_MANY_BOOT_TAGS;
    tags[*count].tag = tag;
    tags[*count].reserved = 0;
    tags[*count].data = data;
    ++*count;
    return AROS_APPLE_STAGE2_OK;
}

/* Bits actually carried by one 32-bit pixel of a supported format, or zero
 * when the boot environment described a format no AROS display path can
 * consume.
 */
static uint32_t framebuffer_depth(uint32_t format)
{
    switch (format) {
    case AROS_BOOTINFO_FB_RGBX8888:
    case AROS_BOOTINFO_FB_BGRX8888:
        return 32U;
    case AROS_BOOTINFO_FB_X2R10G10B10:
        return 30U;
    default:
        return 0U;
    }
}

#define APPEND(tag_value, data_value) do { \
    status = append_tag(tags, &tag_count, (tag_value), (data_value)); \
    if (status != AROS_APPLE_STAGE2_OK) \
        return status; \
} while (0)

enum AROSAppleStage2Status AROSAppleStage2BuildTags(
    const struct AROSBootInfo *boot_info,
    uintptr_t stage2_image_start,
    struct AROSAppleStage2Result *result,
    uint64_t arena_end,
    uint64_t *cursor)
{
    struct Stage2MemoryRange ranges[AROS_APPLE_BOOT_MEMORY_MAX_RANGES];
    struct AROSAppleTagItem *tags;
    enum AROSAppleStage2Status status;
    uint64_t tags_start;
    uint64_t tags_end;
    uint64_t pitch;
    uint32_t depth;
    uint32_t range_count;
    uint32_t tag_count = 0;
    uint32_t protected_end_index;
    uint32_t index;

    status = collect_memory_ranges(boot_info, ranges, &range_count,
        &result->memory_range_drop_count);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    if (!align_up(*cursor, 16U, &tags_start) ||
        !add_u64(tags_start,
            (uint64_t)AROS_APPLE_MAX_BOOT_TAGS * sizeof(*tags), &tags_end) ||
        tags_end > arena_end)
        return AROS_APPLE_STAGE2_ARENA_EXHAUSTED;
    tags = (struct AROSAppleTagItem *)(uintptr_t)tags_start;
    for (index = 0; index < AROS_APPLE_MAX_BOOT_TAGS; ++index) {
        tags[index].tag = AROS_APPLE_TAG_DONE;
        tags[index].reserved = 0;
        tags[index].data = 0;
    }

    APPEND(AROS_APPLE_KRN_KERNEL_BASE, result->readonly_start);
    APPEND(AROS_APPLE_KRN_KERNEL_LOWEST, result->readonly_start);
    APPEND(AROS_APPLE_KRN_KERNEL_HIGHEST, result->writable_end);
    APPEND(AROS_APPLE_KRN_KERNEL_BSS, (uintptr_t)result->bss);
    APPEND(AROS_APPLE_KRN_PROT_AREA_START, stage2_image_start);
    protected_end_index = tag_count;
    APPEND(AROS_APPLE_KRN_PROT_AREA_END, 0);
    APPEND(AROS_APPLE_KRN_FLATTENED_DEVICE_TREE,
        boot_info->fdt_physical);
    APPEND(AROS_APPLE_KRN_FDT_SIZE, boot_info->fdt_size);
    APPEND(AROS_APPLE_KRN_BOOT_LOADER,
        (uintptr_t)(boot_info->producer == AROS_BOOT_PRODUCER_M1N1 ?
            m1n1_boot_loader_name : uefi_boot_loader_name));
    APPEND(AROS_APPLE_KRN_BOOT_IMAGE_START, result->ramdisk_address);
    APPEND(AROS_APPLE_KRN_BOOT_IMAGE_SIZE, result->ramdisk_size);
    APPEND(AROS_APPLE_KRN_PAGE_ARENA_START, result->page_arena_start);
    APPEND(AROS_APPLE_KRN_PAGE_ARENA_SIZE, result->page_arena_size);
    APPEND(AROS_APPLE_KRN_PAYLOAD_GENERATION,
        boot_info->payload_generation_sequence);
    APPEND(AROS_APPLE_KRN_PAYLOAD_SIZE, boot_info->payload_size);
    APPEND(AROS_APPLE_KRN_PAYLOAD_SHA256_0,
        load_u64_le(&boot_info->payload_sha256[0]));
    APPEND(AROS_APPLE_KRN_PAYLOAD_SHA256_1,
        load_u64_le(&boot_info->payload_sha256[8]));
    APPEND(AROS_APPLE_KRN_PAYLOAD_SHA256_2,
        load_u64_le(&boot_info->payload_sha256[16]));
    APPEND(AROS_APPLE_KRN_PAYLOAD_SHA256_3,
        load_u64_le(&boot_info->payload_sha256[24]));
    APPEND(AROS_APPLE_KRN_PAYLOAD_SLOT, boot_info->payload_slot);
    APPEND(AROS_APPLE_KRN_PAYLOAD_FLAGS, boot_info->payload_flags);
    APPEND(AROS_APPLE_KRN_PAYLOAD_PRODUCER, boot_info->producer);

    for (index = 0; index < range_count; ++index) {
        APPEND(AROS_APPLE_KRN_MEM_LOWER, ranges[index].start);
        APPEND(AROS_APPLE_KRN_MEM_UPPER, ranges[index].end);
    }

    depth = framebuffer_depth(boot_info->framebuffer_format);
    if ((boot_info->flags & AROS_BOOTINFO_FLAG_FRAMEBUFFER) != 0 &&
        depth != 0) {
        pitch = (uint64_t)boot_info->framebuffer_stride * sizeof(uint32_t);
        APPEND(AROS_APPLE_KRN_FB_ADDR,
            boot_info->framebuffer_physical);
        APPEND(AROS_APPLE_KRN_FRAMEBUFFER_WIDTH,
            boot_info->framebuffer_width);
        APPEND(AROS_APPLE_KRN_FRAMEBUFFER_HEIGHT,
            boot_info->framebuffer_height);
        APPEND(AROS_APPLE_KRN_FRAMEBUFFER_DEPTH, depth);
        APPEND(AROS_APPLE_KRN_FRAMEBUFFER_PITCH, pitch);
        APPEND(AROS_APPLE_KRN_FRAMEBUFFER_FORMAT,
            boot_info->framebuffer_format);
        APPEND(AROS_APPLE_KRN_FRAMEBUFFER_SIZE,
            boot_info->framebuffer_size);
    }
    APPEND(AROS_APPLE_TAG_DONE, 0);

    tags[protected_end_index].data = tags_end;
    result->boot_tags = (uintptr_t)tags;
    result->boot_tag_count = tag_count;
    result->memory_range_count = range_count;
    result->arena_used_end = (uintptr_t)tags_end;
    *cursor = tags_end;
    return AROS_APPLE_STAGE2_OK;
}

#undef APPEND

int AROSAppleStage2AppendDeviceTags(struct AROSAppleStage2Result *result)
{
    const struct AROSAppleDeviceRole *console;
    struct AROSAppleTagItem *tags;
    uint32_t count;
    uint32_t index;

    if (result == NULL || result->boot_tags == 0 ||
        result->boot_tag_count == 0 ||
        result->boot_tag_count > AROS_APPLE_MAX_BOOT_TAGS)
        return 0;
    count = result->boot_tag_count;
    tags = (struct AROSAppleTagItem *)(uintptr_t)result->boot_tags;
    if (tags[count - 1U].tag != AROS_APPLE_TAG_DONE ||
        tags[count - 1U].reserved != 0 || tags[count - 1U].data != 0)
        return 0;
    for (index = 0; index + 1U < count; ++index) {
        if (tags[index].tag == AROS_APPLE_KRN_DEBUG_UART_START ||
            tags[index].tag == AROS_APPLE_KRN_DEBUG_UART_END ||
            (tags[index].tag >= AROS_APPLE_KRN_GIC_DIST_START &&
             tags[index].tag <= AROS_APPLE_KRN_GIC_CPU_END))
            return 0;
    }
    if (result->device_map.profile == AROS_APPLE_DEVICE_MAP_PROFILE_NONE)
        return 1;
    if (result->device_map.profile != AROS_APPLE_DEVICE_MAP_PROFILE_APPLE &&
        result->device_map.profile != AROS_APPLE_DEVICE_MAP_PROFILE_VIRT)
        return 0;
    console = NULL;
    for (index = 0; index < result->device_map.role_count; ++index) {
        if (result->device_map.roles[index].kind ==
                AROS_APPLE_DEVICE_ROLE_CONSOLE &&
            result->device_map.roles[index].instance == 0) {
            if (console != NULL)
                return 0;
            console = &result->device_map.roles[index];
        }
    }
    if (console == NULL || console->start == 0 ||
        console->end <= console->start ||
        console->end - console->start < UINT64_C(0x30))
        return 0;
    const struct AROSAppleDeviceRole *gic[2] = {NULL, NULL};
    for (index = 0; index < result->device_map.role_count; ++index) {
        const struct AROSAppleDeviceRole *role = &result->device_map.roles[index];
        if (role->kind != AROS_APPLE_DEVICE_ROLE_GIC) continue;
        if (role->instance > 1 || gic[role->instance] != NULL) return 0;
        gic[role->instance] = role;
    }
    if ((gic[0] == NULL) != (gic[1] == NULL)) return 0;
    if (count > AROS_APPLE_MAX_BOOT_TAGS - (gic[0] ? 6U : 2U))
        return 0;
    tags[count - 1U].tag = AROS_APPLE_KRN_DEBUG_UART_START;
    tags[count - 1U].data = console->start;
    tags[count].tag = AROS_APPLE_KRN_DEBUG_UART_END;
    tags[count].reserved = 0;
    tags[count].data = console->end;
    tags[count + 1U].tag = AROS_APPLE_TAG_DONE;
    tags[count + 1U].reserved = 0;
    tags[count + 1U].data = 0;
    count += 2U;
    if (gic[0]) {
        uint64_t values[4] = {gic[0]->start, gic[0]->end, gic[1]->start, gic[1]->end};
        for (index = 0; index < 4; ++index) {
            tags[count - 1U].tag = AROS_APPLE_KRN_GIC_DIST_START + index;
            tags[count - 1U].reserved = 0;
            tags[count - 1U].data = values[index];
            ++count;
        }
        tags[count - 1U].tag = AROS_APPLE_TAG_DONE;
        tags[count - 1U].reserved = 0;
        tags[count - 1U].data = 0;
    }
    result->boot_tag_count = count;
    return 1;
}
