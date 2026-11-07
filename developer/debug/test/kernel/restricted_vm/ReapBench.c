/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/kernel.h>
#include <proto/exec.h>
#include <aros/apple/restricted.h>
#include <aros/debug.h>
#include <stdint.h>
APTR KernelBase;
static uint64_t counter(void)
{
    uint64_t value;
    __asm__ volatile("isb; mrs %0,cntvct_el0":"=r"(value)::"memory");
    return value;
}
int main(void)
{
    KernelBase=OpenResource((CONST_STRPTR)"kernel.resource");
    if (!KernelBase) return 20;
    intptr_t address=KrnGetSystemAttr(AROS_RESTRICTED_KATTR_API);
    if (!address || address==-1) return 20;
    const struct AROSRestrictedAPI *api=(const void *)address;
    if (api->version!=36 || api->size!=sizeof(*api)) return 20;
    uint64_t frequency;
    __asm__ volatile("mrs %0,cntfrq_el0":"=r"(frequency));
    if (!frequency) return 20;
    struct AROSRestrictedLifetime life;
    api->lifetime(&life);
    if (life.live || life.domains || life.pages || life.shared_objects) return 20;
    uint64_t start=counter();
    for (unsigned i=0;i<10000;++i) api->lifetime(&life);
    uint64_t elapsed=counter()-start;
    if (life.live || life.domains || life.pages || life.shared_objects) return 20;
    bug("[REAPBENCH] idle_lifetime_calls=10000 ticks=%llu frequency=%llu elapsed_us=%llu resources=0\n",
        (unsigned long long)elapsed,(unsigned long long)frequency,
        (unsigned long long)(elapsed*1000000/frequency));
    return 0;
}
