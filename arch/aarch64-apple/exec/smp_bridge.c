/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Keep upstream private calls intact without cross-module ELF imports. */
#include <aros/debug.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include "smp_api.h"
#include "tls.h"
struct CallIPIEntry *core_ClaimCallIPI(int cpu) { return AROSAppleExecSMPAPIGet()->claim(cpu); }
void core_CommitCallIPI(struct CallIPIEntry *entry,int cpu,struct Hook *hook,int nargs,IPTR *args)
{ AROSAppleExecSMPAPIGet()->commit(entry,cpu,hook,nargs,args); }
void core_AbortCallIPI(struct CallIPIEntry *entry,int cpu) { AROSAppleExecSMPAPIGet()->abort(entry,cpu); }
void core_CancelCallIPIs(APTR hook,IPTR arg) { AROSAppleExecSMPAPIGet()->cancel(hook,arg); }
struct CallIPIEntry *core_TryClaimCallIPI(int cpu) { return AROSAppleExecSMPAPIGet()->try_claim(cpu); }

/*
 * A message port's lock is taken with IRQ and FIQ masked, from tasks and
 * from interrupts (the timer's FIQ replies to timer requests). A port built
 * by hand without exec's initialisation (SDL2's ports, before they were
 * fixed) holds garbage there that reads as a held lock, and an unbounded
 * spin then stops the CPU that owns the timer with every interrupt masked:
 * the whole machine stops. No port is held for long, so after 50 ms the
 * lock is reported once per port address on the kernel log, with what is
 * known of the port and the caller, and taken: the machine goes on and the
 * report names the port.
 */
static struct MsgPort *last_reported;
static unsigned long port_lock_forced;

void AROSAppleExecPortLock(struct MsgPort *port)
{
    const struct AROSAppleExecSMPAPI *api = AROSAppleExecSMPAPIGet();
    spinlock_t *lock = &port->mp_SpinLock;
    uint64_t start, now, hz;

    if (api->spin_trylock(lock, SPINLOCK_MODE_WRITE))
        return;
    __asm__ volatile("isb; mrs %0,cntvct_el0; mrs %1,cntfrq_el0" : "=r"(start), "=r"(hz));
    for (;;) {
        __asm__ volatile("yield");
        if (api->spin_trylock(lock, SPINLOCK_MODE_WRITE))
            return;
        __asm__ volatile("isb; mrs %0,cntvct_el0" : "=r"(now));
        if (now - start > hz / 20)
            break;
    }
    ++port_lock_forced;
    if (port != last_reported) {
        last_reported = port;
        bug("[EXEC] port lock: port %p held 50 ms (lock 0x%08x owner %p), taken: "
            "sigtask %p sigbit %d flags %d node type %d, caller %p, task %p, forced %lu\n",
            port, (unsigned)lock->lock, lock->s_Owner, port->mp_SigTask, (int)port->mp_SigBit,
            (int)port->mp_Flags, (int)port->mp_Node.ln_Type, __builtin_return_address(0),
            TLS_GET(ThisTask), port_lock_forced);
    }
    __atomic_store_n(&lock->lock, 0x80000000U, __ATOMIC_SEQ_CST);
    lock->s_Owner = TLS_GET(ThisTask);
}
