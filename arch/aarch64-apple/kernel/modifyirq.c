/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <aros/libcall.h>
#include <resources/kernel.h>
#include "kernel_base.h"
#include "platform_irq.h"
AROS_LH2(ULONG, KrnModifyIRQA,
    AROS_LHA(ULONG, irq, D0),
    AROS_LHA(struct TagItem *, attribs, A0),
    struct KernelBase *, KernelBase, 39, Kernel)
{
    AROS_LIBFUNC_INIT
    return AROSPlatformModifyIRQ(irq, attribs, KernelBase);
    AROS_LIBFUNC_EXIT
}
