/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#include <aros/kernel.h>
#include <aros/libcall.h>

#include "kernel_base.h"
#include "code_window.h"

#include <proto/kernel.h>

/*
 * Memory is identity-mapped, except the code window, whose pages lie
 * anywhere: its addresses are translated through its tables (0 when the
 * page is not mapped). Drivers turn buffer addresses into DMA addresses
 * here and check that a buffer is physically contiguous (dma.resource).
 */
AROS_LH1I(void *, KrnVirtualToPhysical,
    AROS_LHA(void *, virtual, A0),
    struct KernelBase *, KernelBase, 20, Kernel)
{
    AROS_LIBFUNC_INIT

    (void)KernelBase;
    if (AROSAppleCodeWindowContains(virtual, 1))
        return (void *)(uintptr_t)AROSAppleCodeWindowPhysical(virtual);
    return virtual;

    AROS_LIBFUNC_EXIT
}
