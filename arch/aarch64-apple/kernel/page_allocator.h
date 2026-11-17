/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#ifndef AROS_AARCH64_APPLE_PAGE_ALLOCATOR_H
#define AROS_AARCH64_APPLE_PAGE_ALLOCATOR_H

#include <stddef.h>
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

/*
 * The arena is a set of segments: stage two's part, and 2 MiB-aligned chunks
 * taken from Exec. A page's membership does not change while it is allocated,
 * so its owner may test it without the allocator lock.
 */
int AROSApplePageArenaContains(uintptr_t address);
/* Pages of every segment: the bound of what the arena can hand out now. */
uint32_t AROSApplePageArenaPages(void);
int AROSApplePageAllocatorInitialize(uintptr_t start, uint64_t size);
void *AROSApplePageAllocate(void *requested, uintptr_t length, uint32_t flags);
void *AROSApplePageTableAllocate(uintptr_t length);
int AROSApplePageFreeRaw(void *address, uintptr_t length);
int AROSApplePageFree(void *address, uintptr_t length);
int AROSApplePageProtect(void *address, uint32_t length, uint32_t flags);
/* EL1 private-page publication: caller owns PageMapLock, a completed remote
 * rendezvous and the restricted coordinator. Generic READ/WRITE flags only;
 * arena leaves are pre-split. No allocation or lock acquisition. */
int AROSApplePageProtectArenaLocked(uintptr_t address,unsigned permissions);
int AROSAppleGuardedStackCreate(struct AArch64GuardedStack *, size_t size);
int AROSAppleGuardedStackDestroy(struct AArch64GuardedStack *);
void AROSApplePageAllocatorGetStats(
    struct AROSApplePageAllocatorStats *stats);
void AROSApplePageAllocatorRecordProtection(uint32_t flags);

/*
 * Growth. A request that fits no segment, made where Exec may be called,
 * takes a chunk of whole 2 MiB blocks from Exec. The chunk's first pages hold
 * its bitmaps and one level-3 table per block; attach replaces each block's
 * level-2 identity descriptor by its table in the native root and in every
 * restricted domain root cloned from it, then publishes the chunk, so that
 * the chunk's pages are page-granular in every root, as stage two made its
 * part, before anyone can allocate them. A chunk whose pages are all free
 * again goes back to Exec (AROSApplePageArenaTrim) with its blocks restored.
 */
struct AROSApplePageArenaGrowth {
    uintptr_t start;            /* the chunk, 2 MiB aligned */
    uint32_t blocks;            /* 2 MiB blocks in it */
    uint32_t member_pages;      /* arena pages once it is published */
    uint64_t *tables;           /* blocks level-3 tables, one page each */
    uint64_t *saved;            /* per block: the descriptor its table replaced,
                                 * 0 when the block already had page leaves */
    uintptr_t *index;           /* restricted backing index storage offered, */
    size_t index_slots;         /* taken (set to NULL) when the index grows */
    uintptr_t *old_index;       /* the index it replaced, for the caller to free */
    size_t old_index_slots;
    void *segment;              /* the allocator's own record */
};

struct AROSApplePageArenaOps {
    /* A task, not an exception handler or a lock holder: Exec may be called */
    int (*can_grow)(void);
    /* bytes of RAM, 2 MiB aligned, out of Exec; 0 when there is none */
    uintptr_t (*take)(uint64_t bytes);
    void (*give)(uintptr_t start, uint64_t bytes);
    /* Make the chunk page-granular everywhere and publish it with
     * AROSApplePageArenaPublish: 1 done, 0 failed with every root as it
     * was, -1 to be asked again (the restricted index needs storage). */
    int (*attach)(struct AROSApplePageArenaGrowth *growth);
    /* Restore the blocks of a chunk nothing uses: 1 done, 0 refused */
    int (*detach)(uintptr_t start, uint32_t blocks, const uint64_t *saved);
};

void AROSApplePageArenaSetOps(const struct AROSApplePageArenaOps *ops);
/* While attaching (locks held, chunk unpublished): pages of the chunk for
 * the restricted domain records that attach needs, from its top; NULL when
 * the chunk has too few left. Internal pages are page-table pages. */
void *AROSApplePageArenaCarve(struct AROSApplePageArenaGrowth *growth,
    uint32_t pages, int internal);
/* The last step of attach, with its locks held: the chunk becomes arena. */
void AROSApplePageArenaPublish(struct AROSApplePageArenaGrowth *growth);
/* Give chunks nothing uses back to Exec, keeping some free pages; called
 * where Exec may be called (a free in a task). */
void AROSApplePageArenaTrim(void);

/* The kernel's growth operations (page_arena_kernel.c) */
extern const struct AROSApplePageArenaOps AROSApplePageArenaKernelOps;

#endif /* AROS_AARCH64_APPLE_PAGE_ALLOCATOR_H */
