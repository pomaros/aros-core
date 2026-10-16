/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * FDT-opt-in actual Exec-task guard proof. No automatic task-stack policy.
 */
#include <aros/apple/runtime.h>
#include <aros/debug.h>
#include <aros/kernel.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#include "kernel_base.h"
#include <exec_intern.h>
#include "page_allocator.h"
#include "guard_proof.h"
#include "../../aarch64-all/kernel/guard_fault.h"

extern void AROSAppleGuardProbe(void *lower);
extern char AROSAppleGuardProbeFault[], AROSAppleGuardProbeResume[];

static struct {
    struct AArch64GuardedStack stack;
    struct Task *parent;
    struct Task *child;
    ULONG signal;
    volatile unsigned armed, hits, continued;
    uintptr_t fault_address;
} fixture;

int AROSAppleGuardProofFault(struct AROSAppleRuntimeFrame *frame)
{
    if (!frame || !fixture.armed || fixture.hits || !SysBase ||
        !AArch64GuardProbeMatches((uintptr_t)fixture.child,
            (uintptr_t)GET_THIS_TASK, (uintptr_t)fixture.stack.lower,
            (uintptr_t)fixture.stack.upper, (uintptr_t)AROSAppleGuardProbeFault,
            frame->vector, frame->spsr, frame->esr, frame->far, frame->elr,
            frame->sp, frame->x[9]))
        return 0;
    fixture.fault_address = frame->far;
    fixture.hits = 1;
    fixture.armed = 0;
    frame->elr = (uintptr_t)AROSAppleGuardProbeResume;
    return 1;
}

static void guard_task(void)
{
    AROSAppleGuardProbe(fixture.stack.lower);
    fixture.continued = 1;
    Signal(fixture.parent, fixture.signal);
    for (;;)
        Wait(0);
}

static int run_task(int guarded)
{
    struct Task *task = AllocMem(sizeof(*task), MEMF_PUBLIC | MEMF_CLEAR);
    int passed;
    if (!task)
        return 0;
    task->tc_Node.ln_Type = NT_TASK;
    task->tc_Node.ln_Name = guarded ? "mp1-guard" : "mp1-control";
    task->tc_Node.ln_Pri = fixture.parent->tc_Node.ln_Pri + 1;
    task->tc_SPLower = fixture.stack.lower;
    task->tc_SPUpper = fixture.stack.upper;
    task->tc_SPReg = fixture.stack.upper;
    /* Both Task and arena stack remain caller-owned. FreeEntry must never
     * receive the page arena pointer; RemTask(other) only frees its ETask. */
    NEWLIST(&task->tc_MemEntry);
    fixture.child = task;
    fixture.armed = guarded;
    fixture.hits = fixture.continued = 0;
    SetSignal(0, fixture.signal);
    if (!AddTask(task, guard_task, NULL)) {
        fixture.child = NULL;
        fixture.armed = 0;
        FreeMem(task, sizeof(*task));
        return 0;
    }
    while (!fixture.continued)
        Wait(fixture.signal);
    /* Child priority is strictly higher: after signalling us it must park
     * before this parent runs. Never release a live stack on an assumption. */
    Forbid();
    passed = task->tc_State == TS_WAIT && fixture.continued &&
        fixture.hits == (unsigned)guarded;
    fixture.armed = 0;
    if (task->tc_State != TS_WAIT) {
        Permit();
        return 0; /* Retain allocations rather than free a potentially live stack. */
    }
    RemTask(task);
    fixture.child = NULL;
    Permit();
    FreeMem(task, sizeof(*task));
    return passed;
}

int AROSAppleGuardProof(void)
{
    BYTE bit;
    int ok = 0;
    if (fixture.child || fixture.stack.allocation)
        return 0;
    fixture.parent = FindTask(NULL);
    if (!fixture.parent || fixture.parent->tc_Node.ln_Pri == 127)
        return 0;
    bit = AllocSignal(-1);
    if (bit < 0)
        return 0;
    fixture.signal = 1UL << bit;
    if (!AROSAppleGuardedStackCreate(&fixture.stack, 65536))
        goto done;
    if (!AROSApplePageProtect(fixture.stack.allocation, fixture.stack.guard_size,
            MAP_Readable | MAP_Writable) || !run_task(0))
        goto done;
    bug("[MP1] (control) no-guard: access completed\n");
    if (!AROSApplePageProtect(fixture.stack.allocation, fixture.stack.guard_size, 0) ||
        !run_task(1))
        goto done;
    ok = 1;
done:
    if (!fixture.child) {
        if (!AROSAppleGuardedStackDestroy(&fixture.stack))
            ok = 0;
        FreeSignal(bit);
    } else {
        ok = 0;
    }
    if (ok) {
        bug("[MP1] PASS guard hit: task=mp1-guard addr=%p (stack guard), overflow caught\n",
            (void *)fixture.fault_address);
        bug("[MP1] continuation: child resumed, parked, removed; parent running\n");
    } else {
        bug("[MP1] FAIL controlled task guard proof\n");
    }
    return ok;
}
