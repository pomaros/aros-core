/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Save and restore Apple AArch64 task contexts.
 */

#include <aros/apple/runtime.h>

#include <exec/execbase.h>
#include <exec/tasks.h>
#include <hardware/intbits.h>
#include <aros/aarch64/cpucontext.h>

#include "kernel_cpu.h"
#include "jit_protect.h"
#include "platform_cpu.h"
#include "cpu_monitor.h"
#include "kernel_intr.h"
#include "kernel_scheduler.h"
#include "kernel_intern.h"
#if defined(__AROSEXEC_SMP__)
#include "../exec/tls.h"
#include "smp.h"
#endif
#include <etask.h>
#include "task_accounting.h"
#include "task_tls.h"
int AROSAppleRestrictedExecutionCheck(struct Task *, struct AROSAppleTaskExecution *);
#include "restricted.h"

extern int AROSAppleKernelDebugPutDirect(int chr);

#define TASK_SPSR_ALLOWED UINT64_C(0xf00003c0)
#define TASK_SPSR_IRQ_MASK UINT64_C(0x80)
#define TASK_SPSR_FIQ_MASK UINT64_C(0x40)

/*
 * The one rule for a task's I and F bits, applied wherever the kernel returns
 * to a task: masked while the task is inside Disable() (its nesting count is
 * at or above zero), otherwise per the current IRQ and FIQ readiness. The
 * saved SPSR never decides, so a task first dispatched before the timer armed
 * cannot keep F for good, and a task that continues without a switch (the
 * same task after Reschedule() or an interrupt) is treated like a dispatched
 * one.
 */
void cpu_ApplyTaskMasks(uint64_t *spsr, int nesting)
{
    *spsr = (*spsr & ~(TASK_SPSR_IRQ_MASK | TASK_SPSR_FIQ_MASK)) |
        AROSAppleRequiredTaskMasks(AROSAppleRuntimeLocal(), nesting);
}

/*
 * The running task's masks for a frame about to return to it. Only a frame
 * that returns to EL0t is a task's: a SysCall or interrupt may have replaced
 * the frame with the idle context (enter_idle, EL1h, its own masks) while
 * the vector still says lower EL, and ThisTask then names a sleeping task.
 */
void cpu_ApplyCurrentTaskMasks(uint64_t *spsr)
{
    /* Early EL1 interrupts have no task TLS yet. */
    if ((*spsr & UINT64_C(0x1f)) != 0)
        return;
#if defined(__AROSEXEC_SMP__)
    struct Task *task = TLS_GET(ThisTask);
    int nesting = TLS_GET(IDNestCnt);
#else
    struct Task *task = SysBase != NULL ? GET_THIS_TASK : NULL;
    int nesting = SysBase != NULL ? SysBase->IDNestCnt : -1;
#endif

    if (task == NULL)
        return;
    cpu_ApplyTaskMasks(spsr, nesting);
}

uint64_t cpu_RequiredCurrentTaskMasks(void)
{
#if defined(__AROSEXEC_SMP__)
    int nesting = AROSCPUCurrentIndex() != AROS_CPU_MAX && TLS_GET(ThisTask) != NULL ?
        TLS_GET(IDNestCnt) : -1;
#else
    int nesting = SysBase != NULL && SysBase->ThisTask != NULL ?
        SysBase->IDNestCnt : -1;
#endif
    return AROSAppleRequiredIRQMasks(AROSAppleRuntimeLocal(), nesting);
}

static _Noreturn void dispatch_contract_halt(void)
{
    for (;;) {
        __asm__ volatile("wfe");
    }
}

static struct ExceptionContext *task_context(struct Task *task)
{
    struct ETask *etask;

    if (task == NULL || (task->tc_Flags & TF_ETASK) == 0)
        return NULL;
    etask = task->tc_UnionETask.tc_ETask;
    if (etask == NULL)
        return NULL;
    return etask->et_RegFrame;
}

#if defined(__AROSEXEC_SMP__)
/*
 * Whether the frame may lie inside a multi-instruction access to this CPU's
 * Exec state (task_tls.h). Only a system call is known not to: the SVC is a
 * boundary no such access spans. Trusted code interrupted anywhere else may
 * hold this CPU's block address. Restricted code has no access to it.
 */
static int frame_resumes_here(const regs_t *regs)
{
    uint64_t trusted = AROSAppleRuntimeLocal()->trusted_ttbr0;

    if (trusted != 0 && regs->entry_ttbr0 != trusted)
        return 0;
    /* Lower-EL synchronous vector, exception class SVC from AArch64. */
    return !(regs->vector == 8 && ((regs->esr >> 26) & 0x3f) == 0x15);
}
#endif

/* The frame into the task's saved context; 0 when it has none. */
int cpu_SaveContext(regs_t *regs, struct Task *task)
{
    struct ExceptionContext *context = task_context(task);
    uint32_t index;

    if (regs == NULL || context == NULL || context->fpuContext == NULL)
        return 0;
    AROSAppleRestrictedCheckStack(regs, task);
    /* x0-x28, x18 included: native x18 is ordinary task state. */
    for (index = 0; index < 29; ++index)
        context->x[index] = regs->x[index];
    context->fp = regs->x[29];
    context->lr = regs->x[30];
    context->sp = regs->sp;
    context->pc = regs->elr;
    context->cpsr = (uint32_t)regs->spsr;
    context->Flags |= ECF_FPU;
#if defined(__AROSEXEC_SMP__)
    AROSAppleTaskResumeCPUSet(context, frame_resumes_here(regs) ? GetCPUNumber() : -1);
#endif
    for (index = 0; index < 66; ++index)
        ((volatile uint64_t *)context->fpuContext)[index] = regs->simd[index];
    if (AROSAppleTaskExecution(context)->root)
        AROSAppleTaskExecution(context)->user_tls = regs->task_tls;
    task->tc_SPReg = (APTR)(uintptr_t)regs->sp;
    return 1;
}

void cpu_Switch(regs_t *regs)
{
    struct Task *task = SysBase != NULL ? GET_THIS_TASK : NULL;
#if defined(__AROSEXEC_SMP__)
    if (TLS_GET(TDNestCnt)>=0)
        bug("[SCHED] switching forbidden task=%p td=%ld pc=%llx lr=%llx esr=%llx\n",
            task,(long)TLS_GET(TDNestCnt),(unsigned long long)regs->elr,
            (unsigned long long)regs->x[30],(unsigned long long)regs->esr);
    AROSAppleDispatchTransition(1);
    BOOL wait_lock_held = task != NULL && task->tc_State == TS_WAIT;
#endif

    if (!cpu_SaveContext(regs, task))
        dispatch_contract_halt();
#if !defined(__AROSEXEC_SMP__)
    AppleTaskSwitch(GetIntETask(task),AppleTaskCounter(),AppleTaskFrequency());
#endif
    core_Switch();
#if defined(__AROSEXEC_SMP__)
    /* Wait carries this lock across KrnSwitch. Publish the complete saved
     * context, including nesting and quantum, before a remote Signal can
     * make the task runnable again. */
    if (wait_lock_held)
        EXEC_SPINLOCK_UNLOCK(&task->tc_SpinLock);
#endif
}

/*
 * The idle context.
 *
 * cpu_Dispatch runs inside an exception taken from a task, on the kernel
 * stack, with every interrupt masked by the exception entry. When no task is
 * ready, the only thing that can make one ready is an interrupt, above all
 * the timer FIQ that wakes timer.device clients. Waiting here with interrupts
 * masked therefore waits forever: the first moment every task sleeps, the
 * system never comes back (seen as a desktop freeze on a window-drag press,
 * when the one busy task blocked on the layers lock).
 *
 * Instead the dispatcher returns to a two-instruction EL1 loop that waits for
 * an interrupt with the ready lines unmasked. An interrupt taken from that
 * loop arrives on a current-EL vector; its handler calls cpu_IdleInterrupted,
 * which dispatches directly. It must never take the ordinary exit path:
 * ThisTask still names the last task, which is waiting, and cpu_Switch would
 * save the idle registers over that task's context.
 */
__asm__(
    ".text\n"
    ".balign 16\n"
    ".global AROSAppleKernelIdleLoop\n"
    ".type AROSAppleKernelIdleLoop, %function\n"
    "AROSAppleKernelIdleLoop:\n"
    "1:  wfi\n"
    "    b 1b\n"
    ".size AROSAppleKernelIdleLoop, . - AROSAppleKernelIdleLoop\n");
extern void AROSAppleKernelIdleLoop(void);

#if defined(__AROSEXEC_SMP__) && defined(AROS_VM_GUEST) && AROS_VM_GUEST
/*
 * Idle polling, for a virtual machine.
 *
 * Under a hypervisor WFI leaves the guest: the virtual CPU's host thread
 * sleeps, and waking it costs a host thread wake-up, then the interrupt
 * controller accesses of the IPI on both CPUs, each one more exit. A
 * restricted domain's thread and its controller call each other across two
 * CPUs thousands of times a second, and every call paid that wake-up.
 *
 * So an idle CPU first watches a word of its own for a short while. A CPU
 * that makes a task ready for it stores IDLE_KICKED there instead of sending
 * the interrupt (cpu_IdleKick); the loop sees it and enters the kernel with
 * an SVC, which dispatches exactly as an interrupt taken from the idle loop
 * does. When the watch expires the loop halts the word and falls into the
 * WFI loop, and wakes cost what they did before.
 *
 * No wake is lost: the dispatcher arms the word, then looks at the ready
 * list once more before it returns to the loop, and a waker queues its task
 * before it looks at the word.
 */
#define IDLE_POLLING_AVAILABLE 1
/* 2 ms: a restricted domain's calls come in bursts a few milliseconds apart.
 * With 200 us most wakes found the CPU halted and paid the IPI; a page load
 * sent 2,200 a second and the IPI write alone took 30% of a CPU. */
#define IDLE_POLL_US UINT64_C(2000)
#define IDLE_KICK_SVC 0xa5f0
enum { IDLE_HALTED, IDLE_POLLING, IDLE_KICKED };
static struct { volatile uint32_t state; } __attribute__((aligned(64)))
    idle_poll[AROS_APPLE_SMP_MAX_CPUS];
/* x0 = this CPU's word, x1 = virtual counter value that ends the watch. */
__asm__(
    ".text\n"
    ".balign 16\n"
    ".type AROSAppleKernelIdlePoll, %function\n"
    "AROSAppleKernelIdlePoll:\n"
    "1:  ldr w2, [x0]\n"
    "    cmp w2, #2\n"
    "    b.eq 3f\n"
    "    yield\n"
    "    mrs x3, cntvct_el0\n"
    "    cmp x3, x1\n"
    "    b.lo 1b\n"
    "2:  ldxr w2, [x0]\n"
    "    cmp w2, #1\n"
    "    b.ne 3f\n"
    "    stxr w3, wzr, [x0]\n"
    "    cbnz w3, 2b\n"
    "    b AROSAppleKernelIdleLoop\n"
    "3:  clrex\n"
    "    svc #0xa5f0\n"
    "    b AROSAppleKernelIdleLoop\n"
    ".size AROSAppleKernelIdlePoll, . - AROSAppleKernelIdlePoll\n");
extern void AROSAppleKernelIdlePoll(void);
_Static_assert(IDLE_KICKED == 2 && IDLE_POLLING == 1 && IDLE_HALTED == 0 &&
    IDLE_KICK_SVC == 0xa5f0, "the idle poll loop is written with these values");

/* The frame returns to the idle loop: make it the watching one. */
static void idle_poll_arm(regs_t *regs)
{
    unsigned cpu = AROSCPUCurrentIndex();
    uint32_t halted = IDLE_HALTED;
    uint64_t now, frequency;

    if (cpu >= AROS_APPLE_SMP_MAX_CPUS)
        return;
    __atomic_compare_exchange_n(&idle_poll[cpu].state, &halted, IDLE_POLLING, 0,
        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(now));
    regs->x[0] = (uint64_t)(uintptr_t)&idle_poll[cpu].state;
    regs->x[1] = now + frequency * IDLE_POLL_US / UINT64_C(1000000);
    regs->elr = (uint64_t)(uintptr_t)AROSAppleKernelIdlePoll;
}
/* A task was selected: nobody may count on this CPU's watch any more. */
static void idle_poll_disarm(void)
{
    unsigned cpu = AROSCPUCurrentIndex();

    if (cpu < AROS_APPLE_SMP_MAX_CPUS)
        __atomic_store_n(&idle_poll[cpu].state, IDLE_HALTED, __ATOMIC_SEQ_CST);
}
int cpu_IdleKick(unsigned cpu)
{
    uint32_t state = IDLE_POLLING;

    if (cpu >= AROS_APPLE_SMP_MAX_CPUS)
        return 0;
    return __atomic_compare_exchange_n(&idle_poll[cpu].state, &state, IDLE_KICKED, 0,
        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) || state == IDLE_KICKED;
}
int cpu_IdleInterrupted(regs_t *regs);
int cpu_IdleKicked(regs_t *regs)
{
    if (regs == NULL || regs->vector != 4 ||
        ((regs->esr >> 26) & UINT64_C(0x3f)) != UINT64_C(0x15) ||
        (regs->esr & UINT64_C(0xffff)) != IDLE_KICK_SVC)
        return 0;
    /* Refused when this CPU is not idle: nothing but the loop issues it. */
    if (!cpu_IdleInterrupted(regs))
        return 0;
    cpu_ApplyCurrentTaskMasks(&regs->spsr);
    return 1;
}
#else
int cpu_IdleKick(unsigned cpu) { (void)cpu; return 0; }
int cpu_IdleKicked(regs_t *regs) { (void)regs; return 0; }
#endif

/* 0 decide at the next entry, 1 plain WFI, 2 deep WFI. The desktop hold
 * writes it to compare the two (chosen aros,idle-ab). */
volatile uint32_t AROSAppleKernelIdleMode;
/* Non-zero while a sleep holds the secondary cores' ticks (smp.c). */
volatile uint32_t AROSAppleKernelTicksHeld;

/* Entries into the idle loop, for observers. */
volatile uint64_t AROSAppleKernelIdleEntries;

/* EL1h with SP_EL1; debug and SError masked; IRQ and FIQ per readiness. */
#define IDLE_SPSR_EL1H UINT64_C(0x305)

/* The SPSR the last dispatched task resumed with; read by the coldstart
 * proof right after a wait to check the mask rule. */
volatile uint64_t AROSAppleKernelLastDispatchSPSR;

volatile uint32_t AROSAppleKernelIdleTraceEnable;
/* The direct debug sink is not a multicore console. Keep optional idle
 * diagnostics on the BSP, with single-owner counters and no AP interleaving. */
static int idle_trace_owner(void)
{
#if defined(__AROSEXEC_SMP__)
    return AROSCPUCurrentIndex() == AROSCPUMonitorTopology()->boot_cpu;
#else
    return 1;
#endif
}
static uint32_t enter_idle_trace;
static void idle_trace_puts(const char *text);
static void idle_trace_hex(const char *label, uint64_t value);

static void enter_idle(regs_t *regs)
{
    uint32_t index;

    /* Idle is not the last sleeping task and must not retain its TCB. */
    AROSAArch64WriteTaskPointer(0);
    regs->task_tls = 0;
    AROSAppleRuntimeLocal()->kernel_tls = 0;
    if (AROSAppleRuntimeLocal()->trusted_ttbr0)
        regs->return_ttbr0 = AROSAppleRuntimeLocal()->trusted_ttbr0;
    /* No task owns an idle core's EL0 JIT write permission. A task that
     * yielded inside its write scope regains it only on its own dispatch. */
    AROSAppleJitRestore(NULL);
    for (index = 0; index < 31; ++index)
        regs->x[index] = 0;
    /* EL1h returns keep SP_EL1 as it is after the frame pops: the kernel
     * stack top. The loop uses no stack; the next exception frames below. */
    if (AROSAppleKernelIdleMode != 1 && AROSAppleKernelIdleMode != 2)
        AROSAppleKernelIdleMode = AROSPlatformDefaultIdleMode();
    ++AROSAppleKernelIdleEntries;
    regs->elr = AROSPlatformIdleEntry(AROSAppleKernelIdleMode);
    regs->spsr = IDLE_SPSR_EL1H;
    if (AROSAppleRuntimeLocal()->irq_ready == 0)
        regs->spsr |= TASK_SPSR_IRQ_MASK;
    if (AROSAppleRuntimeLocal()->fiq_ready == 0)
        regs->spsr |= TASK_SPSR_FIQ_MASK;
    AROSAppleRuntimeLocal()->idle_active = 1;
#if defined(__AROSEXEC_SMP__)
    TLS_SET(ThisTask, NULL);
    TLS_SET(IDNestCnt, -1);
    TLS_SET(TDNestCnt, -1);
#endif
    AROSCPUCurrentState(AROS_CPU_IDLE);
    if (idle_trace_owner() && AROSAppleKernelIdleTraceEnable != 0 && enter_idle_trace < 12) {
        struct Task *this = SysBase != NULL ? GET_THIS_TASK : NULL;
        uint64_t ctl, cval, now;

        ++enter_idle_trace;
        __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(ctl));
        __asm__ volatile("mrs %0, cntv_cval_el0" : "=r"(cval));
        __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(now));
        idle_trace_hex("[KT] enter-idle fiq_ready=0x", AROSAppleRuntimeLocal()->fiq_ready);
        idle_trace_hex(" irq_ready=0x", AROSAppleRuntimeLocal()->irq_ready);
        idle_trace_hex(" spsr=0x", regs->spsr);
        idle_trace_hex(" cntv_ctl=0x", ctl);
        idle_trace_hex(" cval=0x", cval);
        idle_trace_hex(" now=0x", now);
        idle_trace_hex(" idnest=0x", SysBase != NULL ?
            (uint64_t)(uint8_t)IDNESTCOUNT_GET : 0);
        idle_trace_puts(" this=");
        idle_trace_puts(this != NULL && this->tc_Node.ln_Name != NULL ?
            this->tc_Node.ln_Name : "?");
        AROSAppleKernelDebugPutDirect('\n');
    }
}

/* Bounded trace of what an interrupt taken from the idle loop dispatched. */
static uint32_t idle_trace;

static void idle_trace_puts(const char *text)
{
    while (*text != 0)
        AROSAppleKernelDebugPutDirect(*text++);
}

static void idle_trace_hex(const char *label, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    idle_trace_puts(label);
    for (shift = 60; shift >= 0; shift -= 4)
        AROSAppleKernelDebugPutDirect(digits[(value >> shift) & 0xfU]);
}

#if defined(__AROSEXEC_SMP__)
int cpu_SoftIntAllowed(void)
{
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
    return 1;
#else
    return AROSCPUCurrentIndex() == AROSCPUMonitorTopology()->boot_cpu;
#endif
}
#endif
#ifndef CPU_SOFTINT_ALLOWED
#define CPU_SOFTINT_ALLOWED() 1
#endif

int cpu_IdleInterrupted(regs_t *regs)
{
    uint32_t refused = 0;

    /* Current-EL vectors are 0..7; the idle loop runs at EL1h (4..7). */
    if (regs == NULL || regs->vector >= 8)
        return 0;
    if (AROSAppleRuntimeLocal()->idle_active == 0)
        refused = 1;
    if (!refused) {
        AROSCPUCurrentState(AROS_CPU_BUSY);
        /* Software interrupts raised by the handler run before any task
         * does, exactly as core_ExitInterrupt orders them for a task. */
        if (CPU_SOFTINT_ALLOWED() && SysBase != NULL && (SysBase->SysFlags & SFF_SoftInt))
            core_Cause(INTB_SOFTINT, 1L << INTB_SOFTINT);
        cpu_Dispatch(regs);
    }
    if (idle_trace_owner() && idle_trace < 12) {
        struct Task *this = SysBase != NULL ? GET_THIS_TASK : NULL;

        ++idle_trace;
        idle_trace_hex("[KT] idle-int vector=0x", regs->vector);
        idle_trace_hex(" refused=0x", refused);
        idle_trace_hex(" dispatched=0x", refused ? 0 : (AROSAppleRuntimeLocal()->idle_active == 0));
#if !defined(__AROSEXEC_SMP__)
        idle_trace_hex(" ready_nonempty=0x", SysBase != NULL &&
            SysBase->TaskReady.lh_Head->ln_Succ != NULL);
#endif
        idle_trace_hex(" spsr=0x", regs->spsr);
        idle_trace_puts(" this=");
        idle_trace_puts(this != NULL && this->tc_Node.ln_Name != NULL ?
            this->tc_Node.ln_Name : "?");
        idle_trace_hex(" state=0x", this != NULL ? this->tc_State : 0);
        AROSAppleKernelDebugPutDirect('\n');
    }
    return refused ? 0 : 1;
}

#if defined(__AROSEXEC_SMP__)
#include "self_exit.h"
extern int AROSAppleSelfExitDetached(void *);
#endif

void cpu_Dispatch(regs_t *regs)
{
    struct ExceptionContext *context;
    struct Task *task;
    uint32_t index;
#if defined(__AROSEXEC_SMP__)
    AROSAppleDispatchTransition(1);
    void *departing = TLS_GET(ThisTask);
    unsigned self_exit_rescanned = 0;
#endif
#if defined(IDLE_POLLING_AVAILABLE)
    unsigned idle_poll_rescanned = 0;
#endif

    if (regs == NULL)
        dispatch_contract_halt();
#if defined(__AROSEXEC_SMP__)
dispatch_after_self_exit:
#endif
    task = core_Dispatch();
    if (task == NULL) {
        enter_idle(regs);
#if defined(__AROSEXEC_SMP__)
        int detached = AROSAppleSelfExitDetached(departing);
        if (detached == APPLE_EXIT_REFUSED)
            dispatch_contract_halt();
        if (detached == APPLE_EXIT_PUBLISHED) {
            /* Signal with no ThisTask only enqueues a local service task.
             * Inspect readiness once, without relying on a timer/IPI wake.
             * enter_idle has removed every outgoing Task/TLS reference. */
            if (self_exit_rescanned)
                dispatch_contract_halt();
            self_exit_rescanned = 1;
            departing = NULL;
            goto dispatch_after_self_exit;
        }
#if defined(IDLE_POLLING_AVAILABLE)
        /* Arm the watch, then look once more: a task queued before the
         * word said "polling" was not announced through it. */
        idle_poll_arm(regs);
        if (!idle_poll_rescanned) {
            idle_poll_rescanned = 1;
            departing = NULL;
            goto dispatch_after_self_exit;
        }
#endif
        AROSAppleDispatchTransition(0);
#endif
        return;
    }
    AROSAppleRuntimeLocal()->idle_active = 0;
#if defined(IDLE_POLLING_AVAILABLE)
    idle_poll_disarm();
#endif
#if defined(__AROSEXEC_SMP__)
    GetIntETask(task)->iet_CpuNumber = GetCPUNumber();
#endif
    AROSCPUCurrentState(AROS_CPU_BUSY);
    context = task_context(task);
    if (context == NULL || context->sp == 0 || context->pc == 0 ||
        (context->Flags & ECF_FPU) == 0 || context->fpuContext == NULL)
        dispatch_contract_halt();
    for (index = 0; index < 29; ++index)
        regs->x[index] = context->x[index];
    regs->x[29] = context->fp;
    regs->x[30] = context->lr;
    regs->sp = context->sp;
    regs->elr = context->pc;
    /* The masks never come from the task's saved SPSR: a task first
     * dispatched before the timer armed (the Initial CLI, every
     * startup-script process) would otherwise keep F set for good and never
     * see the timer FIQ it sleeps on. They come from the task's own nesting
     * first: a task that did Disable() and then Wait() resumes with both
     * lines masked, as core_Dispatch restores its IDNestCnt (Cli masks I and
     * F alike here); otherwise from the current readiness, as enter_idle and
     * the Sti SVC do. */
    regs->spsr = (uint64_t)context->cpsr & TASK_SPSR_ALLOWED;
    for (index = 0; index < 66; ++index)
        regs->simd[index] =
            ((const volatile uint64_t *)context->fpuContext)[index];
    /* A controller may close a domain after its last save while this task
     * waits on a scheduler list. Validate the restored frame after dispatch
     * released the task lock, before choosing its return root and TLS. The
     * cancellation path replaces the frame with the retained native return. */
    AROSAppleRestrictedCheckStack(regs, task);
    cpu_ApplyTaskMasks(&regs->spsr, task->tc_IDNestCnt);
    AROSAppleKernelLastDispatchSPSR = regs->spsr;
    task->tc_SPReg = (APTR)(uintptr_t)regs->sp;
    AROSAppleJitRestore(task);
#if !defined(__AROSEXEC_SMP__)
    AppleTaskDispatch(GetIntETask(task),AppleTaskCounter());
#endif
    /* core_Dispatch has selected this task on this CPU: its TLS anchor's
     * address, the same on every CPU for the task's whole life. */
    AROSAppleTaskTLSRestore(context);
    regs->task_tls = AROSAArch64ReadTaskPointer();
    AROSAppleRuntimeLocal()->kernel_tls = regs->task_tls;
    if (AROSAppleRuntimeLocal()->trusted_ttbr0)
        regs->return_ttbr0 = AROSAppleRuntimeLocal()->trusted_ttbr0;
    if (AROSAppleTaskExecution(context)->root &&
        AROSAppleRestrictedExecutionCheck(task, AROSAppleTaskExecution(context))) {
        regs->return_ttbr0 = AROSAppleTaskExecution(context)->root;
        regs->task_tls = AROSAppleTaskExecution(context)->user_tls;
    }
#if defined(__AROSEXEC_SMP__)
    if (!AROSAppleSelfExitDetached(departing))
        dispatch_contract_halt();
    AROSAppleDispatchTransition(0);
#endif
}
