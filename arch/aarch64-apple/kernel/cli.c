/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Mask IRQ and FIQ delivery through the Apple runtime boundary.
 */

#include <aros/apple/runtime.h>
#include <aros/kernel.h>
#include <aros/libcall.h>

#include <kernel_base.h>
#include <proto/kernel.h>

AROS_LH0I(void, KrnCli,
    struct KernelBase *, KernelBase, 9, Kernel)
{
    AROS_LIBFUNC_INIT

    AROSAppleRuntimeCli();

    AROS_LIBFUNC_EXIT
}
