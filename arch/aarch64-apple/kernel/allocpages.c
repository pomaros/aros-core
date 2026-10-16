/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#include <aros/libcall.h>

#include "kernel_base.h"
#include "page_allocator.h"

#include <proto/kernel.h>

AROS_LH3(void *, KrnAllocPages,
    AROS_LHA(void *, address, A0),
    AROS_LHA(uintptr_t, length, D0),
    AROS_LHA(uint32_t, flags, D1),
    struct KernelBase *, KernelBase, 27, Kernel)
{
    AROS_LIBFUNC_INIT

    (void)KernelBase;
    return AROSApplePageAllocate(address, length, flags);

    AROS_LIBFUNC_EXIT
}
