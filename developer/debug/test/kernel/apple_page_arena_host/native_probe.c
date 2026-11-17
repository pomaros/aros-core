/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1
 * Grow beyond the initial arena, touch every page, free and repeat.
 */
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/kernel.h>
#include <stdint.h>
#include <stdio.h>

APTR KernelBase;

int main(void)
{
    const uintptr_t bytes = 128UL * 1024 * 1024;
    KernelBase = OpenResource("kernel.resource");
    if (!KernelBase) {
        puts("ARENA_NATIVE result=FAIL reason=no-kernel");
        return 20;
    }
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        IPTR before = AvailMem(MEMF_ANY);
        printf("ARENA_NATIVE cycle=%u phase=alloc bytes=%lu free=%lu\n",
            cycle, (unsigned long)bytes, (unsigned long)before);
        fflush(stdout);
        /* No PUBLIC/EXECUTABLE: physical page arena, not the code window. */
        volatile uint64_t *pages = KrnAllocPages(NULL, bytes, MEMF_CLEAR);
        if (!pages) {
            puts("ARENA_NATIVE result=FAIL reason=allocation");
            return 20;
        }
        for (uintptr_t off = 0; off < bytes / 8; off += 4096 / 8) {
            if (pages[off] || pages[off + 511]) {
                KrnFreePages((void *)pages, bytes);
                puts("ARENA_NATIVE result=FAIL reason=not-cleared");
                return 20;
            }
            pages[off] = UINT64_C(0x5a87123400000000) ^ off ^ cycle;
            pages[off + 511] = ~pages[off];
        }
        for (uintptr_t off = 0; off < bytes / 8; off += 4096 / 8) {
            uint64_t expected = UINT64_C(0x5a87123400000000) ^ off ^ cycle;
            if (pages[off] != expected || pages[off + 511] != ~expected) {
                KrnFreePages((void *)pages, bytes);
                puts("ARENA_NATIVE result=FAIL reason=readback");
                return 20;
            }
        }
        printf("ARENA_NATIVE cycle=%u phase=free address=%p free=%lu\n", cycle,
            (void *)pages, (unsigned long)AvailMem(MEMF_ANY));
        fflush(stdout);
        KrnFreePages((void *)pages, bytes);
        printf("ARENA_NATIVE cycle=%u phase=done free=%lu\n", cycle,
            (unsigned long)AvailMem(MEMF_ANY));
        fflush(stdout);
    }
    puts("ARENA_NATIVE result=PASS cycles=3 bytes=134217728 pages_per_cycle=32768");
    return 0;
}
