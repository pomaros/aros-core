/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Page-granular allocator for the stage-two-retained Apple W^X arena.
 */

#include "page_allocator.h"
#include "page_bitmap.h"

#include <aros/kernel.h>
#include <aros/debug.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <proto/exec.h>

/* Capacity, not a reservation: the loader still supplies the actual size. */
#define APPLE_PAGE_ARENA_MAX_PAGES (AROS_APPLE_PAGE_ARENA_MAX_BYTES / AROS_APPLE_PAGE_SIZE)

struct ApplePageAllocator {
    uintptr_t start;
    uint32_t pages;
    uint32_t ready;
    uint64_t used[APPLE_PAGE_ARENA_MAX_PAGES / 64U];
    uint64_t internal[APPLE_PAGE_ARENA_MAX_PAGES / 64U];
    struct AROSApplePageAllocatorStats stats;
};

static struct ApplePageAllocator page_allocator;
static struct AROSAppleMemoryLock allocator_lock;
struct AROSAppleMemoryLock AROSApplePageMapLock;

static int page_used(uint32_t page)
{
    return page_bitmap_test(page_allocator.used, page);
}

static void set_page_used(uint32_t page, int used)
{
    page_bitmap_set(page_allocator.used, page, used);
}

static int page_internal(uint32_t page)
{
    return (page_allocator.internal[page / 64U] &
        (UINT64_C(1) << (page % 64U))) != 0;
}

static void set_page_internal(uint32_t page, int internal)
{
    uint64_t mask = UINT64_C(1) << (page % 64U);

    if (internal)
        page_allocator.internal[page / 64U] |= mask;
    else
        page_allocator.internal[page / 64U] &= ~mask;
}

/*
 * Placement. Every ELF hunk, JIT region and page table comes from this one
 * arena, with very different lifetimes: a page table lives as long as the
 * system, a command's hunks for one run, a browser's for its session. First
 * fit for all of them scatters the long-lived single pages between the big
 * regions, and a 2 MiB request then fails in an arena mostly free. Page
 * tables are therefore taken from the top of the arena, everything else
 * first fit from the bottom. Hunks stay first fit so that the hunks of one
 * segment, which LoadSeg allocates one by one, stay next to each other:
 * their branches (CALL26, +/-128 MiB) and literal loads (+/-1 MiB) reach
 * from one hunk to the next.
 */

int AROSApplePageAllocatorInitialize(uintptr_t start, uint64_t size)
{
    volatile uint64_t *internal_word;
    volatile uint64_t *stats_word;
    volatile uint64_t *used_word;
    uint32_t index;

    if (start == 0 || (start & (AROS_APPLE_PAGE_SIZE - 1U)) != 0 ||
        size < UINT64_C(64) * 1024U ||
        (size & (AROS_APPLE_PAGE_SIZE - 1U)) != 0 ||
        size / AROS_APPLE_PAGE_SIZE > APPLE_PAGE_ARENA_MAX_PAGES ||
        start > UINTPTR_MAX - size)
        return 0;
    page_allocator.start = start;
    page_allocator.pages = (uint32_t)(size / AROS_APPLE_PAGE_SIZE);
    used_word = (volatile uint64_t *)page_allocator.used;
    internal_word = (volatile uint64_t *)page_allocator.internal;
    for (index = 0;
        index < sizeof(page_allocator.used) / sizeof(page_allocator.used[0]);
        ++index) {
        used_word[index] = 0;
        internal_word[index] = 0;
    }
    stats_word = (volatile uint64_t *)&page_allocator.stats;
    for (index = 0;
        index < sizeof(page_allocator.stats) / sizeof(*stats_word); ++index)
        stats_word[index] = 0;
    page_allocator.ready = 1;
    return 1;
}

static void *page_allocate(
    void *requested, uintptr_t length, uint32_t flags, int internal)
{
    uint32_t pages;
    uint32_t first;
    uint32_t limit;
    uint32_t index;
    uint32_t available;
    int found;
    uintptr_t address;
    uintptr_t ceiling = UINTPTR_MAX;

    if (page_allocator.ready == 0 || length == 0 ||
        length > UINTPTR_MAX - (AROS_APPLE_PAGE_SIZE - 1U) ||
        length > (uintptr_t)page_allocator.pages * AROS_APPLE_PAGE_SIZE)
        return NULL;
    pages = (uint32_t)((length + AROS_APPLE_PAGE_SIZE - 1U) /
        AROS_APPLE_PAGE_SIZE);
    if (pages == 0 || pages > page_allocator.pages)
        return NULL;

    /* HUNK relocations require low addresses. Never silently satisfy a
     * constrained request from the high native W^X arena. Include the whole
     * rounded allocation, not only its first byte, in the address check. */
    if ((flags & MEMF_31BIT) != 0)
        ceiling = UINT32_C(0x80000000);
    if ((flags & MEMF_24BITDMA) != 0)
        ceiling = UINT32_C(0x01000000);
    available = page_allocator.pages;
    if (ceiling != UINTPTR_MAX) {
        if (page_allocator.start >= ceiling)
            return NULL;
        uintptr_t below = (ceiling - page_allocator.start) / AROS_APPLE_PAGE_SIZE;
        if (below < available)
            available = (uint32_t)below;
        if (pages > available)
            return NULL;
    }

    uint64_t saved=AROSAppleMemoryLockAcquire(&allocator_lock);
    first = 0;
    limit = available - pages;
    if (requested != NULL) {
        uintptr_t wanted = (uintptr_t)requested;

        if (wanted < page_allocator.start ||
            wanted >= page_allocator.start +
                (uintptr_t)page_allocator.pages * AROS_APPLE_PAGE_SIZE) {
            AROSAppleMemoryLockRelease(&allocator_lock,saved);
            return NULL;
        }
        first = (uint32_t)((wanted - page_allocator.start) /
            AROS_APPLE_PAGE_SIZE);
        if (first > limit) {
            AROSAppleMemoryLockRelease(&allocator_lock,saved);
            return NULL;
        }
        limit = first;
    }
    if (requested != NULL)
        found = page_bitmap_next(page_allocator.used, first, first + pages, 1) ==
            first + pages;
    else if (internal)
        found = page_bitmap_find_down(page_allocator.used, available, pages, &first);
    else
        found = page_bitmap_find_up(page_allocator.used, 0, limit, pages, &first);
    if (!found) {
        uint32_t used = (uint32_t)(page_allocator.stats.pages_in_use +
            page_allocator.stats.internal_pages_in_use);
        uint32_t capacity = page_allocator.pages;
        uint32_t largest = 0;

        /* Tell fragmentation from exhaustion: the longest free run. */
        largest = page_bitmap_longest_free(page_allocator.used, available);
        AROSAppleMemoryLockRelease(&allocator_lock,saved);
        bug("[MM] code page arena allocation unavailable: requested=%lu%s used=%lu capacity=%lu largest_free_run=%lu pages\n",
            (unsigned long)pages, requested != NULL ? " at a fixed address" : "",
            (unsigned long)used, (unsigned long)capacity, (unsigned long)largest);
        return NULL;
    }
    for (index = 0; index < pages; ++index) {
        set_page_used(first + index, 1);
        set_page_internal(first + index, internal);
    }
    if (internal) {
        ++page_allocator.stats.internal_allocation_count;
        page_allocator.stats.internal_pages_in_use += pages;
    } else {
        ++page_allocator.stats.allocation_count;
        page_allocator.stats.pages_in_use += pages;
    }
    AROSAppleMemoryLockRelease(&allocator_lock,saved);

    address = page_allocator.start +
        (uintptr_t)first * AROS_APPLE_PAGE_SIZE;
    if ((flags & MEMF_CLEAR) != 0) {
        volatile uint64_t *word = (volatile uint64_t *)address;

        for (index = 0;
            index < (pages * AROS_APPLE_PAGE_SIZE) / sizeof(*word); ++index)
            word[index] = 0;
    }
    return (void *)address;
}

void *AROSApplePageAllocate(void *requested, uintptr_t length, uint32_t flags)
{
    return page_allocate(requested, length, flags, 0);
}

void *AROSApplePageTableAllocate(uintptr_t length)
{
    return page_allocate(NULL, length, MEMF_CLEAR, 1);
}

int AROSApplePageFreeRaw(void *address, uintptr_t length)
{
    uintptr_t value = (uintptr_t)address;
    uint32_t first;
    uint32_t pages;
    uint32_t index;
    int internal;

    if (page_allocator.ready == 0 || value < page_allocator.start ||
        value - page_allocator.start >= (uintptr_t)page_allocator.pages * AROS_APPLE_PAGE_SIZE ||
        (value & (AROS_APPLE_PAGE_SIZE - 1U)) != 0 || length == 0 ||
        length > UINTPTR_MAX - (AROS_APPLE_PAGE_SIZE - 1U) ||
        length > (uintptr_t)page_allocator.pages * AROS_APPLE_PAGE_SIZE)
        return 0;
    pages = (uint32_t)((length + AROS_APPLE_PAGE_SIZE - 1U) /
        AROS_APPLE_PAGE_SIZE);
    first = (uint32_t)((value - page_allocator.start) /
        AROS_APPLE_PAGE_SIZE);
    if (pages == 0 || first >= page_allocator.pages ||
        pages > page_allocator.pages - first)
        return 0;

    uint64_t saved=AROSAppleMemoryLockAcquire(&allocator_lock);
    internal = page_internal(first);
    for (index = 0; index < pages; ++index) {
        if (!page_used(first + index) ||
            page_internal(first + index) != internal) {
            AROSAppleMemoryLockRelease(&allocator_lock,saved);
            return 0;
        }
    }
    for (index = 0; index < pages; ++index) {
        set_page_used(first + index, 0);
        set_page_internal(first + index, 0);
    }
    if (internal) {
        ++page_allocator.stats.internal_free_count;
        page_allocator.stats.internal_pages_in_use -= pages;
    } else {
        ++page_allocator.stats.free_count;
        page_allocator.stats.pages_in_use -= pages;
    }
    AROSAppleMemoryLockRelease(&allocator_lock,saved);
    return 1;
}

void AROSApplePageAllocatorGetStats(
    struct AROSApplePageAllocatorStats *stats)
{
    if (stats == NULL)
        return;
    uint64_t saved=AROSAppleMemoryLockAcquire(&allocator_lock);
    stats->allocation_count = page_allocator.stats.allocation_count;
    stats->free_count = page_allocator.stats.free_count;
    stats->pages_in_use = page_allocator.stats.pages_in_use;
    stats->rx_protection_count =
        page_allocator.stats.rx_protection_count;
    stats->rw_protection_count =
        page_allocator.stats.rw_protection_count;
    stats->internal_allocation_count =
        page_allocator.stats.internal_allocation_count;
    stats->internal_free_count =
        page_allocator.stats.internal_free_count;
    stats->internal_pages_in_use =
        page_allocator.stats.internal_pages_in_use;
    AROSAppleMemoryLockRelease(&allocator_lock,saved);
}

void AROSApplePageAllocatorRecordProtection(uint32_t flags)
{
    uint64_t saved=AROSAppleMemoryLockAcquire(&allocator_lock);
    if ((flags & MAP_Executable) != 0 && (flags & MAP_Writable) == 0)
        ++page_allocator.stats.rx_protection_count;
    if ((flags & MAP_Writable) != 0 && (flags & MAP_Executable) == 0)
        ++page_allocator.stats.rw_protection_count;
    AROSAppleMemoryLockRelease(&allocator_lock,saved);
}
