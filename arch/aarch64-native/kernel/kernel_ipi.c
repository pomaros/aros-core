/*
    Copyright (C) 2015, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
*/

#include <inttypes.h>
#include <aros/kernel.h>
#include <aros/libcall.h>

#include <aros/types/spinlock_s.h>
#include <aros/aarch64/cpucontext.h>

#include <exec/lists.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <proto/exec.h>

#include <kernel_base.h>

/* Pull in exec_platform.h ahead of etask.h so AROS_NO_ATOMIC_OPERATIONS
 * selects the non-atomic FLAG_SCHEDSWITCH_SET expansion (atomics aren't
 * needed for per-CPU TLS state and AROS_ATOMIC_OR isn't declared here). */
#define AROS_NO_ATOMIC_OPERATIONS
#include <exec_platform.h>

#include "etask.h"

#include <kernel_cpu.h>
#include <kernel_intern.h>
#include "kernel_debug.h"
#include "kernel_scheduler.h"

#if defined(__AROSEXEC_SMP__)

#undef D
#define D(x)

#include "kernel_ipi.h"

/*
 * Per-target queue of pending IPIHook calls: the sender claims an entry
 * from the target's free pool, queues it and sends IPI_CALL_HOOK; the
 * target drains its own queue from handle_ipi().
 *
 * Static pools, not AllocMem: Signal() is IRQ-callable and reaches here,
 * where AllocMem could recurse on mh_SpinLock. Async only - signal.c is
 * the sole caller.
 */
struct CallIPIEntry
{
    struct MinNode cie_Node;
    struct IPIHook cie_IPIH;        /* h_Entry + ih_Args - passed as A0 to the hook */
};

#ifndef CORE_IPI_MAX_CPUS
#define CORE_IPI_MAX_CPUS 4
#define CORE_IPI_ONLINE_MASK() 0xfU
#define CORE_IPI_SEND(msg, data, mask) __arm_arosintern.ARMI_SendIPI(msg, data, mask)
#define CORE_IPI_AVAILABLE() (__arm_arosintern.ARMI_SendIPI != NULL)
#endif
#define IPI_CALL_POOL_PER_CPU   128

static struct CallIPIEntry ipi_call_pool[CORE_IPI_MAX_CPUS][IPI_CALL_POOL_PER_CPU];
static struct MinList      ipi_call_free[CORE_IPI_MAX_CPUS];
static struct MinList      ipi_call_queue[CORE_IPI_MAX_CPUS];
static spinlock_t          ipi_call_queue_lock[CORE_IPI_MAX_CPUS];   /* guards both lists for this target */
static BOOL                ipi_call_inited = FALSE;

/* IRQ/FIQ masked: a preempted holder deadlocks a Disable()d waiter */
static inline uint64_t ipiq_lock(int cpu)
{
    uint64_t daif = EXEC_IRQFIQ_DISABLE();

    EXEC_SPINLOCK_LOCK(&ipi_call_queue_lock[cpu], NULL, SPINLOCK_MODE_WRITE);
    return daif;
}

static inline void ipiq_unlock(int cpu, uint64_t daif)
{
    EXEC_SPINLOCK_UNLOCK(&ipi_call_queue_lock[cpu]);
    EXEC_IRQFIQ_RESTORE(daif);
}

/* What the drain is running now, so a canceller can wait out a call it
 * can no longer find queued. Single writer per CPU. */
static volatile APTR       ipi_call_exec_func[CORE_IPI_MAX_CPUS];
static volatile IPTR       ipi_call_exec_arg1[CORE_IPI_MAX_CPUS];

static void core_HandleCallHookIPI(int cpu);

void core_IPIInit(void)
{
    int cpu, i;
    if (ipi_call_inited)
        return;
    for (cpu = 0; cpu < CORE_IPI_MAX_CPUS; cpu++)
    {
        NewMinList(&ipi_call_free[cpu]);
        NewMinList(&ipi_call_queue[cpu]);
        for (i = 0; i < IPI_CALL_POOL_PER_CPU; i++)
            ADDTAIL((struct List *)&ipi_call_free[cpu],
                    (struct Node *)&ipi_call_pool[cpu][i].cie_Node);
        /* spinlock_t zero-initialised at link time == SPINLOCK_UNLOCKED */
    }
    ipi_call_inited = TRUE;
}

/*
 * Claim a free entry for the target, spinning rather than dropping the
 * call - a dropped IPI is a lost wakeup that hangs a task forever.
 *
 * Under Disable() this CPU cannot drain its own inbound queue, so two
 * Disable()d CPUs cross-signalling with empty pools would wait on each
 * other. Break that by draining ours inline while we spin, as also in
 * an interrupt handler (FIQ masked, IDNestCnt unchanged).
 *
 * Call with NO task spinlock held: the drain runs hooks that take them.
 */
/* The save token describes the interrupt state before taking the queue
 * lock. Test it after restoring that state, without a privileged DAIF read:
 * the Apple implementation is also called by Exec at EL0. Native IPIs use
 * FIQ; platforms whose transport uses IRQ override this predicate. */
#ifndef CORE_IPI_MASKED
#define CORE_IPI_MASKED(mask) (((mask) & UINT64_C(0x40)) != 0)
#endif

/* Lifetime-protected senders cannot drain callbacks or wait on other CPUs
 * while holding registry/task locks. Return NULL when the pool is empty. */
struct CallIPIEntry *core_TryClaimCallIPI(int cpu)
{
    struct CallIPIEntry *cie;
    uint64_t daif;

    if (!ipi_call_inited || cpu < 0 || cpu >= CORE_IPI_MAX_CPUS ||
        !(CORE_IPI_ONLINE_MASK() & (1U << cpu)) || !CORE_IPI_AVAILABLE())
        return NULL;
    daif = ipiq_lock(cpu);
    cie = (struct CallIPIEntry *)REMHEAD((struct List *)&ipi_call_free[cpu]);
    ipiq_unlock(cpu, daif);
    return cie;
}

struct CallIPIEntry *core_ClaimCallIPI(int cpu)
{
    struct CallIPIEntry *cie;
    int srcCpu = GetCPUNumber();

    for (;;)
    {
        uint64_t daif = ipiq_lock(cpu);
        cie = (struct CallIPIEntry *)REMHEAD((struct List *)&ipi_call_free[cpu]);
        ipiq_unlock(cpu, daif);

        if (cie)
            return cie;

        if (IDNESTCOUNT_GET >= 0 || CORE_IPI_MASKED(daif))
            core_HandleCallHookIPI(srcCpu);
    }
}

void core_CommitCallIPI(struct CallIPIEntry *cie, int cpu,
                        struct Hook *hook, int nargs, IPTR *args)
{
    int srcCpu = GetCPUNumber();
    int i;
    uint64_t daif;

    if (nargs > IPI_CALL_HOOK_MAX_ARGS)
        nargs = IPI_CALL_HOOK_MAX_ARGS;

    cie->cie_IPIH.ih_Hook = *hook;
    for (i = 0; i < nargs; i++)
        cie->cie_IPIH.ih_Args[i] = args[i];

    daif = ipiq_lock(cpu);
    ADDTAIL((struct List *)&ipi_call_queue[cpu], (struct Node *)&cie->cie_Node);
    ipiq_unlock(cpu, daif);

    if (CORE_IPI_AVAILABLE())
    {
        CORE_IPI_SEND(
            (IPI_CALL_HOOK & 0x0fffffff) | (srcCpu << 28),
            0, 1U << cpu);
    }
}

void core_AbortCallIPI(struct CallIPIEntry *cie, int cpu)
{
    uint64_t daif = ipiq_lock(cpu);

    ADDTAIL((struct List *)&ipi_call_free[cpu], (struct Node *)&cie->cie_Node);
    ipiq_unlock(cpu, daif);
}

/*
 * Cancel queued calls matching (h_Entry, ih_Args[1]) everywhere, then
 * wait out one already executing. On return no CPU will touch the object
 * - provided the caller has made new commits impossible.
 * Run with FIQ masked: we take our own CPU's queue lock.
 */
void core_CancelCallIPIs(APTR hookEntry, IPTR matchArg)
{
    int cpu;

    if (!ipi_call_inited)
        return;

    for (cpu = 0; cpu < CORE_IPI_MAX_CPUS; cpu++)
    {
        struct MinNode *node, *next;
        uint64_t daif = ipiq_lock(cpu);

        for (node = ipi_call_queue[cpu].mlh_Head; (next = node->mln_Succ) != NULL; node = next)
        {
            struct CallIPIEntry *cie = (struct CallIPIEntry *)node;

            if ((APTR)cie->cie_IPIH.ih_Hook.h_Entry == hookEntry &&
                cie->cie_IPIH.ih_Args[1] == matchArg)
            {
                REMOVE((struct Node *)node);
                ADDTAIL((struct List *)&ipi_call_free[cpu], (struct Node *)node);
            }
        }
        ipiq_unlock(cpu, daif);

        while (ipi_call_exec_func[cpu] == hookEntry &&
               ipi_call_exec_arg1[cpu] == matchArg)
        {
            EXEC_MEMORY_BARRIER();
        }
    }
}

int core_DoCallIPI(struct Hook *hook, void *cpu_mask, int async,
                   int nargs, IPTR *args, APTR _KB)
{
    int cpu;
    int srcCpu = GetCPUNumber();
    uint32_t mask;

    (void)_KB;

    if (!hook || !cpu_mask)
        return 0;
    if (nargs > IPI_CALL_HOOK_MAX_ARGS)
        return 0;
    /* Sync mode not yet supported on aarch64-native */
    if (!async)
    {
        D(bug("[Kernel:IPI] %s: sync mode not implemented\n", __PRETTY_FUNCTION__));
        return 0;
    }

    /*
     * TASKAFFINITY_ANY / TASKAFFINITY_ALL_BUT_SELF are sentinels, not
     * buffer pointers - resolve them to bitmaps before dereference.
     */
    if ((IPTR)cpu_mask == TASKAFFINITY_ANY)
        mask = CORE_IPI_ONLINE_MASK();
    else if ((IPTR)cpu_mask == TASKAFFINITY_ALL_BUT_SELF)
        mask = CORE_IPI_ONLINE_MASK() & ~(1U << srcCpu);
    else
        mask = *(uint32_t *)cpu_mask;
    mask &= CORE_IPI_ONLINE_MASK();

    for (cpu = 0; cpu < CORE_IPI_MAX_CPUS; cpu++)
    {
        struct CallIPIEntry *cie;

        if (!(mask & (1U << cpu)))
            continue;

        cie = core_ClaimCallIPI(cpu);
        core_CommitCallIPI(cie, cpu, hook, nargs, args);
    }

    return 1;
}

static void core_HandleCallHookIPI(int cpu)
{
    /* Block dispatch across the drain: the hooks call Enable()/Reschedule(),
     * which would switch tasks from inside this FIQ handler. */
    EXEC_BLOCK_DISPATCH_INC;

    for (;;)
    {
        struct CallIPIEntry *cie;
        uint64_t daif = ipiq_lock(cpu);

        cie = (struct CallIPIEntry *)REMHEAD((struct List *)&ipi_call_queue[cpu]);
        if (cie)
        {
            /* Once dequeued the call is invisible to the canceller's
             * queue scan, so it waits on this instead. */
            ipi_call_exec_func[cpu] = (APTR)cie->cie_IPIH.ih_Hook.h_Entry;
            ipi_call_exec_arg1[cpu] = cie->cie_IPIH.ih_Args[1];
        }
        ipiq_unlock(cpu, daif);

        if (!cie)
            break;

        /* signal_hook's signature: A0 = IPIHook*, A1/A2 unused. */
        AROS_UFC3NR(void, cie->cie_IPIH.ih_Hook.h_Entry,
            AROS_UFCA(struct IPIHook *, &cie->cie_IPIH, A0),
            AROS_UFCA(APTR, NULL, A2),
            AROS_UFCA(APTR, NULL, A1));

        /* Hook side effects visible before a canceller is released. */
        EXEC_MEMORY_BARRIER();
        ipi_call_exec_func[cpu] = NULL;

        daif = ipiq_lock(cpu);
        ADDTAIL((struct List *)&ipi_call_free[cpu], (struct Node *)&cie->cie_Node);
        ipiq_unlock(cpu, daif);
    }

    EXEC_BLOCK_DISPATCH_DEC;
}

void handle_ipi(uint32_t ipi, uint32_t ipi_data)
{
    int cpu = GetCPUNumber();
    uint32_t ipi_src = (ipi >> 28) & 0xF;
    uint32_t ipi_msg = ipi & ~(0xF << 28);

    D(bug("[Kernel:IPI] %s: Core #%02d IPI Msg %08x:%08x from Core #%02d\n",
        __PRETTY_FUNCTION__, cpu, ipi_msg,  ipi_data, ipi_src));

    /* Bit flags: the mailbox SET register OR-coalesces, so one delivery
     * can carry several messages. Test each bit. */
    if (ipi_msg & IPI_CAUSE)
        D(bug("[Kernel:IPI] IPI_CAUSE:\n"));
    if (ipi_msg & IPI_DISPATCH)
        D(bug("[Kernel:IPI] IPI_DISPATCH:\n"));
    if (ipi_msg & IPI_SWITCH)
        D(bug("[Kernel:IPI] IPI_SWITCH:\n"));
    if (ipi_msg & IPI_SCHEDULE)
    {
        D(bug("[Kernel:IPI] IPI_SCHEDULE:\n"));
        /* Flag for reschedule, and expire the quantum: core_Schedule()
         * keeps the running task while its quantum is unused, so an
         * equal-priority task would never preempt an idle core. */
        FLAG_SCHEDQUANTUM_SET;
        FLAG_SCHEDSWITCH_SET;
    }
    if (ipi_msg & IPI_CALL_HOOK)
    {
        D(bug("[Kernel:IPI] IPI_CALL_HOOK:\n"));
        core_HandleCallHookIPI(cpu);
    }
    if (ipi_msg & IPI_CLI)
        D(bug("[Kernel:IPI] IPI_CLI:\n"));
    if (ipi_msg & IPI_STI)
        D(bug("[Kernel:IPI] IPI_STI:\n"));
    if (ipi_msg & IPI_REBOOT)
        D(bug("[Kernel:IPI] IPI_REBOOT:\n"));
    if (ipi_msg & IPI_ADDTASK)
        D(bug("[Kernel:IPI] IPI_ADDTASK:\n"));
    if (ipi_msg & IPI_REMTASK)
        D(bug("[Kernel:IPI] IPI_REMTASK:\n"));
}
#endif
