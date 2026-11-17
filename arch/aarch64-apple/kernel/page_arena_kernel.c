/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Where the page arena grows on Apple silicon: 2 MiB blocks of Exec
 *       memory, made page-granular in every translation root.
 *
 * Stage two maps normal RAM with 2 MiB blocks at most, and every block that
 * meets its part of the arena with 4 KiB leaves. A chunk taken from Exec is
 * made the same: each block's level-2 descriptor in the native root becomes
 * a table of leaves with the same attributes, and restricted.c does the same
 * in each domain root (AROSAppleRestrictedArenaAttachLocked). Nothing else
 * uses a block that Exec just gave up, so replacing its descriptor needs only
 * break-before-make and a broadcast TLB invalidation, not a rendezvous of the
 * other CPUs. A block whose descriptor is already a table of plain leaves (a
 * block stage two split at a range boundary) is taken as it is; any other
 * block makes the whole chunk go back to Exec.
 *
 * A chunk going back gets its blocks again in the native root, once every
 * leaf is again what it was: valid, writable, normal memory, not executable.
 * Domain roots keep their tables, privileged aliases of Exec memory as their
 * blocks were before.
 */

#include <aros/apple/runtime.h>
#include <aros/kernel.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include "kernel_base.h"
#include "page_allocator.h"
#include "restricted.h"

#define ARENA_ADDRESS_MASK UINT64_C(0x0000fffffffff000)
#define ARENA_BLOCK_PAGES ((uint32_t)(AROS_APPLE_PAGE_ARENA_BLOCK / AROS_APPLE_PAGE_SIZE))
#define DESCRIPTOR_TYPE UINT64_C(3)
#define BLOCK_DESCRIPTOR UINT64_C(1)
#define TABLE_DESCRIPTOR UINT64_C(3)
#define ATTRIBUTE_INDEX (UINT64_C(7) << 2)
#define NORMAL_MEMORY (UINT64_C(1) << 2)
#define READ_ONLY (UINT64_C(1) << 7)
#define PXN (UINT64_C(1) << 53)

#define ATTACH_FAILED 1U
#define ATTACH_RETRY 2U
#define ATTACH_DONE 3U

static struct AROSApplePageArenaGrowth *arena_growth;
static uintptr_t arena_detach_start;
static uint32_t arena_detach_blocks;
static const uint64_t *arena_detach_saved;

static void clean_range(const void *address, uint64_t bytes)
{
    uintptr_t line;

    for (line = (uintptr_t)address & ~(uintptr_t)63U;
            line < (uintptr_t)address + bytes; line += 64U)
        __asm__ volatile("dc cvac, %0" :: "r"(line) : "memory");
    __asm__ volatile("dsb ishst" ::: "memory");
}

/* The native root's level-2 descriptor of block, at EL1 */
static uint64_t *native_block_slot(uintptr_t block)
{
    uint64_t root, *table;
    unsigned shift;

    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(root));
    table = (uint64_t *)(uintptr_t)(root & ARENA_ADDRESS_MASK);
    for (shift = 39; shift > 21; shift -= 9) {
        uint64_t descriptor = table[(block >> shift) & 511U];

        if ((descriptor & DESCRIPTOR_TYPE) != TABLE_DESCRIPTOR)
            return NULL;
        table = (uint64_t *)(uintptr_t)(descriptor & ARENA_ADDRESS_MASK);
    }
    return &table[(block >> 21) & 511U];
}

/* What a free arena page and an Exec page both are */
static int plain_leaf(uint64_t leaf, uintptr_t page)
{
    return (leaf & DESCRIPTOR_TYPE) == 3U &&
        (leaf & ARENA_ADDRESS_MASK) == page &&
        (leaf & ATTRIBUTE_INDEX) == NORMAL_MEMORY &&
        (leaf & READ_ONLY) == 0 && (leaf & PXN) != 0;
}

/* saved and the leaf tables live inside the chunk being remapped. Complete
 * one block's break-before-make before reading that metadata again. Both the
 * replacement descriptor and its slot must be obtained while it is mapped. */
static void native_replace(uint64_t *slot, uint64_t descriptor)
{
    *slot = 0;
    clean_range(slot, sizeof(*slot));
    __asm__ volatile("dsb ishst; tlbi vmalle1is; dsb ish; isb" ::: "memory");
    *slot = descriptor;
    clean_range(slot, sizeof(*slot));
    __asm__ volatile("dsb ish; isb" ::: "memory");
}

static void native_restore(struct AROSApplePageArenaGrowth *growth, uint32_t blocks)
{
    uint32_t block;

    for (block = 0; block < blocks; ++block) {
        uint64_t descriptor = growth->saved[block];
        uint64_t *slot;

        if (descriptor == 0)
            continue;
        slot = native_block_slot(growth->start +
            (uintptr_t)block * AROS_APPLE_PAGE_ARENA_BLOCK);
        native_replace(slot, descriptor);
    }
}

static uintptr_t attach_one(void)
{
    struct AROSApplePageArenaGrowth *growth = arena_growth;
    uint32_t block, page;
    int status;

    /* Every block first, before any descriptor changes */
    for (block = 0; block < growth->blocks; ++block) {
        uintptr_t address = growth->start +
            (uintptr_t)block * AROS_APPLE_PAGE_ARENA_BLOCK;
        uint64_t *slot = native_block_slot(address), descriptor;

        growth->saved[block] = 0;
        if (slot == NULL)
            return ATTACH_FAILED;
        descriptor = *slot;
        if ((descriptor & DESCRIPTOR_TYPE) == BLOCK_DESCRIPTOR &&
            (descriptor & ARENA_ADDRESS_MASK) == address &&
            (descriptor & ATTRIBUTE_INDEX) == NORMAL_MEMORY &&
            (descriptor & READ_ONLY) == 0 && (descriptor & PXN) != 0) {
            uint64_t *leaves = growth->tables + (uint64_t)block * ARENA_BLOCK_PAGES;

            growth->saved[block] = descriptor;
            for (page = 0; page < ARENA_BLOCK_PAGES; ++page)
                leaves[page] = (descriptor & ~(ARENA_ADDRESS_MASK | DESCRIPTOR_TYPE)) |
                    (address + (uintptr_t)page * AROS_APPLE_PAGE_SIZE) | 3U;
            clean_range(leaves, AROS_APPLE_PAGE_SIZE);
        } else if ((descriptor & DESCRIPTOR_TYPE) == TABLE_DESCRIPTOR) {
            const uint64_t *leaves =
                (const uint64_t *)(uintptr_t)(descriptor & ARENA_ADDRESS_MASK);

            for (page = 0; page < ARENA_BLOCK_PAGES; ++page)
                if (!plain_leaf(leaves[page],
                        address + (uintptr_t)page * AROS_APPLE_PAGE_SIZE))
                    return ATTACH_FAILED;
        } else
            return ATTACH_FAILED;
    }
    for (block = 0; block < growth->blocks; ++block) {
        uint64_t *slot, descriptor;

        if (growth->saved[block] == 0)
            continue;
        slot = native_block_slot(growth->start +
            (uintptr_t)block * AROS_APPLE_PAGE_ARENA_BLOCK);
        descriptor = ((uintptr_t)(growth->tables +
            (uint64_t)block * ARENA_BLOCK_PAGES) & ARENA_ADDRESS_MASK) |
            TABLE_DESCRIPTOR;
        native_replace(slot, descriptor);
    }

    /* The native root stays as it was unless every root is done */
    status = AROSAppleRestrictedArenaAttachLocked(growth);
    if (status != 1)
        native_restore(growth, growth->blocks);
    return status == 1 ? ATTACH_DONE : status < 0 ? ATTACH_RETRY : ATTACH_FAILED;
}

static uintptr_t detach_one(void)
{
    struct AROSApplePageArenaGrowth growth;
    uint32_t block, page;

    for (block = 0; block < arena_detach_blocks; ++block) {
        uintptr_t address = arena_detach_start +
            (uintptr_t)block * AROS_APPLE_PAGE_ARENA_BLOCK;
        uint64_t *slot, saved = arena_detach_saved[block];
        const uint64_t *leaves;

        if (saved == 0)
            continue;
        slot = native_block_slot(address);
        if (slot == NULL || (*slot & DESCRIPTOR_TYPE) != TABLE_DESCRIPTOR)
            return 0;
        leaves = (const uint64_t *)(uintptr_t)(*slot & ARENA_ADDRESS_MASK);
        for (page = 0; page < ARENA_BLOCK_PAGES; ++page)
            if (!plain_leaf(leaves[page],
                    address + (uintptr_t)page * AROS_APPLE_PAGE_SIZE))
                return 0;
    }
    growth.start = arena_detach_start;
    growth.saved = (uint64_t *)arena_detach_saved;
    native_restore(&growth, arena_detach_blocks);
    return 1;
}

static int arena_can_grow(void)
{
    uint64_t daif;

    if (SysBase == NULL || AROSAppleRuntimeIsSuper())
        return 0;
    /* Not under a lock that masks interrupts, nor in Disable() */
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    return (daif & (UINT64_C(3) << 6)) == 0;
}

static uintptr_t arena_take(uint64_t bytes)
{
    const uint64_t block = AROS_APPLE_PAGE_ARENA_BLOCK;
    uint8_t *raw = AllocMem(bytes + block, MEMF_ANY);
    uint8_t *aligned;

    if (raw == NULL)
        return 0;
    /* Keep the aligned chunk, give the rest back */
    aligned = (uint8_t *)(((uintptr_t)raw + block - 1U) & ~(uintptr_t)(block - 1U));
    if (aligned != raw)
        FreeMem(raw, aligned - raw);
    if (aligned + bytes != raw + bytes + block)
        FreeMem(aligned + bytes, (raw + bytes + block) - (aligned + bytes));
    return (uintptr_t)aligned;
}

static void arena_give(uintptr_t start, uint64_t bytes)
{
    FreeMem((void *)start, bytes);
}

static void free_index(uintptr_t *index, size_t slots)
{
    if (index == NULL)
        return;
    /* The first index came from the arena, later ones from Exec */
    if (AROSApplePageArenaContains((uintptr_t)index))
        (void)AROSApplePageFreeRaw(index, slots * sizeof(*index));
    else
        FreeMem(index, slots * sizeof(*index));
}

static int arena_attach(struct AROSApplePageArenaGrowth *growth)
{
    size_t have = AROSAppleRestrictedIndexSlots();
    size_t need = AROSAppleRestrictedIndexSlotsFor(growth->member_pages);
    uintptr_t done;
    uint64_t lock;

    /* Storage for the restricted index outside every lock: a racy look,
     * the attach decides and asks again when it needs some after all */
    if (have != 0 && have < need) {
        growth->index = AllocMem(need * sizeof(uintptr_t), MEMF_PUBLIC | MEMF_CLEAR);
        if (growth->index == NULL)
            return 0;
        growth->index_slots = need;
    }
    lock = AROSApplePageMapAcquire();
    arena_growth = growth;
    done = AROSAppleRuntimeSupervisor(attach_one);
    arena_growth = NULL;
    AROSAppleMemoryLockRelease(&AROSApplePageMapLock, lock);
    if (growth->index != NULL)
        FreeMem(growth->index, growth->index_slots * sizeof(uintptr_t));
    free_index(growth->old_index, growth->old_index_slots);
    return done == ATTACH_DONE ? 1 : done == ATTACH_RETRY ? -1 : 0;
}

static int arena_detach(uintptr_t start, uint32_t blocks, const uint64_t *saved)
{
    uintptr_t done;
    uint64_t lock = AROSApplePageMapAcquire();

    arena_detach_start = start;
    arena_detach_blocks = blocks;
    arena_detach_saved = saved;
    done = AROSAppleRuntimeSupervisor(detach_one);
    AROSAppleMemoryLockRelease(&AROSApplePageMapLock, lock);
    return done == 1;
}

const struct AROSApplePageArenaOps AROSApplePageArenaKernelOps = {
    arena_can_grow, arena_take, arena_give, arena_attach, arena_detach
};
