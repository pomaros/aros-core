/*
    Copyright (C) 2026, John KNIPPER. All rights reserved.

    Desc: Report the privileged EL during the EL1-only bring-up phase.
*/

#include <aros/kernel.h>
#include <aros/apple/runtime.h>
#include <aros/libcall.h>

#include <kernel_base.h>

#include <proto/kernel.h>

AROS_LH0I(int, KrnIsSuper,
    struct KernelBase *, KernelBase, 13, Kernel)
{
    AROS_LIBFUNC_INIT

    return AROSAppleRuntimeIsSuper();

    AROS_LIBFUNC_EXIT
}
