/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: The Apple code arena allocator (kernel/page_allocator.c, compiled
          as is) on the host: every block it hands out is free, a request
          fails only when no run is long enough, page tables come from the
          top and other requests first fit from the bottom (a segment's
          hunks stay adjacent), and a session that mixes lifetimes still
          leaves room for 2 MiB blocks.
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
#define PG 4096UL
#define NPAGES 65536UL               /* a 256 MiB arena */
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
int main(int argc, char **argv)
{
    int scenario_only = argc > 1;
    struct { void *p; uintptr_t len; } live[4000];
    int nlive = 0, i, step;
    void *pt, *big, *small;
    int ok2m;

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
        /* Random sequences: every block returned is free in the model, and a
           request fails only when the model has no run long enough. */
        srand(7);
        for (step = 0; step < 200000; step++) {
            if (nlive < 4000 && (rand() % 3 || nlive == 0)) {
                uintptr_t pages = rand() % 5 == 0 ? 1 + rand() % 2000 : 1 + rand() % 40;
                int internal = rand() % 10 == 0;
                void *p = internal ? AROSApplePageTableAllocate(PG) : AROSApplePageAllocate(NULL, pages * PG, 0);
                if (internal) pages = 1;
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
    return 0;
}
