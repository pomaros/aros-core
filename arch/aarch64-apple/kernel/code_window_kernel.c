/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: The code window's memory and mappings on Apple silicon.
 *
 * The window's pages come from 2 MiB blocks of Exec memory. Stage two maps
 * normal RAM with 2 MiB blocks at most, so a block leaves the identity map
 * by invalidating its one L2 descriptor, and comes back by writing that
 * descriptor again: a code page has exactly one mapping, the window's, and
 * no live block is ever split. A block whose descriptor is not a 2 MiB block
 * (an older stage two, or a block a driver split) is not taken.
 *
 * Window mappings only ever turn an empty slot into a table or a page, or a
 * page into an empty slot followed by a TLB invalidation; neither breaks a
 * live block, so neither needs break-before-make or a rendezvous of the
 * other CPUs. Protection changes on window pages (read-only, executable,
 * JIT) go through KrnSetProtection like any other page.
 */

#include <aros/apple/runtime.h>
#include <aros/kernel.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include "code_window.h"
#include "kernel_base.h"
#include "page_allocator.h"

#define WINDOW_ENTRIES 512U
#define WINDOW_ADDRESS_MASK UINT64_C(0x0000fffffffff000)
#define WINDOW_TABLE UINT64_C(3)
#define WINDOW_PAGE UINT64_C(3)
/* Normal write-back memory (MAIR index 1), inner shareable, accessed,
 * EL0 read-write, never executable: what the arena's free pages are. */
#define WINDOW_LEAF_ATTRIBUTES \
    ((UINT64_C(1) << 2) | (UINT64_C(1) << 6) | (UINT64_C(3) << 8) | \
    (UINT64_C(1) << 10) | (UINT64_C(1) << 53) | (UINT64_C(1) << 54))
#define WINDOW_NEEDS_TABLES 2U
#define WINDOW_BLOCK_DESCRIPTOR UINT64_C(1)
#define WINDOW_PXN (UINT64_C(1) << 53)
#define WINDOW_UXN (UINT64_C(1) << 54)

struct WindowOperation {
    uintptr_t address;
    uint64_t physical;
    uint64_t *tables[3];
    uint32_t used;
    uint64_t found;
};

static struct WindowOperation window_operation;

static void clean_line(const void *address)
{
    __asm__ volatile("dc cvac, %0" :: "r"(address) : "memory");
}

/* The table under slot; with create, a spare table installed in an empty
 * slot. NULL with *needs set when a spare is missing, NULL otherwise when
 * slot holds anything but a table. */
static uint64_t *child(uint64_t *slot, int create, int *needs)
{
    uint64_t *table;
    uint32_t index;

    if ((*slot & 3U) == WINDOW_TABLE)
        return (uint64_t *)(uintptr_t)(*slot & WINDOW_ADDRESS_MASK);
    if (*slot != 0 || !create)
        return NULL;
    if (window_operation.used >= 3U ||
        window_operation.tables[window_operation.used] == NULL) {
        *needs = 1;
        return NULL;
    }
    table = window_operation.tables[window_operation.used++];
    for (index = 0; index < WINDOW_ENTRIES; ++index)
        table[index] = 0;
    for (index = 0; index < AROS_APPLE_PAGE_SIZE / 64U; ++index)
        clean_line((const uint8_t *)table + (uint64_t)index * 64U);
    __asm__ volatile("dsb ishst" ::: "memory");
    *slot = ((uintptr_t)table & WINDOW_ADDRESS_MASK) | WINDOW_TABLE;
    clean_line(slot);
    __asm__ volatile("dsb ishst" ::: "memory");
    return table;
}

/* The leaf slot of window_operation.address, at EL1 */
static uint64_t *leaf(int create, int *needs)
{
    uintptr_t address = window_operation.address;
    uint64_t root, *table;

    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(root));
    table = (uint64_t *)(uintptr_t)(root & WINDOW_ADDRESS_MASK);
    table = child(&table[(address >> 39) & 0x1ffU], create, needs);
    if (table != NULL)
        table = child(&table[(address >> 30) & 0x1ffU], create, needs);
    if (table != NULL)
        table = child(&table[(address >> 21) & 0x1ffU], create, needs);
    return table != NULL ? &table[(address >> 12) & 0x1ffU] : NULL;
}

static uintptr_t map_one(void)
{
    int needs = 0;
    uint64_t *slot = leaf(1, &needs);

    if (slot == NULL)
        return needs ? WINDOW_NEEDS_TABLES : 0;
    if (*slot != 0)
        return 0;
    *slot = (window_operation.physical & WINDOW_ADDRESS_MASK) |
        WINDOW_LEAF_ATTRIBUTES | WINDOW_PAGE;
    clean_line(slot);
    __asm__ volatile("dsb ish; isb" ::: "memory");
    return 1;
}

/* A page that is mapped, or guarded by KrnSetProtection (invalid but
 * still holding its output address), gives its physical page back. */
static uintptr_t unmap_one(void)
{
    int needs = 0;
    uint64_t *slot = leaf(0, &needs);

    window_operation.found = 0;
    if (slot == NULL || *slot == 0)
        return 1;
    window_operation.found = *slot & WINDOW_ADDRESS_MASK;
    /* Valid and executable at either level */
    window_operation.physical = (*slot & 1U) != 0 &&
        (*slot & (WINDOW_PXN | WINDOW_UXN)) != (WINDOW_PXN | WINDOW_UXN);
    *slot = 0;
    clean_line(slot);
    __asm__ volatile(
        "dsb ishst\n"
        "tlbi vaae1is, %0\n"
        "dsb ish\n"
        "isb" :: "r"((uint64_t)window_operation.address >> 12) : "memory");
    return 1;
}

static uintptr_t translate_one(void)
{
    int needs = 0;
    uint64_t *slot = leaf(0, &needs);

    window_operation.found = slot != NULL && *slot != 0 ?
        (*slot & WINDOW_ADDRESS_MASK) : 0;
    return 1;
}

/* The identity map's L2 descriptor of window_operation.address, at EL1 */
static uint64_t *identity_block_slot(void)
{
    uintptr_t address = window_operation.address;
    uint64_t root, *table;

    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(root));
    table = (uint64_t *)(uintptr_t)(root & WINDOW_ADDRESS_MASK);
    table = &table[(address >> 39) & 0x1ffU];
    if ((*table & 3U) != WINDOW_TABLE)
        return NULL;
    table = (uint64_t *)(uintptr_t)(*table & WINDOW_ADDRESS_MASK);
    table = &table[(address >> 30) & 0x1ffU];
    if ((*table & 3U) != WINDOW_TABLE)
        return NULL;
    table = (uint64_t *)(uintptr_t)(*table & WINDOW_ADDRESS_MASK);
    return &table[(address >> 21) & 0x1ffU];
}

/* The block leaves the identity map: nothing else may use it, it is ours */
static uintptr_t block_out_one(void)
{
    uint64_t *slot = identity_block_slot();

    if (slot == NULL || (*slot & 3U) != WINDOW_BLOCK_DESCRIPTOR ||
        (*slot & WINDOW_ADDRESS_MASK) != window_operation.address)
        return 0;
    window_operation.found = *slot;
    *slot = 0;
    clean_line(slot);
    __asm__ volatile("dsb ishst; tlbi vmalle1is; dsb ish; isb" ::: "memory");
    return 1;
}

static uintptr_t block_in_one(void)
{
    uint64_t *slot = identity_block_slot();

    if (slot == NULL || *slot != 0)
        return 0;
    *slot = window_operation.physical;
    clean_line(slot);
    __asm__ volatile("dsb ish; isb" ::: "memory");
    return 1;
}

static uint64_t take_block(void *context, uint64_t *saved)
{
    const uint64_t block = AROS_APPLE_CODE_WINDOW_BLOCK;
    uint8_t *raw = AllocMem(2U * block, MEMF_ANY);
    uint8_t *aligned;
    uintptr_t done;
    uint64_t lock;

    (void)context;
    if (raw == NULL)
        return 0;
    /* Keep the aligned 2 MiB, give the rest back */
    aligned = (uint8_t *)(((uintptr_t)raw + block - 1U) & ~(uintptr_t)(block - 1U));
    if (aligned != raw)
        FreeMem(raw, aligned - raw);
    if (aligned + block != raw + 2U * block)
        FreeMem(aligned + block, (raw + 2U * block) - (aligned + block));

    lock = AROSApplePageMapAcquire();
    window_operation.address = (uintptr_t)aligned;
    done = AROSAppleRuntimeSupervisor(block_out_one);
    *saved = window_operation.found;
    AROSAppleMemoryLockRelease(&AROSApplePageMapLock, lock);
    if (!done) {
        FreeMem(aligned, block);
        return 0;
    }
    return (uint64_t)(uintptr_t)aligned;
}

static void give_block(void *context, uint64_t physical, uint64_t saved)
{
    uint64_t lock = AROSApplePageMapAcquire();
    uintptr_t done;

    (void)context;
    window_operation.address = (uintptr_t)physical;
    window_operation.physical = saved;
    done = AROSAppleRuntimeSupervisor(block_in_one);
    AROSAppleMemoryLockRelease(&AROSApplePageMapLock, lock);
    /* A block whose identity mapping cannot be restored stays out of Exec
     * rather than be handed out without one. */
    if (done)
        FreeMem((void *)(uintptr_t)physical, AROS_APPLE_CODE_WINDOW_BLOCK);
}

static void *allocate_record(void *context, uint32_t size)
{
    (void)context;
    return AllocMem(size, MEMF_PUBLIC | MEMF_CLEAR);
}

static void release_record(void *context, void *memory, uint32_t size)
{
    (void)context;
    FreeMem(memory, size);
}

static int map_page(void *context, uintptr_t address, uint64_t physical)
{
    uint64_t *tables[3] = { NULL, NULL, NULL };
    uintptr_t done = 0;
    uint32_t attempt, index, used = 0;

    (void)context;
    /* As KrnMapGlobal: walk without spares first; allocate them outside the
     * lock only when a table is missing, then walk again. */
    for (attempt = 0; attempt < 2U; ++attempt) {
        uint64_t saved;

        if (attempt != 0) {
            for (index = 0; index < 3U; ++index) {
                tables[index] = AROSApplePageTableAllocate(AROS_APPLE_PAGE_SIZE);
                if (tables[index] == NULL)
                    break;
            }
            if (index != 3U)
                break;
        }
        saved = AROSApplePageMapAcquire();
        window_operation.address = address;
        window_operation.physical = physical;
        for (index = 0; index < 3U; ++index)
            window_operation.tables[index] = tables[index];
        window_operation.used = 0;
        done = AROSAppleRuntimeSupervisor(map_one);
        used = window_operation.used;
        for (index = 0; index < 3U; ++index)
            window_operation.tables[index] = NULL;
        AROSAppleMemoryLockRelease(&AROSApplePageMapLock, saved);
        if (done != WINDOW_NEEDS_TABLES)
            break;
    }
    for (index = used; index < 3U; ++index)
        if (tables[index] != NULL)
            AROSApplePageFreeRaw(tables[index], AROS_APPLE_PAGE_SIZE);
    return done == 1;
}

static uint64_t unmap_page(void *context, uintptr_t address, int *executable)
{
    uint64_t saved = AROSApplePageMapAcquire();
    uint64_t physical;

    (void)context;
    window_operation.address = address;
    window_operation.physical = 0;
    (void)AROSAppleRuntimeSupervisor(unmap_one);
    physical = window_operation.found;
    *executable = physical != 0 && window_operation.physical != 0;
    AROSAppleMemoryLockRelease(&AROSApplePageMapLock, saved);
    return physical;
}

static void clear_page(void *context, uintptr_t address)
{
    volatile uint64_t *word = (volatile uint64_t *)address;
    uint32_t index;

    (void)context;
    for (index = 0; index < AROS_APPLE_PAGE_SIZE / sizeof(*word); ++index)
        word[index] = 0;
}

static uint64_t physical_page(void *context, uintptr_t address)
{
    (void)context;
    return AROSAppleCodeWindowPhysical((const void *)address);
}

const struct AROSAppleCodeWindowOps AROSAppleCodeWindowKernelOps = {
    NULL, take_block, give_block, allocate_record, release_record,
    map_page, unmap_page, clear_page, physical_page
};

void AROSAppleCodeMemoryGetStats(struct AROSApplePageAllocatorStats *stats)
{
    struct AROSAppleCodeWindowStats window;

    if (stats == NULL)
        return;
    AROSApplePageAllocatorGetStats(stats);
    AROSAppleCodeWindowGetStats(&window);
    stats->pages_in_use += window.pages_in_use;
    stats->allocation_count += window.allocation_count;
    stats->free_count += window.free_count;
    stats->rw_protection_count += window.exec_retired;
}

uint64_t AROSAppleCodeWindowPhysical(const void *address)
{
    uint64_t saved, physical;

    if (!AROSAppleCodeWindowContains(address, 1))
        return 0;
    saved = AROSApplePageMapAcquire();
    window_operation.address = (uintptr_t)address & ~(uintptr_t)(AROS_APPLE_PAGE_SIZE - 1U);
    (void)AROSAppleRuntimeSupervisor(translate_one);
    physical = window_operation.found;
    AROSAppleMemoryLockRelease(&AROSApplePageMapLock, saved);
    return physical != 0 ?
        physical | ((uintptr_t)address & (AROS_APPLE_PAGE_SIZE - 1U)) : 0;
}
