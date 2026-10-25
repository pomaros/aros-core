/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: The code window: a range of virtual addresses for every ELF hunk and
 *       JIT region, backed page by page by 2 MiB blocks given up by Exec.
 */

#ifndef AROS_AARCH64_APPLE_CODE_WINDOW_H
#define AROS_AARCH64_APPLE_CODE_WINDOW_H

#include <stdint.h>

/*
 * The window lies far above the identity map (RAM and devices are below
 * 1 TiB), in its own level-0 slot. Its tables are built on its first use.
 * Contiguity is virtual only: a program's 400 MiB of hunks needs 400 MiB of
 * pages, not 400 MiB in one physical piece, and a reservation's pages need
 * not be next to each other.
 */
#define AROS_APPLE_CODE_WINDOW_BASE  UINT64_C(0x0000400000000000)
#define AROS_APPLE_CODE_WINDOW_BYTES (UINT64_C(64) << 30)
#define AROS_APPLE_CODE_WINDOW_BLOCK (UINT64_C(2) << 20)

/* Where the window's physical memory comes from and how it is mapped. */
struct AROSAppleCodeWindowOps {
    void *context;
    /* A 2 MiB block of RAM, aligned to 2 MiB, taken out of every other
     * mapping; *saved receives what giving it back needs. 0 when none. */
    uint64_t (*take_block)(void *context, uint64_t *saved);
    /* Back to Exec, its identity mapping restored. */
    void (*give_block)(void *context, uint64_t physical, uint64_t saved);
    /* Memory for the pool's bookkeeping (task context, never under a lock) */
    void *(*allocate)(void *context, uint32_t size);
    void (*release)(void *context, void *memory, uint32_t size);
    /* Map physical read-write, not executable, at an address with no
     * mapping. Returns 0 when a table cannot be allocated. */
    int (*map)(void *context, uintptr_t address, uint64_t physical);
    /* Remove the mapping and return its physical page (0 when none);
     * *executable tells whether the page could still be executed. */
    uint64_t (*unmap)(void *context, uintptr_t address, int *executable);
    /* Clear a page through its window address. */
    void (*clear)(void *context, uintptr_t address);
    /* The physical page mapped at a window address, 0 when none. */
    uint64_t (*physical)(void *context, uintptr_t address);
};

struct AROSAppleCodeWindowStats {
    uint64_t pages_in_use;      /* window pages backed */
    uint64_t allocation_count;
    uint64_t free_count;
    uint64_t exec_retired;      /* executable pages unmapped by a free */
    uint64_t blocks_held;       /* 2 MiB blocks taken from Exec */
    uint64_t pages_reserved;    /* pages of reservations (MEMF_RESERVE), backed or not */
};

int AROSAppleCodeWindowContains(const void *address, uintptr_t length);
void *AROSAppleCodeWindowAllocate(const struct AROSAppleCodeWindowOps *ops,
    void *requested, uintptr_t length);
int AROSAppleCodeWindowFree(const struct AROSAppleCodeWindowOps *ops,
    void *address, uintptr_t length);
/* A reservation (MEMF_RESERVE): window pages with no memory behind them.
 * Commit backs its unbacked pages in a range (each cleared), so that a
 * protection change can grant access: 1 done, 0 out of memory, -1 when the
 * range is not wholly inside live reservations. Decommit gives a range's
 * pages back, which makes them inaccessible: 1 done, 0 when the range is not
 * wholly inside live reservations. */
void *AROSAppleCodeWindowReserve(const struct AROSAppleCodeWindowOps *ops,
    void *requested, uintptr_t length);
int AROSAppleCodeWindowCommit(const struct AROSAppleCodeWindowOps *ops,
    void *address, uintptr_t length);
int AROSAppleCodeWindowDecommit(const struct AROSAppleCodeWindowOps *ops,
    void *address, uintptr_t length);
void AROSAppleCodeWindowGetStats(struct AROSAppleCodeWindowStats *stats);

/* The kernel's page source and mappings (code_window_kernel.c) */
extern const struct AROSAppleCodeWindowOps AROSAppleCodeWindowKernelOps;
/* The physical address behind a window address; 0 when nothing is mapped */
uint64_t AROSAppleCodeWindowPhysical(const void *address);
/* The code arena's statistics with the window's added: its pages in use,
 * allocations and frees, and its executable pages unmapped by a free
 * counted as pages that left execute permission (rw_protection_count). */
struct AROSApplePageAllocatorStats;
void AROSAppleCodeMemoryGetStats(struct AROSApplePageAllocatorStats *stats);

#endif /* AROS_AARCH64_APPLE_CODE_WINDOW_H */
