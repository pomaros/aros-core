/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    How a contended spinlock waits, shared by KrnSpinLock and KrnSpinUnLock.
*/
#ifndef AARCH64_SPINLOCK_WAIT_H
#define AARCH64_SPINLOCK_WAIT_H

#include <aros/types/spinlock_s.h>

/*
 * Lock word: 0 free, SPINLOCKF_WRITE (bit 27) write-held, otherwise the
 * reader count in bits 0-23. Bit 28 says that somebody waits for the lock.
 *
 * An acquisition tries the lock first and waits only when it is held. A
 * waiter sets the waiting bit, in the same exclusive update that saw the
 * lock held, and then parks in wfe. The release that frees the lock clears
 * the whole word in one exclusive update and sends the event only when the
 * word it replaced had the bit: the uncontended path executes no wfe, no dsb
 * and no sev.
 *
 * No wake-up is lost. The bit is set only while the lock is held and is
 * cleared only by the release that frees it, which sends the event. A waiter
 * that saw the bit, or set it, did so before that release replaced the word,
 * so the event comes after and stays in the Event Register until the wfe
 * consumes it; an exception return in between sets the register as well. A
 * waiter that comes after the release finds the word 0 and takes the lock.
 * A free lock is therefore always exactly 0, which KrnSpinTryLock,
 * KrnSpinIsLocked and the scheduler's own attempt rely on.
 */
#define SPINLOCKB_WAITING       28
#define SPINLOCKF_WAITING       (1 << SPINLOCKB_WAITING)
#define SPINLOCK_READERS        0x00ffffff

#endif
