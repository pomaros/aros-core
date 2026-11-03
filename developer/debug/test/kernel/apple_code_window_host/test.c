/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: The Apple code window (kernel/code_window.c, compiled as is) on the
          host, over a simulated Exec (2 MiB blocks) and mapping: every page
          of a request is backed by its own cleared physical page, frees
          (whole, head, tail) give each page back once, a second free gives
          nothing, a failure while backing gives back what was taken, fixed
          addresses get exactly their run, requests made one after the
          other are adjacent (a segment's hunks), a block goes back to Exec
          only when none of its pages is mapped, and all but a reserve of
          empty blocks go back. A reservation (MEMF_RESERVE) maps nothing
          until a range of it is committed, a commit backs only what is not
          backed yet, a decommit gives the pages back, neither applies
          outside a reservation, and freeing it gives everything back.
          Foreign runs map pages the window does not own, at a run of
          their own: Free, Commit and Decommit refuse them, unmapping takes
          exactly the run back and never gives its pages to the pool, and a
          table failure leaves neither run nor mapping behind.
*/
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "code_window.h"

uint64_t AROSAppleRuntimeIRQSave(void) { return 0; }
void AROSAppleRuntimeIRQRestore(uint64_t mask) { (void)mask; }

#define PG 4096UL
#define BLOCK (2UL << 20)
#define WPAGES (AROS_APPLE_CODE_WINDOW_BYTES / PG)
#define MAXBLOCKS 64UL                 /* what the simulated Exec can give */
#define PAGES (MAXBLOCKS * BLOCK / PG)
#define PHYS_BASE (64UL << 30)
#define FOREIGN_BASE (PHYS_BASE + MAXBLOCKS * BLOCK)  /* pages the window does not own */
static uint64_t *mapped;               /* window page -> physical, 0 if none */
static unsigned char *executable_page; /* window page made executable */
static unsigned char page_mapped[PAGES], cleared[PAGES];
static unsigned char block_out[MAXBLOCKS];
static unsigned long blocks_taken, blocks_given, records, take_fail_after = (unsigned long)-1, maps, map_fail_after = (unsigned long)-1;

static unsigned long page_index(uint64_t physical) { return (physical - PHYS_BASE) / PG; }
static uint64_t take_block(void *c, uint64_t *saved)
{
    unsigned long b;
    (void)c;
    if (blocks_taken - blocks_given >= take_fail_after) return 0;
    for (b = 0; b < MAXBLOCKS; b++)
        if (!block_out[b]) { block_out[b] = 1; blocks_taken++; *saved = 0xb10c0000UL | b; return PHYS_BASE + b * BLOCK; }
    return 0;
}
static void give_block(void *c, uint64_t physical, uint64_t saved)
{
    unsigned long b = (physical - PHYS_BASE) / BLOCK, i;
    (void)c;
    assert(physical % BLOCK == 0 && b < MAXBLOCKS && block_out[b] && saved == (0xb10c0000UL | b));
    for (i = 0; i < BLOCK / PG; i++) assert(!page_mapped[b * (BLOCK / PG) + i]);  /* never while in use */
    block_out[b] = 0; blocks_given++;
}
static void *allocate(void *c, uint32_t size) { (void)c; records++; return malloc(size); }
static void release(void *c, void *m, uint32_t size) { (void)c; (void)size; records--; free(m); }
static int map(void *c, uintptr_t va, uint64_t physical)
{
    uint64_t w = (va - AROS_APPLE_CODE_WINDOW_BASE) / PG;
    unsigned long p = page_index(physical);
    (void)c;
    assert(va % PG == 0 && w < WPAGES && mapped[w] == 0);
    if (physical >= FOREIGN_BASE) {
        if (maps >= map_fail_after) return 0;
        maps++; mapped[w] = physical; return 1;
    }
    assert(physical % PG == 0 && p < PAGES && block_out[p / (BLOCK / PG)] && !page_mapped[p]);
    if (maps >= map_fail_after) return 0;
    maps++; mapped[w] = physical; page_mapped[p] = 1; cleared[p] = 0; return 1;
}
static uint64_t unmap(void *c, uintptr_t va, int *executable)
{
    uint64_t w = (va - AROS_APPLE_CODE_WINDOW_BASE) / PG, p = mapped[w];
    (void)c;
    *executable = p != 0 && executable_page[w];
    executable_page[w] = 0;
    if (p && p < FOREIGN_BASE) page_mapped[page_index(p)] = 0;
    mapped[w] = 0; return p;
}
static void clear(void *c, uintptr_t va)
{
    uint64_t w = (va - AROS_APPLE_CODE_WINDOW_BASE) / PG;
    (void)c;
    assert(mapped[w] != 0);
    cleared[page_index(mapped[w])] = 1;
}
static uint64_t physical(void *c, uintptr_t va)
{
    (void)c;
    return mapped[(va - AROS_APPLE_CODE_WINDOW_BASE) / PG];
}
static const struct AROSAppleCodeWindowOps ops = { NULL, take_block, give_block, allocate, release, map, unmap, clear, physical };

static unsigned long lent_count(void) { unsigned long i, n = 0; for (i = 0; i < PAGES; i++) n += page_mapped[i]; return n; }
static uint64_t in_use(void) { struct AROSAppleCodeWindowStats s; AROSAppleCodeWindowGetStats(&s); return s.pages_in_use; }
static uint64_t blocks_held(void) { struct AROSAppleCodeWindowStats s; AROSAppleCodeWindowGetStats(&s); return s.blocks_held; }
static void backed(void *a, uintptr_t len)
{
    uint64_t w = ((uintptr_t)a - AROS_APPLE_CODE_WINDOW_BASE) / PG, i, n = (len + PG - 1) / PG;
    for (i = 0; i < n; i++) { assert(mapped[w + i]); assert(cleared[page_index(mapped[w + i])]); }
}

int main(void)
{
    struct { void *a; uintptr_t len; } live[3000];
    int n = 0, step, i;
    void *a, *b, *c;

    mapped = calloc(WPAGES, sizeof(*mapped));
    executable_page = calloc(WPAGES, 1);
    assert(mapped && executable_page);
    /* Adjacency and backing */
    a = AROSAppleCodeWindowAllocate(&ops, NULL, 3 * PG + 5);
    b = AROSAppleCodeWindowAllocate(&ops, NULL, PG);
    assert(a == (void *)AROS_APPLE_CODE_WINDOW_BASE && (uintptr_t)b == (uintptr_t)a + 4 * PG);
    backed(a, 4 * PG); backed(b, PG); assert(lent_count() == 5 && in_use() == 5 && blocks_held() == 1);
    /* Head and tail frees, then the middle; a second free gives nothing */
    assert(AROSAppleCodeWindowFree(&ops, a, PG));
    assert(AROSAppleCodeWindowFree(&ops, (char *)a + 3 * PG, PG));
    assert(lent_count() == 3);
    assert(AROSAppleCodeWindowFree(&ops, (char *)a + PG, 2 * PG));
    assert(!AROSAppleCodeWindowFree(&ops, (char *)a + PG, 2 * PG));
    assert(!AROSAppleCodeWindowFree(&ops, a, PG));
    assert(lent_count() == 1 && in_use() == 1);
    /* The freed run is reused, first fit */
    c = AROSAppleCodeWindowAllocate(&ops, NULL, 2 * PG);
    assert(c == a);
    assert(AROSAppleCodeWindowFree(&ops, c, 2 * PG) && AROSAppleCodeWindowFree(&ops, b, PG));
    assert(lent_count() == 0 && in_use() == 0);
    /* A failure while backing gives back what was taken and the run: the
       pool holds one block, so a 600-page request needs a second one */
    take_fail_after = 1;
    assert(AROSAppleCodeWindowAllocate(&ops, NULL, 600 * PG) == NULL);
    take_fail_after = (unsigned long)-1;
    assert(lent_count() == 0 && in_use() == 0);
    map_fail_after = maps + 4;
    assert(AROSAppleCodeWindowAllocate(&ops, NULL, 10 * PG) == NULL);
    map_fail_after = (unsigned long)-1;
    assert(lent_count() == 0 && in_use() == 0);
    /* The run is free again: the next request starts where the failed one did */
    a = AROSAppleCodeWindowAllocate(&ops, NULL, PG);
    assert(a == (void *)AROS_APPLE_CODE_WINDOW_BASE && AROSAppleCodeWindowFree(&ops, a, PG));
    for (i = 0; i < (int)WPAGES; i += 4096) assert(mapped[i] == 0);
    /* Fixed addresses */
    a = AROSAppleCodeWindowAllocate(&ops, (void *)(AROS_APPLE_CODE_WINDOW_BASE + 100 * PG), 2 * PG);
    assert(a == (void *)(AROS_APPLE_CODE_WINDOW_BASE + 100 * PG));
    assert(AROSAppleCodeWindowAllocate(&ops, (void *)(AROS_APPLE_CODE_WINDOW_BASE + 101 * PG), PG) == NULL);
    assert(AROSAppleCodeWindowAllocate(&ops, (void *)(AROS_APPLE_CODE_WINDOW_BASE + 100 * PG + 8), PG) == NULL);
    assert(AROSAppleCodeWindowAllocate(&ops, (void *)0x1000, PG) == NULL);
    assert(AROSAppleCodeWindowFree(&ops, a, 2 * PG));
    /* Outside the window: refused */
    assert(!AROSAppleCodeWindowFree(&ops, (void *)0x800000000UL, PG));
    assert(!AROSAppleCodeWindowContains((void *)(AROS_APPLE_CODE_WINDOW_BASE + AROS_APPLE_CODE_WINDOW_BYTES - PG), 2 * PG));
    /* Random sequences against the model */
    srand(11);
    for (step = 0; step < 100000; step++) {
        if (n < 3000 && (rand() % 2 || n == 0)) {
            uintptr_t len = (1 + rand() % 8) * PG - rand() % 100;
            a = AROSAppleCodeWindowAllocate(&ops, NULL, len);
            if (a) { backed(a, len); live[n].a = a; live[n].len = len; n++; }
            else assert(lent_count() > PAGES - 8);
        } else {
            int k = rand() % n;
            assert(AROSAppleCodeWindowFree(&ops, live[k].a, live[k].len));
            live[k] = live[--n];
        }
    }
    while (n) { n--; assert(AROSAppleCodeWindowFree(&ops, live[n].a, live[n].len)); }
    assert(lent_count() == 0 && in_use() == 0);
    /* Every block but the reserve went back to Exec, with its record */
    assert(blocks_held() <= 4 && blocks_taken - blocks_given == blocks_held() && records == blocks_held());
    /* Exec runs out: requests fail cleanly, then everything comes back */
    for (n = 0; n < 3000; n++) {
        live[n].len = 300 * PG;
        live[n].a = AROSAppleCodeWindowAllocate(&ops, NULL, live[n].len);
        if (!live[n].a) break;
    }
    assert(n > 0 && n < 3000 && blocks_taken - blocks_given == MAXBLOCKS);
    assert(in_use() == (uint64_t)n * 300 && lent_count() == (unsigned long)n * 300);
    while (n) { n--; assert(AROSAppleCodeWindowFree(&ops, live[n].a, live[n].len)); }
    assert(lent_count() == 0 && in_use() == 0 && blocks_held() <= 4 &&
        blocks_taken - blocks_given == blocks_held() && records == blocks_held());
    /* An executable page unmapped by a free counts as execute retired */
    {
        struct AROSAppleCodeWindowStats before, after;
        AROSAppleCodeWindowGetStats(&before);
        a = AROSAppleCodeWindowAllocate(&ops, NULL, 3 * PG);
        executable_page[((uintptr_t)a - AROS_APPLE_CODE_WINDOW_BASE) / PG + 1] = 1;
        assert(AROSAppleCodeWindowFree(&ops, a, 3 * PG));
        AROSAppleCodeWindowGetStats(&after);
        assert(after.exec_retired == before.exec_retired + 1 &&
            after.allocation_count == before.allocation_count + 1 &&
            after.free_count == before.free_count + 1);
    }
    /* Reservations: nothing behind them until committed */
    {
        struct AROSAppleCodeWindowStats s;
        char *r, *q;
        unsigned long base_lent = lent_count();
        uint64_t base_use = in_use();

        r = AROSAppleCodeWindowReserve(&ops, NULL, 4000 * PG);
        assert(r != NULL && lent_count() == base_lent && in_use() == base_use);
        AROSAppleCodeWindowGetStats(&s);
        assert(s.pages_reserved == 4000);
        /* A request no longer gets the reserved run */
        q = AROSAppleCodeWindowAllocate(&ops, NULL, PG);
        assert(q != NULL && (q < r || q >= r + 4000 * PG));
        /* Commit three pages, a partial page range included */
        assert(AROSAppleCodeWindowCommit(&ops, r + 10 * PG + 7, 2 * PG) == 1);
        assert(lent_count() == base_lent + 1 + 3 && in_use() == base_use + 1 + 3);
        backed(r + 10 * PG, 3 * PG);
        assert(mapped[((uintptr_t)r - AROS_APPLE_CODE_WINDOW_BASE) / PG + 9] == 0);
        /* Committing again backs nothing more */
        assert(AROSAppleCodeWindowCommit(&ops, r + 10 * PG, 3 * PG) == 1);
        assert(lent_count() == base_lent + 4);
        /* Decommit gives pages back; recommit gives a cleared page */
        assert(AROSAppleCodeWindowDecommit(&ops, r + 11 * PG, PG) == 1);
        assert(lent_count() == base_lent + 3 && in_use() == base_use + 3);
        assert(AROSAppleCodeWindowCommit(&ops, r + 11 * PG, PG) == 1);
        backed(r + 11 * PG, PG);
        /* Not wholly inside a reservation: refused, nothing changes */
        assert(AROSAppleCodeWindowCommit(&ops, r + 3999 * PG, 2 * PG) == -1);
        assert(AROSAppleCodeWindowCommit(&ops, q, PG) == -1);
        assert(AROSAppleCodeWindowDecommit(&ops, q, PG) == 0);
        assert(mapped[((uintptr_t)q - AROS_APPLE_CODE_WINDOW_BASE) / PG] != 0);
        /* Out of memory while committing: refused, what was backed stays */
        take_fail_after = blocks_taken - blocks_given;
        assert(AROSAppleCodeWindowCommit(&ops, r + 100 * PG, 3500 * PG) == 0);
        take_fail_after = (unsigned long)-1;
        /* Fixed addresses inside a reservation are refused */
        assert(AROSAppleCodeWindowReserve(&ops, r + 5 * PG, PG) == NULL);
        assert(AROSAppleCodeWindowAllocate(&ops, r + 5 * PG, PG) == NULL);
        /* Freeing gives every committed page back */
        assert(AROSAppleCodeWindowFree(&ops, r, 4000 * PG));
        assert(!AROSAppleCodeWindowFree(&ops, r, 4000 * PG));
        assert(AROSAppleCodeWindowFree(&ops, q, PG));
        assert(lent_count() == base_lent && in_use() == base_use);
        AROSAppleCodeWindowGetStats(&s);
        assert(s.pages_reserved == 0);
        /* A fixed reservation, then a commit over all of it */
        r = AROSAppleCodeWindowReserve(&ops, (void *)(AROS_APPLE_CODE_WINDOW_BASE + 4096 * PG), 64 * PG);
        assert(r == (char *)(AROS_APPLE_CODE_WINDOW_BASE + 4096 * PG));
        assert(AROSAppleCodeWindowCommit(&ops, r, 64 * PG) == 1);
        backed(r, 64 * PG);
        assert(AROSAppleCodeWindowDecommit(&ops, r, 64 * PG) == 1 && lent_count() == base_lent);
        assert(AROSAppleCodeWindowFree(&ops, r, 64 * PG) && in_use() == base_use);
    }
    /* Foreign runs: the owner's pages, mapped, never the pool's */
    {
        uintptr_t pages[5];
        unsigned long base_lent = lent_count();
        uint64_t base_use = in_use(), w;
        char *f, *g;

        for (i = 0; i < 5; i++) pages[i] = FOREIGN_BASE + (uintptr_t)(4 - i) * 3 * PG;
        f = AROSAppleCodeWindowMapForeign(&ops, pages, 5);
        assert(f != NULL);
        w = ((uintptr_t)f - AROS_APPLE_CODE_WINDOW_BASE) / PG;
        for (i = 0; i < 5; i++) assert(mapped[w + i] == pages[i]);
        assert(lent_count() == base_lent && in_use() == base_use);
        /* Not window memory: no free, commit or decommit applies */
        assert(!AROSAppleCodeWindowFree(&ops, f, 5 * PG));
        assert(AROSAppleCodeWindowCommit(&ops, f, PG) == -1);
        assert(AROSAppleCodeWindowDecommit(&ops, f, PG) == 0);
        /* A request no longer gets the run; a second run follows it */
        g = AROSAppleCodeWindowMapForeign(&ops, pages, 2);
        assert(g == f + 5 * PG);
        /* Only exactly one run unmaps */
        assert(!AROSAppleCodeWindowUnmapForeign(&ops, f, 4));
        assert(!AROSAppleCodeWindowUnmapForeign(&ops, f + PG, 4));
        assert(!AROSAppleCodeWindowUnmapForeign(&ops, f, 7));
        assert(!AROSAppleCodeWindowUnmapForeign(&ops, g, 1));
        assert(AROSAppleCodeWindowUnmapForeign(&ops, f, 5));
        for (i = 0; i < 5; i++) assert(mapped[w + i] == 0);
        assert(!AROSAppleCodeWindowUnmapForeign(&ops, f, 5));
        assert(mapped[w + 5] == pages[0] && mapped[w + 6] == pages[1]);
        assert(AROSAppleCodeWindowUnmapForeign(&ops, g, 2));
        /* The freed run is reused; nothing went to the pool */
        assert(lent_count() == base_lent && in_use() == base_use);
        a = AROSAppleCodeWindowAllocate(&ops, NULL, PG);
        assert(a == f && AROSAppleCodeWindowFree(&ops, a, PG));
        /* Invalid pages and table failures leave nothing behind */
        pages[2] = 0;
        assert(AROSAppleCodeWindowMapForeign(&ops, pages, 5) == NULL);
        pages[2] = FOREIGN_BASE + 1;
        assert(AROSAppleCodeWindowMapForeign(&ops, pages, 5) == NULL);
        pages[2] = FOREIGN_BASE + 6 * PG;
        map_fail_after = maps + 3;
        assert(AROSAppleCodeWindowMapForeign(&ops, pages, 5) == NULL);
        map_fail_after = (unsigned long)-1;
        for (i = 0; i < 5; i++) assert(mapped[w + i] == 0);
        f = AROSAppleCodeWindowMapForeign(&ops, pages, 5);
        assert(f == (char *)(AROS_APPLE_CODE_WINDOW_BASE + w * PG));
        assert(AROSAppleCodeWindowUnmapForeign(&ops, f, 5));
        assert(lent_count() == base_lent && in_use() == base_use);
    }
    printf("CODEWINDOW: PASS (%lu blocks taken from Exec, %lu given back, %lu kept)\n",
        blocks_taken, blocks_given, (unsigned long)blocks_held());
    return 0;
}
