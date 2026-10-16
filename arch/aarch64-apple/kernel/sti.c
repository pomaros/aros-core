/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Restore ready IRQ and FIQ delivery through the runtime boundary.
 */

#include <aros/apple/runtime.h>
#include <aros/kernel.h>
#include <aros/libcall.h>

#include <kernel_base.h>
#include <proto/kernel.h>

AROS_LH0I(void, KrnSti,
    struct KernelBase *, KernelBase, 10, Kernel)
{
    AROS_LIBFUNC_INIT

    AROSAppleRuntimeSti();

    AROS_LIBFUNC_EXIT
}
