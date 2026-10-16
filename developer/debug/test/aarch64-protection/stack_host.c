/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include "guarded_stack.h"
#include <assert.h>

static unsigned releases, allocations, fail_guard, fail_restore;
static void *allocate(void *context, size_t size)
{
    (void)context;
    assert(size == 81920);
    ++allocations;
    return (void *)(uintptr_t)0x10000;
}
static int protect(void *context, void *base, size_t size, int no_access)
{
    (void)context;
    assert((uintptr_t)base == 0x10000 && size == 16384);
    return no_access ? !fail_guard : !fail_restore;
}
static void release(void *context, void *base, size_t size)
{
    (void)context;
    assert((uintptr_t)base == 0x10000 && size == 81920 && !fail_restore);
    ++releases;
}
void test_guarded_stack(void)
{
    const struct AArch64GuardedStackOps ops = {NULL, allocate, protect, release};
    struct AArch64GuardedStack stack = {0};
    assert(AArch64GuardedStackCreate(&ops, &stack, 65535, 16383, 4096));
    assert((uintptr_t)stack.lower == 0x14000 && (uintptr_t)stack.upper == 0x24000);
    fail_restore = 1;
    assert(!AArch64GuardedStackDestroy(&ops, &stack));
    assert(stack.allocation && releases == 0);
    fail_restore = 0;
    assert(AArch64GuardedStackDestroy(&ops, &stack));
    assert(!stack.allocation && releases == 1);
    fail_guard = 1;
    assert(!AArch64GuardedStackCreate(&ops, &stack, 65536, 16384, 16384));
    assert(!stack.allocation && releases == 2);
    fail_restore = 1;
    assert(!AArch64GuardedStackCreate(&ops, &stack, 65536, 16384, 4096));
    assert(stack.allocation && releases == 2);
    fail_restore = 0;
    assert(AArch64GuardedStackDestroy(&ops, &stack));
    assert(releases == 3);
    assert(!AArch64GuardedStackCreate(&ops, &stack, SIZE_MAX, 16384, 4096));
    assert(!AArch64GuardedStackCreate(&ops, &stack, 65536, 16384, 3));
    assert(allocations == 3);
}
