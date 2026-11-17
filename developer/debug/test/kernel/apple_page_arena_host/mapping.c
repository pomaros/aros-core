/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1
 * Production attach/restore bodies, with host protection replacing MMU ops.
 */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "mapping-types.inc"

#define BLOCKS 4
static uintptr_t chunk;
static uint64_t slots[BLOCKS], original[BLOCKS];
static int attach_status;
static unsigned invalidations;

static uint64_t *native_block_slot(uintptr_t address)
{
    assert(address >= chunk && address < chunk + BLOCKS * AROS_APPLE_PAGE_ARENA_BLOCK);
    return &slots[(address - chunk) / AROS_APPLE_PAGE_ARENA_BLOCK];
}
static void protect(unsigned i, int prot)
{
    assert(mprotect((void *)(chunk + i * AROS_APPLE_PAGE_ARENA_BLOCK),
        AROS_APPLE_PAGE_ARENA_BLOCK, prot) == 0);
}
static void clean_range(const void *address, uint64_t bytes)
{
    (void)bytes;
    for (unsigned i = 0; i < BLOCKS; ++i)
        if (address == &slots[i] && slots[i])
            protect(i, PROT_READ | PROT_WRITE);
}
static void invalidate(void)
{
    ++invalidations;
    for (unsigned i = 0; i < BLOCKS; ++i)
        if (!slots[i]) protect(i, PROT_NONE);
}
static int AROSAppleRestrictedArenaAttachLocked(struct AROSApplePageArenaGrowth *g)
{
    /* All blocks, including in-chunk metadata, must be accessible by now. */
    for (unsigned i = 0; i < BLOCKS; ++i) {
        assert(g->saved[i] == (i == 1 ? 0 : original[i]));
        assert(*(volatile unsigned char *)(chunk + (i + 1) * AROS_APPLE_PAGE_ARENA_BLOCK - 1) == 0);
    }
    return attach_status;
}
#include "mapping-body.inc"

int main(void)
{
    const size_t bytes = BLOCKS * AROS_APPLE_PAGE_ARENA_BLOCK;
    void *raw = mmap(NULL, bytes + AROS_APPLE_PAGE_ARENA_BLOCK,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(raw != MAP_FAILED);
    chunk = ((uintptr_t)raw + AROS_APPLE_PAGE_ARENA_BLOCK - 1) &
        ~(uintptr_t)(AROS_APPLE_PAGE_ARENA_BLOCK - 1);
    /* Metadata in first and last blocks, plus one already split block. */
    for (unsigned metadata = 0; metadata < BLOCKS; metadata += BLOCKS - 1) {
        for (attach_status = -1; attach_status <= 1; ++attach_status) {
            struct AROSApplePageArenaGrowth g = {0};
            g.start = chunk; g.blocks = BLOCKS;
            g.saved = (uint64_t *)(chunk + metadata * AROS_APPLE_PAGE_ARENA_BLOCK);
            g.tables = (uint64_t *)(chunk + metadata * AROS_APPLE_PAGE_ARENA_BLOCK + AROS_APPLE_PAGE_SIZE);
            for (unsigned i = 0; i < BLOCKS; ++i)
                original[i] = slots[i] = chunk + i * AROS_APPLE_PAGE_ARENA_BLOCK |
                    BLOCK_DESCRIPTOR | NORMAL_MEMORY | PXN;
            uint64_t *preexisting = (uint64_t *)(chunk + AROS_APPLE_PAGE_ARENA_BLOCK);
            for (unsigned p = 0; p < ARENA_BLOCK_PAGES; ++p)
                preexisting[p] = chunk + AROS_APPLE_PAGE_ARENA_BLOCK + p * AROS_APPLE_PAGE_SIZE |
                    TABLE_DESCRIPTOR | NORMAL_MEMORY | PXN;
            original[1] = slots[1] = (uintptr_t)preexisting | TABLE_DESCRIPTOR;
            arena_growth = &g;
            uintptr_t result = attach_one();
            assert(result == (attach_status == 1 ? ATTACH_DONE : attach_status < 0 ? ATTACH_RETRY : ATTACH_FAILED));
            if (attach_status == 1) {
                arena_detach_start = chunk; arena_detach_blocks = BLOCKS; arena_detach_saved = g.saved;
                assert(detach_one() == 1);
            }
            for (unsigned i = 0; i < BLOCKS; ++i) assert(slots[i] == original[i]);
        }
    }
    assert(invalidations != 0);
    assert(munmap(raw, bytes + AROS_APPLE_PAGE_ARENA_BLOCK) == 0);
    puts("ARENA: protected metadata attach/retry/rollback/detach PASS");
    return 0;
}
