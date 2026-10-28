/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#ifndef AROS_AARCH64_APPLE_PAGE_ALLOCATOR_H
#define AROS_AARCH64_APPLE_PAGE_ALLOCATOR_H

#include <stdint.h>
#include <aros/config.h>
#include "memory_lock.h"
#include "../../aarch64-all/kernel/guarded_stack.h"

/* One lock for both mapping and protection: they modify the same tables. */
extern struct AROSAppleMemoryLock AROSApplePageMapLock;
#if defined(__AROSEXEC_SMP__)
uint64_t AROSApplePageMapAcquire(void);
uint64_t AROSApplePageMutationBegin(void);
void AROSApplePageMutationEnd(uint64_t token);
#else
static inline uint64_t AROSApplePageMapAcquire(void)
{ return AROSAppleMemoryLockAcquire(&AROSApplePageMapLock); }
static inline uint64_t AROSApplePageMutationBegin(void) { return 1; }
static inline void AROSApplePageMutationEnd(uint64_t token) { (void)token; }
#endif

#include <aros/apple/page_arena.h>

struct AROSApplePageAllocatorStats {
    uint64_t allocation_count;
    uint64_t free_count;
    uint64_t pages_in_use;
    uint64_t rx_protection_count;
    uint64_t rw_protection_count;
    uint64_t internal_allocation_count;
    uint64_t internal_free_count;
    uint64_t internal_pages_in_use;
};

int AROSApplePageAllocatorInitialize(uintptr_t start, uint64_t size);
void *AROSApplePageAllocate(void *requested, uintptr_t length, uint32_t flags);
void *AROSApplePageTableAllocate(uintptr_t length);
int AROSApplePageFreeRaw(void *address, uintptr_t length);
int AROSApplePageFree(void *address, uintptr_t length);
int AROSApplePageProtect(void *address, uint32_t length, uint32_t flags);
int AROSAppleGuardedStackCreate(struct AArch64GuardedStack *, size_t size);
int AROSAppleGuardedStackDestroy(struct AArch64GuardedStack *);
void AROSApplePageAllocatorGetStats(
    struct AROSApplePageAllocatorStats *stats);
void AROSApplePageAllocatorRecordProtection(uint32_t flags);

#endif /* AROS_AARCH64_APPLE_PAGE_ALLOCATOR_H */
