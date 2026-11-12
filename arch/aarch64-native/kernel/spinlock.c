/*
    Copyright (C) 2015-2026, The AROS Development Team. All rights reserved.
*/

#include <aros/types/spinlock_s.h>
#include <aros/kernel.h>
#include <aros/libcall.h>
#include <utility/hooks.h>

#include <asm/cpu.h>

#include <kernel_base.h>

#include "tls.h"
#include "spinlock_wait.h"

#include <proto/kernel.h>

/* The lock word and the waiting protocol: spinlock_wait.h. */
AROS_LH3(spinlock_t *, KrnSpinLock,
        AROS_LHA(spinlock_t *, lock, A1),
        AROS_LHA(struct Hook *, failhook, A0),
        AROS_LHA(ULONG, mode, D0),
        struct KernelBase *, KernelBase, 52, Kernel)
{
    AROS_LIBFUNC_INIT

    unsigned int lock_value, result;

    if (mode == SPINLOCK_MODE_WRITE)
    {
        asm volatile(
                "1:     ldaxr   %w0, [%2]       \n\t"   // Load the lock value, gaining exclusive access
                "       cbnz    %w0, 2f         \n\t"   // Held? Announce ourselves and wait
                "       stxr    %w1, %w3, [%2]  \n\t"   // Try to write the write-locked value
                "       cbnz    %w1, 1b         \n\t"   // Exclusive access lost - re-examine the lock
                "       b       5f              \n\t"
                "2:     tbnz    %w0, #28, 3f    \n\t"   // Somebody already waits (SPINLOCKB_WAITING)
                "       orr     %w0, %w0, #0x10000000 \n\t"
                "       stxr    %w1, %w0, [%2]  \n\t"   // Set the waiting bit on the lock we saw held
                "       cbnz    %w1, 1b         \n\t"   // The word changed - re-examine the lock
                "       b       4f              \n\t"
                "3:     clrex                   \n\t"
                "4:     wfe                     \n\t"   // Until the release that frees the lock
                "       b       1b              \n\t"
                "5:                             \n\t"
                : "=&r"(lock_value), "=&r"(result)
                : "r"(&lock->lock), "r"(SPINLOCKF_WRITE)
                : "memory"
        );

        /* Without an owner a leaked lock is just a set bit. */
        lock->s_Owner = TLS_GET(ThisTask);
    }
    else
    {
        asm volatile(
                "1:     ldaxr   %w0, [%2]       \n\t"   // Load the lock value, gaining exclusive access
                "       tbnz    %w0, #27, 2f    \n\t"   // Write-held (SPINLOCKB_WRITE)? Announce ourselves and wait
                "       add     %w0, %w0, #1    \n\t"   // One more reader
                "       stxr    %w1, %w0, [%2]  \n\t"   // Try to write the new reader count
                "       cbnz    %w1, 1b         \n\t"   // Exclusive access lost - re-examine the lock
                "       b       5f              \n\t"
                "2:     tbnz    %w0, #28, 3f    \n\t"   // Somebody already waits (SPINLOCKB_WAITING)
                "       orr     %w0, %w0, #0x10000000 \n\t"
                "       stxr    %w1, %w0, [%2]  \n\t"   // Set the waiting bit on the lock we saw held
                "       cbnz    %w1, 1b         \n\t"   // The word changed - re-examine the lock
                "       b       4f              \n\t"
                "3:     clrex                   \n\t"
                "4:     wfe                     \n\t"   // Until the release that frees the lock
                "       b       1b              \n\t"
                "5:                             \n\t"
                : "=&r"(lock_value), "=&r"(result)
                : "r"(&lock->lock)
                : "memory"
        );
    }

    return lock;

    AROS_LIBFUNC_EXIT
}
