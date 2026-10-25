/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: The code window's virtual allocator and its pool of physical pages
 *       (integration memo 2026-10-23-GROWABLE-CODE-ARENA, steps 2 to 4).
 *
 * A request reserves a run of window pages under the lock, then backs each
 * page outside it: taking a block from Exec and mapping take their own locks
 * and may call Exec, which a spinlock with interrupts masked must not wait
 * for. A run is live once all its pages are backed. A free ends its life
 * under the lock first, so that two frees of one run cannot both unmap it,
 * then gives each page back and only then releases the reservation, so that
 * no request maps over a page being unmapped. Reservation is first fit from
 * the bottom, so that the hunks of one segment, which LoadSeg allocates one
 * by one, stay adjacent.
 *
 * The pool holds 2 MiB blocks of RAM taken from Exec and out of the identity
 * map. Pages are taken from blocks that have free ones; a block whose pages
 * are all free again goes back to Exec, except a few kept so that a program
 * that loads and quits does not move blocks back and forth.
 *
 * A reservation (MEMF_RESERVE) is a live run with no page behind it: a
 * protection change that grants access commits the pages it covers, one
 * that removes all access gives them back. V8's code range and
 * PartitionAlloc's pools reserve far more than they touch.
 */

#include "code_window.h"
#include "memory_lock.h"
#include "page_bitmap.h"

#include <aros/apple/page_arena.h>

#define WINDOW_PAGES ((uint32_t)(AROS_APPLE_CODE_WINDOW_BYTES / AROS_APPLE_PAGE_SIZE))
#define BLOCK_PAGES ((uint32_t)(AROS_APPLE_CODE_WINDOW_BLOCK / AROS_APPLE_PAGE_SIZE))
#define POOL_RESERVE_BLOCKS 4U
#define POOL_HASH 4096U

struct PoolBlock {
    struct PoolBlock *hash_next;
    struct PoolBlock *partial_next;     /* blocks with a free page */
    struct PoolBlock *partial_previous;
    uint64_t physical;
    uint64_t saved;
    uint64_t used[BLOCK_PAGES / 64U];
    uint32_t used_count;
};

static uint64_t window_reserved[WINDOW_PAGES / 64U];
static uint64_t window_live[WINDOW_PAGES / 64U];
static uint64_t window_lazy[WINDOW_PAGES / 64U];     /* pages of reservations */
static struct PoolBlock *pool_hash[POOL_HASH];
static struct PoolBlock *pool_partial;
static uint32_t pool_empty_blocks;
static struct AROSAppleCodeWindowStats window_stats;
static struct AROSAppleMemoryLock window_lock;

int AROSAppleCodeWindowContains(const void *address, uintptr_t length)
{
    uintptr_t start = (uintptr_t)address;

    return start >= AROS_APPLE_CODE_WINDOW_BASE &&
        start - AROS_APPLE_CODE_WINDOW_BASE < AROS_APPLE_CODE_WINDOW_BYTES &&
        length <= AROS_APPLE_CODE_WINDOW_BYTES -
            (start - AROS_APPLE_CODE_WINDOW_BASE);
}

static uintptr_t page_address(uint32_t page)
{
    return (uintptr_t)(AROS_APPLE_CODE_WINDOW_BASE +
        (uint64_t)page * AROS_APPLE_PAGE_SIZE);
}

static uint32_t hash_of(uint64_t physical)
{
    return (uint32_t)(physical / AROS_APPLE_CODE_WINDOW_BLOCK) % POOL_HASH;
}

/* With the lock held */
static void partial_add(struct PoolBlock *block)
{
    block->partial_previous = NULL;
    block->partial_next = pool_partial;
    if (pool_partial != NULL)
        pool_partial->partial_previous = block;
    pool_partial = block;
}

/* With the lock held */
static void partial_remove(struct PoolBlock *block)
{
    if (block->partial_previous != NULL)
        block->partial_previous->partial_next = block->partial_next;
    else
        pool_partial = block->partial_next;
    if (block->partial_next != NULL)
        block->partial_next->partial_previous = block->partial_previous;
    block->partial_next = block->partial_previous = NULL;
}

/* A physical page for the window: from a block with a free page, else from
 * a new block given up by Exec. 0 when Exec has none to give. */
static uint64_t pool_take(const struct AROSAppleCodeWindowOps *ops)
{
    for (;;) {
        uint64_t saved = AROSAppleMemoryLockAcquire(&window_lock);
        struct PoolBlock *block = pool_partial;
        uint64_t physical, kept;

        if (block != NULL) {
            uint32_t page = page_bitmap_next(block->used, 0, BLOCK_PAGES, 0);

            page_bitmap_set(block->used, page, 1);
            if (block->used_count++ == 0)
                --pool_empty_blocks;
            if (block->used_count == BLOCK_PAGES)
                partial_remove(block);
            AROSAppleMemoryLockRelease(&window_lock, saved);
            return block->physical + (uint64_t)page * AROS_APPLE_PAGE_SIZE;
        }
        AROSAppleMemoryLockRelease(&window_lock, saved);

        block = ops->allocate(ops->context, sizeof(*block));
        if (block == NULL)
            return 0;
        physical = ops->take_block(ops->context, &kept);
        if (physical == 0) {
            ops->release(ops->context, block, sizeof(*block));
            return 0;
        }
        for (uint32_t word = 0; word < BLOCK_PAGES / 64U; ++word)
            block->used[word] = 0;
        block->used_count = 0;
        block->physical = physical;
        block->saved = kept;
        saved = AROSAppleMemoryLockAcquire(&window_lock);
        block->hash_next = pool_hash[hash_of(physical)];
        pool_hash[hash_of(physical)] = block;
        partial_add(block);
        ++pool_empty_blocks;
        ++window_stats.blocks_held;
        AROSAppleMemoryLockRelease(&window_lock, saved);
    }
}

static void pool_give(const struct AROSAppleCodeWindowOps *ops, uint64_t physical)
{
    uint64_t saved = AROSAppleMemoryLockAcquire(&window_lock);
    uint64_t base = physical & ~(AROS_APPLE_CODE_WINDOW_BLOCK - 1U);
    struct PoolBlock **link = &pool_hash[hash_of(base)];
    struct PoolBlock *block;

    while ((block = *link) != NULL && block->physical != base)
        link = &block->hash_next;
    if (block == NULL) {
        AROSAppleMemoryLockRelease(&window_lock, saved);
        return;
    }
    page_bitmap_set(block->used,
        (uint32_t)((physical - base) / AROS_APPLE_PAGE_SIZE), 0);
    if (block->used_count-- == BLOCK_PAGES)
        partial_add(block);
    if (block->used_count != 0 || ++pool_empty_blocks <= POOL_RESERVE_BLOCKS) {
        AROSAppleMemoryLockRelease(&window_lock, saved);
        return;
    }
    /* More empty blocks than the reserve: this one goes back to Exec */
    *link = block->hash_next;
    partial_remove(block);
    --pool_empty_blocks;
    --window_stats.blocks_held;
    AROSAppleMemoryLockRelease(&window_lock, saved);
    ops->give_block(ops->context, block->physical, block->saved);
    ops->release(ops->context, block, sizeof(*block));
}

static void release(uint32_t first, uint32_t pages)
{
    uint64_t saved = AROSAppleMemoryLockAcquire(&window_lock);
    uint32_t index;

    for (index = 0; index < pages; ++index)
        page_bitmap_set(window_reserved, first + index, 0);
    AROSAppleMemoryLockRelease(&window_lock, saved);
}

/* Unmap the backed pages of [first, first + pages), give them back */
static void unback(const struct AROSAppleCodeWindowOps *ops,
    uint32_t first, uint32_t pages)
{
    uint32_t index, backed = 0, executable_pages = 0;
    uint64_t saved;

    for (index = 0; index < pages; ++index) {
        int executable = 0;
        uint64_t physical = ops->unmap(ops->context,
            page_address(first + index), &executable);

        if (physical != 0) {
            pool_give(ops, physical);
            ++backed;
            executable_pages += executable != 0;
        }
    }
    saved = AROSAppleMemoryLockAcquire(&window_lock);
    window_stats.pages_in_use -= backed;
    window_stats.exec_retired += executable_pages;
    AROSAppleMemoryLockRelease(&window_lock, saved);
}

/* With the lock held: reserve a free run, at requested when given */
static int reserve_run(void *requested, uint32_t pages, uint32_t *first)
{
    int found;
    uint32_t index;

    if (requested != NULL) {
        *first = (uint32_t)(((uintptr_t)requested - AROS_APPLE_CODE_WINDOW_BASE) /
            AROS_APPLE_PAGE_SIZE);
        found = page_bitmap_next(window_reserved, *first, *first + pages, 1) ==
            *first + pages;
    } else
        found = page_bitmap_find_up(window_reserved, 0, WINDOW_PAGES - pages,
            pages, first);
    if (found)
        for (index = 0; index < pages; ++index)
            page_bitmap_set(window_reserved, *first + index, 1);
    return found;
}

void *AROSAppleCodeWindowAllocate(const struct AROSAppleCodeWindowOps *ops,
    void *requested, uintptr_t length)
{
    uint32_t pages, first = 0, index;
    uint64_t saved;
    int found;

    if (ops == NULL || length == 0 ||
        length > AROS_APPLE_CODE_WINDOW_BYTES)
        return NULL;
    pages = (uint32_t)((length + AROS_APPLE_PAGE_SIZE - 1U) /
        AROS_APPLE_PAGE_SIZE);
    if (requested != NULL &&
        (!AROSAppleCodeWindowContains(requested, length) ||
         ((uintptr_t)requested & (AROS_APPLE_PAGE_SIZE - 1U)) != 0))
        return NULL;

    saved = AROSAppleMemoryLockAcquire(&window_lock);
    found = reserve_run(requested, pages, &first);
    AROSAppleMemoryLockRelease(&window_lock, saved);
    if (!found)
        return NULL;

    for (index = 0; index < pages; ++index) {
        uint64_t physical = pool_take(ops);

        if (physical == 0 ||
            !ops->map(ops->context, page_address(first + index), physical)) {
            if (physical != 0)
                pool_give(ops, physical);
            saved = AROSAppleMemoryLockAcquire(&window_lock);
            window_stats.pages_in_use += index;
            AROSAppleMemoryLockRelease(&window_lock, saved);
            unback(ops, first, index);
            release(first, pages);
            return NULL;
        }
        /* A page leaves another task's hands: never show its contents */
        ops->clear(ops->context, page_address(first + index));
    }
    saved = AROSAppleMemoryLockAcquire(&window_lock);
    for (index = 0; index < pages; ++index)
        page_bitmap_set(window_live, first + index, 1);
    window_stats.pages_in_use += pages;
    ++window_stats.allocation_count;
    AROSAppleMemoryLockRelease(&window_lock, saved);
    return (void *)page_address(first);
}

int AROSAppleCodeWindowFree(const struct AROSAppleCodeWindowOps *ops,
    void *address, uintptr_t length)
{
    uint32_t pages, first, index;
    uint64_t saved;
    int owned;

    if (ops == NULL || length == 0 ||
        !AROSAppleCodeWindowContains(address, length) ||
        ((uintptr_t)address & (AROS_APPLE_PAGE_SIZE - 1U)) != 0)
        return 0;
    pages = (uint32_t)((length + AROS_APPLE_PAGE_SIZE - 1U) /
        AROS_APPLE_PAGE_SIZE);
    first = (uint32_t)(((uintptr_t)address - AROS_APPLE_CODE_WINDOW_BASE) /
        AROS_APPLE_PAGE_SIZE);

    /* End the run's life first: a second free of it then finds it dead */
    saved = AROSAppleMemoryLockAcquire(&window_lock);
    owned = page_bitmap_next(window_live, first, first + pages, 0) ==
        first + pages;
    if (owned) {
        for (index = 0; index < pages; ++index) {
            page_bitmap_set(window_live, first + index, 0);
            if (page_bitmap_test(window_lazy, first + index)) {
                page_bitmap_set(window_lazy, first + index, 0);
                --window_stats.pages_reserved;
            }
        }
        ++window_stats.free_count;
    }
    AROSAppleMemoryLockRelease(&window_lock, saved);
    if (!owned)
        return 0;
    unback(ops, first, pages);
    release(first, pages);
    return 1;
}

void *AROSAppleCodeWindowReserve(const struct AROSAppleCodeWindowOps *ops,
    void *requested, uintptr_t length)
{
    uint32_t pages, first = 0, index;
    uint64_t saved;
    int found;

    if (ops == NULL || length == 0 || length > AROS_APPLE_CODE_WINDOW_BYTES)
        return NULL;
    pages = (uint32_t)((length + AROS_APPLE_PAGE_SIZE - 1U) /
        AROS_APPLE_PAGE_SIZE);
    if (requested != NULL &&
        (!AROSAppleCodeWindowContains(requested, length) ||
         ((uintptr_t)requested & (AROS_APPLE_PAGE_SIZE - 1U)) != 0))
        return NULL;
    saved = AROSAppleMemoryLockAcquire(&window_lock);
    found = reserve_run(requested, pages, &first);
    if (found) {
        for (index = 0; index < pages; ++index) {
            page_bitmap_set(window_live, first + index, 1);
            page_bitmap_set(window_lazy, first + index, 1);
        }
        window_stats.pages_reserved += pages;
        ++window_stats.allocation_count;
    }
    AROSAppleMemoryLockRelease(&window_lock, saved);
    return found ? (void *)page_address(first) : NULL;
}

/* The page range of [address, address + length), or 0 outside the window */
static int range_of(void *address, uintptr_t length, uint32_t *first,
    uint32_t *pages)
{
    uintptr_t start, end;

    if (length == 0 || !AROSAppleCodeWindowContains(address, length))
        return 0;
    start = (uintptr_t)address & ~(uintptr_t)(AROS_APPLE_PAGE_SIZE - 1U);
    end = ((uintptr_t)address + length + AROS_APPLE_PAGE_SIZE - 1U) &
        ~(uintptr_t)(AROS_APPLE_PAGE_SIZE - 1U);
    *first = (uint32_t)((start - AROS_APPLE_CODE_WINDOW_BASE) / AROS_APPLE_PAGE_SIZE);
    *pages = (uint32_t)((end - start) / AROS_APPLE_PAGE_SIZE);
    return 1;
}

/* With the lock held: every page live and part of a reservation */
static int all_lazy(uint32_t first, uint32_t pages)
{
    return page_bitmap_next(window_live, first, first + pages, 0) ==
            first + pages &&
        page_bitmap_next(window_lazy, first, first + pages, 0) ==
            first + pages;
}

int AROSAppleCodeWindowCommit(const struct AROSAppleCodeWindowOps *ops,
    void *address, uintptr_t length)
{
    uint32_t first, pages, index, added = 0;
    uint64_t saved;
    int lazy;

    if (ops == NULL || !range_of(address, length, &first, &pages))
        return -1;
    saved = AROSAppleMemoryLockAcquire(&window_lock);
    lazy = all_lazy(first, pages);
    AROSAppleMemoryLockRelease(&window_lock, saved);
    if (!lazy)
        return -1;
    for (index = 0; index < pages; ++index) {
        uintptr_t page = page_address(first + index);
        uint64_t physical;

        if (ops->physical(ops->context, page) != 0)
            continue;
        physical = pool_take(ops);
        if (physical == 0)
            break;
        if (!ops->map(ops->context, page, physical)) {
            pool_give(ops, physical);
            /* Another task committed it meanwhile; else out of tables */
            if (ops->physical(ops->context, page) != 0)
                continue;
            break;
        }
        /* A page leaves another task's hands: never show its contents */
        ops->clear(ops->context, page);
        ++added;
    }
    saved = AROSAppleMemoryLockAcquire(&window_lock);
    window_stats.pages_in_use += added;
    AROSAppleMemoryLockRelease(&window_lock, saved);
    return index == pages;
}

int AROSAppleCodeWindowDecommit(const struct AROSAppleCodeWindowOps *ops,
    void *address, uintptr_t length)
{
    uint32_t first, pages;
    uint64_t saved;
    int lazy;

    if (ops == NULL || !range_of(address, length, &first, &pages))
        return 0;
    saved = AROSAppleMemoryLockAcquire(&window_lock);
    lazy = all_lazy(first, pages);
    AROSAppleMemoryLockRelease(&window_lock, saved);
    if (!lazy)
        return 0;
    unback(ops, first, pages);
    return 1;
}

void AROSAppleCodeWindowGetStats(struct AROSAppleCodeWindowStats *stats)
{
    uint64_t saved;

    if (stats == NULL)
        return;
    saved = AROSAppleMemoryLockAcquire(&window_lock);
    *stats = window_stats;
    AROSAppleMemoryLockRelease(&window_lock, saved);
}
