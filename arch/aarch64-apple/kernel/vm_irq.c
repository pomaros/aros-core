/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * VM adapter for the shared GIC and architectural timer. */
#include <aros/config.h>
#include "vm_irq.h"
#include "gic.h"
#include "cpu_monitor.h"
#include <aros/apple/runtime.h>
#include "kernel_cpu.h"
#include "kernel_intr.h"
#include "kernel_intern.h"
#include "kernel_globals.h"
#include "kernel_base.h"
#include "kernel_interrupts.h"
#include <aros/apple/startup.h>
static struct AROSGIC controller;
static int active;
static uint32_t timer_id;
static struct KernelBase *irq_base;
struct AROSGIC *AROSVMKernelBootController(void) { return active ? &controller : NULL; }
static uint8_t trigger_by_source[1020];
static uint8_t registered[1020];
/* These handoff words are serialized by callers holding Disable(), mono-CPU. */
static uint32_t change_source;
static uintptr_t enable_source(void)
{
    uint32_t id = change_source;
    if (!AROSGICEnable(&controller, id, trigger_by_source[id])) return 0;
    registered[id] = 1;
    return 1;
}
static uintptr_t disable_source(void)
{
    uint32_t id = change_source;
    registered[id] = 0;
    return AROSGICDisable(&controller, id);
}
int AROSVMKernelConfigureIRQ(uint32_t source, uint32_t trigger)
{
    /* SPI binding is explicit, before handler registration. Timer PPIs and
     * software IRQs belong to the kernel, not to device drivers. */
    if (!active || source < 32 || source >= controller.limit || registered[source] ||
        (trigger != 1 && trigger != 4)) return 0;
    trigger_by_source[source] = (uint8_t)trigger;
    return 1;
}
int AROSVMKernelIRQAllowed(uint32_t source, struct KernelBase *base)
{
    return active && base == irq_base && source < controller.limit &&
        source != timer_id && trigger_by_source[source] != 0;
}
uintptr_t AROSVMKernelProbePrepare(void)
{
    if (!active || registered[32] || trigger_by_source[32] != 1) return 0;
    /* The qualification owns SPI 32 until ProbeFinish. */
    return 1;
}
uintptr_t AROSVMKernelProbeFinish(void)
{
    if (!active || registered[32] ||
        (*(volatile uint32_t *)(controller.distributor + 0x104) & 1U)) return 0;
    trigger_by_source[32] = 0;
    return 1;
}
void AROSVMKernelEnableIRQ(uint32_t source, struct KernelBase *base)
{
    if (!active || base != irq_base || source >= controller.limit ||
        !trigger_by_source[source] || source == timer_id) return;
    /* Adding another handler shares an already enabled source. Do not
     * rewrite its trigger/routing registers while hardware can signal it. */
    if (registered[source]) return;
    change_source = source;
    (void)AROSAppleRuntimeSupervisor(enable_source);
}
void AROSVMKernelDisableIRQ(uint32_t source, struct KernelBase *base)
{
    if (!active || base != irq_base || source >= controller.limit || source == timer_id) return;
    change_source = source;
    (void)AROSAppleRuntimeSupervisor(disable_source);
}
uintptr_t AROSVMKernelProbeIRQ(void)
{
    if (!active || !registered[32]) return 0;
    /* Inject a pending SPI through the distributor, on this single CPU.
     * Unlike GICv2 SGIs, SPIs can always be disabled at the source. This
     * exercises the device-handler path, not a physical device signal. */
    uint32_t enabled = *(volatile uint32_t *)(controller.distributor + 0x104);
    if (!(enabled & 1U)) return 0;
    *(volatile uint32_t *)(controller.distributor + 0x204) = 1U;
    __asm__ volatile("dsb sy\n\tisb" ::: "memory");
    return 1;
}

static int mapped_windows_match(const struct AROSEarlyFDT *tree, int node)
{
    const struct AROSAppleTagItem *tags = (const void *)BootMsg;
    uint64_t windows[4] = {0};
    uint32_t seen = 0, index;
    if (!tags) return 0;
    for (index = 0; index < AROS_APPLE_MAX_BOOT_TAGS; ++index) {
        const struct AROSAppleTagItem *tag = &tags[index];
        if (tag->reserved) return 0;
        if (tag->tag == AROS_APPLE_TAG_DONE) break;
        if (tag->tag >= AROS_APPLE_KRN_GIC_DIST_START && tag->tag <= AROS_APPLE_KRN_GIC_CPU_END) {
            unsigned slot = tag->tag - AROS_APPLE_KRN_GIC_DIST_START;
            if (seen & (1U << slot)) return 0;
            seen |= 1U << slot;
            windows[slot] = tag->data;
        }
    }
    if (index == AROS_APPLE_MAX_BOOT_TAGS || seen != 15U) return 0;
    for (index = 0; index < 2; ++index) {
        uint64_t base, size;
        if (!AROSEarlyFDTReg(tree, node, index, &base, &size) ||
            windows[index * 2] != base || windows[index * 2 + 1] < base ||
            windows[index * 2 + 1] - base != size) return 0;
    }
    return 1;
}

int AROSVMKernelTimerInitialize(const void *blob, size_t size, struct KernelBase *base)
{
    struct AROSEarlyFDT tree;
    struct AROSFDTIRQ irq;
    int node = -1, depth = -1, timer = -1;
    uint32_t trigger;
    active = 0;
    if (!AROSEarlyFDTOpen(&tree, blob, size) ||
        (!AROSEarlyFDTCompatible(&tree, 0, "linux,dummy-virt") &&
         !AROSEarlyFDTCompatible(&tree, 0, "aros,virtualization")))
        return 0;
    while ((node = AROSEarlyFDTNext(&tree, node, &depth)) >= 0 && depth >= 0) {
        if (AROSEarlyFDTEnabled(&tree, node) &&
            AROSEarlyFDTCompatible(&tree, node, "arm,armv8-timer")) {
            if (timer >= 0) return 0;
            timer = node;
        }
    }
    /* Named bindings may omit the secure timer on machines without EL3. */
    if (timer < 0) return 0;
    int length;
    int found = AROSEarlyFDTProperty(&tree, timer, "interrupt-names", &length) ?
        AROSEarlyFDTInterruptNamed(&tree, timer, "virt", &irq) :
        AROSEarlyFDTInterrupt(&tree, timer, 2, &irq);
    if (!found ||
        !AROSGICResolve(&irq, &timer_id, &trigger) || timer_id < 16 || timer_id > 31)
        return 0;
    if (!mapped_windows_match(&tree, irq.controller)) {
        const char *message = "[VM] FAIL: GIC mapping contract\n";
        while (*message) AROSAppleKernelDebugPutDirect(*message++);
        return 0;
    }
    if (!AROSGICInit(&controller, &tree, irq.controller) ||
        !AROSGICEnable(&controller, timer_id, trigger))
        return 0;
    irq_base = base;
    active = 1;
    AROSAppleRuntimeSetIRQReady(1);
    const char *message = "[VM] GIC + CNTV deadline ready\n";
    while (*message) AROSAppleKernelDebugPutDirect(*message++);
    return 1;
}

int AROSVMKernelHandleIRQ(struct AROSAppleRuntimeFrame *frame)
{
    uint32_t token, id;
    struct AROSGIC *local_controller=&controller;
    int handled;
    if (!active || !frame) return 0;
    __asm__ volatile("msr DAIFSet, #3" ::: "memory");
    token = AROSGICAcknowledge(local_controller);
    id = token & 0x3ff;
    if (id >= 1020) { /* Spurious: no EOI, but restore idle accounting. */
        if (frame->vector != 9) cpu_IdleInterrupted(frame);
        return 1;
    }
    if (AROSCPUProbeHandleIPI(id)) handled = 1;
    else if (id == timer_id) {
        handled = AROSVMKernelTimerFire();
    }
    else if (id < controller.limit && registered[id]) {
        krnRunIRQHandlers(irq_base, (irqid_t)id);
        handled = 1;
    } else handled = 0;
    if (!handled) AROSGICDisable(local_controller, id);
    /* Deactivate the source before a callback's reschedule takes effect. */
    AROSGICEnd(local_controller, token);
    if (!handled) return 0;
    if (frame->vector == 9) core_ExitInterrupt(frame);
    else cpu_IdleInterrupted(frame);
    return 1;
}
