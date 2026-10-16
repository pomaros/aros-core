/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#ifndef AROS_AARCH64_GUARDED_STACK_H
#define AROS_AARCH64_GUARDED_STACK_H
#include <stddef.h>
#include <stdint.h>

struct AArch64GuardedStack {
    void *allocation;
    size_t allocation_size;
    size_t guard_size;
    void *lower;
    void *upper;
};
struct AArch64GuardedStackOps {
    void *context;
    void *(*allocate)(void *, size_t);
    int (*protect)(void *, void *, size_t, int no_access);
    void (*release)(void *, void *, size_t);
};

/* Caller supplies a zero-initialized record and power-of-two page size.
 * The allocation callback returns aligned RW memory. Guard span is distinct
 * from translation granule: 16KB guards can use four 4KB pages.
 * If restoration fails, retain the allocation/record for a later Destroy;
 * inaccessible memory must never be returned to the allocator. */
int AArch64GuardedStackCreate(const struct AArch64GuardedStackOps *,
    struct AArch64GuardedStack *, size_t stack_size, size_t guard_size,
    size_t page_size);
int AArch64GuardedStackDestroy(const struct AArch64GuardedStackOps *,
    struct AArch64GuardedStack *);
#endif
