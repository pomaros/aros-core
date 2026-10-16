/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Validate the Apple stage-two to AROS kernel startup contract.
 */

#include <aros/apple/startup.h>
#include <aros/apple/bootinfo.h>
#include <stddef.h>

#if defined(__AROS__)
#include <aros/kernel.h>
#include <utility/tagitem.h>

_Static_assert(sizeof(struct TagItem) == sizeof(struct AROSAppleTagItem),
    "Apple startup tag size must match AROS TagItem");
_Static_assert(offsetof(struct TagItem, ti_Tag) ==
    offsetof(struct AROSAppleTagItem, tag), "Apple startup tag ID offset");
_Static_assert(offsetof(struct TagItem, ti_Data) ==
    offsetof(struct AROSAppleTagItem, data), "Apple startup tag data offset");
_Static_assert(AROS_BOOT_MAGIC == AROS_APPLE_BOOT_MAGIC,
    "Apple startup magic drift");
_Static_assert(KRN_KernelBase == AROS_APPLE_KRN_KERNEL_BASE,
    "KRN_KernelBase drift");
_Static_assert(KRN_KernelLowest == AROS_APPLE_KRN_KERNEL_LOWEST,
    "KRN_KernelLowest drift");
_Static_assert(KRN_KernelHighest == AROS_APPLE_KRN_KERNEL_HIGHEST,
    "KRN_KernelHighest drift");
_Static_assert(KRN_KernelBss == AROS_APPLE_KRN_KERNEL_BSS,
    "KRN_KernelBss drift");
_Static_assert(KRN_ProtAreaStart == AROS_APPLE_KRN_PROT_AREA_START,
    "KRN_ProtAreaStart drift");
_Static_assert(KRN_ProtAreaEnd == AROS_APPLE_KRN_PROT_AREA_END,
    "KRN_ProtAreaEnd drift");
_Static_assert(KRN_MEMLower == AROS_APPLE_KRN_MEM_LOWER,
    "KRN_MEMLower drift");
_Static_assert(KRN_MEMUpper == AROS_APPLE_KRN_MEM_UPPER,
    "KRN_MEMUpper drift");
_Static_assert(KRN_BootLoader == AROS_APPLE_KRN_BOOT_LOADER,
    "KRN_BootLoader drift");
_Static_assert(KRN_FlattenedDeviceTree ==
    AROS_APPLE_KRN_FLATTENED_DEVICE_TREE, "KRN FDT drift");
_Static_assert(KRN_FBAddr == AROS_APPLE_KRN_FB_ADDR,
    "KRN framebuffer address drift");
_Static_assert(KRN_FrameBufferWidth == AROS_APPLE_KRN_FRAMEBUFFER_WIDTH,
    "KRN framebuffer width drift");
_Static_assert(KRN_FrameBufferHeight == AROS_APPLE_KRN_FRAMEBUFFER_HEIGHT,
    "KRN framebuffer height drift");
_Static_assert(KRN_FrameBufferDepth == AROS_APPLE_KRN_FRAMEBUFFER_DEPTH,
    "KRN framebuffer depth drift");
_Static_assert(KRN_FrameBufferPitch == AROS_APPLE_KRN_FRAMEBUFFER_PITCH,
    "KRN framebuffer pitch drift");
_Static_assert(KRN_FrameBufferFormat == AROS_APPLE_KRN_FRAMEBUFFER_FORMAT,
    "KRN framebuffer format drift");
_Static_assert(KRN_FrameBufferSize == AROS_APPLE_KRN_FRAMEBUFFER_SIZE,
    "KRN framebuffer size drift");
_Static_assert(KRN_FB_FORMAT_RGBX8888 == AROS_BOOTINFO_FB_RGBX8888,
    "RGBX framebuffer format drift");
_Static_assert(KRN_FB_FORMAT_BGRX8888 == AROS_BOOTINFO_FB_BGRX8888,
    "BGRX framebuffer format drift");
_Static_assert(KRN_FB_FORMAT_X2R10G10B10 == AROS_BOOTINFO_FB_X2R10G10B10,
    "X2R10G10B10 framebuffer format drift");
#endif

/* One 32-bit pixel per position in every supported format; only the number of
 * bits it actually carries differs, and depth and format must agree.
 */
static int framebuffer_depth_matches_format(uint64_t depth, uint64_t format)
{
    if (format == AROS_BOOTINFO_FB_RGBX8888 ||
        format == AROS_BOOTINFO_FB_BGRX8888)
        return depth == 32;
    if (format == AROS_BOOTINFO_FB_X2R10G10B10)
        return depth == 30;
    return 0;
}

#define REQUIRED_BASE (UINT32_C(1) << 0)
#define REQUIRED_LOWEST (UINT32_C(1) << 1)
#define REQUIRED_HIGHEST (UINT32_C(1) << 2)
#define REQUIRED_BSS (UINT32_C(1) << 3)
#define REQUIRED_PROT_START (UINT32_C(1) << 4)
#define REQUIRED_PROT_END (UINT32_C(1) << 5)
#define REQUIRED_FDT (UINT32_C(1) << 6)
#define REQUIRED_BOOT_LOADER (UINT32_C(1) << 7)
#define REQUIRED_FDT_SIZE (UINT32_C(1) << 8)
#define REQUIRED_ALL UINT32_C(0x1ff)

static int ranges_overlap(
    uint64_t first_start, uint64_t first_end,
    uint64_t second_start, uint64_t second_end)
{
    return first_start < second_end && second_start < first_end;
}

enum AROSAppleStartupContractStatus AROSAppleKernelStartupContract(
    const struct AROSAppleTagItem *tags, uint32_t magic)
{
    uint64_t gic[4] = {0};
    uint32_t gic_seen = 0;
    uint64_t kernel_base = 0;
    uint64_t kernel_lowest = 0;
    uint64_t kernel_highest = 0;
    uint64_t bss = 0;
    uint64_t protected_start = 0;
    uint64_t protected_end = 0;
    uint64_t memory_lower = 0;
    uint64_t previous_memory_end = 0;
    uint64_t framebuffer_width = 0;
    uint64_t framebuffer_height = 0;
    uint64_t framebuffer_depth = 0;
    uint64_t framebuffer_pitch = 0;
    uint64_t framebuffer_format = 0;
    uint64_t framebuffer_size = 0;
    uint64_t framebuffer_address = 0;
    uint64_t boot_image_start = 0;
    uint64_t boot_image_size = 0;
    uint64_t fdt_size = 0;
    uint64_t page_arena_start = 0;
    uint64_t page_arena_size = 0;
    uint64_t debug_uart_start = 0;
    uint64_t debug_uart_end = 0;
    uint64_t payload_generation = 0;
    uint64_t payload_size = 0;
    uint64_t payload_digest_or = 0;
    uint64_t payload_slot = 0;
    uint64_t payload_flags = 0;
    uint64_t payload_producer = 0;
    uint32_t required = 0;
    uint32_t payload = 0;
    uint32_t framebuffer = 0;
    uint32_t boot_image = 0;
    uint32_t page_arena = 0;
    uint32_t debug_uart = 0;
    uint32_t memory_ranges = 0;
    uint32_t index;
    uintptr_t tag_list_end;
    int memory_pair_open = 0;
    int terminated = 0;

    if (tags == NULL || ((uintptr_t)tags & 15U) != 0)
        return AROS_APPLE_STARTUP_CONTRACT_ARGUMENT;
    if (magic != AROS_APPLE_BOOT_MAGIC)
        return AROS_APPLE_STARTUP_CONTRACT_MAGIC;

    for (index = 0; index < AROS_APPLE_MAX_BOOT_TAGS; ++index) {
        const struct AROSAppleTagItem *tag = &tags[index];
        uint32_t bit = 0;
        uint32_t payload_bit = 0;

        if (tag->reserved != 0)
            return AROS_APPLE_STARTUP_CONTRACT_RESERVED;
        if (tag->tag == AROS_APPLE_TAG_DONE) {
            if (tag->data != 0)
                return AROS_APPLE_STARTUP_CONTRACT_TERMINATION;
            terminated = 1;
            break;
        }

        if (tag->tag >= AROS_APPLE_KRN_GIC_DIST_START &&
            tag->tag <= AROS_APPLE_KRN_GIC_CPU_END) {
            unsigned slot = tag->tag - AROS_APPLE_KRN_GIC_DIST_START;
            if (gic_seen & (1U << slot)) return AROS_APPLE_STARTUP_CONTRACT_DUPLICATE;
            gic_seen |= 1U << slot;
            gic[slot] = tag->data;
            continue;
        }
        switch (tag->tag) {
        case AROS_APPLE_KRN_KERNEL_BASE:
            bit = REQUIRED_BASE;
            kernel_base = tag->data;
            break;
        case AROS_APPLE_KRN_KERNEL_LOWEST:
            bit = REQUIRED_LOWEST;
            kernel_lowest = tag->data;
            break;
        case AROS_APPLE_KRN_KERNEL_HIGHEST:
            bit = REQUIRED_HIGHEST;
            kernel_highest = tag->data;
            break;
        case AROS_APPLE_KRN_KERNEL_BSS:
            bit = REQUIRED_BSS;
            bss = tag->data;
            break;
        case AROS_APPLE_KRN_PROT_AREA_START:
            bit = REQUIRED_PROT_START;
            protected_start = tag->data;
            break;
        case AROS_APPLE_KRN_PROT_AREA_END:
            bit = REQUIRED_PROT_END;
            protected_end = tag->data;
            break;
        case AROS_APPLE_KRN_FLATTENED_DEVICE_TREE:
            bit = REQUIRED_FDT;
            if (tag->data == 0)
                return AROS_APPLE_STARTUP_CONTRACT_REQUIRED;
            break;
        case AROS_APPLE_KRN_FDT_SIZE:
            bit = REQUIRED_FDT_SIZE;
            fdt_size = tag->data;
            break;
        case AROS_APPLE_KRN_BOOT_LOADER:
            bit = REQUIRED_BOOT_LOADER;
            if (tag->data == 0)
                return AROS_APPLE_STARTUP_CONTRACT_REQUIRED;
            break;
        case AROS_APPLE_KRN_MEM_LOWER:
            if (memory_pair_open || tag->data < previous_memory_end)
                return AROS_APPLE_STARTUP_CONTRACT_MEMORY;
            memory_lower = tag->data;
            memory_pair_open = 1;
            break;
        case AROS_APPLE_KRN_MEM_UPPER:
            if (!memory_pair_open || tag->data <= memory_lower ||
                ranges_overlap(memory_lower, tag->data,
                    protected_start, protected_end))
                return AROS_APPLE_STARTUP_CONTRACT_MEMORY;
            previous_memory_end = tag->data;
            memory_pair_open = 0;
            if (++memory_ranges > AROS_APPLE_MAX_MEMORY_RANGES)
                return AROS_APPLE_STARTUP_CONTRACT_MEMORY;
            break;
        case AROS_APPLE_KRN_FB_ADDR:
            if ((framebuffer & (UINT32_C(1) << 0)) != 0)
                return AROS_APPLE_STARTUP_CONTRACT_DUPLICATE;
            framebuffer |= UINT32_C(1) << 0;
            framebuffer_address = tag->data;
            if (tag->data == 0)
                return AROS_APPLE_STARTUP_CONTRACT_FRAMEBUFFER;
            break;
        case AROS_APPLE_KRN_FRAMEBUFFER_WIDTH:
            if ((framebuffer & (UINT32_C(1) << 1)) != 0)
                return AROS_APPLE_STARTUP_CONTRACT_DUPLICATE;
            framebuffer |= UINT32_C(1) << 1;
            framebuffer_width = tag->data;
            break;
        case AROS_APPLE_KRN_FRAMEBUFFER_HEIGHT:
            if ((framebuffer & (UINT32_C(1) << 2)) != 0)
                return AROS_APPLE_STARTUP_CONTRACT_DUPLICATE;
            framebuffer |= UINT32_C(1) << 2;
            framebuffer_height = tag->data;
            if (tag->data == 0)
                return AROS_APPLE_STARTUP_CONTRACT_FRAMEBUFFER;
            break;
        case AROS_APPLE_KRN_FRAMEBUFFER_DEPTH:
            if ((framebuffer & (UINT32_C(1) << 3)) != 0)
                return AROS_APPLE_STARTUP_CONTRACT_DUPLICATE;
            framebuffer |= UINT32_C(1) << 3;
            framebuffer_depth = tag->data;
            break;
        case AROS_APPLE_KRN_FRAMEBUFFER_PITCH:
            if ((framebuffer & (UINT32_C(1) << 4)) != 0)
                return AROS_APPLE_STARTUP_CONTRACT_DUPLICATE;
            framebuffer |= UINT32_C(1) << 4;
            framebuffer_pitch = tag->data;
            break;
        case AROS_APPLE_KRN_FRAMEBUFFER_FORMAT:
            if ((framebuffer & (UINT32_C(1) << 5)) != 0)
                return AROS_APPLE_STARTUP_CONTRACT_DUPLICATE;
            framebuffer |= UINT32_C(1) << 5;
            framebuffer_format = tag->data;
            break;
        case AROS_APPLE_KRN_FRAMEBUFFER_SIZE:
            if ((framebuffer & (UINT32_C(1) << 6)) != 0)
                return AROS_APPLE_STARTUP_CONTRACT_DUPLICATE;
            framebuffer |= UINT32_C(1) << 6;
            framebuffer_size = tag->data;
            break;
        case AROS_APPLE_KRN_BOOT_IMAGE_START:
            if ((boot_image & (UINT32_C(1) << 0)) != 0)
                return AROS_APPLE_STARTUP_CONTRACT_DUPLICATE;
            boot_image |= UINT32_C(1) << 0;
            boot_image_start = tag->data;
            break;
        case AROS_APPLE_KRN_BOOT_IMAGE_SIZE:
            if ((boot_image & (UINT32_C(1) << 1)) != 0)
                return AROS_APPLE_STARTUP_CONTRACT_DUPLICATE;
            boot_image |= UINT32_C(1) << 1;
            boot_image_size = tag->data;
            break;
        case AROS_APPLE_KRN_PAGE_ARENA_START:
            if ((page_arena & (UINT32_C(1) << 0)) != 0)
                return AROS_APPLE_STARTUP_CONTRACT_DUPLICATE;
            page_arena |= UINT32_C(1) << 0;
            page_arena_start = tag->data;
            break;
        case AROS_APPLE_KRN_PAGE_ARENA_SIZE:
            if ((page_arena & (UINT32_C(1) << 1)) != 0)
                return AROS_APPLE_STARTUP_CONTRACT_DUPLICATE;
            page_arena |= UINT32_C(1) << 1;
            page_arena_size = tag->data;
            break;
        case AROS_APPLE_KRN_DEBUG_UART_START:
            if ((debug_uart & (UINT32_C(1) << 0)) != 0)
                return AROS_APPLE_STARTUP_CONTRACT_DUPLICATE;
            debug_uart |= UINT32_C(1) << 0;
            debug_uart_start = tag->data;
            break;
        case AROS_APPLE_KRN_DEBUG_UART_END:
            if ((debug_uart & (UINT32_C(1) << 1)) != 0)
                return AROS_APPLE_STARTUP_CONTRACT_DUPLICATE;
            debug_uart |= UINT32_C(1) << 1;
            debug_uart_end = tag->data;
            break;
        case AROS_APPLE_KRN_PAYLOAD_GENERATION:
            payload_bit = UINT32_C(1) << 0;
            payload_generation = tag->data;
            break;
        case AROS_APPLE_KRN_PAYLOAD_SIZE:
            payload_bit = UINT32_C(1) << 1;
            payload_size = tag->data;
            break;
        case AROS_APPLE_KRN_PAYLOAD_SHA256_0:
            payload_bit = UINT32_C(1) << 2;
            payload_digest_or |= tag->data;
            break;
        case AROS_APPLE_KRN_PAYLOAD_SHA256_1:
            payload_bit = UINT32_C(1) << 3;
            payload_digest_or |= tag->data;
            break;
        case AROS_APPLE_KRN_PAYLOAD_SHA256_2:
            payload_bit = UINT32_C(1) << 4;
            payload_digest_or |= tag->data;
            break;
        case AROS_APPLE_KRN_PAYLOAD_SHA256_3:
            payload_bit = UINT32_C(1) << 5;
            payload_digest_or |= tag->data;
            break;
        case AROS_APPLE_KRN_PAYLOAD_SLOT:
            payload_bit = UINT32_C(1) << 6;
            payload_slot = tag->data;
            break;
        case AROS_APPLE_KRN_PAYLOAD_FLAGS:
            payload_bit = UINT32_C(1) << 7;
            payload_flags = tag->data;
            break;
        case AROS_APPLE_KRN_PAYLOAD_PRODUCER:
            payload_bit = UINT32_C(1) << 8;
            payload_producer = tag->data;
            break;
        default:
            return AROS_APPLE_STARTUP_CONTRACT_REQUIRED;
        }
        if (bit != 0) {
            if ((required & bit) != 0)
                return AROS_APPLE_STARTUP_CONTRACT_DUPLICATE;
            required |= bit;
        }
        if (payload_bit != 0) {
            if ((payload & payload_bit) != 0)
                return AROS_APPLE_STARTUP_CONTRACT_DUPLICATE;
            payload |= payload_bit;
        }
    }

    if (gic_seen && (gic_seen != 15U || !gic[0] || !gic[2] ||
        gic[1] <= gic[0] || gic[3] <= gic[2] ||
        ((gic[0] | gic[1] | gic[2] | gic[3]) & 4095U) ||
        ranges_overlap(gic[0], gic[1], gic[2], gic[3])))
        return AROS_APPLE_STARTUP_CONTRACT_LAYOUT;
    if (!terminated || memory_pair_open)
        return AROS_APPLE_STARTUP_CONTRACT_TERMINATION;
    if (required != REQUIRED_ALL || memory_ranges == 0 || bss == 0)
        return AROS_APPLE_STARTUP_CONTRACT_REQUIRED;
    if (fdt_size < 40U || fdt_size > AROS_APPLE_FDT_MAX_SIZE)
        return AROS_APPLE_STARTUP_CONTRACT_REQUIRED;
    if (kernel_lowest == 0 || kernel_base < kernel_lowest ||
        kernel_base >= kernel_highest || protected_start == 0 ||
        protected_start > kernel_lowest || protected_end <= kernel_highest)
        return AROS_APPLE_STARTUP_CONTRACT_LAYOUT;
    if (bss < protected_start || bss >= protected_end ||
        (uintptr_t)tags < protected_start)
        return AROS_APPLE_STARTUP_CONTRACT_LAYOUT;
    tag_list_end = (uintptr_t)tags + (index + 1U) * sizeof(*tags);
    if (tag_list_end < (uintptr_t)tags || tag_list_end > protected_end)
        return AROS_APPLE_STARTUP_CONTRACT_LAYOUT;
    if (framebuffer != 0 &&
        (framebuffer != UINT32_C(0x7f) || framebuffer_width == 0 ||
         framebuffer_height == 0 ||
         !framebuffer_depth_matches_format(
             framebuffer_depth, framebuffer_format) ||
         framebuffer_width > UINT64_MAX / 4U ||
         framebuffer_pitch < framebuffer_width * 4U ||
         framebuffer_height > UINT64_MAX / framebuffer_pitch ||
         framebuffer_size < framebuffer_height * framebuffer_pitch ||
         framebuffer_address > UINT64_MAX - framebuffer_size))
        return AROS_APPLE_STARTUP_CONTRACT_FRAMEBUFFER;
    if (boot_image != UINT32_C(3) || boot_image_start == 0 ||
        boot_image_size == 0 || boot_image_start > UINT64_MAX - boot_image_size)
        return AROS_APPLE_STARTUP_CONTRACT_BOOT_IMAGE;
    if (page_arena != UINT32_C(3) || page_arena_start == 0 ||
        (page_arena_start & (AROS_APPLE_MEMORY_PLAN_PAGE_SIZE - 1U)) != 0 ||
        page_arena_size < UINT64_C(64) * 1024U ||
        (page_arena_size & (AROS_APPLE_MEMORY_PLAN_PAGE_SIZE - 1U)) != 0 ||
        page_arena_start > UINT64_MAX - page_arena_size ||
        page_arena_start < protected_start ||
        page_arena_start + page_arena_size > protected_end)
        return AROS_APPLE_STARTUP_CONTRACT_PAGE_ARENA;
    if (debug_uart != 0 &&
        (debug_uart != UINT32_C(3) || debug_uart_start == 0 ||
         debug_uart_end <= debug_uart_start ||
         debug_uart_end - debug_uart_start < UINT64_C(0x30)))
        return AROS_APPLE_STARTUP_CONTRACT_DEBUG_UART;
    if (payload != UINT32_C(0x1ff) || payload_size == 0 ||
        payload_digest_or == 0 || payload_slot > UINT32_MAX ||
        payload_flags > UINT32_MAX || payload_producer > UINT32_MAX ||
        (payload_flags & ~(AROS_BOOT_PAYLOAD_FALLBACK |
            AROS_BOOT_PAYLOAD_DEGRADED | AROS_BOOT_PAYLOAD_DIRECT)) != 0)
        return AROS_APPLE_STARTUP_CONTRACT_PAYLOAD_IDENTITY;
    if (payload_producer == AROS_BOOT_PRODUCER_UEFI &&
        (payload_flags & AROS_BOOT_PAYLOAD_DIRECT) == 0) {
        if (payload_generation == 0 || payload_slot > 1 ||
            (payload_flags & AROS_BOOT_PAYLOAD_DIRECT) != 0 ||
            ((payload_flags & AROS_BOOT_PAYLOAD_FALLBACK) != 0 &&
             (payload_flags & AROS_BOOT_PAYLOAD_DEGRADED) == 0))
            return AROS_APPLE_STARTUP_CONTRACT_PAYLOAD_IDENTITY;
    } else if ((payload_producer != AROS_BOOT_PRODUCER_M1N1 &&
                payload_producer != AROS_BOOT_PRODUCER_UEFI) ||
            payload_generation != 0 ||
            payload_slot != AROS_BOOT_PAYLOAD_SLOT_DIRECT ||
            payload_flags != AROS_BOOT_PAYLOAD_DIRECT) {
        return AROS_APPLE_STARTUP_CONTRACT_PAYLOAD_IDENTITY;
    }
    return AROS_APPLE_STARTUP_CONTRACT_OK;
}
