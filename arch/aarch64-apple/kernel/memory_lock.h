/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_APPLE_MEMORY_LOCK_H
#define AROS_APPLE_MEMORY_LOCK_H
#include <stdint.h>
#include <aros/apple/runtime.h>

/* Non-recursive, zero-initialized. Mask local preemption before acquiring:
 * an interrupt or a switched-in task must not wait for its own CPU's lock.
 * No allocation, device I/O or blocking Exec call is allowed while held. */
struct AROSAppleMemoryLock { uint32_t held; };
static inline uint64_t AROSAppleMemoryLockAcquire(struct AROSAppleMemoryLock *lock)
{
    uint64_t saved=AROSAppleRuntimeIRQSave();
    while (__atomic_exchange_n(&lock->held,1,__ATOMIC_ACQUIRE))
        while (__atomic_load_n(&lock->held,__ATOMIC_RELAXED))
            __asm__ volatile("yield" ::: "memory");
    return saved;
}
static inline void AROSAppleMemoryLockRelease(struct AROSAppleMemoryLock *lock,
                                             uint64_t saved)
{
    __atomic_store_n(&lock->held,0,__ATOMIC_RELEASE);
    AROSAppleRuntimeIRQRestore(saved);
}
#endif
