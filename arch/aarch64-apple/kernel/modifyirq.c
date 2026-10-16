/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * IRQ trigger attributes for VM GIC; source masking for native Apple AIC.
 * Returns 1 for accepted configuration, 0 for unsupported profiles, sources,
 * attribute lists or reconfiguration while a handler is installed. */
#include <aros/kernel.h>
#include <aros/libcall.h>
#include <resources/kernel.h>
#include <proto/exec.h>
#include <aros/apple/runtime.h>
#include "kernel_base.h"
#include <proto/kernel.h>
#include "vm_irq.h"


static uint32_t config_source, config_trigger;
static uintptr_t configure_irq(void)
{
    return AROSVMKernelConfigureIRQ(config_source, config_trigger);
}
AROS_LH2(ULONG, KrnModifyIRQA,
    AROS_LHA(ULONG, irq, D0),
    AROS_LHA(struct TagItem *, attribs, A0),
    struct KernelBase *, KernelBase, 39, Kernel)
{
    AROS_LIBFUNC_INIT
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
    AROS_LIBFUNC_EXIT
}
