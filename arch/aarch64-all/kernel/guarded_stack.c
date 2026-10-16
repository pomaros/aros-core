/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include "guarded_stack.h"

int AArch64GuardedStackDestroy(const struct AArch64GuardedStackOps *ops,
    struct AArch64GuardedStack *stack)
{
    if (!ops || !ops->protect || !ops->release || !stack)
        return 0;
    if (!stack->allocation)
        return 1;
    if (!ops->protect(ops->context, stack->allocation, stack->guard_size, 0))
        return 0;
    ops->release(ops->context, stack->allocation, stack->allocation_size);
    *stack = (struct AArch64GuardedStack){0};
    return 1;
}

int AArch64GuardedStackCreate(const struct AArch64GuardedStackOps *ops,
    struct AArch64GuardedStack *stack, size_t size, size_t guard, size_t page)
{
    size_t total;
    void *base;
    if (!ops || !ops->allocate || !ops->protect || !ops->release || !stack ||
        stack->allocation || !size || !guard || !page || (page & (page - 1)) ||
        size > SIZE_MAX - (page - 1) || guard > SIZE_MAX - (page - 1))
        return 0;
    size = (size + page - 1) & ~(page - 1);
    guard = (guard + page - 1) & ~(page - 1);
    if (size > SIZE_MAX - guard)
        return 0;
    total = size + guard;
    base = ops->allocate(ops->context, total);
    if (!base)
        return 0;
    if (((uintptr_t)base & (page - 1)) || (uintptr_t)base > UINTPTR_MAX - total) {
        ops->release(ops->context, base, total);
        return 0;
    }
    stack->allocation = base;
    stack->allocation_size = total;
    stack->guard_size = guard;
    stack->lower = (void *)((uintptr_t)base + guard);
    stack->upper = (void *)((uintptr_t)base + total);
    if (!ops->protect(ops->context, base, guard, 1)) {
        (void)AArch64GuardedStackDestroy(ops, stack);
        return 0;
    }
    return 1;
}
