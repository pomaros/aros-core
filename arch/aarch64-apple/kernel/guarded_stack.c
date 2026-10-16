/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include <aros/kernel.h>
#include <exec/memory.h>
#include "page_allocator.h"

static void *stack_allocate(void *context, size_t size)
{
    (void)context;
    return AROSApplePageAllocate(NULL, size, MEMF_CLEAR);
}
static int stack_protect(void *context, void *base, size_t size, int no_access)
{
    (void)context;
    return AROSApplePageProtect(base, size,
        no_access ? 0 : MAP_Readable | MAP_Writable);
}
static void stack_release(void *context, void *base, size_t size)
{
    (void)context;
    (void)AROSApplePageFreeRaw(base, size);
}
static const struct AArch64GuardedStackOps stack_ops = {
    NULL, stack_allocate, stack_protect, stack_release
};

int AROSAppleGuardedStackCreate(struct AArch64GuardedStack *stack, size_t size)
{
    return AArch64GuardedStackCreate(&stack_ops, stack, size, 16384,
        AROS_APPLE_PAGE_SIZE);
}
int AROSAppleGuardedStackDestroy(struct AArch64GuardedStack *stack)
{
    return AArch64GuardedStackDestroy(&stack_ops, stack);
}

#include "../../aarch64-all/kernel/guarded_stack.c"
