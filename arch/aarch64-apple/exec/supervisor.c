/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Execute a bounded callback through the Apple AArch64 SVC boundary.
 */

#include <aros/apple/runtime.h>

#include <proto/exec.h>

static uintptr_t apple_runtime_supervisor(uintptr_t (*callback)(void))
{
    register uintptr_t x0 __asm__("x0") = (uintptr_t)callback;

    __asm__ volatile("svc %1"
        : "+r"(x0)
        : "i"(AROS_APPLE_RUNTIME_SVC_SUPERVISOR)
        : "memory", "cc");

    return x0;
}

AROS_LH1(IPTR, Supervisor,
    AROS_LHA(void *, userFunction, A5),
    struct ExecBase *, SysBase, 5, Exec)
{
    AROS_LIBFUNC_INIT

    return (IPTR)apple_runtime_supervisor((uintptr_t (*)(void))userFunction);

    AROS_LIBFUNC_EXIT
}
