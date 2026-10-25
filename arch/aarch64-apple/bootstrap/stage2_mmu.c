/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Build a bounded identity map for validated RAM and enable EL1 stage 1.
 */

#include "stage2_mmu.h"

#include <aros/apple/startup.h>

#define STAGE2_MMU_PAGE_SIZE       UINT64_C(4096)
#define STAGE2_MMU_L2_BLOCK_SIZE   (UINT64_C(2) * 1024U * 1024U)
#define STAGE2_MMU_L1_BLOCK_SIZE   (UINT64_C(1024) * 1024U * 1024U)
#define STAGE2_MMU_ENTRIES         512U
#define STAGE2_MMU_ADDRESS_MASK    UINT64_C(0x0000fffffffff000)
#define STAGE2_MMU_ADDRESS_LIMIT   (UINT64_C(1) << 48)
#define STAGE2_MMU_VALID           UINT64_C(1)
#define STAGE2_MMU_BLOCK           UINT64_C(1)
#define STAGE2_MMU_TABLE           UINT64_C(3)
#define STAGE2_MMU_PAGE            UINT64_C(3)
#define STAGE2_MMU_ATTR_NORMAL     (UINT64_C(1) << 2)
#define STAGE2_MMU_AP_EL0_RW       (UINT64_C(1) << 6)
#define STAGE2_MMU_AP_READ_ONLY    (UINT64_C(1) << 7)
#define STAGE2_MMU_INNER_SHAREABLE (UINT64_C(3) << 8)
#define STAGE2_MMU_ACCESS_FLAG     (UINT64_C(1) << 10)
#define STAGE2_MMU_PXN             (UINT64_C(1) << 53)
#define STAGE2_MMU_UXN             (UINT64_C(1) << 54)
#define STAGE2_MMU_NORMAL          \
    (STAGE2_MMU_ATTR_NORMAL | STAGE2_MMU_INNER_SHAREABLE | \
    STAGE2_MMU_ACCESS_FLAG)
#define STAGE2_MMU_RW_NX           \
    (STAGE2_MMU_NORMAL | STAGE2_MMU_AP_EL0_RW | STAGE2_MMU_PXN | \
    STAGE2_MMU_UXN)
#define STAGE2_MMU_RO_X            \
    (STAGE2_MMU_NORMAL | STAGE2_MMU_AP_EL0_RW | \
    STAGE2_MMU_AP_READ_ONLY)

struct Stage2MMUBuilder {
    uint64_t cursor;
    uint64_t arena_end;
    uint64_t page_arena_start;
    uint64_t page_arena_end;
    uint64_t root;
    uint64_t mapped_bytes;
    uint32_t table_count;
};

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

static const struct AROSAppleMemoryPlanRange *range_at(
    const struct AROSAppleMemoryPlan *plan, uint32_t index)
{
    return (const struct AROSAppleMemoryPlanRange *)(
        (const uint8_t *)plan + sizeof(*plan) +
        (uint64_t)index * plan->entry_size);
}

static int ranges_overlap(
    uint64_t first_start, uint64_t first_end,
    uint64_t second_start, uint64_t second_end)
{
    return first_start < second_end && second_start < first_end;
}

static int framebuffer_range(
    const struct AROSAppleStage2Result *result,
    uint64_t *start, uint64_t *end)
{
    const struct AROSAppleTagItem *tags;
    uint64_t physical = 0;
    uint64_t size = 0;
    uint64_t raw_end;
    uint32_t count;
    uint32_t index;
    int terminated = 0;

    *start = 0;
    *end = 0;
    if (result->boot_tags == 0)
        return 0;
    count = result->boot_tag_count;
    if (count == 0 || count > AROS_APPLE_MAX_BOOT_TAGS)
        return -1;
    tags = (const struct AROSAppleTagItem *)(uintptr_t)result->boot_tags;
    for (index = 0; index < count; ++index) {
        if (tags[index].tag == AROS_APPLE_TAG_DONE) {
            terminated = 1;
            break;
        }
        if (tags[index].tag == AROS_APPLE_KRN_FB_ADDR)
            physical = tags[index].data;
        else if (tags[index].tag == AROS_APPLE_KRN_FRAMEBUFFER_SIZE)
            size = tags[index].data;
    }
    if (!terminated)
        return -1;
    if (physical == 0 && size == 0)
        return 0;
    if (physical == 0 || size == 0 ||
        !add_u64(physical, size, &raw_end) ||
        raw_end > STAGE2_MMU_ADDRESS_LIMIT)
        return -1;
    *start = physical & ~(STAGE2_MMU_PAGE_SIZE - 1U);
    if (!align_up(raw_end, STAGE2_MMU_PAGE_SIZE, end) || *end <= *start)
        return -1;
    return 1;
}

static uint64_t *allocate_table(struct Stage2MMUBuilder *builder)
{
    uint64_t *table;
    uint64_t end;
    uint32_t index;

    if (!align_up(builder->cursor, STAGE2_MMU_PAGE_SIZE,
            &builder->cursor) ||
        builder->cursor > UINT64_MAX - STAGE2_MMU_PAGE_SIZE)
        return NULL;
    end = builder->cursor + STAGE2_MMU_PAGE_SIZE;
    if (end > builder->arena_end)
        return NULL;
    table = (uint64_t *)(uintptr_t)builder->cursor;
    for (index = 0; index < STAGE2_MMU_ENTRIES; ++index)
        table[index] = 0;
    builder->cursor = end;
    ++builder->table_count;
    return table;
}

static uint64_t *table_from_entry(uint64_t entry)
{
    if ((entry & UINT64_C(3)) != STAGE2_MMU_TABLE)
        return NULL;
    return (uint64_t *)(uintptr_t)(entry & STAGE2_MMU_ADDRESS_MASK);
}

static int observed_table_valid(
    const struct AROSAppleStage2Result *result, uint64_t address)
{
    return address >= result->translation_table &&
        address < result->arena_used_end &&
        address <= result->arena_used_end - STAGE2_MMU_PAGE_SIZE &&
        (address & (STAGE2_MMU_PAGE_SIZE - 1U)) == 0;
}

static uint64_t *child_table(
    struct Stage2MMUBuilder *builder, uint64_t *entry)
{
    uint64_t *table;

    if ((*entry & STAGE2_MMU_VALID) != 0)
        return table_from_entry(*entry);
    table = allocate_table(builder);
    if (table == NULL)
        return NULL;
    *entry = ((uint64_t)(uintptr_t)table & STAGE2_MMU_ADDRESS_MASK) |
        STAGE2_MMU_TABLE;
    return table;
}

/*
 * Normal memory is mapped with 2 MiB blocks at most. The kernel never splits
 * a block of normal memory while it runs (the block may hold the code, stack
 * or table doing the break-before-make), but it may give a whole 2 MiB block
 * to the code arena or take it back by rewriting that one L2 entry, which a
 * 1 GiB block would forbid. Device memory keeps 1 GiB blocks.
 */
static int level1_block_allowed(uint64_t attributes)
{
    /* MAIR index 0 is Device-nGnRnE, 3 Device-nGnRE (stage2_mmu.h) */
    uint64_t index = (attributes >> 2) & 7U;

    return index == 0 || index == 3;
}

static int map_range(
    struct Stage2MMUBuilder *builder, uint64_t start, uint64_t end,
    uint64_t attributes)
{
    uint64_t *level0 = (uint64_t *)(uintptr_t)builder->root;
    uint64_t *level1;
    uint64_t *level2;
    uint64_t *level3;
    uint64_t remaining;
    uint64_t size;
    uint64_t descriptor;
    uint32_t index0;
    uint32_t index1;
    uint32_t index2;
    uint32_t index3;

    while (start < end) {
        remaining = end - start;
        index0 = (uint32_t)((start >> 39) & UINT64_C(0x1ff));
        index1 = (uint32_t)((start >> 30) & UINT64_C(0x1ff));
        index2 = (uint32_t)((start >> 21) & UINT64_C(0x1ff));
        index3 = (uint32_t)((start >> 12) & UINT64_C(0x1ff));
        level1 = child_table(builder, &level0[index0]);
        if (level1 == NULL)
            return 0;

        if (level1_block_allowed(attributes) &&
            (start & (STAGE2_MMU_L1_BLOCK_SIZE - 1U)) == 0 &&
            remaining >= STAGE2_MMU_L1_BLOCK_SIZE &&
            !ranges_overlap(start, start + STAGE2_MMU_L1_BLOCK_SIZE,
                builder->page_arena_start, builder->page_arena_end) &&
            (level1[index1] & STAGE2_MMU_VALID) == 0) {
            size = STAGE2_MMU_L1_BLOCK_SIZE;
            descriptor = STAGE2_MMU_BLOCK;
        } else {
            level2 = child_table(builder, &level1[index1]);
            if (level2 == NULL)
                return 0;
            if ((start & (STAGE2_MMU_L2_BLOCK_SIZE - 1U)) == 0 &&
                remaining >= STAGE2_MMU_L2_BLOCK_SIZE &&
                !ranges_overlap(start, start + STAGE2_MMU_L2_BLOCK_SIZE,
                    builder->page_arena_start, builder->page_arena_end) &&
                (level2[index2] & STAGE2_MMU_VALID) == 0) {
                size = STAGE2_MMU_L2_BLOCK_SIZE;
                descriptor = STAGE2_MMU_BLOCK;
                level1 = level2;
                index1 = index2;
            } else {
                level3 = child_table(builder, &level2[index2]);
                if (level3 == NULL ||
                    (level3[index3] & STAGE2_MMU_VALID) != 0)
                    return 0;
                size = STAGE2_MMU_PAGE_SIZE;
                descriptor = STAGE2_MMU_PAGE;
                level1 = level3;
                index1 = index3;
            }
        }
        level1[index1] = (start & STAGE2_MMU_ADDRESS_MASK) | descriptor |
            attributes;
        if (builder->mapped_bytes > UINT64_MAX - size)
            return 0;
        builder->mapped_bytes += size;
        start += size;
    }
    return 1;
}

/* Maps one device range, giving the parts inside PCI aperture roles the
 * posted Device type and everything else the non-posted one. */
static int map_device_range(struct Stage2MMUBuilder *builder,
    const struct AROSAppleDeviceMap *map, uint64_t start, uint64_t end)
{
    while (start < end) {
        uint64_t next = end, window_end = 0;
        for (uint32_t i = 0; i < map->role_count; ++i) {
            const struct AROSAppleDeviceRole *role = &map->roles[i];
            if (role->kind != AROS_APPLE_DEVICE_ROLE_PCI_WINDOW) continue;
            if (role->start <= start && start < role->end) {
                window_end = role->end < end ? role->end : end;
                break;
            }
            if (role->start > start && role->start < next) next = role->start;
        }
        if (window_end) {
            if (!map_range(builder, start, window_end,
                    AROS_APPLE_STAGE2_POSTED_DEVICE_ATTRIBUTES)) return 0;
            start = window_end;
        } else {
            if (!map_range(builder, start, next, AROS_APPLE_STAGE2_DEVICE_ATTRIBUTES))
                return 0;
            start = next;
        }
    }
    return 1;
}

static int map_plan_range(
    struct Stage2MMUBuilder *builder, uint64_t start, uint64_t end,
    const uint64_t executable_start[2], const uint64_t executable_end[2],
    uint64_t *executable_bytes)
{
    uint64_t overlap_start;
    uint64_t overlap_end;
    uint64_t cursor = start;
    uint32_t index;

    for (index = 0; index < 2; ++index) {
        if (executable_end[index] <= cursor ||
            executable_start[index] >= end)
            continue;
        overlap_start = executable_start[index] > cursor ?
            executable_start[index] : cursor;
        overlap_end = executable_end[index] < end ?
            executable_end[index] : end;
        if (overlap_start > cursor &&
            !map_range(builder, cursor, overlap_start, STAGE2_MMU_RW_NX))
            return 0;
        if (!map_range(builder, overlap_start, overlap_end,
                STAGE2_MMU_RO_X) ||
            *executable_bytes > UINT64_MAX -
                (overlap_end - overlap_start))
            return 0;
        *executable_bytes += overlap_end - overlap_start;
        cursor = overlap_end;
    }
    return cursor == end || map_range(builder, cursor, end, STAGE2_MMU_RW_NX);
}

static int enable_translation(uint64_t root)
{
#if defined(__aarch64__)
    uint64_t mmfr0;
    uint64_t physical_size;
    uint64_t tcr;
    uint64_t sctlr;
    uint64_t observed;

    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
    physical_size = mmfr0 & UINT64_C(0xf);
    if (physical_size > 5U)
        physical_size = 5U;
    tcr = (physical_size << 32) | (UINT64_C(1) << 23) |
        (UINT64_C(3) << 12) | (UINT64_C(1) << 10) |
        (UINT64_C(1) << 8) | UINT64_C(16);

    __asm__ volatile(
        "dsb sy\n"
        "msr mair_el1, %0\n"
        "msr tcr_el1, %1\n"
        "msr ttbr0_el1, %2\n"
        "isb\n"
        "tlbi vmalle1\n"
        "dsb sy\n"
        "isb\n"
        "ic iallu\n"
        "dsb sy\n"
        "isb"
        :: "r"(AROS_APPLE_STAGE2_MAIR_VALUE), "r"(tcr), "r"(root) : "memory");
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (UINT64_C(1) << 0) | (UINT64_C(1) << 2) |
        (UINT64_C(1) << 12);
    sctlr &= ~(UINT64_C(1) << 1);
    __asm__ volatile("msr sctlr_el1, %0; isb" :: "r"(sctlr) : "memory");
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(observed));
    return (observed & (UINT64_C(1) | (UINT64_C(1) << 2) |
        (UINT64_C(1) << 12))) ==
        (UINT64_C(1) | (UINT64_C(1) << 2) | (UINT64_C(1) << 12)) &&
        (observed & (UINT64_C(1) << 1)) == 0;
#else
    (void)root;
    return 0;
#endif
}

enum AROSAppleStage2Status AROSAppleStage2BuildIdentityMap(
    struct AROSAppleStage2Result *result,
    uintptr_t stage2_image_start,
    uintptr_t stage2_writable_start)
{
    const struct AROSAppleMemoryPlan *plan;
    const struct AROSAppleMemoryPlanRange *range;
    struct Stage2MMUBuilder builder;
    uint64_t executable_start[2];
    uint64_t executable_end[2];
    uint64_t executable_bytes = 0;
    uint64_t expected_executable_bytes;
    uint64_t expected_mapped_bytes;
    uint64_t framebuffer_start;
    uint64_t framebuffer_end;
    uint64_t framebuffer_bytes = 0;
    uint64_t total_bytes;
    int have_framebuffer;
    uint32_t index;

    if (result == NULL || result->memory_plan == 0 ||
        result->arena_used_end == 0 ||
        result->arena_used_end > result->arena_end ||
        stage2_image_start == 0 ||
        stage2_writable_start <= stage2_image_start ||
        (stage2_image_start & (STAGE2_MMU_PAGE_SIZE - 1U)) != 0 ||
        (stage2_writable_start & (STAGE2_MMU_PAGE_SIZE - 1U)) != 0 ||
        (result->readonly_start & (STAGE2_MMU_PAGE_SIZE - 1U)) != 0 ||
        (result->writable_start & (STAGE2_MMU_PAGE_SIZE - 1U)) != 0 ||
        result->writable_start <= result->readonly_start)
        return AROS_APPLE_STAGE2_INVALID_ARGUMENT;
    plan = (const struct AROSAppleMemoryPlan *)result->memory_plan;
    if (!AROSAppleMemoryPlanValidate(plan))
        return AROS_APPLE_STAGE2_INVALID_MEMORY_PLAN;
    if (!AROSAppleStage2DeviceMapValidate(&result->device_map, plan))
        return AROS_APPLE_STAGE2_INVALID_DEVICE_MAP;
    have_framebuffer = framebuffer_range(result,
        &framebuffer_start, &framebuffer_end);
    if (have_framebuffer < 0)
        return AROS_APPLE_STAGE2_INVALID_MEMORY_PLAN;
    if (have_framebuffer) {
        for (index = 0; index < plan->entry_count; ++index) {
            range = range_at(plan, index);
            if (ranges_overlap(range->start, range->end,
                    framebuffer_start, framebuffer_end))
                return AROS_APPLE_STAGE2_INVALID_MEMORY_PLAN;
        }
        for (index = 0; index < result->device_map.entry_count; ++index) {
            if (ranges_overlap(framebuffer_start, framebuffer_end,
                    result->device_map.ranges[index].start,
                    result->device_map.ranges[index].end))
                return AROS_APPLE_STAGE2_INVALID_DEVICE_MAP;
        }
    }
    total_bytes = plan->conventional_bytes;
    if (total_bytes > UINT64_MAX - plan->release_bytes)
        return AROS_APPLE_STAGE2_INVALID_MEMORY_PLAN;
    total_bytes += plan->release_bytes;
    if (total_bytes > UINT64_MAX - plan->retained_bytes)
        return AROS_APPLE_STAGE2_INVALID_MEMORY_PLAN;
    total_bytes += plan->retained_bytes;
    executable_start[0] = stage2_image_start;
    executable_end[0] = stage2_writable_start;
    executable_start[1] = result->readonly_start;
    executable_end[1] = result->writable_start;
    if (executable_end[0] > executable_start[1] ||
        executable_end[1] <= executable_start[1])
        return AROS_APPLE_STAGE2_INVALID_MEMORY_PLAN;
    expected_executable_bytes = executable_end[0] - executable_start[0];
    if (expected_executable_bytes > UINT64_MAX -
            (executable_end[1] - executable_start[1]))
        return AROS_APPLE_STAGE2_INVALID_MEMORY_PLAN;
    expected_executable_bytes += executable_end[1] - executable_start[1];

    builder.cursor = result->arena_used_end;
    builder.arena_end = result->arena_end;
    /* Install mutable mappings before enabling translation.  A runtime
     * block split uses break-before-make; the block being broken may also
     * contain the bootstrap table slot that must be written to remake it. */
    builder.page_arena_start = result->page_arena_start;
    if ((result->page_arena_start & (STAGE2_MMU_PAGE_SIZE - 1U)) != 0 ||
        (result->page_arena_size & (STAGE2_MMU_PAGE_SIZE - 1U)) != 0 ||
        (result->page_arena_start == 0) != (result->page_arena_size == 0) ||
        !add_u64(result->page_arena_start, result->page_arena_size,
            &builder.page_arena_end) ||
        builder.page_arena_end > result->arena_used_end)
        return AROS_APPLE_STAGE2_INVALID_ARGUMENT;
    builder.mapped_bytes = 0;
    builder.table_count = 0;
    builder.root = (uintptr_t)allocate_table(&builder);
    if (builder.root == 0)
        return AROS_APPLE_STAGE2_ARENA_EXHAUSTED;
    for (index = 0; index < plan->entry_count; ++index) {
        range = range_at(plan, index);
        if (range->end > STAGE2_MMU_ADDRESS_LIMIT)
            return AROS_APPLE_STAGE2_INVALID_MEMORY_PLAN;
        if (!map_plan_range(&builder, range->start, range->end,
                executable_start, executable_end, &executable_bytes))
            return AROS_APPLE_STAGE2_ARENA_EXHAUSTED;
    }
    if (builder.mapped_bytes != total_bytes ||
        executable_bytes != expected_executable_bytes)
        return AROS_APPLE_STAGE2_INVALID_MEMORY_PLAN;
    result->translation_normal_bytes = builder.mapped_bytes;
    if (have_framebuffer) {
        if (!map_range(&builder, framebuffer_start, framebuffer_end,
                AROS_APPLE_STAGE2_FRAMEBUFFER_ATTRIBUTES))
            return AROS_APPLE_STAGE2_ARENA_EXHAUSTED;
        framebuffer_bytes = framebuffer_end - framebuffer_start;
    }
    for (index = 0; index < result->device_map.entry_count; ++index) {
        if (!map_device_range(&builder, &result->device_map,
                result->device_map.ranges[index].start,
                result->device_map.ranges[index].end))
            return AROS_APPLE_STAGE2_ARENA_EXHAUSTED;
    }
    if (!add_u64(total_bytes, framebuffer_bytes, &expected_mapped_bytes) ||
        !add_u64(expected_mapped_bytes, result->device_map.mapped_bytes,
            &expected_mapped_bytes) ||
        builder.mapped_bytes != expected_mapped_bytes)
        return AROS_APPLE_STAGE2_INVALID_DEVICE_MAP;

    result->translation_table = (uintptr_t)builder.root;
    result->translation_table_count = builder.table_count;
    result->translation_mapped_bytes = builder.mapped_bytes;
    result->translation_device_bytes = result->device_map.mapped_bytes;
    result->translation_framebuffer_bytes = framebuffer_bytes;
    result->arena_used_end = (uintptr_t)builder.cursor;
    return AROS_APPLE_STAGE2_OK;
}

enum AROSAppleStage2Status AROSAppleStage2EnableIdentityMMU(
    struct AROSAppleStage2Result *result,
    uintptr_t stage2_image_start,
    uintptr_t stage2_writable_start)
{
    enum AROSAppleStage2Status status;

    status = AROSAppleStage2BuildIdentityMap(result, stage2_image_start,
        stage2_writable_start);
    if (status != AROS_APPLE_STAGE2_OK)
        return status;
    if (!enable_translation(result->translation_table))
        return AROS_APPLE_STAGE2_MMU;
    return AROS_APPLE_STAGE2_OK;
}

int AROSAppleStage2InspectTranslation(
    const struct AROSAppleStage2Result *result,
    uint64_t virtual_address,
    struct AROSAppleStage2Translation *translation)
{
    static const uint32_t shifts[] = {39U, 30U, 21U, 12U};
    const uint64_t *table;
    uint64_t table_address;
    uint64_t descriptor;
    uint32_t level;

    if (result == NULL || translation == NULL ||
        result->translation_table == 0 || result->translation_table_count == 0 ||
        result->arena_used_end < STAGE2_MMU_PAGE_SIZE ||
        virtual_address >= STAGE2_MMU_ADDRESS_LIMIT ||
        !observed_table_valid(result, result->translation_table))
        return 0;
    translation->descriptor = 0;
    translation->level = 0;
    translation->valid = 0;
    table_address = result->translation_table;
    for (level = 0; level < 4U; ++level) {
        if (!observed_table_valid(result, table_address))
            return 0;
        table = (const uint64_t *)(uintptr_t)table_address;
        descriptor = table[(virtual_address >> shifts[level]) &
            UINT64_C(0x1ff)];
        translation->level = level;
        if ((descriptor & STAGE2_MMU_VALID) == 0)
            return descriptor == 0;
        if (level != 3U && (descriptor & UINT64_C(3)) == STAGE2_MMU_BLOCK) {
            translation->descriptor = descriptor;
            translation->valid = 1;
            return level != 0U;
        }
        if ((descriptor & UINT64_C(3)) != STAGE2_MMU_TABLE)
            return 0;
        if (level == 3U) {
            translation->descriptor = descriptor;
            translation->valid = 1;
            return 1;
        }
        table_address = descriptor & STAGE2_MMU_ADDRESS_MASK;
    }
    return 0;
}
