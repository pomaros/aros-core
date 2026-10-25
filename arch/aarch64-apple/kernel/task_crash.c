/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: A crashed task is suspended, not the machine.
 *
 * A synchronous exception from EL0 that no handler owns (a data abort, a
 * branch to NULL, an undefined instruction) is a task's crash. When
 * AROSAppleCrashDecide (task_crash_policy.h) finds that nothing beyond the
 * task is caught in it, the kernel
 *
 *  1. records it: one summary line
 *       [KI] task crashed and suspended: <name> ...
 *     then the decoded fault, ELR and LR as module+offset+symbol (the map
 *     stage two left in AROSAppleKernelModuleMap, or the task's own
 *     seglist), every register, the registers that point into code, a
 *     frame-pointer backtrace bounded by the task's stack and a stack
 *     excerpt, on the evidence channel and the kernel log (C:KLog), and
 *     the same as one entry of the crash log (aros/apple/crashlog.h,
 *     KrnGetSystemAttr(AROS_APPLE_KATTR_CRASH_LOG), C:CrashLog);
 *  2. takes the task out of scheduling for good: its context is saved,
 *     it leaves the running list for the wait list in TS_WAIT waiting for
 *     no signal and taking no exception, so neither Signal() nor a thaw
 *     can make it run again; its memory, its semaphores and its ports stay
 *     as they are (a task blocked on one of them stays blocked);
 *  3. dispatches the next ready task on this CPU (or the idle loop). The
 *     task's Forbid() and Disable() nesting leave the CPU with it.
 *
 * Nothing user-visible is raised from the exception: an Intuition requester
 * from the crashed task cannot be answered when the crashed task is
 * input.device, and no other task can be started from here. C:CrashLog
 * WATCH posts a non-blocking requester from a task of its own.
 *
 * Every other case keeps the visible halt of AROSAppleRuntimeHaltReport,
 * now with the same decoded report: a fault at EL1, in an interrupt, in an
 * IRQ-masked section or holding the task or a scheduler lock, before the
 * scheduler ran, during a runtime proof, an SError, and every fault when
 * the chosen property aros,task-crash-halt is "1" (the debugging switch
 * back to halting on any crash).
 *
 * Exception context only: no allocation, no Exec call that can block,
 * bounded loops, and every pointer that does not come from the frame is
 * checked with AT S1E1R (mapped, Normal memory) before it is read.
 */

#include <aros/config.h>
#include <aros/apple/runtime.h>
#include <aros/apple/crashlog.h>
#include <aros/apple/modulemap.h>

#include <exec/execbase.h>
#include <exec/nodes.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>

#include <stdint.h>

#include "kernel_cpu.h"
#include "kernel_intern.h"
#include "kernel_intr.h"
#include "cpu_monitor.h"

#define AROS_NO_ATOMIC_OPERATIONS
#include <exec_platform.h>
#include <aros/types/spinlock_s.h>
#include <etask.h>
#undef bug
#include "exec_intern.h"

#include "task_crash_policy.h"

extern int AROSAppleKernelDebugPutDirect(int chr);
extern uint64_t AROSAppleKernelChosenDecimal(const char *name);

/*
 * Written by stage two (stage2_module_map.c) before the kernel runs. In
 * .data so that no clearing of .bss after the handoff can lose it.
 */
const struct AROSAppleModuleMap *AROSAppleKernelModuleMap
    __attribute__((section(".data"))) = 0;

/* Initialised: readers at EL0 never write it, the header is valid before
 * the first crash. */
static struct AROSAppleCrashLog crash_log = {
    .version = AROS_APPLE_CRASH_LOG_VERSION,
    .size = sizeof(struct AROSAppleCrashLog),
    .capacity = AROS_APPLE_CRASH_LOG_CAPACITY,
    .entry_size = sizeof(struct AROSAppleCrashEntry),
};
/* The verdict (+1) the task route reached for a fault that then halts, so
 * the halt report, printed after the panel takes the screen log back,
 * names it. Indexed by CPU; 0 none. */
static volatile uint8_t crash_pending_verdict[AROS_CPU_MAX];

#define CRASH_LOCK_TIMEOUT_US 50000U
#define CRASH_EL1_STACK_SPAN UINT64_C(0x10000)

static void crash_puts(const char *text)
{
    uint32_t n;

    for (n = 0; text[n] != 0 && n < 512U; ++n)
        AROSAppleKernelDebugPutDirect(text[n]);
}

static void crash_hex(uint64_t value)
{
    char buffer[24];
    struct AROSAppleCrashText t;

    crash_text_init(&t, buffer, sizeof(buffer));
    crash_text_hex(&t, value);
    crash_puts(buffer);
}

/* 0x and sixteen digits, as the halt report and the [TRAP] record print. */
static void crash_hex16(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    crash_puts("0x");
    for (shift = 60; shift >= 0; shift -= 4)
        AROSAppleKernelDebugPutDirect(digits[(value >> shift) & 0xfU]);
}

static uint64_t crash_counter(void)
{
    uint64_t ticks;

    __asm__ volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(ticks));
    return ticks;
}

static uint64_t crash_frequency(void)
{
    uint64_t frequency;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    return frequency != 0 ? frequency : 24000000U;
}

/* Mapped for an EL1 read and Normal memory (never device registers). */
static int crash_mapped(uint64_t address)
{
    uint64_t par;

    __asm__ volatile("at s1e1r, %1\n\tisb\n\tmrs %0, par_el1"
        : "=r"(par) : "r"(address) : "memory");
    if (par & 1U)
        return 0;
    return ((par >> 56) & 0xf0U) != 0;
}

static int crash_read(void *context, uint64_t address, uint64_t *value)
{
    (void)context;
    if ((address & 7U) != 0 || !crash_mapped(address))
        return 0;
    *value = *(const volatile uint64_t *)(uintptr_t)address;
    return 1;
}

static int crash_read32(uint64_t address, uint32_t *value)
{
    if ((address & 3U) != 0 || !crash_mapped(address))
        return 0;
    *value = *(const volatile uint32_t *)(uintptr_t)address;
    return 1;
}

/* A structure of size bytes at address, entirely mapped. */
static int crash_object(const void *address, uint64_t size)
{
    uint64_t start = (uint64_t)(uintptr_t)address;
    uint64_t page;

    if (start == 0 || size == 0 || start + size < start)
        return 0;
    for (page = start & ~UINT64_C(0xfff); page < start + size; page += 0x1000U)
        if (!crash_mapped(page < start ? start : page))
            return 0;
    return 1;
}

static void crash_read_name(const char *name, char *out, uint32_t capacity)
{
    uint64_t address = (uint64_t)(uintptr_t)name;
    uint32_t n = 0;

    if (capacity == 0)
        return;
    if (name != NULL && crash_mapped(address)) {
        for (; n + 1U < capacity; ++n) {
            if (((address + n) & 0xfffU) == 0 && !crash_mapped(address + n))
                break;
            if (name[n] == 0)
                break;
            out[n] = name[n];
        }
    }
    if (n == 0 && capacity > 1U)
        out[n++] = '-';
    out[n] = 0;
}

static uint32_t crash_cpu(void)
{
    unsigned cpu = AROSCPUCurrentIndex();

    return cpu < AROS_CPU_MAX ? (uint32_t)cpu : 0U;
}

static struct Task *crash_task(void)
{
    struct Task *task;

    /* GET_THIS_TASK traps without the CPU's Exec TLS (early boot). */
    if (SysBase == NULL || AROSAppleRuntimeLocal()->exec_tls == NULL)
        return NULL;
    task = GET_THIS_TASK;
    if (task == NULL || ((uintptr_t)task & 7U) != 0 ||
            !crash_object(task, sizeof(*task)))
        return NULL;
    return task;
}

static struct ExceptionContext *crash_context(struct Task *task)
{
    struct ETask *etask;
    struct ExceptionContext *context;

    if ((task->tc_Flags & TF_ETASK) == 0)
        return NULL;
    etask = task->tc_UnionETask.tc_ETask;
    if (etask == NULL || !crash_object(etask, sizeof(struct IntETask)))
        return NULL;
    context = etask->et_RegFrame;
    if (context == NULL || !crash_object(context, sizeof(*context)) ||
            context->fpuContext == NULL)
        return NULL;
    return context;
}

#if defined(__AROSEXEC_SMP__)
static int crash_lock_owned_by(const spinlock_t *lock, struct Task *task)
{
    return (lock->lock & 0x80000000U) != 0 && lock->s_Owner == task;
}
#endif

/* Gather the decision's inputs. Pure reads, safe on any vector. */
static void crash_gather(const struct AROSAppleRuntimeFrame *frame,
    struct AROSAppleCrashState *s, struct Task **task_out)
{
    struct AROSAppleRuntimeLocal *local = AROSAppleRuntimeLocal();
    static int kill_switch = -1;
    struct Task *task;

    *s = (struct AROSAppleCrashState){0};
    s->vector = (uint32_t)frame->vector;
    s->esr = frame->esr;
    if (kill_switch < 0)
        kill_switch = AROSAppleKernelChosenDecimal("aros,task-crash-halt") == 1U;
    s->kill_switch = (uint32_t)kill_switch;
    s->proof_active = local->proof != NULL;
    s->interrupt_depth = local->interrupt_depth;
    s->irq_mask_depth = local->irq_mask_depth;
    s->scheduler_live = SysBase != NULL && SysBase->DispCount != 0;
    task = crash_task();
    *task_out = task;
    if (task == NULL)
        return;
    s->have_task = 1;
    s->task_is_etask = (task->tc_Flags & TF_ETASK) != 0 &&
        task->tc_UnionETask.tc_ETask != NULL;
    s->task_state = task->tc_State;
#if defined(__AROSEXEC_SMP__)
    s->task_lock_held = crash_lock_owned_by(&task->tc_SpinLock, task);
    s->list_lock_held =
        crash_lock_owned_by(&PrivExecBase(SysBase)->TaskRunningSpinLock, task) ||
        crash_lock_owned_by(&PrivExecBase(SysBase)->TaskReadySpinLock, task) ||
        crash_lock_owned_by(&PrivExecBase(SysBase)->TaskWaitSpinLock, task) ||
        crash_lock_owned_by(&PrivExecBase(SysBase)->TaskSpinningLock, task);
#endif
    s->context_saveable = s->task_is_etask && crash_context(task) != NULL;
}

/* An address in the task's own loaded program: "<name> segN+offset". */
static int crash_place_seglist(struct Task *task, uint64_t address,
    struct AROSAppleCrashPlace *p)
{
    struct Process *process = (struct Process *)task;
    uint64_t segment = 0;
    uint32_t index;

    if (task == NULL || task->tc_Node.ln_Type != NT_PROCESS ||
            !crash_object(process, sizeof(*process)))
        return 0;
    if (process->pr_CLI != BNULL) {
        struct CommandLineInterface *cli = BADDR(process->pr_CLI);

        if (crash_object(cli, sizeof(*cli)))
            segment = (uint64_t)(uintptr_t)BADDR(cli->cli_Module);
    }
    if (segment == 0 && process->pr_SegList != BNULL) {
        uint64_t array = (uint64_t)(uintptr_t)BADDR(process->pr_SegList);
        uint64_t value;

        if (crash_read(NULL, array + 3U * sizeof(BPTR), &value))
            segment = (uint64_t)(uintptr_t)BADDR((BPTR)value);
    }
    /* struct hunk { ULONG size; BPTR next; char data[]; } (packed): the
     * BPTR points at next, the size is the 4 bytes before it. */
    for (index = 0; segment != 0 && index < 64U; ++index) {
        uint32_t low, high, size;
        uint64_t next, start, end;

        if (!crash_read32(segment, &low) || !crash_read32(segment + 4U, &high) ||
                !crash_read32(segment - 4U, &size))
            return 0;
        next = ((uint64_t)high << 32) | low;
        start = segment + sizeof(BPTR);
        end = segment - sizeof(ULONG) + size;
        if (size != 0 && end > start && address >= start && address < end) {
            struct AROSAppleCrashText t;

            crash_place_clear(p, address);
            crash_read_name(task->tc_Node.ln_Name, p->module, sizeof(p->module));
            crash_text_init(&t, p->section, sizeof(p->section));
            crash_text_str(&t, "seg");
            crash_text_dec(&t, index);
            p->offset = address - start;
            return 1;
        }
        segment = (uint64_t)(uintptr_t)BADDR((BPTR)next);
    }
    return 0;
}

/* The task that created this one (ETask et_Parent), or NULL */
static struct Task *crash_parent(struct Task *task)
{
    struct ETask *etask;

    if (task == NULL || (task->tc_Flags & TF_ETASK) == 0)
        return NULL;
    etask = task->tc_UnionETask.tc_ETask;
    if (etask == NULL || !crash_object(etask, sizeof(*etask)))
        return NULL;
    return etask->et_Parent;
}

static int crash_place(struct Task *task, uint64_t address,
    struct AROSAppleCrashPlace *p)
{
    struct Task *owner = task;
    uint32_t depth;

    if (AROSAppleCrashPlaceModule(AROSAppleKernelModuleMap, address, p))
        return 1;
    /* A thread (a process a program created, with no program of its own)
     * runs its creator's code: look in the creators' programs too. */
    for (depth = 0; owner != NULL && depth < 8U; ++depth) {
        if (crash_place_seglist(owner, address, p))
            return 1;
        owner = crash_parent(owner);
        if (owner != NULL && !crash_object(owner, sizeof(struct Task)))
            break;
    }
    crash_place_clear(p, address);
    return 0;
}

/* A return address names the call before it: look up ret - 4 so a call at
 * the end of a function is not attributed to the next one. */
static int crash_place_return(struct Task *task, uint64_t address,
    struct AROSAppleCrashPlace *p)
{
    int found;

    if (address < 4U)
        return crash_place(task, address, p);
    found = crash_place(task, address - 4U, p);
    p->address = address;
    if (found) {
        p->offset += 4U;
        if (p->symbol[0] != 0)
            p->symbol_offset += 4U;
    }
    return found;
}

static void crash_print_place(const char *label,
    const struct AROSAppleCrashPlace *p)
{
    char buffer[160];
    struct AROSAppleCrashText t;

    crash_text_init(&t, buffer, sizeof(buffer));
    crash_text_str(&t, "[KI] crash ");
    crash_text_str(&t, label);
    crash_text_str(&t, "=");
    crash_text_hex(&t, p->address);
    crash_text_char(&t, ' ');
    AROSAppleCrashFormatPlace(&t, p);
    crash_text_char(&t, '\n');
    crash_puts(buffer);
}

/*
 * Build the entry and print the report. The entry is the crash log's when
 * a slot can be taken; the report never depends on the log.
 */
static void crash_report(const struct AROSAppleRuntimeFrame *frame,
    struct Task *task, uint32_t verdict)
{
    static const char *const regnames[31] = {
        "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10",
        "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x19", "x20",
        "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "fp", "lr"
    };
    uint64_t sequence = __atomic_add_fetch(&crash_log.count, 1, __ATOMIC_ACQ_REL);
    struct AROSAppleCrashEntry *e =
        &crash_log.entries[(sequence - 1U) % AROS_APPLE_CRASH_LOG_CAPACITY];
    uint64_t returns[AROS_APPLE_CRASH_FRAMES];
    struct AROSAppleCrashText t;
    char line[200];
    uint64_t lower = 0, upper = 0;
    uint32_t index, frames;

    __atomic_store_n(&e->sequence, 0, __ATOMIC_RELEASE);
    e->counter = crash_counter();
    e->task = (uint64_t)(uintptr_t)task;
    e->cpu = crash_cpu();
    e->verdict = verdict;
    e->flags = 0;
    e->task_type = 0;
    e->td_nest = -1;
    e->id_nest = -1;
    crash_read_name(task != NULL ? task->tc_Node.ln_Name : NULL,
        e->task_name, sizeof(e->task_name));
    if (task != NULL) {
        e->task_type = task->tc_Node.ln_Type;
        if (frame->vector >= 8U && SysBase != NULL) {
            /* The CPU's live nesting is the running task's. */
            e->td_nest = TDNESTCOUNT_GET;
            e->id_nest = IDNESTCOUNT_GET;
        }
        lower = (uint64_t)(uintptr_t)task->tc_SPLower;
        upper = (uint64_t)(uintptr_t)task->tc_SPUpper;
    }
    if (e->td_nest >= 0)
        e->flags |= AROS_APPLE_CRASH_IN_FORBID;
    if (e->id_nest >= 0)
        e->flags |= AROS_APPLE_CRASH_IN_DISABLE;
    /* An EL1 fault walks the kernel stack it ran on, bounded. */
    if (frame->vector < 8U || task == NULL || upper <= lower) {
        lower = frame->sp;
        upper = frame->sp + CRASH_EL1_STACK_SPAN;
    } else if (frame->sp < lower || frame->sp > upper) {
        e->flags |= AROS_APPLE_CRASH_STACK_OUTSIDE;
    }
    e->stack_lower = lower;
    e->stack_upper = upper;
    e->esr = frame->esr;
    e->far = frame->far;
    e->elr = frame->elr;
    e->spsr = frame->spsr;
    e->sp = frame->sp;
    for (index = 0; index < 31U; ++index)
        e->x[index] = frame->x[index];
    crash_text_init(&t, e->fault, sizeof(e->fault));
    AROSAppleCrashDescribe(&t, frame->esr, frame->far, frame->elr);
    crash_place(task, frame->elr, &e->elr_place);
    crash_place_return(task, frame->x[30], &e->lr_place);

    frames = AROSAppleCrashWalk(frame->x[29], lower, upper, crash_read, NULL,
        returns, AROS_APPLE_CRASH_FRAMES);
    e->frame_count = 0;
    for (index = 0; index < frames; ++index) {
        /* A function that pushed its frame saved the LR already shown. */
        if (index == 0 && returns[0] == frame->x[30])
            continue;
        crash_place_return(task, returns[index], &e->frames[e->frame_count++]);
    }
    e->stack_count = 0;
    for (index = 0; index < AROS_APPLE_CRASH_STACK_WORDS; ++index) {
        uint64_t at = frame->sp + 8U * index, value;

        if (at + 8U > upper || !crash_read(NULL, at, &value))
            break;
        e->stack[e->stack_count++] = value;
    }
    e->reg_place_count = 0;
    for (index = 0; index < 29U &&
            e->reg_place_count < AROS_APPLE_CRASH_REG_PLACES; ++index) {
        struct AROSAppleCrashRegPlace *r = &e->reg_places[e->reg_place_count];

        if (AROSAppleCrashPlaceModule(AROSAppleKernelModuleMap,
                frame->x[index], &r->place)) {
            r->reg = index;
            r->reserved = 0;
            ++e->reg_place_count;
        }
    }

    /* The summary line: the one the log readers look for. */
    crash_text_init(&t, line, sizeof(line));
    if (verdict == AROS_APPLE_CRASH_SUSPEND)
        crash_text_str(&t, "[KI] task crashed and suspended: ");
    else {
        crash_text_str(&t, "[KI] crash not recoverable (");
        crash_text_str(&t, AROSAppleCrashVerdictName(verdict));
        crash_text_str(&t, "), this CPU halts: ");
    }
    crash_text_str(&t, e->task_name);
    crash_text_str(&t, " task=");
    crash_text_hex(&t, e->task);
    crash_text_str(&t, e->task_type == NT_PROCESS ? " process" :
        e->task_type == NT_TASK ? " task" : " ?");
    crash_text_str(&t, " cpu=");
    crash_text_dec(&t, e->cpu);
    crash_text_str(&t, " forbid=");
    crash_text_dec(&t, (uint64_t)(int64_t)(e->td_nest + 1));
    crash_text_str(&t, " disable=");
    crash_text_dec(&t, (uint64_t)(int64_t)(e->id_nest + 1));
    crash_text_str(&t, " record=");
    crash_text_dec(&t, sequence);
    crash_text_char(&t, '\n');
    crash_puts(line);

    /* The one-line record of the D-125 route, kept verbatim for the
     * VM qualification tools that match it (tools/keyring/qualify,
     * tools/settings/check-vm, tools/display-scale/check-vm). */
    if (frame->vector == 8U) {
        crash_text_init(&t, line, sizeof(line));
        crash_text_str(&t, "[TRAP] task=");
        crash_text_str(&t, e->task_name);
        crash_text_str(&t, " code=");
        crash_text_dec(&t, AROSAppleCrashTrapNumber(frame->esr));
        crash_puts(line);
        crash_puts(" esr=");
        crash_hex16(frame->esr);
        crash_puts(" far=");
        crash_hex16(frame->far);
        crash_puts(" elr=");
        crash_hex16(frame->elr);
        crash_puts(" sp=");
        crash_hex16(frame->sp);
        crash_puts(" x30=");
        crash_hex16(frame->x[30]);
        crash_puts("\n");
    }
    crash_puts("[KI] crash fault: ");
    crash_puts(e->fault);
    crash_puts("\n");
    crash_print_place("elr", &e->elr_place);
    crash_print_place("lr", &e->lr_place);
    crash_puts("[KI] crash esr=");
    crash_hex(e->esr);
    crash_puts(" far=");
    crash_hex(e->far);
    crash_puts(" spsr=");
    crash_hex(e->spsr);
    crash_puts(" sp=");
    crash_hex(e->sp);
    crash_puts(" stack=");
    crash_hex(lower);
    crash_puts("..");
    crash_hex(upper);
    if (e->flags & AROS_APPLE_CRASH_STACK_OUTSIDE)
        crash_puts(" SP OUTSIDE");
    crash_puts("\n");
    for (index = 0; index < 31U; index += 4U) {
        uint32_t k;

        crash_text_init(&t, line, sizeof(line));
        crash_text_str(&t, "[KI] crash");
        for (k = index; k < index + 4U && k < 31U; ++k) {
            crash_text_char(&t, ' ');
            crash_text_str(&t, regnames[k]);
            crash_text_char(&t, '=');
            crash_text_hex(&t, e->x[k]);
        }
        crash_text_char(&t, '\n');
        crash_puts(line);
    }
    for (index = 0; index < e->reg_place_count; ++index)
        crash_print_place(regnames[e->reg_places[index].reg],
            &e->reg_places[index].place);
    for (index = 0; index < e->frame_count; ++index) {
        char label[12];
        struct AROSAppleCrashText l;

        crash_text_init(&l, label, sizeof(label));
        crash_text_str(&l, "frame");
        crash_text_dec(&l, index);
        crash_print_place(label, &e->frames[index]);
    }
    for (index = 0; index < e->stack_count; index += 4U) {
        uint32_t k;

        crash_text_init(&t, line, sizeof(line));
        crash_text_str(&t, "[KI] crash sp+");
        crash_text_hex(&t, 8U * index);
        crash_text_char(&t, ':');
        for (k = index; k < index + 4U && k < e->stack_count; ++k) {
            crash_text_char(&t, ' ');
            crash_text_hex(&t, e->stack[k]);
        }
        crash_text_char(&t, '\n');
        crash_puts(line);
    }
    __atomic_store_n(&e->sequence, sequence, __ATOMIC_RELEASE);
}

#if defined(__AROSEXEC_SMP__)
/* One bounded attempt to write-lock: the faulting CPU may never wait for a
 * lock it could itself be holding. */
static int crash_lock(spinlock_t *lock, struct Task *owner, uint64_t until)
{
    for (;;) {
        unsigned int value, failed = 1;

        __asm__ volatile(
            "1:  ldaxr %w0, [%2]\n\t"
            "    cbnz  %w0, 2f\n\t"
            "    stxr  %w1, %w3, [%2]\n\t"
            "    cbnz  %w1, 1b\n\t"
            "    b     3f\n\t"
            "2:  clrex\n\t"
            "3:\n\t"
            : "=&r"(value), "+&r"(failed)
            : "r"(&lock->lock), "r"(0x80000000U)
            : "memory");
        if (value == 0) {
            lock->s_Owner = owner;
            return 1;
        }
        if (crash_counter() > until)
            return 0;
        __asm__ volatile("yield");
    }
}

static void crash_unlock(spinlock_t *lock)
{
    lock->s_Owner = NULL;
    __asm__ volatile("stlr wzr, [%0]\n\tdsb ish\n\tsev"
        :: "r"(&lock->lock) : "memory");
}
#endif

/*
 * Park the running task for good and dispatch the next one into frame.
 * Returns 0, with nothing changed, when the scheduler locks cannot be had.
 */
static int crash_suspend(struct AROSAppleRuntimeFrame *frame,
    struct Task *task)
{
#if defined(__AROSEXEC_SMP__)
    uint64_t until = crash_counter() +
        crash_frequency() / (1000000U / CRASH_LOCK_TIMEOUT_US);
    struct IntExecBase *exec = PrivExecBase(SysBase);

    if (!crash_lock(&task->tc_SpinLock, task, until))
        return 0;
    if (!crash_lock(&exec->TaskRunningSpinLock, task, until)) {
        crash_unlock(&task->tc_SpinLock);
        return 0;
    }
    if (!crash_lock(&exec->TaskWaitSpinLock, task, until)) {
        crash_unlock(&exec->TaskRunningSpinLock);
        crash_unlock(&task->tc_SpinLock);
        return 0;
    }
#endif
    if (!cpu_SaveContext(frame, task)) {
#if defined(__AROSEXEC_SMP__)
        crash_unlock(&exec->TaskWaitSpinLock);
        crash_unlock(&exec->TaskRunningSpinLock);
        crash_unlock(&task->tc_SpinLock);
#endif
        return 0;
    }
    task->tc_IDNestCnt = IDNESTCOUNT_GET;
    task->tc_TDNestCnt = TDNESTCOUNT_GET;
    task->tc_SigWait = 0;
    task->tc_SigExcept = 0;
    task->tc_Flags &= ~(TF_EXCEPT | TF_SWITCH | TF_LAUNCH);
#if defined(__AROSEXEC_SMP__)
    __atomic_fetch_and(&GetIntETask(task)->iet_FreezeFlags,
        (UBYTE)~IETF_FREEZE_PENDING, __ATOMIC_ACQ_REL);
    Remove(&task->tc_Node);
#endif
    task->tc_State = TS_WAIT;
    Enqueue(&SysBase->TaskWait, &task->tc_Node);
#if defined(__AROSEXEC_SMP__)
    crash_unlock(&exec->TaskWaitSpinLock);
    crash_unlock(&exec->TaskRunningSpinLock);
    crash_unlock(&task->tc_SpinLock);
#endif
    /* The task's Forbid() and Disable() leave the CPU with it; the next
     * task's own nesting comes from core_Dispatch. */
    TDNESTCOUNT_SET(-1);
    IDNESTCOUNT_SET(-1);
    __atomic_add_fetch(&crash_log.suspended, 1, __ATOMIC_ACQ_REL);
    cpu_Dispatch(frame);
    return 1;
}

/*
 * The task route. 1: the task is suspended and frame holds the next task.
 * 0: the CPU halts; AROSAppleRuntimeHaltReport prints the decoded report
 * with the verdict reached here (the runtime proof's continuation owns a
 * fault during a proof).
 */
int AROSAppleKernelTaskCrash(struct AROSAppleRuntimeFrame *frame)
{
    struct AROSAppleCrashState state;
    struct Task *task;
    uint32_t verdict;

    crash_gather(frame, &state, &task);
    verdict = AROSAppleCrashDecide(&state);
    if (verdict == AROS_APPLE_CRASH_SUSPEND) {
        crash_report(frame, task, verdict);
        if (crash_suspend(frame, task))
            return 1;
        crash_puts("[KI] crash: scheduler locks busy, the task could not be "
            "suspended, this CPU halts\n");
        verdict = AROS_APPLE_CRASH_HALT_LOCK_TIMEOUT;
    }
    if (verdict != AROS_APPLE_CRASH_HALT_PROOF)
        crash_pending_verdict[crash_cpu()] = (uint8_t)(verdict + 1U);
    return 0;
}

/* The decoded report of a halting fault, after the halt's register page. */
void AROSAppleKernelCrashHaltDetails(const struct AROSAppleRuntimeFrame *frame)
{
    uint32_t cpu = crash_cpu();
    uint32_t verdict = crash_pending_verdict[cpu];

    crash_pending_verdict[cpu] = 0;
    if (verdict != 0)
        --verdict;
    else
        /* Not through the task route: an IRQ or FIQ nobody handled, an EL1
         * fault, an SError. */
        verdict = AROS_APPLE_CRASH_HALT_NOT_TASK;
    crash_report(frame, crash_task(), verdict);
}

intptr_t AROSAppleKernelCrashLogRecord(void)
{
    return (intptr_t)&crash_log;
}
