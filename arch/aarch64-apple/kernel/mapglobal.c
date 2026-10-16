/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: KrnMapGlobal - identity-map physical memory outside the RAM map.

    Firmware keeps data AROS must reach above the usable RAM m1n1 hands
    over: the DCP's DART page tables, for one. The stage2 identity map does
    not cover it, and KrnSetProtection only changes pages that are already
    mapped. KrnMapGlobal adds identity pages (virt must equal phys) where
    nothing is mapped yet, building the missing level-2 and level-3 tables:
    an invalid slot becomes valid, so no live block is ever broken and no
    break-before-make is needed. Pages that are already mapped are left as
    they are. MAP_Guarded gives Device-nGnRnE, otherwise MAP_CacheInhibit
    gives normal non-cacheable memory; without MAP_Supervisor the pages are
    EL0 read-write, never executable.
*/

#include <aros/apple/runtime.h>
#include <aros/kernel.h>
#include <aros/libcall.h>
#include <exec/memory.h>

#include "kernel_base.h"
#include "page_allocator.h"
#include "map_range.h"

#include <proto/exec.h>

#define MAP_NEEDS_TABLES 2U
#define MAP_ENTRIES 512U
#define MAP_ADDRESS_MASK UINT64_C(0x0000fffffffff000)
#define MAP_TYPE_MASK UINT64_C(3)
#define MAP_INVALID UINT64_C(0)
#define MAP_BLOCK UINT64_C(1)
#define MAP_TABLE UINT64_C(3)
#define MAP_PAGE UINT64_C(3)
#define MAP_ATTR_INDEX(index) ((uint64_t)(index) << 2)
#define MAP_AP_EL0 (UINT64_C(1) << 6)
#define MAP_AP_RO (UINT64_C(1) << 7)
#define MAP_OUTER_SHAREABLE (UINT64_C(2) << 8)
#define MAP_ACCESS_FLAG (UINT64_C(1) << 10)
#define MAP_PXN (UINT64_C(1) << 53)
#define MAP_UXN (UINT64_C(1) << 54)
/* MAIR indices of the stage2 map: 0 Device-nGnRnE, 2 normal non-cacheable. */
#define MAP_DEVICE_INDEX 0U
#define MAP_NONCACHEABLE_INDEX 2U

struct AppleMapOperation {
    uintptr_t address;
    uint64_t attributes;
    uint64_t *tables[2];
    uint32_t used;
    uint32_t added;
};

static struct AppleMapOperation map_operation;

static void clean_line(const void *address)
{
    __asm__ volatile("dc cvac, %0" :: "r"(address) : "memory");
}

static uint64_t *new_table(void)
{
    uint64_t *table;
    uint32_t index;

    if (map_operation.used >= 2U)
        return NULL;
    table = map_operation.tables[map_operation.used++];
    for (index = 0; index < MAP_ENTRIES; ++index)
        table[index] = 0;
    for (index = 0; index < AROS_APPLE_PAGE_SIZE / 64U; ++index)
        clean_line((const UBYTE *)table + (uint64_t)index * 64U);
    __asm__ volatile("dsb ishst" ::: "memory");
    return table;
}

/* The next-level table under slot: the existing one, or a new one
 * installed in an invalid slot. NULL when slot is a block (mapped). */
static uint64_t *descend(uint64_t *slot, int *mapped)
{
    uint64_t *table;

    if ((*slot & MAP_TYPE_MASK) == MAP_TABLE)
        return (uint64_t *)(uintptr_t)(*slot & MAP_ADDRESS_MASK);
    if ((*slot & MAP_TYPE_MASK) == MAP_BLOCK) {
        *mapped = 1;
        return NULL;
    }
    if ((*slot & MAP_TYPE_MASK) != MAP_INVALID)
        return NULL;
    /* A table-free first walk must not publish a partial table chain. */
    if (map_operation.tables[0] == NULL) {
        *mapped = MAP_NEEDS_TABLES;
        return NULL;
    }
    table = new_table();
    if (table == NULL)
        return NULL;
    *slot = ((uintptr_t)table & MAP_ADDRESS_MASK) | MAP_TABLE;
    clean_line(slot);
    __asm__ volatile("dsb ishst" ::: "memory");
    return table;
}

static uintptr_t map_one_page(void)
{
    uintptr_t address = map_operation.address;
    uint64_t root_value;
    uint64_t *level0;
    uint64_t *level1;
    uint64_t *level2;
    uint64_t *level3;
    uint64_t *slot;
    int mapped = 0;

    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(root_value));
    level0 = (uint64_t *)(uintptr_t)(root_value & MAP_ADDRESS_MASK);
    slot = &level0[(address >> 39) & UINT64_C(0x1ff)];
    if ((*slot & MAP_TYPE_MASK) != MAP_TABLE)
        return 0;
    level1 = (uint64_t *)(uintptr_t)(*slot & MAP_ADDRESS_MASK);
    level2 = descend(&level1[(address >> 30) & UINT64_C(0x1ff)], &mapped);
    if (level2 == NULL)
        return (uintptr_t)mapped;
    level3 = descend(&level2[(address >> 21) & UINT64_C(0x1ff)], &mapped);
    if (level3 == NULL)
        return (uintptr_t)mapped;
    slot = &level3[(address >> 12) & UINT64_C(0x1ff)];
    if ((*slot & MAP_TYPE_MASK) == MAP_PAGE)
        return 1;
    if (*slot != 0)
        return 0;
    *slot = ((uint64_t)address & MAP_ADDRESS_MASK) | map_operation.attributes | MAP_PAGE;
    clean_line(slot);
    __asm__ volatile("dsb ish; isb" ::: "memory");
    ++map_operation.added;
    return 1;
}

AROS_LH4I(int, KrnMapGlobal,
    AROS_LHA(void *, virt, A0),
    AROS_LHA(void *, phys, A1),
    AROS_LHA(uint32_t, length, D0),
    AROS_LHA(KRN_MapAttr, flags, D1),
    struct KernelBase *, KernelBase, 16, Kernel)
{
    AROS_LIBFUNC_INIT

    uintptr_t start;
    uintptr_t end;
    uint64_t attributes;
    int ok = 1;

    (void)KernelBase;
    if (virt != phys ||
        !AROSAppleMapRange((uintptr_t)phys, length, &start, &end) ||
        (flags & (MAP_CacheInhibit | MAP_Guarded)) == 0 ||
        (flags & MAP_Executable) != 0)
        return 0;
    attributes = MAP_ACCESS_FLAG | MAP_PXN | MAP_UXN;
    if ((flags & MAP_Guarded) != 0)
        attributes |= MAP_ATTR_INDEX(MAP_DEVICE_INDEX);
    else
        attributes |= MAP_ATTR_INDEX(MAP_NONCACHEABLE_INDEX) | MAP_OUTER_SHAREABLE;
    if ((flags & MAP_Supervisor) == 0)
        attributes |= MAP_AP_EL0;
    if ((flags & MAP_Writable) == 0)
        attributes |= MAP_AP_RO;

    while (ok && start < end) {
        void *tables[2] = { NULL, NULL };
        uint32_t used = 0;
        uint32_t index;
        uintptr_t done = 0;

        /* Existing mappings need no spare pages. Walk under the map lock
         * first; only a missing intermediate table needs allocation. Keep
         * allocation outside the lock and repeat the whole walk afterwards,
         * since another CPU can publish the mapping while we allocate. */
        for (uint32_t attempt = 0; attempt < 2U; ++attempt) {
            if (attempt != 0) {
                tables[0] = AROSApplePageTableAllocate(AROS_APPLE_PAGE_SIZE);
                tables[1] = AROSApplePageTableAllocate(AROS_APPLE_PAGE_SIZE);
                if (tables[0] == NULL || tables[1] == NULL) {
                    done = 0;
                    break;
                }
            }
            uint64_t saved=AROSApplePageMapAcquire();
            map_operation.address = start;
            map_operation.attributes = attributes;
            map_operation.tables[0] = tables[0];
            map_operation.tables[1] = tables[1];
            map_operation.used = 0;
            done = AROSAppleRuntimeSupervisor(map_one_page);
            used = map_operation.used;
            map_operation.tables[0] = NULL;
            map_operation.tables[1] = NULL;
            map_operation.used = 0;
            AROSAppleMemoryLockRelease(&AROSApplePageMapLock,saved);
            if (done != MAP_NEEDS_TABLES)
                break;
        }
        ok = done == 1;
        for (index = used; index < 2U; ++index) {
            if (tables[index] != NULL)
                AROSApplePageFreeRaw(tables[index], AROS_APPLE_PAGE_SIZE);
        }
        start += AROS_APPLE_PAGE_SIZE;
    }
    return ok;

    AROS_LIBFUNC_EXIT
}
