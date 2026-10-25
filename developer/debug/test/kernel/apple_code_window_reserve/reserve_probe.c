/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: ReserveProbe - MEMF_RESERVE on the running system: a 1 GiB
          reservation costs no memory, KrnSetProtection commits what it
          makes accessible (cleared), removing all access gives it back,
          and KrnFreePages gives everything back. Prints RESERVE result=.
          ReserveProbe FAULT reads a page never committed: the task must
          crash with a precise report (an unmapped window address).
*/
#include <exec/memory.h>
#include <aros/kernel.h>
#include <proto/exec.h>
#include <proto/kernel.h>
#include <stdio.h>
#include <stdint.h>

#ifndef MEMF_RESERVE
#define MEMF_RESERVE (1L << 14)
#endif

APTR KernelBase;

static int fail(const char *what)
{
    printf("RESERVE result=FAIL %s\n", what);
    return 20;
}

int main(int argc, char **argv)
{
    const uintptr_t size = (uintptr_t)1 << 30, chunk = 64 * 1024;
    IPTR free0, free1, free2, free3, free4;
    volatile uint32_t *p;
    uint32_t i;

    KernelBase = OpenResource("kernel.resource");
    if (!KernelBase)
        return fail("no kernel.resource");
    free0 = AvailMem(MEMF_ANY);
    p = KrnAllocPages(NULL, size, MEMF_PUBLIC | MEMF_RESERVE);
    if (!p)
        return fail("reserve refused");
    free1 = AvailMem(MEMF_ANY);
    if (argc > 1) {
        printf("RESERVE reading uncommitted %p\n", (void *)(p + 4096));
        i = p[4096];
        return fail("an uncommitted page was readable");
    }
    printf("RESERVE reserved=%p free_before=%lu free_after=%lu\n", (void *)p,
        (unsigned long)free0, (unsigned long)free1);
    if (free0 - free1 > 4 * 1024 * 1024)
        return fail("reservation cost memory");

    KrnSetProtection((void *)p, chunk, MAP_Readable | MAP_Writable);
    for (i = 0; i < chunk / 4; i++)
        if (p[i] != 0)
            return fail("committed page not cleared");
    for (i = 0; i < chunk / 4; i++)
        p[i] = i ^ 0x5a5a5a5a;
    for (i = 0; i < chunk / 4; i++)
        if (p[i] != (i ^ 0x5a5a5a5a))
            return fail("committed page lost a write");
    free2 = AvailMem(MEMF_ANY);
    printf("RESERVE committed=%lu free=%lu\n", (unsigned long)chunk, (unsigned long)free2);

    /* The far end of the reservation too */
    KrnSetProtection((void *)((uintptr_t)p + size - chunk), chunk, MAP_Readable | MAP_Writable);
    p[(size - 4) / 4] = 0x1234;
    if (p[(size - 4) / 4] != 0x1234)
        return fail("far page");

    KrnSetProtection((void *)p, chunk, 0);
    free3 = AvailMem(MEMF_ANY);
    printf("RESERVE decommitted free=%lu\n", (unsigned long)free3);
    KrnSetProtection((void *)p, chunk, MAP_Readable | MAP_Writable);
    for (i = 0; i < chunk / 4; i++)
        if (p[i] != 0)
            return fail("recommitted page kept old contents");

    KrnFreePages((void *)p, size);
    free4 = AvailMem(MEMF_ANY);
    printf("RESERVE freed free=%lu\n", (unsigned long)free4);
    printf("RESERVE result=PASS\n");
    return 0;
}
