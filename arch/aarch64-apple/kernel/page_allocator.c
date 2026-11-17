/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Page-granular allocator for the Apple page arena: stage two's part
 *       and the chunks it takes from Exec as it grows.
 */

#include "page_allocator.h"
#include "page_bitmap.h"

#include <aros/kernel.h>
#include <aros/debug.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <proto/exec.h>

#define PAGE AROS_APPLE_PAGE_SIZE
#define BLOCK_PAGES ((uint32_t)(AROS_APPLE_PAGE_ARENA_BLOCK / PAGE))
/* Stage two's part: its bitmaps are static, sized for its largest size. */
#define FIRST_PAGES ((uint32_t)(AROS_APPLE_PAGE_ARENA_MAX_BYTES / PAGE))
#define SEGMENTS 128U
/* A chunk is at least 32 MiB and an eighth of the arena, so that a large
 * restricted domain grows the arena in few steps and few slots. */
#define GROW_MIN_BLOCKS 16U
/* A chunk keeps room for the records attach carves from it: one record
 * page and one table page per block for each live restricted domain. */
#define GROW_SLACK_PAGES 1024U
#define GROW_SLACK_DOMAINS 64U
/* A task that allocates grows the arena ahead of need when fewer pages are
 * free, so that exception handlers and lock holders, which cannot grow it,
 * still find pages; a chunk goes back to Exec only when this many stay
 * free without it, so that a program that loads and quits does not move
 * chunks back and forth. */
#define LOW_WATER_PAGES 2048U
#define KEEP_FREE_PAGES (GROW_MIN_BLOCKS * BLOCK_PAGES)

struct ArenaSegment {
    /* Read without the lock by AROSApplePageArenaContains: pages is set
     * last when the segment is published and cleared first when it is not
     * any more. */
    uintptr_t start;
    uint32_t pages;
    uint32_t claimed;       /* the slot is in use, published or not */
    uint32_t first_free;    /* every page below is in use */
    uint32_t meta;          /* leading pages holding the segment's records */
    uint32_t in_use;        /* pages handed out, meta excluded */
    uint32_t blocks;        /* 2 MiB blocks taken from Exec; 0 for stage two's part */
    uint32_t kept;          /* detach refused: not tried again until used again */
    uint64_t *used;
    uint64_t *internal;
    uint64_t *saved;        /* per block, for detach */
    /* What attach carved, counted when the chunk is published:
     * [0] ordinary pages, [1] page-table pages */
    uint32_t carved_allocations[2];
    uint32_t carved_pages[2];
};

static struct ArenaSegment segments[SEGMENTS];
static uint32_t segment_limit;          /* claimed slots are below */
static uint64_t first_used[FIRST_PAGES / 64U];
static uint64_t first_internal[FIRST_PAGES / 64U];
static uint32_t ready;
static uint32_t member_pages;           /* pages of published segments */
static uint32_t free_pages;             /* free pages of published segments */
static uint32_t trim_pending;
static struct AROSApplePageAllocatorStats arena_stats;
static const struct AROSApplePageArenaOps *arena_ops;
static struct AROSAppleMemoryLock allocator_lock;
struct AROSAppleMemoryLock AROSApplePageMapLock;

/* A published segment holding address, NULL when none. */
static struct ArenaSegment *segment_of(uintptr_t address)
{
    uint32_t limit = __atomic_load_n(&segment_limit, __ATOMIC_ACQUIRE);
    uint32_t index;

    for (index = 0; index < limit; ++index) {
        struct ArenaSegment *segment = &segments[index];
        uint32_t pages = __atomic_load_n(&segment->pages, __ATOMIC_ACQUIRE);

        if (pages != 0 && address >= segment->start &&
            address - segment->start < (uintptr_t)pages * PAGE)
            return segment;
    }
    return NULL;
}

static void segment_mark(struct ArenaSegment *segment, uint32_t first,
    uint32_t pages, int used, int internal)
{
    uint32_t index;

    for (index = 0; index < pages; ++index) {
        page_bitmap_set(segment->used, first + index, used);
        page_bitmap_set(segment->internal, first + index, used && internal);
    }
}

static void stats_take(uint32_t pages, int internal)
{
    if (internal) {
        ++arena_stats.internal_allocation_count;
        arena_stats.internal_pages_in_use += pages;
    } else {
        ++arena_stats.allocation_count;
        arena_stats.pages_in_use += pages;
    }
}

/*
 * Placement. Every page table, DMA buffer and restricted domain page comes
 * from the arena, with very different lifetimes: a page table lives as long
 * as the system, a domain's pages as long as the domain. First fit for all
 * of them scatters long-lived single pages between the big regions, and a
 * 2 MiB request then fails in an arena mostly free; a chunk holding one of
 * them can never go back to Exec. Page tables are therefore taken from the
 * top of a segment, everything else first fit from the bottom, and both try
 * stage two's part first, which never goes back.
 */
int AROSApplePageAllocatorInitialize(uintptr_t start, uint64_t size)
{
    volatile uint64_t *word;
    uint32_t index;

    if (start == 0 || (start & (PAGE - 1U)) != 0 ||
        size < UINT64_C(64) * 1024U || (size & (PAGE - 1U)) != 0 ||
        size / PAGE > FIRST_PAGES || start > UINTPTR_MAX - size)
        return 0;
    /* volatile: no library call before the kernel's own are bound */
    word = (volatile uint64_t *)segments;
    for (index = 0; index < (uint32_t)(sizeof(segments) / (sizeof(uint64_t))); ++index)
        word[index] = 0;
    for (index = 0; index < FIRST_PAGES / 64U; ++index) {
        ((volatile uint64_t *)first_used)[index] = 0;
        ((volatile uint64_t *)first_internal)[index] = 0;
    }
    word = (volatile uint64_t *)&arena_stats;
    for (index = 0; index < sizeof(arena_stats) / sizeof(*word); ++index)
        word[index] = 0;
    segments[0].start = start;
    segments[0].claimed = 1;
    segments[0].used = first_used;
    segments[0].internal = first_internal;
    /* Pages past the part are never free */
    for (index = (uint32_t)(size / PAGE); index < FIRST_PAGES; ++index)
        page_bitmap_set(first_used, index, 1);
    segments[0].pages = (uint32_t)(size / PAGE);
    segment_limit = 1;
    member_pages = free_pages = (uint32_t)(size / PAGE);
    trim_pending = 0;
    arena_ops = NULL;
    ready = 1;
    return 1;
}

void AROSApplePageArenaSetOps(const struct AROSApplePageArenaOps *ops)
{
    arena_ops = ops;
}

/* With the lock held: a run of pages in segment, at requested when given. */
static int segment_find(struct ArenaSegment *segment, uintptr_t requested,
    uint32_t pages, uintptr_t ceiling, int internal, uint32_t *first)
{
    uint32_t available = segment->pages;

    if (ceiling != UINTPTR_MAX) {
        uintptr_t below;

        if (segment->start >= ceiling)
            return 0;
        below = (ceiling - segment->start) / PAGE;
        if (below < available)
            available = (uint32_t)below;
    }
    if (pages > available)
        return 0;
    if (requested != 0) {
        *first = (uint32_t)((requested - segment->start) / PAGE);
        return *first <= available - pages &&
            page_bitmap_next(segment->used, *first, *first + pages, 1) ==
                *first + pages;
    }
    if (internal)
        return page_bitmap_find_down(segment->used, available, pages, first);
    return page_bitmap_find_up(segment->used, segment->first_free,
        available - pages, pages, first);
}

/* With the lock held */
static void segment_take(struct ArenaSegment *segment, uint32_t first,
    uint32_t pages, int internal)
{
    segment_mark(segment, first, pages, 1, internal);
    segment->kept = 0;
    if (first <= segment->first_free && segment->first_free < first + pages)
        segment->first_free = page_bitmap_next(segment->used, first + pages,
            segment->pages, 0);
    segment->in_use += pages;
    free_pages -= pages;
    stats_take(pages, internal);
}

static int grow(uint32_t pages);

static void *page_allocate(
    void *requested, uintptr_t length, uint32_t flags, int internal)
{
    struct ArenaSegment *segment = NULL;
    uintptr_t ceiling = UINTPTR_MAX;
    uintptr_t address;
    uint32_t pages, first = 0, index, attempt;
    uint64_t saved;
    int found = 0;

    if (!ready || length == 0 ||
        length > (uintptr_t)UINT32_MAX * PAGE - (PAGE - 1U))
        return NULL;
    pages = (uint32_t)((length + PAGE - 1U) / PAGE);

    /* HUNK relocations require low addresses. Never silently satisfy a
     * constrained request from the high native W^X arena. Include the whole
     * rounded allocation, not only its first byte, in the address check. */
    if ((flags & MEMF_31BIT) != 0)
        ceiling = UINT32_C(0x80000000);
    if ((flags & MEMF_24BITDMA) != 0)
        ceiling = UINT32_C(0x01000000);

    for (attempt = 0;; ++attempt) {
        uint32_t limit;

        saved = AROSAppleMemoryLockAcquire(&allocator_lock);
        limit = segment_limit;
        if (requested != NULL) {
            segment = segment_of((uintptr_t)requested);
            found = segment != NULL &&
                segment_find(segment, (uintptr_t)requested, pages, ceiling,
                    internal, &first);
        } else {
            for (index = 0; index < limit && !found; ++index) {
                segment = &segments[index];
                found = segment->pages != 0 &&
                    segment_find(segment, 0, pages, ceiling, internal, &first);
            }
        }
        if (found) {
            segment_take(segment, first, pages, internal);
            AROSAppleMemoryLockRelease(&allocator_lock, saved);
            break;
        }
        AROSAppleMemoryLockRelease(&allocator_lock, saved);
        /* Only a free run elsewhere in RAM helps: not a fixed address, not
         * an address limit (Apple RAM is above 4 GiB) */
        if (attempt == 0 && requested == NULL && ceiling == UINTPTR_MAX &&
            arena_ops != NULL && arena_ops->can_grow() && grow(pages))
            continue;
        {
            uint32_t largest = 0, used, capacity;

            /* Tell fragmentation from exhaustion: the longest free run. */
            saved = AROSAppleMemoryLockAcquire(&allocator_lock);
            for (index = 0; index < segment_limit; ++index) {
                uint32_t run;

                if (segments[index].pages == 0)
                    continue;
                run = page_bitmap_longest_free(segments[index].used,
                    segments[index].pages);
                if (run > largest)
                    largest = run;
            }
            used = member_pages - free_pages;
            capacity = member_pages;
            AROSAppleMemoryLockRelease(&allocator_lock, saved);
            bug("[MM] page arena allocation unavailable: requested=%lu%s used=%lu capacity=%lu largest_free_run=%lu pages\n",
                (unsigned long)pages, requested != NULL ? " at a fixed address" : "",
                (unsigned long)used, (unsigned long)capacity, (unsigned long)largest);
        }
        return NULL;
    }

    address = segment->start + (uintptr_t)first * PAGE;
    if ((flags & MEMF_CLEAR) != 0) {
        volatile uint64_t *word = (volatile uint64_t *)address;

        for (index = 0; index < (pages * PAGE) / sizeof(*word); ++index)
            word[index] = 0;
    }
    /* Ahead of need, where it is allowed (see LOW_WATER_PAGES) */
    if (__atomic_load_n(&free_pages, __ATOMIC_RELAXED) < LOW_WATER_PAGES &&
        arena_ops != NULL && arena_ops->can_grow())
        (void)grow(0);
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
    struct ArenaSegment *segment;
    uint32_t first, pages, index;
    uint64_t saved;
    int internal;

    if (!ready || (value & (PAGE - 1U)) != 0 || length == 0 ||
        length > (uintptr_t)UINT32_MAX * PAGE - (PAGE - 1U))
        return 0;
    pages = (uint32_t)((length + PAGE - 1U) / PAGE);

    saved = AROSAppleMemoryLockAcquire(&allocator_lock);
    segment = segment_of(value);
    if (segment == NULL) {
        AROSAppleMemoryLockRelease(&allocator_lock, saved);
        return 0;
    }
    first = (uint32_t)((value - segment->start) / PAGE);
    if (first < segment->meta || pages > segment->pages - first) {
        AROSAppleMemoryLockRelease(&allocator_lock, saved);
        return 0;
    }
    internal = page_bitmap_test(segment->internal, first);
    for (index = 0; index < pages; ++index) {
        if (!page_bitmap_test(segment->used, first + index) ||
            page_bitmap_test(segment->internal, first + index) != internal) {
            AROSAppleMemoryLockRelease(&allocator_lock, saved);
            return 0;
        }
    }
    segment_mark(segment, first, pages, 0, 0);
    if (first < segment->first_free)
        segment->first_free = first;
    segment->in_use -= pages;
    free_pages += pages;
    if (internal) {
        ++arena_stats.internal_free_count;
        arena_stats.internal_pages_in_use -= pages;
    } else {
        ++arena_stats.free_count;
        arena_stats.pages_in_use -= pages;
    }
    if (segment->blocks != 0 && segment->in_use == 0)
        trim_pending = 1;
    AROSAppleMemoryLockRelease(&allocator_lock, saved);
    return 1;
}

uint32_t AROSApplePageArenaPages(void)
{
    return ready ? __atomic_load_n(&member_pages, __ATOMIC_RELAXED) : 0;
}

int AROSApplePageArenaContains(uintptr_t address)
{
    return ready && segment_of(address) != NULL;
}

void AROSApplePageAllocatorGetStats(
    struct AROSApplePageAllocatorStats *stats)
{
    if (stats == NULL)
        return;
    uint64_t saved=AROSAppleMemoryLockAcquire(&allocator_lock);
    stats->allocation_count = arena_stats.allocation_count;
    stats->free_count = arena_stats.free_count;
    stats->pages_in_use = arena_stats.pages_in_use;
    stats->rx_protection_count = arena_stats.rx_protection_count;
    stats->rw_protection_count = arena_stats.rw_protection_count;
    stats->internal_allocation_count = arena_stats.internal_allocation_count;
    stats->internal_free_count = arena_stats.internal_free_count;
    stats->internal_pages_in_use = arena_stats.internal_pages_in_use;
    AROSAppleMemoryLockRelease(&allocator_lock,saved);
}

void AROSApplePageAllocatorRecordProtection(uint32_t flags)
{
    uint64_t saved=AROSAppleMemoryLockAcquire(&allocator_lock);
    if ((flags & MAP_Executable) != 0 && (flags & MAP_Writable) == 0)
        ++arena_stats.rx_protection_count;
    if ((flags & MAP_Writable) != 0 && (flags & MAP_Executable) == 0)
        ++arena_stats.rw_protection_count;
    AROSAppleMemoryLockRelease(&allocator_lock,saved);
}

/* Growth ---------------------------------------------------------------- */

/* Pages at the start of a chunk of blocks for its bitmaps and saved
 * descriptors, then one level-3 table per block. */
static uint32_t chunk_records(uint32_t blocks)
{
    uint64_t words = (uint64_t)blocks * BLOCK_PAGES / 64U;
    uint64_t bytes = (2U * words + blocks) * sizeof(uint64_t);

    return (uint32_t)((bytes + PAGE - 1U) / PAGE);
}

/* Take a chunk from Exec for a request of pages (0: ahead of need) and make
 * it arena. 1 when the arena grew. */
static int grow(uint32_t pages)
{
    struct AROSApplePageArenaGrowth growth;
    struct ArenaSegment *segment = NULL;
    uint32_t blocks, records, meta, slot, total, index;
    uint64_t saved;
    uintptr_t start;
    int attached;

    total = __atomic_load_n(&member_pages, __ATOMIC_RELAXED);
    blocks = GROW_MIN_BLOCKS;
    if (total / 8U / BLOCK_PAGES > blocks)
        blocks = total / 8U / BLOCK_PAGES;
    for (;;) {
        uint64_t need = (uint64_t)pages + chunk_records(blocks) + blocks +
            GROW_SLACK_PAGES + (uint64_t)GROW_SLACK_DOMAINS * (blocks + 1U);

        if ((uint64_t)blocks * BLOCK_PAGES >= need)
            break;
        if (blocks > UINT32_MAX / BLOCK_PAGES / 2U)
            return 0;
        blocks *= 2U;
    }
    if ((uint64_t)total + (uint64_t)blocks * BLOCK_PAGES > UINT32_MAX)
        return 0;

    /* A slot first: without one the chunk could not be published */
    saved = AROSAppleMemoryLockAcquire(&allocator_lock);
    for (slot = 1; slot < SEGMENTS; ++slot)
        if (!segments[slot].claimed) {
            segment = &segments[slot];
            segment->claimed = 1;
            break;
        }
    AROSAppleMemoryLockRelease(&allocator_lock, saved);
    if (segment == NULL)
        return 0;

    start = arena_ops->take((uint64_t)blocks * AROS_APPLE_PAGE_ARENA_BLOCK);
    if (start == 0 || (start & (AROS_APPLE_PAGE_ARENA_BLOCK - 1U)) != 0) {
        if (start != 0)
            arena_ops->give(start, (uint64_t)blocks * AROS_APPLE_PAGE_ARENA_BLOCK);
        saved = AROSAppleMemoryLockAcquire(&allocator_lock);
        segment->claimed = 0;
        AROSAppleMemoryLockRelease(&allocator_lock, saved);
        return 0;
    }

    /* The chunk's records, in the chunk, before anyone can see it */
    records = chunk_records(blocks);
    meta = records + blocks;
    segment->start = start;
    segment->blocks = blocks;
    segment->meta = meta;
    segment->in_use = 0;
    segment->kept = 0;
    segment->used = (uint64_t *)start;
    segment->internal = segment->used + (uint64_t)blocks * BLOCK_PAGES / 64U;
    segment->saved = segment->internal + (uint64_t)blocks * BLOCK_PAGES / 64U;
    for (index = 0; index < (uint64_t)blocks * BLOCK_PAGES / 64U; ++index) {
        segment->used[index] = 0;
        segment->internal[index] = 0;
    }
    for (index = 0; index < blocks; ++index)
        segment->saved[index] = 0;
    segment_mark(segment, 0, meta, 1, 1);
    segment->first_free = meta;
    segment->carved_allocations[0] = segment->carved_allocations[1] = 0;
    segment->carved_pages[0] = segment->carved_pages[1] = 0;

    for (;;) {
        growth.start = start;
        growth.blocks = blocks;
        growth.member_pages = total + blocks * BLOCK_PAGES;
        growth.tables = (uint64_t *)(start + (uintptr_t)records * PAGE);
        growth.saved = segment->saved;
        growth.index = NULL;
        growth.index_slots = 0;
        growth.old_index = NULL;
        growth.old_index_slots = 0;
        growth.segment = segment;
        attached = arena_ops->attach(&growth);
        if (attached >= 0)
            break;
        /* Restore the pages attach carved, then ask again */
        segment_mark(segment, meta, blocks * BLOCK_PAGES - meta, 0, 0);
        segment->in_use = 0;
        segment->first_free = meta;
        segment->carved_allocations[0] = segment->carved_allocations[1] = 0;
        segment->carved_pages[0] = segment->carved_pages[1] = 0;
        total = __atomic_load_n(&member_pages, __ATOMIC_RELAXED);
    }
    if (!attached) {
        arena_ops->give(start, (uint64_t)blocks * AROS_APPLE_PAGE_ARENA_BLOCK);
        saved = AROSAppleMemoryLockAcquire(&allocator_lock);
        segment->blocks = 0;
        segment->claimed = 0;
        AROSAppleMemoryLockRelease(&allocator_lock, saved);
        return 0;
    }
    bug("[MM] page arena grew by %lu MiB at 0x%lx: %lu MiB\n",
        (unsigned long)(blocks * 2U), (unsigned long)start,
        (unsigned long)(AROSApplePageArenaPages() / 256U));
    return 1;
}

void *AROSApplePageArenaCarve(struct AROSApplePageArenaGrowth *growth,
    uint32_t pages, int internal)
{
    struct ArenaSegment *segment = growth->segment;
    uint32_t first;

    if (pages == 0 ||
        !page_bitmap_find_down(segment->used, segment->blocks * BLOCK_PAGES,
            pages, &first) || first < segment->meta)
        return NULL;
    segment_mark(segment, first, pages, 1, internal);
    segment->in_use += pages;
    ++segment->carved_allocations[internal != 0];
    segment->carved_pages[internal != 0] += pages;
    return (void *)(segment->start + (uintptr_t)first * PAGE);
}

void AROSApplePageArenaPublish(struct AROSApplePageArenaGrowth *growth)
{
    struct ArenaSegment *segment = growth->segment;
    uint32_t pages = segment->blocks * BLOCK_PAGES;
    uint32_t slot = (uint32_t)(segment - segments), page;
    uint64_t saved = AROSAppleMemoryLockAcquire(&allocator_lock);

    /* What attach carved counts as allocations */
    for (page = 0; page < 2U; ++page) {
        if (segment->carved_allocations[page] == 0)
            continue;
        stats_take(segment->carved_pages[page], (int)page);
        if (page != 0) {
            arena_stats.internal_allocation_count +=
                segment->carved_allocations[page] - 1U;
        } else {
            arena_stats.allocation_count +=
                segment->carved_allocations[page] - 1U;
        }
    }
    member_pages += pages;
    free_pages += pages - segment->meta - segment->in_use;
    if (slot + 1U > segment_limit)
        __atomic_store_n(&segment_limit, slot + 1U, __ATOMIC_RELEASE);
    __atomic_store_n(&segment->pages, pages, __ATOMIC_RELEASE);
    AROSAppleMemoryLockRelease(&allocator_lock, saved);
}

void AROSApplePageArenaTrim(void)
{
    struct ArenaSegment *segment = NULL;
    uint32_t index;
    uint64_t saved;

    if (!__atomic_load_n(&trim_pending, __ATOMIC_RELAXED) || arena_ops == NULL ||
        !arena_ops->can_grow())
        return;
    saved = AROSAppleMemoryLockAcquire(&allocator_lock);
    trim_pending = 0;
    for (index = 1; index < segment_limit; ++index) {
        struct ArenaSegment *candidate = &segments[index];
        uint32_t usable = candidate->pages - candidate->meta;

        if (candidate->pages == 0 || candidate->blocks == 0 ||
            candidate->in_use != 0 || candidate->kept)
            continue;
        if (free_pages - usable < KEEP_FREE_PAGES) {
            /* Kept for now; a later free looks again */
            continue;
        }
        segment = candidate;
        break;
    }
    if (segment != NULL) {
        /* Nothing in it is allocated: no owner can be testing it */
        __atomic_store_n(&segment->pages, 0, __ATOMIC_RELEASE);
        member_pages -= segment->blocks * BLOCK_PAGES;
        free_pages -= segment->blocks * BLOCK_PAGES - segment->meta;
    }
    AROSAppleMemoryLockRelease(&allocator_lock, saved);
    if (segment == NULL)
        return;

    if (arena_ops->detach(segment->start, segment->blocks, segment->saved)) {
        arena_ops->give(segment->start,
            (uint64_t)segment->blocks * AROS_APPLE_PAGE_ARENA_BLOCK);
        bug("[MM] page arena gave %lu MiB at 0x%lx back: %lu MiB\n",
            (unsigned long)(segment->blocks * 2U), (unsigned long)segment->start,
            (unsigned long)(AROSApplePageArenaPages() / 256U));
        saved = AROSAppleMemoryLockAcquire(&allocator_lock);
        segment->blocks = 0;
        segment->claimed = 0;
        AROSAppleMemoryLockRelease(&allocator_lock, saved);
        return;
    }
    /* Its blocks could not be restored: it stays arena, and another chunk
     * may go back instead */
    saved = AROSAppleMemoryLockAcquire(&allocator_lock);
    segment->kept = 1;
    trim_pending = 1;
    member_pages += segment->blocks * BLOCK_PAGES;
    free_pages += segment->blocks * BLOCK_PAGES - segment->meta;
    __atomic_store_n(&segment->pages, segment->blocks * BLOCK_PAGES,
        __ATOMIC_RELEASE);
    AROSAppleMemoryLockRelease(&allocator_lock, saved);
}
