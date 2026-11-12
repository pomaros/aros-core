/*
    Copyright (C) 2015-2026, The AROS Development Team. All rights reserved.
*/

#include <aros/types/spinlock_s.h>
#include <aros/kernel.h>
#include <aros/libcall.h>

#include <asm/cpu.h>

#include <kernel_base.h>

#include <proto/kernel.h>

#include "spinlock_wait.h"

AROS_LH1(void, KrnSpinUnLock,
        AROS_LHA(spinlock_t *, lock, A0),
        struct KernelBase *, KernelBase, 53, Kernel)
{
    AROS_LIBFUNC_INIT

    unsigned int lock_value, new_value, write_result;

    if (lock->lock == 0)
        return;

    /* The exclusive store with release semantics publishes the critical
     * section. The event goes out only when a waiter announced itself on
     * the lock this release frees (spinlock_wait.h); the dsb keeps the sev
     * from overtaking the store. */
    if (lock->lock & SPINLOCKF_WRITE)
    {
        lock->s_Owner = NULL;
        asm volatile(
                "1:     ldxr    %w0, [%2]       \n\t"   // The word we free, with its waiting bit
                "       stlxr   %w1, wzr, [%2]  \n\t"   // Free the lock
                "       cbnz    %w1, 1b         \n\t"   // A waiter announced itself meanwhile - read it again
                : "=&r"(lock_value), "=&r"(write_result)
                : "r"(&lock->lock)
                : "memory"
        );
    }
    else
    {
        asm volatile(
                "1:     ldxr    %w0, [%3]       \n\t"   // Read lock value and gain exclusive access
                "       sub     %w1, %w0, #1    \n\t"   // One reader less
                "       tst     %w1, #0xffffff  \n\t"   // Any reader left (SPINLOCK_READERS)?
                "       csel    %w1, wzr, %w1, eq \n\t" // None: the lock is free, the waiting bit goes with it
                "       stlxr   %w2, %w1, [%3]  \n\t"   // Try to update the lock value
                "       cbnz    %w2, 1b         \n\t"   // Try again if the write failed
                : "=&r"(lock_value), "=&r"(new_value), "=&r"(write_result)
                : "r"(&lock->lock)
                : "memory", "cc"
        );
        if (new_value != 0)
            return;
    }

    if (lock_value & SPINLOCKF_WAITING)
    {
        dsb();
        sev();
    }

    AROS_LIBFUNC_EXIT
}
