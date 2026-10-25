/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Apple AArch64 runtime privilege and SVC boundary.
 */

#include <aros/apple/runtime.h>
#include <aros/apple/startup.h>

#include <exec/execbase.h>
#include <exec/tasks.h>
#include <proto/exec.h>

#include <stdint.h>

#include "kernel_cpu.h"
#include "kernel_intern.h"
#include "cpu_monitor.h"
#include "kernel_intr.h"
#include "kernel_syscall.h"
#include "platform_irq.h"
#include "platform_timer_kernel.h"
#include "guard_proof.h"
#include "jit_protect.h"
#include <aros/apple/jit.h>

#define ESR_EC_SHIFT 26U
#define ESR_EC_SVC64 UINT64_C(0x15)
#define SPSR_IRQ_MASK UINT64_C(0x80)
#define SPSR_FIQ_MASK UINT64_C(0x40)
#define SPSR_INTERRUPT_MASK (SPSR_IRQ_MASK | SPSR_FIQ_MASK)
#define SPSR_EL1H_MASKED UINT64_C(0x3c5)
#define RUNTIME_PROBE_MAGIC UINT64_C(0xa55aa55aa55aa55a)

/* The runtime owns privileged access to the debug UART once tasks run at
 * EL0.  Keep the direct MMIO primitive private to kernel-side callers. */
extern int AROSAppleKernelDebugPutDirect(int chr);

static void clear_proof(struct AROSAppleRuntimeProof *proof)
{
    uint64_t *word = (uint64_t *)proof;
    uint32_t index;

    for (index = 0; index < sizeof(*proof) / sizeof(*word); ++index)
        word[index] = 0;
}

static uint64_t runtime_svc_is_super(void)
{
    register uint64_t value __asm__("x0") = 0;

    __asm__ volatile("svc #0xa500" : "+r"(value) :: "memory", "cc");
    return value;
}

static uint64_t runtime_svc_supervisor(uintptr_t (*callback)(void))
{
    register uintptr_t value __asm__("x0") = (uintptr_t)callback;

    __asm__ volatile("svc #0xa501" : "+r"(value) :: "memory", "cc");
    return value;
}

static uint64_t runtime_svc_invalid(void)
{
    register uint64_t value __asm__("x0") = 0;

    __asm__ volatile("svc #0xa5ff" : "+r"(value) :: "memory", "cc");
    return value;
}

static uint64_t runtime_svc_kernel_switch(void)
{
    register uint64_t value __asm__("x0") = 0;

    __asm__ volatile("svc #0xa602" : "+r"(value) :: "memory", "cc");
    return value;
}

_Noreturn void AROSAppleRuntimeReturnEL1(uint64_t status)
{
    register uint64_t value __asm__("x0") = status;

    __asm__ volatile("svc #0xa502" : "+r"(value) :: "memory", "cc");
    for (;;)
        __asm__ volatile("wfe");
}

int AROSAppleRuntimeIsSuper(void)
{
    uint64_t current_el;

    if (AROSAppleRuntimeLocal()->active != 0)
        return (int)runtime_svc_is_super();
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(current_el));
    return (int)(current_el >> 2);
}

void AROSAppleRuntimeCli(void)
{
    if (AROSAppleRuntimeLocal()->active != 0) {
        __asm__ volatile("svc #0xa503" ::: "memory", "cc");
        return;
    }
    __asm__ volatile("msr DAIFSet, #3" ::: "memory");
}

void AROSAppleRuntimeSti(void)
{
    if (AROSAppleRuntimeLocal()->active != 0) {
        __asm__ volatile("svc #0xa504" ::: "memory", "cc");
        return;
    }
    uint64_t required=cpu_RequiredCurrentTaskMasks();
    if (!(required & SPSR_IRQ_MASK))
        __asm__ volatile("msr DAIFClr, #2" ::: "memory");
    if (!(required & SPSR_FIQ_MASK))
        __asm__ volatile("msr DAIFClr, #1" ::: "memory");
}

static uint64_t irq_mask_clock(void)
{
    uint64_t ticks;
    __asm__ volatile("isb\n\tmrs %0,cntvct_el0":"=r"(ticks));
    return ticks;
}

/* Called only with local I/F masked. Counters are CPU-owned diagnostics. */
static int irq_mask_enter(void)
{
    struct AROSAppleRuntimeLocal *local=AROSAppleRuntimeLocal();
    if (local->irq_mask_depth==UINT32_MAX) return 0;
    if (!local->irq_mask_depth) local->irq_mask_started=irq_mask_clock();
    ++local->irq_mask_depth;
    ++local->irq_mask_entries;
    return 1;
}

static int irq_mask_leave(void)
{
    struct AROSAppleRuntimeLocal *local=AROSAppleRuntimeLocal();
    if (!local->irq_mask_depth) return 0;
    if (!--local->irq_mask_depth) {
        uint64_t ticks=irq_mask_clock()-local->irq_mask_started;
        if (ticks>local->irq_mask_max_ticks) local->irq_mask_max_ticks=ticks;
    }
    return 1;
}

/* Exec SMP critical sections cannot read/write DAIF directly from EL0.
 * Save only IRQ/FIQ bits: callers cannot change privilege, debug or abort masks. */
uint64_t AROSAppleRuntimeIRQSave(void)
{
    register uint64_t mask __asm__("x0");
    if (AROSAppleRuntimeLocal()->active) {
        __asm__ volatile("svc #0xa508" : "=r"(mask) :: "memory", "cc");
    } else {
        uint64_t saved;
        __asm__ volatile("mrs %0,daif\n\tmsr daifset,#3" : "=r"(saved) :: "memory");
        if (!irq_mask_enter()) for (;;) __asm__ volatile("wfe");
        return saved & SPSR_INTERRUPT_MASK;
    }
    return mask & SPSR_INTERRUPT_MASK;
}

void AROSAppleRuntimeIRQRestore(uint64_t saved)
{
    if (AROSAppleRuntimeLocal()->active) {
        register uint64_t mask __asm__("x0")=saved & SPSR_INTERRUPT_MASK;
        __asm__ volatile("svc #0xa509" : "+r"(mask) :: "memory", "cc");
    } else {
        if (!irq_mask_leave()) return;
        uint64_t daif;
        __asm__ volatile("mrs %0,daif":"=r"(daif));
        daif=(daif & ~SPSR_INTERRUPT_MASK) | (saved & SPSR_INTERRUPT_MASK) |
            cpu_RequiredCurrentTaskMasks();
        __asm__ volatile("msr daif,%0"::"r"(daif):"memory");
    }
}

int AROSAppleRuntimeDebugPutChar(int chr)
{
    register uint64_t value __asm__("x0") = (uint8_t)chr;

    __asm__ volatile("svc #0xa507" : "+r"(value) :: "memory", "cc");
    return value != 0;
}

void AROSAppleRuntimeSetFIQReady(int ready)
{
    ready = ready || AROSPlatformFIQRequired();
    if (ready == 0) {
        __asm__ volatile("msr DAIFSet, #1" ::: "memory");
        AROSAppleRuntimeLocal()->fiq_ready = 0;
        return;
    }
    AROSAppleRuntimeLocal()->fiq_ready = 1;
}

void AROSAppleRuntimeSetIRQReady(int ready)
{
    if (ready == 0) {
        __asm__ volatile("msr DAIFSet, #2" ::: "memory");
        AROSAppleRuntimeLocal()->irq_ready = 0;
        return;
    }
    AROSAppleRuntimeLocal()->irq_ready = 1;
}

uintptr_t AROSAppleRuntimeSupervisor(uintptr_t (*callback)(void))
{
    if (callback == NULL)
        return 0;
    if (AROSAppleRuntimeLocal()->active == 0)
        return callback();
    return (uintptr_t)runtime_svc_supervisor(callback);
}

extern int AROSAppleKernelDebugPutDirect(int chr);

static void halt_puts(const char *text)
{
    while (*text != 0)
        AROSAppleKernelDebugPutDirect(*text++);
}

static void halt_put_hex(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    halt_puts("0x");
    for (shift = 60; shift >= 0; shift -= 4)
        AROSAppleKernelDebugPutDirect(digits[(value >> shift) & 0xfU]);
}

/* One line on the evidence channel before an unhandled vector parks. */
static void runtime_exception_report(const struct AROSAppleRuntimeFrame *frame,
    const char *label)
{
    halt_puts(label);
    halt_puts(" vector=");
    halt_put_hex(frame->vector);
    halt_puts(" esr=");
    halt_put_hex(frame->esr);
    halt_puts(" far=");
    halt_put_hex(frame->far);
    halt_puts(" elr=");
    halt_put_hex(frame->elr);
    halt_puts(" spsr=");
    halt_put_hex(frame->spsr);
    halt_puts(" depth=");
    halt_put_hex(AROSAppleRuntimeLocal()->interrupt_depth);
    halt_puts("\n");
    halt_puts(label);
    halt_puts(" x19=");
    halt_put_hex(frame->x[19]);
    halt_puts(" x20=");
    halt_put_hex(frame->x[20]);
    halt_puts(" x21=");
    halt_put_hex(frame->x[21]);
    halt_puts(" x22=");
    halt_put_hex(frame->x[22]);
    halt_puts(" x23=");
    halt_put_hex(frame->x[23]);
    halt_puts(" x24=");
    halt_put_hex(frame->x[24]);
    halt_puts(" x29=");
    halt_put_hex(frame->x[29]);
    halt_puts(" x30=");
    halt_put_hex(frame->x[30]);
    halt_puts("\n");
}

static void irq_mask_abort(void);
extern int AROSAppleKernelTaskCrash(struct AROSAppleRuntimeFrame *frame);
extern void AROSAppleKernelCrashHaltDetails(
    const struct AROSAppleRuntimeFrame *frame);

static const char *current_task_name(void)
{
    struct Task *task = SysBase != NULL ? GET_THIS_TASK : NULL;

    if (task == NULL || task->tc_Node.ln_Name == NULL)
        return "-";
    return task->tc_Node.ln_Name;
}

/*
 * The exception that halts this CPU, on the panel and the evidence
 * channel: the boot log on the panel comes back for it, over whatever the
 * desktop drew, so a crash that nothing can recover from is read as a
 * crash and not as a frozen screen. Every general register is listed; the
 * two-line report of the proofs stays for the evidence channel readers.
 */
void AROSAppleRuntimeHaltReport(const struct AROSAppleRuntimeFrame *frame);
__attribute__((noinline)) void AROSAppleRuntimeHaltReport(
    const struct AROSAppleRuntimeFrame *frame)
{
    static const char *const vector_names[16] = {
        "sync EL1t", "IRQ EL1t", "FIQ EL1t", "SError EL1t",
        "sync EL1h", "IRQ EL1h", "FIQ EL1h", "SError EL1h",
        "sync EL0", "IRQ EL0", "FIQ EL0", "SError EL0",
        "sync EL0/32", "IRQ EL0/32", "FIQ EL0/32", "SError EL0/32"
    };
    unsigned index;

    AROSAppleKernelScreenLogResume();
    runtime_exception_report(frame, "[KI] runtime halt");
    halt_puts("\n[KI] UNHANDLED EXCEPTION, this CPU halts\n");
    halt_puts("[KI] task=");
    halt_puts(current_task_name());
    halt_puts(" vector=");
    halt_puts(frame->vector < 16 ? vector_names[frame->vector] : "?");
    halt_puts(" ec=");
    halt_put_hex((frame->esr >> ESR_EC_SHIFT) & UINT64_C(0x3f));
    halt_puts("\n[KI] esr=");
    halt_put_hex(frame->esr);
    halt_puts(" far=");
    halt_put_hex(frame->far);
    halt_puts("\n[KI] elr=");
    halt_put_hex(frame->elr);
    halt_puts(" sp=");
    halt_put_hex(frame->sp);
    halt_puts("\n[KI] spsr=");
    halt_put_hex(frame->spsr);
    halt_puts(" depth=");
    halt_put_hex(AROSAppleRuntimeLocal()->interrupt_depth);
    halt_puts("\n");
    for (index = 0; index < 31; ++index) {
        halt_puts(index % 2 ? " x" : "[KI] x");
        AROSAppleKernelDebugPutDirect('0' + (int)(index / 10));
        AROSAppleKernelDebugPutDirect('0' + (int)(index % 10));
        halt_puts("=");
        halt_put_hex(frame->x[index]);
        if (index % 2 || index == 30)
            halt_puts("\n");
    }
    /* The decoded fault, module+offset+symbol, backtrace and crash-log
     * entry (task_crash.c), unless the task route printed them already. */
    AROSAppleKernelCrashHaltDetails(frame);
}

/*
 * A task crashed: a synchronous exception at EL0 that no handler owns.
 * task_crash.c decides whether only the task pays for it (it is suspended
 * and the next task dispatched into frame) or this CPU halts with the
 * decoded report; see the comment block there. Out of line, with symbols of
 * their own: the dispatcher stays integer-only and free of libc calls
 * (tests/early-kernel-runtime.sh reads it up to the next symbol).
 */
int AROSAppleRuntimeTaskTrap(struct AROSAppleRuntimeFrame *frame);
__attribute__((noinline)) int AROSAppleRuntimeTaskTrap(
    struct AROSAppleRuntimeFrame *frame)
{
    if (frame->vector != 8 || SysBase == NULL)
        return 0;
    return AROSAppleKernelTaskCrash(frame);
}


static void irq_mask_abort(void)
{
    struct AROSAppleRuntimeLocal *local=AROSAppleRuntimeLocal();
    if (!local->irq_mask_depth) return;
    halt_puts("[KI] FAIL: abandoning IRQ exclusion depth=");
    halt_put_hex(local->irq_mask_depth);halt_puts("\n");
    local->irq_mask_depth=1;
    (void)irq_mask_leave();
    ++local->irq_mask_abandoned;
}

int AROSAppleRuntimeHandleException(struct AROSAppleRuntimeFrame *frame)
{
    uintptr_t (*callback)(void);
    uint64_t service;
    int from_el0;
    int handled;

    AROSCPUCurrentState(AROS_CPU_BUSY);

    if (frame != NULL &&
        (frame->vector == 1 || frame->vector == 5 ||
         frame->vector == 9 || frame->vector == 13)) {
        if (AROSAppleRuntimeLocal()->interrupt_depth == UINT32_MAX)
            goto unhandled;
        /* Charge interrupt work to the executing CPU, including idle wakes. */
        AROSCPUCurrentState(AROS_CPU_BUSY);
        ++AROSAppleRuntimeLocal()->interrupt_depth;
        handled = AROSPlatformHandleIRQ(frame);
        --AROSAppleRuntimeLocal()->interrupt_depth;
        if (handled) {
            /* Whether or not the handler switched tasks, the frame returns
             * to a task: give it that task's masks. */
            /* The return mode, not the entry vector, decides: an idle IRQ
             * enters at EL1 and can dispatch an EL0 task. Depth is now zero. */
            cpu_ApplyCurrentTaskMasks(&frame->spsr);
            return handled;
        }
        goto unhandled;
    }
    if (frame != NULL &&
        (frame->vector == 2 || frame->vector == 6 ||
         frame->vector == 10 || frame->vector == 14)) {
        if (AROSAppleRuntimeLocal()->interrupt_depth == UINT32_MAX)
            goto unhandled;
        /* Charge interrupt work to the executing CPU, including idle wakes. */
        AROSCPUCurrentState(AROS_CPU_BUSY);
        ++AROSAppleRuntimeLocal()->interrupt_depth;
        handled = AROSPlatformHandleFIQ(frame);
        --AROSAppleRuntimeLocal()->interrupt_depth;
        if (handled) {
            /* The return mode, not the entry vector, decides: an idle IRQ
             * enters at EL1 and can dispatch an EL0 task. Depth is now zero. */
            cpu_ApplyCurrentTaskMasks(&frame->spsr);
            return handled;
        }
        goto unhandled;
    }
    if (AROSAppleGuardProofFault(frame))
        return 1;
    if (frame == NULL ||
        ((frame->esr >> ESR_EC_SHIFT) & UINT64_C(0x3f)) != ESR_EC_SVC64 ||
        (frame->vector != 0 && frame->vector != 4 && frame->vector != 8)) {
        if (frame != NULL && AROSAppleRuntimeTaskTrap(frame))
            return 1;
        goto unhandled;
    }
    from_el0 = frame->vector == 8;
    service = frame->esr & UINT64_C(0xffff);
    frame->service = service;
    frame->status = AROS_APPLE_RUNTIME_STATUS_HANDLED;
    if (AROSAppleRuntimeLocal()->proof != NULL) {
        if (from_el0)
            ++AROSAppleRuntimeLocal()->proof->el0_service_count;
        else
            ++AROSAppleRuntimeLocal()->proof->el1_service_count;
    }

    if (service >= AROS_APPLE_RUNTIME_SVC_KERNEL_BASE &&
        service <= AROS_APPLE_RUNTIME_SVC_KERNEL_BASE + SC_SCHEDULE) {
        if (!from_el0)
            goto rejected;
        if (AROSAppleRuntimeLocal()->irq_mask_depth) {
            struct AROSAppleRuntimeLocal *local=AROSAppleRuntimeLocal();
            ++local->irq_mask_rejected;
            if (!local->proof && !local->irq_mask_refusal_reported) {
                local->irq_mask_refusal_reported=1;
                halt_puts("[KI] IRQ exclusion rejected scheduler service=");
                halt_put_hex(service);halt_puts("\n");
            }
            frame->spsr |= SPSR_INTERRUPT_MASK;
            goto rejected;
        }
        core_SysCall((int)(service - AROS_APPLE_RUNTIME_SVC_KERNEL_BASE),
            frame);
        /* A schedule or switch that kept the same task returns its own
         * frame: its masks follow the rule, not the SPSR it trapped with. */
        cpu_ApplyCurrentTaskMasks(&frame->spsr);
        return 1;
    }

    switch (service) {
    case AROS_APPLE_RUNTIME_SVC_IS_SUPER:
        frame->x[0] = from_el0 ? 0 : 1;
        return 1;
    case AROS_APPLE_RUNTIME_SVC_SUPERVISOR:
        callback = (uintptr_t (*)(void))(uintptr_t)frame->x[0];
        if (callback == NULL)
            break;
        if (AROSAppleRuntimeLocal()->proof != NULL)
            ++AROSAppleRuntimeLocal()->proof->supervisor_count;
        frame->x[0] = callback();
        return 1;
    case AROS_APPLE_RUNTIME_SVC_RETURN_EL1:
        if (!from_el0 || AROSAppleRuntimeLocal()->transition_active == 0 ||
            AROSAppleRuntimeLocal()->return_pc == 0)
            break;
        irq_mask_abort();
        AROSAppleRuntimeLocal()->transition_active = 0;
        frame->elr = AROSAppleRuntimeLocal()->return_pc;
        frame->spsr = SPSR_EL1H_MASKED;
        return 1;
    case AROS_APPLE_RUNTIME_SVC_IRQ_SAVE:
        if (!irq_mask_enter()) goto rejected;
        frame->x[0] = frame->spsr & SPSR_INTERRUPT_MASK;
        frame->spsr |= SPSR_INTERRUPT_MASK;
        return 1;
    case AROS_APPLE_RUNTIME_SVC_IRQ_RESTORE:
        if (!irq_mask_leave()) goto rejected;
        frame->spsr = (frame->spsr & ~SPSR_INTERRUPT_MASK) |
            (frame->x[0] & SPSR_INTERRUPT_MASK) | cpu_RequiredCurrentTaskMasks();
        return 1;
    case AROS_APPLE_RUNTIME_SVC_CLI:
        frame->spsr |= SPSR_INTERRUPT_MASK;
        return 1;
    case AROS_APPLE_RUNTIME_SVC_STI:
        frame->spsr = (frame->spsr & ~SPSR_INTERRUPT_MASK) |
            cpu_RequiredCurrentTaskMasks();
        return 1;
    case AROS_APPLE_RUNTIME_SVC_JIT_WRITE:
        frame->x[0] = AROSAppleJitSetWritable(
            SysBase != NULL ? GET_THIS_TASK : NULL, frame->x[0] != 0);
        return 1;
    case AROS_APPLE_RUNTIME_SVC_DEBUG_PUT_CHAR:
        frame->x[0] = (uint64_t)AROSAppleKernelDebugPutDirect(
            (int)(uint8_t)frame->x[0]);
        return 1;
    case AROS_APPLE_TIMER_SVC:
        frame->x[0] = (uint64_t)AROSAppleKernelTimerControl(
            (struct AROSAppleTimerControl *)(uintptr_t)frame->x[0]);
        return 1;
    default:
        if (AROSPlatformHandleService(service, frame))
            return 1;
        break;
    }
rejected:
    frame->x[0] = AROS_APPLE_RUNTIME_SVC_ERROR;
    frame->status = AROS_APPLE_RUNTIME_STATUS_REJECTED;
    if (AROSAppleRuntimeLocal()->proof != NULL)
        ++AROSAppleRuntimeLocal()->proof->rejected_count;
    return 1;

unhandled:
    /* EnterEL0 installs a private EL1 return continuation for the duration of
     * each observation proof.  Use it for a fail-fast post-mortem; outside
     * that bounded transition the normal unhandled-vector halt is retained. */
    if (frame != NULL && AROSAppleRuntimeLocal()->transition_active != 0 &&
        AROSAppleRuntimeLocal()->return_pc != 0) {
        AROSAppleColdStartRecordRuntimeException(frame);
        /* Outside the runtime proof this continuation unwinds the whole
         * session into stage two's halt: the panel gets the report first. */
        if (AROSAppleRuntimeLocal()->proof == NULL)
            AROSAppleRuntimeHaltReport(frame);
        runtime_exception_report(frame, "[KI] runtime exception before recovery");
        irq_mask_abort();
        AROSAppleRuntimeLocal()->transition_active = 0;
        frame->x[0] = 0;
        frame->elr = AROSAppleRuntimeLocal()->return_pc;
        frame->spsr = SPSR_EL1H_MASKED;
        return 1;
    }
    /* The halt that follows is silent; say what led to it first. */
    if (frame != NULL)
        AROSAppleRuntimeHaltReport(frame);
    return 0;
}

static uintptr_t runtime_supervisor_probe(void)
{
    return AROSAppleRuntimeIsSuper() == 1 ? RUNTIME_PROBE_MAGIC : 0;
}

/* Nested saves must restore the caller's actual state in both privilege modes. */
static int runtime_irq_mask_probe(void)
{
    uint64_t outer=AROSAppleRuntimeIRQSave();
    uint64_t inner=AROSAppleRuntimeIRQSave();
    /* Even a permissive token cannot unmask while the outer hold remains. */
    AROSAppleRuntimeIRQRestore(0);
    uint64_t nested=AROSAppleRuntimeIRQSave();
    AROSAppleRuntimeIRQRestore(nested);
    AROSAppleRuntimeIRQRestore(outer);
    uint64_t restored=AROSAppleRuntimeIRQSave();
    AROSAppleRuntimeIRQRestore(restored);
    outer |= cpu_RequiredCurrentTaskMasks();
    if (restored!=outer || inner!=SPSR_INTERRUPT_MASK || nested!=SPSR_INTERRUPT_MASK) {
        halt_puts("[KI] IRQ masks outer="); halt_put_hex(outer);
        halt_puts(" inner="); halt_put_hex(inner);
        halt_puts(" restored="); halt_put_hex(restored); halt_puts("\n");
    }
    return restored==outer && inner==SPSR_INTERRUPT_MASK && nested==SPSR_INTERRUPT_MASK;
}

static void runtime_el0_probe(struct AROSAppleRuntimeProof *proof)
{
    uint64_t status = RUNTIME_PROBE_MAGIC;
    uint64_t stack_marker;

    if (!runtime_irq_mask_probe()) status=0;
    uint64_t saved=AROSAppleRuntimeIRQSave();
    register uint64_t scheduled __asm__("x0")=0;
    __asm__ volatile("svc #0xa603":"+r"(scheduled)::"memory","cc");
    uint64_t schedule_result=scheduled;
    uint64_t after=AROSAppleRuntimeIRQSave();
    AROSAppleRuntimeIRQRestore(after);
    AROSAppleRuntimeIRQRestore(saved);
    if (schedule_result!=AROS_APPLE_RUNTIME_SVC_ERROR || after!=SPSR_INTERRUPT_MASK)
        status=0;
    proof->el0_stack_observed = (uintptr_t)&stack_marker;
    proof->el0_is_super = (uint64_t)AROSAppleRuntimeIsSuper();
    proof->el0_invalid_result = runtime_svc_invalid();
    proof->el0_supervisor_result =
        AROSAppleRuntimeSupervisor(runtime_supervisor_probe);
    if (proof->el0_is_super != 0 ||
        proof->el0_invalid_result != AROS_APPLE_RUNTIME_SVC_ERROR ||
        proof->el0_supervisor_result != RUNTIME_PROBE_MAGIC)
        status = 0;
    AROSAppleRuntimeReturnEL1(status);
}

int AROSAppleKernelProveRuntime(
    uintptr_t user_stack, uint64_t user_stack_size,
    struct AROSAppleRuntimeProof *proof)
{
    uintptr_t stack_top;
    uintptr_t vectors = (uintptr_t)AROSAppleRuntimeVectors;
    uint64_t current_el;
    uint64_t installed_vectors;

    if (proof == NULL || ((uintptr_t)proof & 15U) != 0 ||
        user_stack == 0 || (user_stack & 15U) != 0 ||
        user_stack_size < AROS_APPLE_RUNTIME_STACK_MIN ||
        user_stack_size > UINTPTR_MAX - user_stack ||
        (vectors & (AROS_APPLE_RUNTIME_VECTOR_TABLE_SIZE - 1U)) != 0)
        return 0;
    stack_top = user_stack + (uintptr_t)user_stack_size;
    if ((stack_top & 15U) != 0)
        return 0;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(current_el));
    if ((current_el >> 2) != 1)
        return 0;

    clear_proof(proof);
    proof->version = AROS_APPLE_RUNTIME_PROOF_VERSION;
    proof->size = sizeof(*proof);
    proof->frame_size = sizeof(struct AROSAppleRuntimeFrame);
    proof->vector_table_size = AROS_APPLE_RUNTIME_VECTOR_TABLE_SIZE;
    proof->vectors = vectors;
    proof->user_stack_lower = user_stack;
    proof->user_stack_upper = stack_top;
    AROSAppleRuntimeLocal()->proof = proof;
    __asm__ volatile("msr VBAR_EL1, %0; isb" :: "r"(vectors) : "memory");
    __asm__ volatile("mrs %0, VBAR_EL1" : "=r"(installed_vectors));
    if (installed_vectors != vectors)
        return 0;
    AROSAppleRuntimeLocal()->active = 1;
    /* EL0 tasks may read CTR_EL0 (UCT) and clean/invalidate caches by VA
     * (UCI): code generators such as JIT compilers and compiler-rt's
     * __clear_cache rely on both. APs inherit this SCTLR. */
    {
        uint64_t sctlr;
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
        sctlr |= (UINT64_C(1) << 15) | (UINT64_C(1) << 26);
        __asm__ volatile("msr sctlr_el1, %0; isb" :: "r"(sctlr) : "memory");
    }
#if defined(__AROSEXEC_SMP__)
    /* Exec's shared spinlocks use WFE at EL0. Permit event waits rather
     * than inheriting firmware's nTWE=0 trap policy. APs inherit this
     * SCTLR through the SMP handoff. WFI remains a kernel operation. */
    {
        uint64_t sctlr;
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
        sctlr |= UINT64_C(1) << 18;
        __asm__ volatile("msr sctlr_el1, %0; isb" :: "r"(sctlr) : "memory");
    }
#endif

    proof->el1_is_super = (uint64_t)AROSAppleRuntimeIsSuper();
    proof->el1_invalid_result = runtime_svc_invalid();
    proof->el1_kernel_result = runtime_svc_kernel_switch();
    proof->el1_supervisor_result =
        AROSAppleRuntimeSupervisor(runtime_supervisor_probe);
    int irq_masks_ok=runtime_irq_mask_probe();
    proof->return_status = AROSAppleRuntimeEnterEL0(runtime_el0_probe,
        proof, stack_top);
    AROSAppleRuntimeLocal()->proof = NULL;
    if (!irq_masks_ok || proof->return_status!=RUNTIME_PROBE_MAGIC ||
        proof->el1_service_count!=AROS_APPLE_RUNTIME_PROOF_EL1_CALLS ||
        proof->el0_service_count!=AROS_APPLE_RUNTIME_PROOF_EL0_CALLS) {
    halt_puts("[KI] IRQ proof el1calls="); halt_put_hex(proof->el1_service_count);
    halt_puts(" el0calls="); halt_put_hex(proof->el0_service_count);
    halt_puts(" return="); halt_put_hex(proof->return_status); halt_puts("\n");
    }

    return irq_masks_ok && proof->el1_is_super == 1 && proof->el0_is_super == 0 &&
        proof->el1_invalid_result == AROS_APPLE_RUNTIME_SVC_ERROR &&
        proof->el1_kernel_result == AROS_APPLE_RUNTIME_SVC_ERROR &&
        proof->el0_invalid_result == AROS_APPLE_RUNTIME_SVC_ERROR &&
        proof->el1_supervisor_result == RUNTIME_PROBE_MAGIC &&
        proof->el0_supervisor_result == RUNTIME_PROBE_MAGIC &&
        proof->return_status == RUNTIME_PROBE_MAGIC &&
        proof->el0_stack_observed >= proof->user_stack_lower &&
        proof->el0_stack_observed < proof->user_stack_upper &&
        proof->el1_service_count == AROS_APPLE_RUNTIME_PROOF_EL1_CALLS && proof->el0_service_count == AROS_APPLE_RUNTIME_PROOF_EL0_CALLS &&
        proof->supervisor_count == AROS_APPLE_RUNTIME_PROOF_SUPERVISORS &&
        proof->rejected_count == AROS_APPLE_RUNTIME_PROOF_REJECTIONS &&
        AROSAppleRuntimeLocal()->active == 1 &&
        AROSAppleRuntimeLocal()->transition_active == 0;
}
