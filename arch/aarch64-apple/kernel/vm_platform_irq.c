/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <aros/kernel.h>
#include <resources/kernel.h>
#include <proto/exec.h>
#include <aros/apple/runtime.h>
#include "platform_irq.h"
#include "platform_timer_kernel.h"
#include "vm_irq.h"
#include <aros/apple/platformirq.h>
int AROSPlatformHandleIRQ(struct AROSAppleRuntimeFrame *frame)
{ return AROSVMKernelHandleIRQ(frame); }
int AROSPlatformHandleFIQ(struct AROSAppleRuntimeFrame *frame)
{ return AROSAppleKernelTimerHandleFIQ(frame); }
int AROSPlatformFIQRequired(void) { return 0; }
int AROSPlatformHandleService(uint64_t service, struct AROSAppleRuntimeFrame *frame)
{ (void)service; (void)frame; return 0; }
/* The GIC owns IRQ readiness; disarming the timer deasserts its PPI. */
void AROSPlatformTimerDeliveryReady(int ready) { (void)ready; }
int AROSPlatformTimerInitialize(const void *fdt, size_t size, struct KernelBase *base)
{ return AROSVMKernelTimerInitialize(fdt, size, base); }
static uint32_t config_source, config_trigger;
static uintptr_t configure_irq(void)
{ return AROSVMKernelConfigureIRQ(config_source, config_trigger); }
uint32_t AROSPlatformModifyIRQ(uint32_t irq, struct TagItem *attribs, struct KernelBase *KernelBase)
{
    (void)KernelBase;
    uint32_t trigger = 0, seen = 0;
    int ended = 0;
    /* Follow ordinary TagItem controls, with a bounded traversal. */
    for (unsigned steps = 0; attribs && steps < 256; ++steps) {
        struct TagItem tag = *attribs++;
        if (tag.ti_Tag == TAG_DONE) { ended = 1; break; }
        if (tag.ti_Tag == TAG_IGNORE) continue;
        if (tag.ti_Tag == TAG_MORE) {
            if (!tag.ti_Data) { ended = 1; break; }
            attribs = (void *)tag.ti_Data; continue;
        }
        if (tag.ti_Tag == TAG_SKIP) {
            if (tag.ti_Data > 255 - steps) return 0;
            attribs += tag.ti_Data; steps += tag.ti_Data; continue;
        }
        if (tag.ti_Tag == KERNELTAG_IRQ_TRIGGERLEVEL) {
            if ((seen & 1) || (tag.ti_Data != 1 && tag.ti_Data != 2)) return 0;
            /* Existing kernel tag convention: 1=level, 2=edge. */
            trigger = tag.ti_Data == 1 ? 4 : 1; seen |= 1;
        } else if (tag.ti_Tag == KERNELTAG_IRQ_POLARITY) {
            if ((seen & 2) || tag.ti_Data != 1) return 0; /* active high */
            seen |= 2;
        } else return 0;
    }
    if (!ended || !trigger) return 0;
    Disable();
    config_source = irq; config_trigger = trigger;
    ULONG result = (ULONG)AROSAppleRuntimeSupervisor(configure_irq);
    Enable();
    return result;
}

void AROSPlatformEnableIRQ(uint32_t source, struct KernelBase *base)
{ AROSVMKernelEnableIRQ(source, base); }
void AROSPlatformDisableIRQ(uint32_t source, struct KernelBase *base)
{ AROSVMKernelDisableIRQ(source, base); }
int AROSPlatformIRQAllowed(uint32_t source, struct KernelBase *base)
{ return AROSVMKernelIRQAllowed(source, base); }
intptr_t AROSPlatformIRQAttribute(uint32_t id)
{ return id == AROS_APPLE_KATTR_IRQ_MASK ? 1 : -1; }
