/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 *
 * Apple adapter for the generic AArch64 page-protection engine.
 * Mutable arena mappings are pre-split by stage2_mmu.c before MMU enable.
 * Only Device MAIR-index-0 blocks may be split live: normal RAM may contain
 * the very instructions, stack or table aliases doing break-before-make.
 */
#include <aros/apple/runtime.h>
#include <aros/apple/runtime_local.h>
#include <aros/kernel.h>
#include <aros/libcall.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include "kernel_base.h"
#include "page_allocator.h"
#include "../../aarch64-all/kernel/protection.h"


struct AppleProtectionOperation {
    uintptr_t address;
    unsigned permissions;
    uint64_t *tables[2];
    unsigned used;
    unsigned needs_tables;
};
static struct AppleProtectionOperation protection_operation;

static uint64_t *resolve_table(void *context, uint64_t physical)
{
    (void)context;
    return (uint64_t *)(uintptr_t)physical;
}

static uint64_t take_table(void *context, unsigned page_bits)
{
    struct AppleProtectionOperation *operation = context;
    if (page_bits != 12 || operation->used == 2)
        return 0;
    if (operation->tables[operation->used] == NULL) {
        operation->needs_tables = 1;
        return 0;
    }
    return (uintptr_t)operation->tables[operation->used++];
}

static int can_split(void *context, uint64_t descriptor)
{
    (void)context;
    /* MAIR is installed by stage2: index 0 is Device-nGnRnE. */
    return ((descriptor >> 2) & 7) == 0;
}

static void clean_descriptors(void *context, const void *address, size_t size)
{
    uint64_t ctr;
    uintptr_t line, cursor, end = (uintptr_t)address + size;
    (void)context;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    line = UINT64_C(4) << ((ctr >> 16) & 15);
    for (cursor = (uintptr_t)address & ~(line - 1); cursor < end; cursor += line)
        __asm__ volatile("dc cvac, %0" :: "r"(cursor) : "memory");
}

static void invalidate_translation(void *context)
{
    (void)context;
    __asm__ volatile("dsb ishst; tlbi vmalle1is; dsb ish; isb" ::: "memory");
}

static void synchronize_code_page(void *context, uint64_t address, size_t size)
{
    uint64_t ctr;
    uintptr_t dline;
    uintptr_t iline;
    uintptr_t cursor;
    uintptr_t end = address + size;

    (void)context;

    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    dline = UINT64_C(4) << ((ctr >> 16) & UINT64_C(0xf));
    iline = UINT64_C(4) << (ctr & UINT64_C(0xf));
    for (cursor = address & ~(dline - 1U); cursor < end; cursor += dline)
        __asm__ volatile("dc cvau, %0" :: "r"(cursor) : "memory");
    __asm__ volatile("dsb ish" ::: "memory");
    for (cursor = address & ~(iline - 1U); cursor < end; cursor += iline)
        __asm__ volatile("ic ivau, %0" :: "r"(cursor) : "memory");
    __asm__ volatile("dsb ish; isb" ::: "memory");
}

static uintptr_t protect_one_page(void)
{
    uint64_t ttbr0, tcr;
    const struct AArch64ProtectionOps ops = {
        &protection_operation, resolve_table, take_table, clean_descriptors,
        invalidate_translation, synchronize_code_page, can_split
    };
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr));
    /* The allocator and stage-two contract still use 4KB, not 16KB. */
    if (((tcr >> 14) & 3) != 0)
        return 0;
    return AArch64ProtectPage(&ops, ttbr0, tcr, protection_operation.address,
        protection_operation.permissions);
}

/* Status-bearing internal entry: never release backing after a failed restore.
 * Public KrnSetProtection has a historical void return type. */
int AROSApplePageProtect(void *address, uint32_t length, uint32_t flags)
{
    uintptr_t start, end;
    unsigned permissions = 0;
    if (length == 0 || (uintptr_t)address > UINTPTR_MAX - length ||
        (uintptr_t)address + length > UINTPTR_MAX - (AROS_APPLE_PAGE_SIZE - 1) ||
        AROSAppleRuntimeLocal()->interrupt_depth != 0)
        return 0;
    if (flags & MAP_Readable) permissions |= AARCH64_PROTECT_READ;
    if (flags & MAP_Writable) permissions |= AARCH64_PROTECT_WRITE;
    if (flags & MAP_Executable) permissions |= AARCH64_PROTECT_EXEC;
    if (!(flags & MAP_Supervisor)) {
        permissions |= AARCH64_PROTECT_USER;
        /* Native AROS library text is shared by EL0 callers and EL1 callbacks. */
        if (flags & MAP_Executable) permissions |= AARCH64_PROTECT_EXEC_BOTH;
    }
    start = (uintptr_t)address & ~(AROS_APPLE_PAGE_SIZE - 1);
    end = ((uintptr_t)address + length + AROS_APPLE_PAGE_SIZE - 1) &
        ~(AROS_APPLE_PAGE_SIZE - 1);
    while (start < end) {
        uint64_t *tables[2] = { NULL, NULL };
        unsigned attempt, used = 0, needs_tables = 0, index;
        uintptr_t done = 0;
        for (attempt = 0; attempt < 2; ++attempt) {
            /* The map lock serialises every mapping and protection change
             * and the shared argument record; the mutation rendezvous holds
             * every other online CPU outside the tables while the walk
             * breaks and remakes descriptors (the engine's quiescence
             * contract). Without SMP the rendezvous is a no-op. Interrupt-
             * context callers are rejected above. */
            uint64_t saved = AROSApplePageMapAcquire();
            uint64_t token = AROSApplePageMutationBegin();
            protection_operation.address = start;
            protection_operation.permissions = permissions;
            protection_operation.tables[0] = tables[0];
            protection_operation.tables[1] = tables[1];
            protection_operation.used = 0;
            protection_operation.needs_tables = 0;
            done = token ? AROSAppleRuntimeSupervisor(protect_one_page) : 0;
            if (token)
                AROSApplePageMutationEnd(token);
            used = protection_operation.used;
            needs_tables = protection_operation.needs_tables;
            protection_operation.tables[0] = NULL;
            protection_operation.tables[1] = NULL;
            AROSAppleMemoryLockRelease(&AROSApplePageMapLock, saved);
            if (done || !needs_tables || attempt != 0)
                break;
            /* Arena leaves take no allocation at all, even when full. */
            tables[0] = AROSApplePageTableAllocate(AROS_APPLE_PAGE_SIZE);
            tables[1] = AROSApplePageTableAllocate(AROS_APPLE_PAGE_SIZE);
            if (!tables[0] || !tables[1])
                break;
        }
        for (index = used; index < 2; ++index)
            if (tables[index])
                AROSApplePageFreeRaw(tables[index], AROS_APPLE_PAGE_SIZE);
        if (!done)
            return 0;
        start += AROS_APPLE_PAGE_SIZE;
    }
    AROSApplePageAllocatorRecordProtection(flags);
    return 1;
}

AROS_LH3I(void, KrnSetProtection,
    AROS_LHA(void *, address, A0),
    AROS_LHA(uint32_t, length, D0),
    AROS_LHA(KRN_MapAttr, flags, D1),
    struct KernelBase *, KernelBase, 21, Kernel)
{
    AROS_LIBFUNC_INIT
    (void)KernelBase;
    (void)AROSApplePageProtect(address, length, (uint32_t)flags);
    AROS_LIBFUNC_EXIT
}

/* Build the shared engine in this adapter's translation unit. This avoids
 * competing architecture overrides named setprotection in MetaMake and keeps
 * hosted AArch64's host-specific backend selected. */
#include "../../aarch64-all/kernel/setprotection.c"
