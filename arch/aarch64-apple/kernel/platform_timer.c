/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Publish and own the ARM virtual timer for the Apple target.
 */

#include <aros/apple/platformtimer.h>
#include <aros/apple/runtime.h>
#include <aros/apple/startup.h>
#include <aros/symbolsets.h>

#include <stdint.h>

#include "kernel_base.h"
#include "kernel_globals.h"
#include "kernel_intr.h"
#include "kernel_cpu.h"
#include "kernel_intern.h"
#include "platform_timer_kernel.h"
#include "vm_irq.h"
#include "platform_irq.h"
#if defined(__AROSEXEC_SMP__)
#include "smp.h"
#include "cpu_monitor.h"
#endif

#define CNTKCTL_EL0VCTEN UINT64_C(1U << 1)

struct KrnPlatformTimer AROSApplePlatformTimer;
static struct AROSAppleTimerState kernel_timer;

static uint64_t target_read_counter(void *context)
{
    uint64_t value;

    (void)context;
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(value) :: "memory");
    return value;
}

static uint32_t target_read_control(void *context)
{
    uint64_t value;

    (void)context;
    __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(value));
    return (uint32_t)value;
}

static void target_write_control(void *context, uint32_t value)
{
    (void)context;
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"((uint64_t)value) :
        "memory");
}

static void target_write_compare(void *context, uint64_t value)
{
    (void)context;
    __asm__ volatile("msr cntv_cval_el0, %0" :: "r"(value) : "memory");
}

static void target_synchronize(void *context)
{
    (void)context;
    __asm__ volatile("isb" ::: "memory");
}

static int retained_fdt(const void **fdt, size_t *fdt_size)
{
    const struct AROSAppleTagItem *tags =
        (const struct AROSAppleTagItem *)(const void *)BootMsg;
    uint32_t index;
    uint32_t seen_fdt = 0;
    uint32_t seen_size = 0;

    if (tags == NULL || fdt == NULL || fdt_size == NULL)
        return 0;
    *fdt = NULL;
    *fdt_size = 0;
    for (index = 0; index < AROS_APPLE_MAX_BOOT_TAGS; ++index) {
        const struct AROSAppleTagItem *tag = &tags[index];

        if (tag->tag == AROS_APPLE_TAG_DONE)
            break;
        if (tag->reserved != 0)
            return 0;
        if (tag->tag == AROS_APPLE_KRN_FLATTENED_DEVICE_TREE) {
            if (seen_fdt++)
                return 0;
            *fdt = (const void *)(uintptr_t)tag->data;
        } else if (tag->tag == AROS_APPLE_KRN_FDT_SIZE) {
            if (seen_size++ || tag->data > SIZE_MAX)
                return 0;
            *fdt_size = (size_t)tag->data;
        }
    }
    return index < AROS_APPLE_MAX_BOOT_TAGS && seen_fdt == 1U &&
        seen_size == 1U && *fdt != NULL && *fdt_size >= 40U &&
        *fdt_size <= AROS_APPLE_FDT_MAX_SIZE;
}

#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
int AROSVMKernelTimerFire(void)
{
    enum AROSAppleTimerStatus status = AROSAppleTimerHandle(&kernel_timer);
    if (status != AROS_APPLE_TIMER_OK && status != AROS_APPLE_TIMER_NOT_PENDING) {
        const char *message = "[VM] timer fire failed: ";
        while (*message) AROSAppleKernelDebugPutDirect(*message++);
        message = AROSAppleTimerStatusName(status);
        while (*message) AROSAppleKernelDebugPutDirect(*message++);
        AROSAppleKernelDebugPutDirect('\n');
    }
    /* A source may deassert after the GIC sampled it; it is still ours. */
    return status == AROS_APPLE_TIMER_OK || status == AROS_APPLE_TIMER_NOT_PENDING;
}
#endif

static void fill_control(
    struct AROSAppleTimerControl *control,
    enum AROSAppleTimerStatus status)
{
    control->status = (uint32_t)status;
    control->programmed_deadline = kernel_timer.programmed_deadline;
    control->generation = kernel_timer.generation;
    control->fired_count = kernel_timer.fired_count;
    control->unexpected_count = kernel_timer.unexpected_count;
    control->flags = 0;
    if (kernel_timer.claimed != 0)
        control->flags |= AROS_APPLE_TIMER_CONTROL_CLAIMED;
    if (AROSAppleTimerArmed(&kernel_timer))
        control->flags |= AROS_APPLE_TIMER_CONTROL_ARMED;
}

static uint32_t idle_fiq_trace;
static uint64_t busy_fiq_count;
static uint32_t busy_fiq_trace;
static uint32_t control_trace;

static void fiq_report(const char *label, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    while (*label != 0)
        AROSAppleKernelDebugPutDirect(*label++);
    for (shift = 60; shift >= 0; shift -= 4)
        AROSAppleKernelDebugPutDirect(digits[(value >> shift) & 0xfU]);
}

/* Runs only on the owner CPU, with interrupt dispatch excluded. Notifications
 * coalesce: the callback always derives the deadline from the current queue. */
int AROSAppleKernelTimerNotify(void)
{
#if defined(__AROSEXEC_SMP__)
    if (AROSAppleSMPIsSecondary())
        return AROS_APPLE_TIMER_OWNERSHIP;
#endif
    /* A queued notification can outlive release; there is no work left. */
    if (!kernel_timer.claimed)
        return AROS_APPLE_TIMER_OK;
    enum AROSAppleTimerStatus status = AROSAppleTimerRefresh(
        &kernel_timer, kernel_timer.owner);
    AROSPlatformTimerDeliveryReady(AROSAppleTimerArmed(&kernel_timer));
    if (status != AROS_APPLE_TIMER_OK)
        fiq_report("[KT] FAIL timer refresh status=", status);
    return status;
}

int AROSAppleKernelTimerControl(struct AROSAppleTimerControl *control)
{
    enum AROSAppleTimerStatus status;
    uint32_t operation;
    uint64_t owner;

    if (control == NULL || ((uintptr_t)control & 7U) != 0 ||
        control->version != AROS_APPLE_TIMER_CONTROL_VERSION ||
        control->size != sizeof(*control) ||
        (AROSApplePlatformTimer.kpt_Flags & KPTF_DEADLINE_READY) == 0)
        return AROS_APPLE_TIMER_ARGUMENT;
#if defined(__AROSEXEC_SMP__)
    /* CNTV belongs to this CPU. Remote callers may only send a notification,
     * never query/mutate the BSP state or touch their scheduler's registers. */
    if (control->operation == AROS_APPLE_TIMER_OP_NOTIFY) {
        if (!kernel_timer.claimed || control->owner != kernel_timer.owner)
            return AROS_APPLE_TIMER_OWNERSHIP;
        if (AROSAppleSMPIsSecondary())
            AROSAppleSMPSendIPI(AROS_APPLE_TIMER_IPI, 0,
                1U << AROSCPUMonitorTopology()->boot_cpu);
        else {
            status = AROSAppleKernelTimerNotify();
            control->status = status;
            return status;
        }
        control->status = AROS_APPLE_TIMER_OK;
        return AROS_APPLE_TIMER_OK;
    }
    if (AROSAppleSMPIsSecondary())
        return AROS_APPLE_TIMER_OWNERSHIP;
#endif
    operation = control->operation;
    owner = control->owner;
    switch (operation) {
    case AROS_APPLE_TIMER_OP_QUERY:
        status = AROS_APPLE_TIMER_OK;
        break;
    case AROS_APPLE_TIMER_OP_CLAIM:
        if (control->callback == 0 || (control->callback & 3U) != 0) {
            status = AROS_APPLE_TIMER_ARGUMENT;
            break;
        }
        status = AROSAppleTimerClaim(&kernel_timer, owner,
            (AROSAppleTimerCallback)(uintptr_t)control->callback,
            (void *)(uintptr_t)owner);
        break;
    case AROS_APPLE_TIMER_OP_ARM:
        status = AROSAppleTimerArm(&kernel_timer, owner, control->deadline);
        if (status == AROS_APPLE_TIMER_OK)
            AROSPlatformTimerDeliveryReady(1);
        break;
    case AROS_APPLE_TIMER_OP_DISARM:
        status = AROSAppleTimerDisarm(&kernel_timer, owner);
        if (status == AROS_APPLE_TIMER_OK)
            AROSPlatformTimerDeliveryReady(0);
        break;
    case AROS_APPLE_TIMER_OP_RELEASE:
        status = AROSAppleTimerRelease(&kernel_timer, owner);
        if (status == AROS_APPLE_TIMER_OK)
            AROSPlatformTimerDeliveryReady(0);
        break;
    default:
        status = AROS_APPLE_TIMER_ARGUMENT;
        break;
    }
    fill_control(control, status);
    if (AROSAppleKernelIdleTraceEnable != 0 && control_trace < 16 &&
            (operation == AROS_APPLE_TIMER_OP_ARM ||
             operation == AROS_APPLE_TIMER_OP_DISARM ||
             operation == AROS_APPLE_TIMER_OP_RELEASE)) {
        uint64_t ctl, now;

        ++control_trace;
        __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(ctl));
        __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(now));
        fiq_report("[KT] timer op=0x", operation);
        fiq_report(" status=0x", (uint64_t)(uint32_t)status);
        fiq_report(" deadline=0x", control->deadline);
        fiq_report(" now=0x", now);
        fiq_report(" cntv_ctl=0x", ctl);
        fiq_report(" fiq_ready=0x", AROSAppleRuntimeLocal()->fiq_ready);
        AROSAppleKernelDebugPutDirect('\n');
    }
    return status;
}

/*
 * An FIQ this handler cannot claim ends the session as an unexpected
 * exception; before it does, say once what the timer looked like, on the
 * EL1 sink directly (no service call from exception context).
 */

static void fiq_unhandled(uint32_t status)
{
    static int reported;
    uint64_t ctl;
    uint64_t pctl;
    uint64_t isr;

    if (reported)
        return;
    reported = 1;
    __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(ctl));
    __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(pctl));
    __asm__ volatile("mrs %0, isr_el1" : "=r"(isr));
    fiq_report("[KI] fiq unhandled cntv_ctl=0x", ctl);
    fiq_report(" cntp_ctl=0x", pctl);
    fiq_report(" isr=0x", isr);
    fiq_report(" flags=0x", AROSApplePlatformTimer.kpt_Flags);
    fiq_report(" status=0x", status);
    fiq_report(" claimed=0x", kernel_timer.claimed);
    fiq_report(" armed=0x", kernel_timer.armed);
    fiq_report(" deadline=0x", kernel_timer.programmed_deadline);
    AROSAppleKernelDebugPutDirect('\n');
}

int AROSAppleKernelTimerHandleFIQ(struct AROSAppleRuntimeFrame *frame)
{
    enum AROSAppleTimerStatus status;

    if (frame == NULL ||
        (AROSApplePlatformTimer.kpt_Flags & KPTF_DEADLINE_READY) == 0) {
        fiq_unhandled(0xffffffffU);
        return 0;
    }
    /* timer.device and Exec queues must not be re-entered by IRQ or FIQ. */
    __asm__ volatile("msr DAIFSet, #3" ::: "memory");
    status = AROSAppleTimerHandle(&kernel_timer);
    /*
     * Bounded trace of the FIQs taken from a current-EL context, i.e. the
     * dispatcher's idle loop: the first twelve say what the timer looked
     * like and how many task-context FIQs came before. A wait that only
     * completes while a task is running is diagnosed here.
     */
    if (frame->vector >= 8) {
        ++busy_fiq_count;
        if (AROSAppleKernelIdleTraceEnable != 0 && busy_fiq_trace < 6) {
            ++busy_fiq_trace;
            fiq_report("[KT] busy fiq status=0x", (uint64_t)(uint32_t)status);
            fiq_report(" armed=0x", kernel_timer.armed);
            fiq_report(" deadline=0x", kernel_timer.programmed_deadline);
            fiq_report(" fired=0x", kernel_timer.fired_count);
            AROSAppleKernelDebugPutDirect('\n');
        }
    } else if (idle_fiq_trace < 12) {
        ++idle_fiq_trace;
        fiq_report("[KT] idle fiq vector=0x", frame->vector);
        fiq_report(" status=0x", (uint64_t)(uint32_t)status);
        fiq_report(" armed=0x", kernel_timer.armed);
        fiq_report(" deadline=0x", kernel_timer.programmed_deadline);
        fiq_report(" fired=0x", kernel_timer.fired_count);
        fiq_report(" busy_fiqs=0x", busy_fiq_count);
        fiq_report(" spsr=0x", frame->spsr);
        AROSAppleKernelDebugPutDirect('\n');
    }
    if (status == AROS_APPLE_TIMER_NOT_PENDING) {
        /*
         * Under the m1n1 hypervisor the guest's virtual FIQ line is raised
         * when the timer expires and lowered only at the guest's next trap,
         * so the FIQ that follows a handled, re-armed expiry arrives with
         * the timer no longer asserted. It is stale, not unexpected: touch a
         * trapped register so the line is recomputed, and report it handled.
         * A source that is neither our timer nor this echo is still a fault.
         */
        if (kernel_timer.claimed != 0) {
            AROSAppleKernelDebugTouchSink();
            AROSPlatformTimerDeliveryReady(AROSAppleTimerArmed(&kernel_timer));
            cpu_IdleInterrupted(frame);
            return 1;
        }
        fiq_unhandled((uint32_t)status);
        return 0;
    }
    if (status != AROS_APPLE_TIMER_OK) {
        fiq_unhandled((uint32_t)status);
        target_write_control(NULL, AROS_APPLE_TIMER_CTL_IMASK);
        target_synchronize(NULL);
        AROSPlatformTimerDeliveryReady(0);
        return 0;
    }
    AROSPlatformTimerDeliveryReady(AROSAppleTimerArmed(&kernel_timer));
    /* A task (lower-EL vector) takes the ordinary scheduler exit; the idle
     * context (current-EL vector) dispatches directly, see kernel_cpu.c. */
    if (frame->vector == 10)
        core_ExitInterrupt(frame);
    else
        cpu_IdleInterrupted(frame);
    return 1;
}

static int platform_timer_Init(struct KernelBase *KernelBase)
{
    struct AROSAppleTimerIO io;
    const void *fdt;
    size_t fdt_size;
    uint64_t control;
    uint64_t frequency;

    (void)KernelBase;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    if (frequency == 0 || frequency > UINT32_MAX)
        return FALSE;

    /* AROS tasks read EClock at EL0; permit only the virtual count register. */
    __asm__ volatile("mrs %0, cntkctl_el1" : "=r"(control));
    control |= CNTKCTL_EL0VCTEN;
    __asm__ volatile("msr cntkctl_el1, %0; isb" :: "r"(control) : "memory");
    __asm__ volatile("mrs %0, cntkctl_el1" : "=r"(control));
    if ((control & CNTKCTL_EL0VCTEN) == 0)
        return FALSE;

    AROSApplePlatformTimer.kpt_Frequency = (ULONG)frequency;
    AROSApplePlatformTimer.kpt_InitialCounter = KrnPlatformTimerRead();
    AROSApplePlatformTimer.kpt_Flags = KPTF_COUNTER_READY;

    io.read_counter = target_read_counter;
    io.read_control = target_read_control;
    io.write_control = target_write_control;
    io.write_compare = target_write_compare;
    io.synchronize = target_synchronize;
    if (AROSAppleTimerInitialize(&kernel_timer, frequency, &io, NULL) !=
            AROS_APPLE_TIMER_OK)
        return FALSE;
    if (retained_fdt(&fdt, &fdt_size) &&
        AROSPlatformTimerInitialize(fdt, fdt_size, KernelBase))
        AROSApplePlatformTimer.kpt_Flags |= KPTF_DEADLINE_READY;
    return TRUE;
}

ADD2INITLIB(platform_timer_Init, 4);
