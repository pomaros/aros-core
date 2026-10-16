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
#include "cpu_monitor.h"
#include "kernel_intr.h"
#include "kernel_scheduler.h"
#include "kernel_intern.h"
#include <etask.h>
#include "task_accounting.h"

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
    struct Task *task = SysBase != NULL ? GET_THIS_TASK : NULL;
    int nesting = SysBase != NULL ? SysBase->IDNestCnt : -1;

    if (task == NULL)
        return;
    cpu_ApplyTaskMasks(spsr, nesting);
}

uint64_t cpu_RequiredCurrentTaskMasks(void)
{
    int nesting = SysBase != NULL && SysBase->ThisTask != NULL ?
        SysBase->IDNestCnt : -1;
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

void cpu_Switch(regs_t *regs)
{
    struct Task *task = SysBase != NULL ? GET_THIS_TASK : NULL;
    struct ExceptionContext *context = task_context(task);
    uint32_t index;

    if (regs == NULL || context == NULL)
        dispatch_contract_halt();
    /* x0-x28, x18 included: native x18 is ordinary task state. */
    for (index = 0; index < 29; ++index)
        context->x[index] = regs->x[index];
    context->fp = regs->x[29];
    context->lr = regs->x[30];
    context->sp = regs->sp;
    context->pc = regs->elr;
    context->cpsr = (uint32_t)regs->spsr;
    context->Flags |= ECF_FPU;
    if (context->fpuContext == NULL)
        dispatch_contract_halt();
    for (index = 0; index < 66; ++index)
        ((volatile uint64_t *)context->fpuContext)[index] = regs->simd[index];
    task->tc_SPReg = (APTR)(uintptr_t)regs->sp;
    AppleTaskSwitch(GetIntETask(task),AppleTaskCounter(),AppleTaskFrequency());
    core_Switch();
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

/*
 * The deep idle loop: Apple cores power down in WFI when the
 * implementation-defined CYC_OVRD register (s3_5_c15_c5_0) has WFI_MODE,
 * bits 25:24, set to 3; m1n1 leaves it at 2, a plain clock-gated WFI. The
 * core keeps SP and wakes on an interrupt like the plain loop; its SIMD
 * state is not kept, which nothing here needs: the interrupted task's
 * context was saved before the dispatcher came here, and the idle loop has
 * none. Linux (Asahi cpuidle-apple) and OpenBSD idle the same way. It is
 * the default on an Apple core (MIDR implementer 0x61); chosen
 * aros,plain-idle = 1 keeps the plain loop, and any other core (QEMU), where
 * the register does not exist, always gets the plain loop.
 */
__asm__(
    ".text\n"
    ".balign 16\n"
    ".global AROSAppleKernelDeepIdleLoop\n"
    ".type AROSAppleKernelDeepIdleLoop, %function\n"
    "AROSAppleKernelDeepIdleLoop:\n"
    "    mrs  x0, s3_5_c15_c5_0\n"
    "    orr  x0, x0, #(3 << 24)\n"
    "    msr  s3_5_c15_c5_0, x0\n"
    "    isb\n"
    "1:  dsb  sy\n"
    "    wfi\n"
    "    b 1b\n"
    ".size AROSAppleKernelDeepIdleLoop, . - AROSAppleKernelDeepIdleLoop\n");
extern void AROSAppleKernelDeepIdleLoop(void);

/* The plain mode on an Apple core: WFI_MODE back to 2, m1n1's clock-gated
 * WFI. The register keeps whatever was written last, so the plain loop
 * alone would go on powering down once the deep loop has run. */
__asm__(
    ".text\n"
    ".balign 16\n"
    ".global AROSAppleKernelGatedIdleLoop\n"
    ".type AROSAppleKernelGatedIdleLoop, %function\n"
    "AROSAppleKernelGatedIdleLoop:\n"
    "    mrs  x0, s3_5_c15_c5_0\n"
    "    bic  x0, x0, #(3 << 24)\n"
    "    orr  x0, x0, #(2 << 24)\n"
    "    msr  s3_5_c15_c5_0, x0\n"
    "    isb\n"
    "1:  dsb  sy\n"
    "    wfi\n"
    "    b 1b\n"
    ".size AROSAppleKernelGatedIdleLoop, . - AROSAppleKernelGatedIdleLoop\n");
extern void AROSAppleKernelGatedIdleLoop(void);
extern uint64_t AROSAppleKernelChosenDecimal(const char *name);

/* 0 decide at the next entry, 1 plain WFI, 2 deep WFI. The desktop hold
 * writes it to compare the two (chosen aros,idle-ab). */
volatile uint32_t AROSAppleKernelIdleMode;

static int apple_core(void)
{
    /* HVF can expose Apple's MIDR while trapping implementation registers.
     * CPU identity does not grant a guest access to native power controls. */
    return 0;
}

static uint32_t default_idle_mode(void)
{
    if (!apple_core())
        return 1;
    return AROSAppleKernelChosenDecimal("aros,plain-idle") == 1 ? 1 : 2;
}
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
    return 1;
}
static uint32_t enter_idle_trace;
static void idle_trace_puts(const char *text);
static void idle_trace_hex(const char *label, uint64_t value);

static void enter_idle(regs_t *regs)
{
    uint32_t index;

    for (index = 0; index < 31; ++index)
        regs->x[index] = 0;
    /* EL1h returns keep SP_EL1 as it is after the frame pops: the kernel
     * stack top. The loop uses no stack; the next exception frames below. */
    if (AROSAppleKernelIdleMode != 1 && AROSAppleKernelIdleMode != 2)
        AROSAppleKernelIdleMode = default_idle_mode();
    ++AROSAppleKernelIdleEntries;
    regs->elr = (uint64_t)(uintptr_t)(!apple_core() ? AROSAppleKernelIdleLoop :
        AROSAppleKernelIdleMode == 2 ? AROSAppleKernelDeepIdleLoop :
        AROSAppleKernelGatedIdleLoop);
    regs->spsr = IDLE_SPSR_EL1H;
    if (AROSAppleRuntimeLocal()->irq_ready == 0)
        regs->spsr |= TASK_SPSR_IRQ_MASK;
    if (AROSAppleRuntimeLocal()->fiq_ready == 0)
        regs->spsr |= TASK_SPSR_FIQ_MASK;
    AROSAppleRuntimeLocal()->idle_active = 1;
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
        idle_trace_hex(" ready_nonempty=0x", SysBase != NULL &&
            SysBase->TaskReady.lh_Head->ln_Succ != NULL);
        idle_trace_hex(" spsr=0x", regs->spsr);
        idle_trace_puts(" this=");
        idle_trace_puts(this != NULL && this->tc_Node.ln_Name != NULL ?
            this->tc_Node.ln_Name : "?");
        idle_trace_hex(" state=0x", this != NULL ? this->tc_State : 0);
        AROSAppleKernelDebugPutDirect('\n');
    }
    return refused ? 0 : 1;
}

void cpu_Dispatch(regs_t *regs)
{
    struct ExceptionContext *context;
    struct Task *task;
    uint32_t index;

    if (regs == NULL)
        dispatch_contract_halt();
    task = core_Dispatch();
    if (task == NULL) {
        enter_idle(regs);
        return;
    }
    AROSAppleRuntimeLocal()->idle_active = 0;
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
    cpu_ApplyTaskMasks(&regs->spsr, task->tc_IDNestCnt);
    AROSAppleKernelLastDispatchSPSR = regs->spsr;
    for (index = 0; index < 66; ++index)
        regs->simd[index] =
            ((const volatile uint64_t *)context->fpuContext)[index];
    task->tc_SPReg = (APTR)(uintptr_t)context->sp;
    AppleTaskDispatch(GetIntETask(task),AppleTaskCounter());
}
