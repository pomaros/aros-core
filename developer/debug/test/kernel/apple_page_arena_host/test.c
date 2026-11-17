/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: The Apple page arena allocator (kernel/page_allocator.c, compiled
          as is) on the host: every block it hands out is free, a request
          fails only when no run is long enough, page tables come from the
          top and other requests first fit from the bottom (a segment's
          hunks stay adjacent), and a session that mixes lifetimes still
          leaves room for 2 MiB blocks. Growth: a request that fits no
          segment takes a chunk through the growth operations, the chunk
          is arena only once attach publishes it, what attach carves is
          allocated and freeable, and a chunk nothing uses goes back.
          With an argument, only the session runs (to compare allocators).
*/
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int AROSApplePageAllocatorInitialize(uintptr_t start, uint64_t size);
void *AROSApplePageAllocate(void *requested, uintptr_t length, uint32_t flags);
void *AROSApplePageTableAllocate(uintptr_t length);
int AROSApplePageFreeRaw(void *address, uintptr_t length);
#ifdef ARENA_GROWTH                  /* not in a --compare build of an older allocator */
#include "page_allocator.h"
#include <exec/memory.h>
#define HAVE_GROWTH 1
#endif
#define PG 4096UL
#define NPAGES 16384UL               /* a 64 MiB arena, stage two's largest */
uint64_t AROSAppleRuntimeIRQSave(void) { return 0; }
void AROSAppleRuntimeIRQRestore(uint64_t mask) { (void)mask; }
static uintptr_t base;
static unsigned char model[NPAGES];  /* independent record of what was handed out */
static void take(void *p, uintptr_t len)
{
    uintptr_t i, first = ((uintptr_t)p - base) / PG, n = (len + PG - 1) / PG;
    assert(((uintptr_t)p - base) % PG == 0 && first + n <= NPAGES);
    for (i = 0; i < n; i++) { assert(!model[first + i]); model[first + i] = 1; }
}
static void give(void *p, uintptr_t len)
{
    uintptr_t i, first = ((uintptr_t)p - base) / PG, n = (len + PG - 1) / PG;
    assert(AROSApplePageFreeRaw(p, len));
    for (i = 0; i < n; i++) model[first + i] = 0;
}
static int longest(void)
{
    int best = 0, run = 0; unsigned long i;
    for (i = 0; i < NPAGES; i++) { run = model[i] ? 0 : run + 1; if (run > best) best = run; }
    return best;
}
static uintptr_t first_fit(uintptr_t pages)
{
    uintptr_t run = 0;
    for (uintptr_t i = 0; i < NPAGES; ++i) {
        run = model[i] ? 0 : run + 1;
        if (run == pages) return base + (i + 1 - pages) * PG;
    }
    return 0;
}

#ifdef HAVE_GROWTH
/* Growth through fake operations: chunks from the host heap, attach as the
 * kernel's: it may carve records first, then publishes. */
#define BLOCK (2UL * 1024 * 1024)
static int takes, gives, attaches, retries_left, fail_attach, carve_domains, detach_refused;
static uintptr_t taken[64]; static uint64_t taken_bytes[64];
static void *carved[64]; static uint32_t carved_pages_n[64]; static int ncarved;
static int fake_can_grow(void) { return 1; }
static uintptr_t fake_take(uint64_t bytes)
{
    void *p = aligned_alloc(BLOCK, bytes);
    assert(p && takes < 64);
    taken[takes] = (uintptr_t)p; taken_bytes[takes] = bytes; ++takes;
    return (uintptr_t)p;
}
static void fake_give(uintptr_t start, uint64_t bytes)
{
    int i;
    for (i = 0; i < takes; i++) if (taken[i] == start) { assert(taken_bytes[i] == bytes); taken[i] = 0; }
    ++gives; free((void *)start);
}
static int fake_attach(struct AROSApplePageArenaGrowth *g)
{
    uint32_t b; int d;
    ++attaches;
    assert(g->blocks * BLOCK >= 32UL * 1024 * 1024 && (g->start & (BLOCK - 1)) == 0);
    /* Not arena before the publication */
    assert(!AROSApplePageArenaContains(g->start + BLOCK));
    for (b = 0; b < g->blocks; b++) {
        g->saved[b] = 0x1000 + b;
        g->tables[b * 512] = 3;        /* the tables are writable chunk pages */
    }
    if (retries_left) { --retries_left; return -1; }
    if (fail_attach) return 0;
    for (d = 0; d < carve_domains; d++) {
        void *record = AROSApplePageArenaCarve(g, 1, 0);
        void *tables = AROSApplePageArenaCarve(g, g->blocks, 1);
        assert(record && tables && ncarved + 2 <= 64);
        assert((uintptr_t)record >= g->start && (uintptr_t)tables >= g->start);
        carved[ncarved] = record; carved_pages_n[ncarved++] = 1;
        carved[ncarved] = tables; carved_pages_n[ncarved++] = g->blocks;
    }
    AROSApplePageArenaPublish(g);
    assert(AROSApplePageArenaContains(g->start + BLOCK));
    return 1;
}
static int fake_detach(uintptr_t start, uint32_t blocks, const uint64_t *saved)
{
    uint32_t b;
    for (b = 0; b < blocks; b++) assert(saved[b] == 0x1000 + b);
    (void)start;
    return !detach_refused;
}
static const struct AROSApplePageArenaOps fake_ops = {
    fake_can_grow, fake_take, fake_give, fake_attach, fake_detach
};
static void growth_test(void)
{
    struct AROSApplePageAllocatorStats st;
    uintptr_t first = (uintptr_t)aligned_alloc(PG, 1024 * PG), chunk_a, chunk_b;
    void *fill, *big, *more, *table, *huge;
    uint32_t pages0;
    int i;

    assert(first && AROSApplePageAllocatorInitialize(first, 1024 * PG));
    AROSApplePageArenaSetOps(&fake_ops);
    pages0 = AROSApplePageArenaPages();
    /* The stage-two part serves first; with few pages left free the arena
     * grows ahead of need, after a retry the attach asked for */
    retries_left = 1;
    fill = AROSApplePageAllocate(NULL, 1024 * PG, 0);
    assert(fill == (void *)first && takes == 1 && attaches == 2);
    chunk_a = taken[0];
    assert(AROSApplePageArenaContains(chunk_a + BLOCK));
    assert(!AROSApplePageArenaContains(chunk_a + taken_bytes[0]));
    assert(AROSApplePageArenaPages() == pages0 + taken_bytes[0] / PG);
    /* The chunk's own records are never handed out or freed */
    assert(!AROSApplePageFreeRaw((void *)chunk_a, PG));
    /* A page table: the stage-two part is full, so the chunk's top */
    table = AROSApplePageTableAllocate(PG);
    assert((uintptr_t)table == chunk_a + taken_bytes[0] - PG);
    big = AROSApplePageAllocate(NULL, 3000 * PG, MEMF_CLEAR);
    assert(big && (uintptr_t)big >= chunk_a && (uintptr_t)big + 3000 * PG <= chunk_a + taken_bytes[0]);
    /* A request larger than the free run takes a larger chunk; a chunk with
     * domains to give leaves to carries their records, allocated */
    carve_domains = 3;
    more = AROSApplePageAllocate(NULL, 20000 * PG, 0);
    assert(more && takes == 2 && ncarved == 6);
    chunk_b = taken[1];
    assert(taken_bytes[1] >= 20000 * PG && (uintptr_t)more >= chunk_b);
    AROSApplePageAllocatorGetStats(&st);
    assert(st.internal_pages_in_use == 1 + 3 * (taken_bytes[1] / BLOCK));
    /* A failed attach leaves no arena behind and gives the chunk back */
    fail_attach = 1; carve_domains = 0;
    assert(!AROSApplePageAllocate(NULL, 100000 * PG, 0));
    assert(takes == 3 && gives == 1 && taken[2] == 0);
    fail_attach = 0;
    /* A chunk goes back only when nothing in it is used, carved records
     * included, and enough stays free without it */
    assert(AROSApplePageFreeRaw(more, 20000 * PG));
    AROSApplePageArenaTrim();
    assert(gives == 1);
    for (i = 0; i < ncarved; i++)
        assert(AROSApplePageFreeRaw(carved[i], carved_pages_n[i] * PG));
    AROSApplePageArenaTrim();
    assert(gives == 1 && AROSApplePageArenaContains(chunk_b));
    assert(AROSApplePageFreeRaw(big, 3000 * PG));
    assert(AROSApplePageFreeRaw(table, PG));
    AROSApplePageArenaTrim();
    assert(gives == 2 && taken[0] == 0 && !AROSApplePageArenaContains(chunk_a + BLOCK));
    /* A refused detach keeps the chunk arena; another one may go instead */
    huge = AROSApplePageAllocate(NULL, 40000 * PG, 0);
    assert(huge && takes == 4);
    uintptr_t chunk_c = taken[3];
    assert(AROSApplePageFreeRaw(huge, 40000 * PG));
    detach_refused = 1;
    AROSApplePageArenaTrim();
    assert(gives == 2 && AROSApplePageArenaContains(chunk_b) &&
        AROSApplePageArenaContains(chunk_c));
    detach_refused = 0;
    AROSApplePageArenaTrim();
    assert(gives == 3 && AROSApplePageArenaContains(chunk_b) !=
        AROSApplePageArenaContains(chunk_c));
    assert(AROSApplePageFreeRaw(fill, 1024 * PG));
    AROSApplePageAllocatorGetStats(&st);
    assert(st.pages_in_use == 0 && st.internal_pages_in_use == 0);
    printf("ARENA: growth (%d chunks taken, %d given back) PASS\n", takes, gives);
    AROSApplePageArenaSetOps(NULL);
}
#endif
int main(int argc, char **argv)
{
    int scenario_only = argc > 1;
    struct { void *p; uintptr_t len; } live[4000];
    int nlive = 0, i, step;
    void *pt, *big, *small;
    int ok2m;

    /* Metadata-only high-page coverage: no backing is touched without CLEAR.
       Exercise addresses beyond 4 GiB and the capacity boundary. */
    uintptr_t synthetic = UINT64_C(0x900000000);
    uint64_t capacity = NPAGES * PG;
    assert(AROSApplePageAllocatorInitialize(synthetic, capacity));
    void *high = (void *)(synthetic + capacity / 2);
    void *last = (void *)(synthetic + capacity - PG);
    assert(AROSApplePageAllocate(high, PG, 0) == high);
    assert(AROSApplePageAllocate(last, PG, 0) == last);
    assert(!AROSApplePageAllocate(last, 2 * PG, 0));
    assert(!AROSApplePageAllocate((void *)(synthetic + capacity), PG, 0));
    assert(AROSApplePageFreeRaw(high, PG));
    assert(AROSApplePageFreeRaw(last, PG));
    assert(!AROSApplePageAllocatorInitialize(synthetic, capacity + PG));
    puts("ARENA: high-page capacity and bounds PASS");

    base = (uintptr_t)aligned_alloc(PG, NPAGES * PG);
    assert(base && AROSApplePageAllocatorInitialize(base, NPAGES * PG));
    if (!scenario_only) {
        /* Placement */
        pt = AROSApplePageTableAllocate(PG); take(pt, PG);
        small = AROSApplePageAllocate(NULL, 8 * PG, 0); take(small, 8 * PG);
        big = AROSApplePageAllocate(NULL, 512 * PG, 0); take(big, 512 * PG);
        printf("page table at page %lu, small at %lu, 2 MiB at %lu\n",
            ((uintptr_t)pt - base) / PG, ((uintptr_t)small - base) / PG, ((uintptr_t)big - base) / PG);
        assert(((uintptr_t)pt - base) / PG == NPAGES - 1);
        assert(((uintptr_t)small - base) / PG == 0);          /* first fit */
        assert(((uintptr_t)big - base) / PG == 8);            /* right after it */
        give(pt, PG); give(small, 8 * PG); give(big, 512 * PG);
        /* A full arena, then holes below its occupied prefix. A failed
           large request must not hide a smaller free run from first fit. */
        big = AROSApplePageAllocate(NULL, NPAGES * PG, 0);
        assert(big == (void *)base); take(big, NPAGES * PG);
        assert(!AROSApplePageAllocate(NULL, PG, 0));
        give((void *)(base + 7 * PG), 2 * PG);
        assert(!AROSApplePageAllocate(NULL, 3 * PG, 0));
        small = AROSApplePageAllocate(NULL, PG, 0);
        assert(small == (void *)(base + 7 * PG)); take(small, PG);
        small = AROSApplePageAllocate((void *)(base + 8 * PG), PG, 0);
        assert(small == (void *)(base + 8 * PG)); take(small, PG);
        give(big, NPAGES * PG);
        /* Random sequences: every block returned is free in the model, and a
           request fails only when the model has no run long enough. */
        srand(7);
        for (step = 0; step < 200000; step++) {
            if (nlive < 4000 && (rand() % 3 || nlive == 0)) {
                uintptr_t pages = rand() % 5 == 0 ? 1 + rand() % 2000 : 1 + rand() % 40;
                int internal = rand() % 10 == 0;
                void *p = internal ? AROSApplePageTableAllocate(PG) : AROSApplePageAllocate(NULL, pages * PG, 0);
                if (internal) pages = 1;
                if (!internal) assert((uintptr_t)p == first_fit(pages));
                if (p) { take(p, pages * PG); live[nlive].p = p; live[nlive].len = pages * PG; nlive++; }
                else assert(longest() < (int)pages);
            } else {
                int k = rand() % nlive;
                give(live[k].p, live[k].len);
                live[k] = live[--nlive];
            }
        }
        while (nlive) { nlive--; give(live[nlive].p, live[nlive].len); }
        /* Fixed address: only that exact run */
        big = AROSApplePageAllocate((void *)(base + 100 * PG), 10 * PG, 0); assert(big == (void *)(base + 100 * PG)); take(big, 10 * PG);
        assert(AROSApplePageAllocate((void *)(base + 105 * PG), PG, 0) == NULL);
        give(big, 10 * PG);
        printf("ARENA: random and placement PASS\n");
    }
    /* A session: a big program's hunks fill the arena while drivers add
       page tables and small long-lived blocks; then most hunks are freed
       (the program quits, about 40 % stays in use) and 2 MiB JIT blocks
       are asked for, as V8 does. */
    {
        static void *h[4000]; static uintptr_t hl[4000]; int n = 0, used = 0, k;
        for (n = 0; n < 4000; n++) {
            hl[n] = (64 + (n % 7) * 32) * PG;
            h[n] = AROSApplePageAllocate(NULL, hl[n], 0);
            if (!h[n]) break;
            if (n % 3 == 0) AROSApplePageTableAllocate(PG);
            if (n % 5 == 0) AROSApplePageAllocate(NULL, 2 * PG, 0);
        }
        for (k = 0; k < n; k++) if (k % 5 >= 2) { AROSApplePageFreeRaw(h[k], hl[k]); h[k] = NULL; } else used += hl[k] / PG;
        ok2m = 0;
        for (i = 0; i < 40; i++) if (AROSApplePageAllocate(NULL, 512 * PG, 0)) ok2m++;
        printf("filled with %d hunks, %d%% kept; 2 MiB requests satisfied: %d of 40\n", n, (int)(100.0 * used / NPAGES), ok2m);
    }
#ifdef HAVE_GROWTH
    if (!scenario_only)
        growth_test();
#endif
    return 0;
}
