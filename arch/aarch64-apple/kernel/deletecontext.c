/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <aros/kernel.h>
#include <aros/libcall.h>
#include <proto/exec.h>
#include <kernel_base.h>
#include "restricted.h"

AROS_LH1(void, KrnDeleteContext,
    AROS_LHA(void *, context, A0),
    struct KernelBase *, KernelBase, 19, Kernel)
{
    AROS_LIBFUNC_INIT
    /* RemTask has detached the task; SMP Exec retires/quiesces it first.
     * No wait or allocation on the exception stack. Owner backing remains
     * retained until worker leases drain, then native-context reaping frees it. */
    if (context) AROSAppleRestrictedContextRetired(context);
    FreeMem(context,KernelBase->kb_ContextSize);
    AROS_LIBFUNC_EXIT
}
