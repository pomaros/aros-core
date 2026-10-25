/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Prove the Apple AArch64 boot task and two Exec task contexts.
 */

#include <aros/apple/runtime.h>

#include <exec/execbase.h>
#include <exec/interrupts.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#define __KERNEL_NOLIBBASE__
#include <proto/kernel.h>
#include <utility/tagitem.h>

#include <stdint.h>

#include "kernel_base.h"
#include <exec_intern.h>

#define DISPATCH_BOOT_1 UINT64_C(0xb001)
#define DISPATCH_TASK_A_1 UINT64_C(0xa001)
#define DISPATCH_TASK_B_1 UINT64_C(0xb101)
#define DISPATCH_BOOT_2 UINT64_C(0xb002)
#define DISPATCH_TASK_A_2 UINT64_C(0xa002)
#define DISPATCH_TASK_B_2 UINT64_C(0xb102)
#define DISPATCH_BOOT_3 UINT64_C(0xb003)

_Static_assert(sizeof(struct ExceptionContext) ==
    AROS_APPLE_EXCEPTION_CONTEXT_SIZE, "AArch64 exception context size");

struct DispatchTaskAllocation {
    struct Task *task;
    struct MemList *memory;
};

static const char task_a_name[] = "B2 dispatch task A";
static const char task_b_name[] = "B2 dispatch task B";
static const char boot_task_name[] = "B2 dispatch boot task";

extern void AROSAppleDispatchSwitchState(
    uint64_t gpr_base, uint64_t simd_low, uint64_t simd_high,
    uint64_t gpr_observed[AROS_APPLE_DISPATCH_GPR_COUNT],
    uint64_t simd_observed[2], uint64_t *platform_observed);

static void dispatch_boot_probe(struct AROSAppleDispatchProof *proof);

static void clear_dispatch_proof(struct AROSAppleDispatchProof *proof)
{
    volatile uint64_t *word = (volatile uint64_t *)proof;
    uint32_t index;

    for (index = 0; index < sizeof(*proof) / sizeof(*word); ++index)
        word[index] = 0;
}

static int record_sequence(
    struct AROSAppleDispatchProof *proof, uint64_t value)
{
    uint64_t index = proof->sequence_count;

    if (index >= AROS_APPLE_DISPATCH_SEQUENCE_COUNT)
        return 0;
    proof->sequence[index] = value;
    proof->sequence_count = index + 1U;
    return 1;
}

static int gprs_valid(const uint64_t *observed, uint64_t base)
{
    uint32_t index;

    for (index = 0; index < AROS_APPLE_DISPATCH_GPR_COUNT; ++index) {
        if (observed[index] != base + index)
            return 0;
    }
    return 1;
}

static void free_unlaunched_task(struct DispatchTaskAllocation *allocation)
{
    if (allocation->memory != NULL) {
        FreeEntry(allocation->memory);
    } else if (allocation->task != NULL) {
        FreeMem(allocation->task, sizeof(*allocation->task));
    }
    allocation->task = NULL;
    allocation->memory = NULL;
}

static int allocate_task(
    struct DispatchTaskAllocation *allocation,
    const char *name, void (*entry)(struct AROSAppleDispatchProof *),
    struct AROSAppleDispatchProof *proof,
    uint64_t *stack_lower, uint64_t *stack_upper)
{
    const uint32_t entry_count = 2;
    const uint32_t memory_size = sizeof(struct MemList) -
        sizeof(struct MemEntry) + entry_count * sizeof(struct MemEntry);
    struct TagItem tags[2];
    unsigned char *stack;

    allocation->task = NULL;
    allocation->memory = AllocMem(memory_size, MEMF_PUBLIC | MEMF_CLEAR);
    if (allocation->memory == NULL)
        return 0;
    allocation->task = AllocMem(sizeof(*allocation->task),
        MEMF_PUBLIC | MEMF_CLEAR);
    stack = AllocMem(AROS_APPLE_DISPATCH_TASK_STACK_SIZE,
        MEMF_PUBLIC | MEMF_CLEAR);
    if (allocation->task == NULL || stack == NULL) {
        if (stack != NULL)
            FreeMem(stack, AROS_APPLE_DISPATCH_TASK_STACK_SIZE);
        if (allocation->task != NULL)
            FreeMem(allocation->task, sizeof(*allocation->task));
        FreeMem(allocation->memory, memory_size);
        allocation->task = NULL;
        allocation->memory = NULL;
        return 0;
    }

    allocation->memory->ml_NumEntries = entry_count;
    allocation->memory->ml_ME[0].me_Addr = allocation->task;
    allocation->memory->ml_ME[0].me_Length = sizeof(*allocation->task);
    allocation->memory->ml_ME[1].me_Addr = stack;
    allocation->memory->ml_ME[1].me_Length =
        AROS_APPLE_DISPATCH_TASK_STACK_SIZE;
    allocation->task->tc_Node.ln_Name = (char *)name;
    allocation->task->tc_Node.ln_Pri = 0;
    allocation->task->tc_SPLower = stack;
    allocation->task->tc_SPUpper =
        stack + AROS_APPLE_DISPATCH_TASK_STACK_SIZE;
    allocation->task->tc_SPReg = allocation->task->tc_SPUpper;
    NEWLIST(&allocation->task->tc_MemEntry);
    AddHead(&allocation->task->tc_MemEntry,
        &allocation->memory->ml_Node);

    tags[0].ti_Tag = TASKTAG_ARG1;
    tags[0].ti_Data = (IPTR)proof;
    tags[1].ti_Tag = TAG_DONE;
    tags[1].ti_Data = 0;
    if (NewAddTask(allocation->task, (APTR)entry, NULL, tags) == NULL) {
        free_unlaunched_task(allocation);
        return 0;
    }
    *stack_lower = (uint64_t)(uintptr_t)stack;
    *stack_upper = (uint64_t)(uintptr_t)stack +
        AROS_APPLE_DISPATCH_TASK_STACK_SIZE;
    return 1;
}

static int prepare_boot_task(
    struct DispatchTaskAllocation *allocation,
    uintptr_t stack_lower, uintptr_t stack_upper,
    struct AROSAppleDispatchProof *proof)
{
    struct TagItem tags[2];

    allocation->task = NULL;
    allocation->memory = AllocMem(sizeof(struct MemList),
        MEMF_PUBLIC | MEMF_CLEAR);
    if (allocation->memory == NULL) {
        proof->status = UINT64_C(0x91);
        return 0;
    }
    allocation->task = AllocMem(sizeof(*allocation->task),
        MEMF_PUBLIC | MEMF_CLEAR);
    if (allocation->task == NULL) {
        proof->status = UINT64_C(0x92);
        FreeMem(allocation->memory, sizeof(struct MemList));
        allocation->memory = NULL;
        return 0;
    }

    allocation->memory->ml_NumEntries = 1;
    allocation->memory->ml_ME[0].me_Addr = allocation->task;
    allocation->memory->ml_ME[0].me_Length = sizeof(*allocation->task);
    allocation->task->tc_Node.ln_Name = (char *)boot_task_name;
    allocation->task->tc_Node.ln_Pri = 0;
    allocation->task->tc_SPLower = (APTR)stack_lower;
    allocation->task->tc_SPUpper = (APTR)stack_upper;
    allocation->task->tc_SPReg = (APTR)stack_upper;
    NEWLIST(&allocation->task->tc_MemEntry);
    AddHead(&allocation->task->tc_MemEntry,
        &allocation->memory->ml_Node);

    tags[0].ti_Tag = TASKTAG_ARG1;
    tags[0].ti_Data = (IPTR)proof;
    tags[1].ti_Tag = TAG_DONE;
    tags[1].ti_Data = 0;
    if (NewAddTask(allocation->task, (APTR)dispatch_boot_probe,
            NULL, tags) == NULL) {
        proof->status = UINT64_C(0x93);
        free_unlaunched_task(allocation);
        return 0;
    }
    Remove(&allocation->task->tc_Node);
    allocation->task->tc_State = TS_RUN;
    SET_THIS_TASK(allocation->task);
    return 1;
}

static void dispatch_task_a(struct AROSAppleDispatchProof *proof)
{
    uint64_t stack_marker;

    proof->stack_a_observed = (uintptr_t)&stack_marker;
    if (!record_sequence(proof, DISPATCH_TASK_A_1))
        proof->status = 0;
    ++proof->task_a_switches;
    AROSAppleDispatchSwitchState(AROS_APPLE_DISPATCH_TASK_A_GPR_BASE,
        AROS_APPLE_DISPATCH_TASK_A_SIMD_LOW,
        AROS_APPLE_DISPATCH_TASK_A_SIMD_HIGH, proof->task_a_gpr_after,
        proof->task_a_simd_after, &proof->task_a_x18_after);
    if (!record_sequence(proof, DISPATCH_TASK_A_2))
        proof->status = 0;
    ++proof->task_a_switches;
    KrnSwitch();
    for (;;) {
        proof->status = 0;
        KrnSwitch();
    }
}

static void dispatch_task_b(struct AROSAppleDispatchProof *proof)
{
    uint64_t stack_marker;

    proof->stack_b_observed = (uintptr_t)&stack_marker;
    if (!record_sequence(proof, DISPATCH_TASK_B_1))
        proof->status = 0;
    ++proof->task_b_switches;
    AROSAppleDispatchSwitchState(AROS_APPLE_DISPATCH_TASK_B_GPR_BASE,
        AROS_APPLE_DISPATCH_TASK_B_SIMD_LOW,
        AROS_APPLE_DISPATCH_TASK_B_SIMD_HIGH, proof->task_b_gpr_after,
        proof->task_b_simd_after, &proof->task_b_x18_after);
    if (!record_sequence(proof, DISPATCH_TASK_B_2))
        proof->status = 0;
    ++proof->task_b_switches;
    KrnSwitch();
    for (;;) {
        proof->status = 0;
        KrnSwitch();
    }
}

static int dispatch_sequence_valid(const struct AROSAppleDispatchProof *proof)
{
    static const uint64_t expected[AROS_APPLE_DISPATCH_SEQUENCE_COUNT] = {
        DISPATCH_BOOT_1,
        DISPATCH_TASK_A_1,
        DISPATCH_TASK_B_1,
        DISPATCH_BOOT_2,
        DISPATCH_TASK_A_2,
        DISPATCH_TASK_B_2,
        DISPATCH_BOOT_3
    };
    uint32_t index;

    if (proof->sequence_count != AROS_APPLE_DISPATCH_SEQUENCE_COUNT)
        return 0;
    for (index = 0; index < AROS_APPLE_DISPATCH_SEQUENCE_COUNT; ++index) {
        if (proof->sequence[index] != expected[index])
            return 0;
    }
    return 1;
}

static void dispatch_boot_probe(struct AROSAppleDispatchProof *proof)
{
    struct DispatchTaskAllocation task_a;
    struct DispatchTaskAllocation task_b;
    int valid = 1;

    task_a.task = NULL;
    task_a.memory = NULL;
    task_b.task = NULL;
    task_b.memory = NULL;
    proof->status = AROS_APPLE_DISPATCH_STATUS_PASS;
    if (!allocate_task(&task_a, task_a_name, dispatch_task_a, proof,
            &proof->stack_a_lower, &proof->stack_a_upper))
        valid = 0;
    if (valid && !allocate_task(&task_b, task_b_name, dispatch_task_b, proof,
            &proof->stack_b_lower, &proof->stack_b_upper))
        valid = 0;
    if (!valid) {
        if (task_a.task != NULL && task_a.task->tc_State == TS_READY)
            RemTask(task_a.task);
        else
            free_unlaunched_task(&task_a);
        free_unlaunched_task(&task_b);
        proof->status = 0;
        AROSAppleRuntimeReturnEL1(0);
    }

    proof->task_a = (uintptr_t)task_a.task;
    proof->task_b = (uintptr_t)task_b.task;
    proof->context_a = (uintptr_t)
        task_a.task->tc_UnionETask.tc_ETask->et_RegFrame;
    proof->context_b = (uintptr_t)
        task_b.task->tc_UnionETask.tc_ETask->et_RegFrame;
    if (!record_sequence(proof, DISPATCH_BOOT_1))
        proof->status = 0;
    ++proof->boot_switches;
    AROSAppleDispatchSwitchState(AROS_APPLE_DISPATCH_BOOT_GPR_BASE,
        AROS_APPLE_DISPATCH_BOOT_SIMD_LOW,
        AROS_APPLE_DISPATCH_BOOT_SIMD_HIGH, proof->boot_gpr_after,
        proof->boot_simd_after, &proof->boot_x18_after);
    if (!record_sequence(proof, DISPATCH_BOOT_2))
        proof->status = 0;
    ++proof->boot_switches;
    KrnSwitch();
    if (!record_sequence(proof, DISPATCH_BOOT_3))
        proof->status = 0;
    proof->dispatch_count_after = SysBase->DispCount;

    valid = proof->status == AROS_APPLE_DISPATCH_STATUS_PASS &&
        dispatch_sequence_valid(proof) &&
        proof->boot_switches == 2 && proof->task_a_switches == 2 &&
        proof->task_b_switches == 2 &&
        proof->dispatch_count_after == proof->dispatch_count_before + 6 &&
        gprs_valid(proof->boot_gpr_after,
            AROS_APPLE_DISPATCH_BOOT_GPR_BASE) &&
        gprs_valid(proof->task_a_gpr_after,
            AROS_APPLE_DISPATCH_TASK_A_GPR_BASE) &&
        gprs_valid(proof->task_b_gpr_after,
            AROS_APPLE_DISPATCH_TASK_B_GPR_BASE) &&
        proof->boot_simd_after[0] == AROS_APPLE_DISPATCH_BOOT_SIMD_LOW &&
        proof->boot_simd_after[1] == AROS_APPLE_DISPATCH_BOOT_SIMD_HIGH &&
        proof->task_a_simd_after[0] ==
            AROS_APPLE_DISPATCH_TASK_A_SIMD_LOW &&
        proof->task_a_simd_after[1] ==
            AROS_APPLE_DISPATCH_TASK_A_SIMD_HIGH &&
        proof->task_b_simd_after[0] ==
            AROS_APPLE_DISPATCH_TASK_B_SIMD_LOW &&
        proof->task_b_simd_after[1] ==
            AROS_APPLE_DISPATCH_TASK_B_SIMD_HIGH &&
        proof->boot_x18_after == AROS_APPLE_DISPATCH_BOOT_GPR_BASE +
            AROS_APPLE_DISPATCH_X18_OFFSET &&
        proof->task_a_x18_after == AROS_APPLE_DISPATCH_TASK_A_GPR_BASE +
            AROS_APPLE_DISPATCH_X18_OFFSET &&
        proof->task_b_x18_after == AROS_APPLE_DISPATCH_TASK_B_GPR_BASE +
            AROS_APPLE_DISPATCH_X18_OFFSET &&
        proof->stack_a_observed >= proof->stack_a_lower &&
        proof->stack_a_observed < proof->stack_a_upper &&
        proof->stack_b_observed >= proof->stack_b_lower &&
        proof->stack_b_observed < proof->stack_b_upper &&
        GET_THIS_TASK == (struct Task *)(uintptr_t)proof->boot_task;

    RemTask(task_a.task);
    ++proof->cleanup_count;
    RemTask(task_b.task);
    ++proof->cleanup_count;
    if (proof->cleanup_count != 2 || !IsListEmpty(&SysBase->TaskReady))
        valid = 0;
    proof->status = valid ? AROS_APPLE_DISPATCH_STATUS_PASS : 0;
    AROSAppleRuntimeReturnEL1(proof->status);
}

int AROSAppleKernelProveDispatch(
    uintptr_t user_stack, uint64_t user_stack_size,
    struct AROSAppleDispatchProof *proof)
{
    struct DispatchTaskAllocation boot_task;
    uintptr_t stack_top;

    if (proof == NULL || ((uintptr_t)proof & 15U) != 0)
        return 0;
    clear_dispatch_proof(proof);
    proof->version = AROS_APPLE_DISPATCH_PROOF_VERSION;
    proof->size = sizeof(*proof);
    proof->context_size = AROS_APPLE_DISPATCH_CONTEXT_SIZE;
    proof->frame_size = sizeof(struct AROSAppleRuntimeFrame);
    if (user_stack == 0 || (user_stack & 15U) != 0 ||
        user_stack_size < AROS_APPLE_RUNTIME_STACK_MIN ||
        user_stack_size > UINTPTR_MAX - user_stack) {
        proof->status = 1;
        return 0;
    }
    if (AROSAppleRuntimeLocal()->active == 0) {
        proof->status = 2;
        return 0;
    }
    if (AROSAppleRuntimeLocal()->transition_active != 0) {
        proof->status = 3;
        return 0;
    }
    if (SysBase == NULL) {
        proof->status = 4;
        return 0;
    }
    if (GET_THIS_TASK != NULL) {
        proof->status = 6;
        return 0;
    }
    if (!IsListEmpty(&SysBase->TaskReady)) {
        proof->status = 7;
        return 0;
    }
    stack_top = user_stack + (uintptr_t)user_stack_size;
    if ((stack_top & 15U) != 0) {
        proof->status = 8;
        return 0;
    }

    boot_task.task = NULL;
    boot_task.memory = NULL;
    if (!prepare_boot_task(&boot_task, user_stack, stack_top, proof)) {
        if (proof->status == 0)
            proof->status = 9;
        return 0;
    }
    proof->boot_task = (uintptr_t)boot_task.task;
    proof->dispatch_count_before = SysBase->DispCount;
    proof->return_status = AROSAppleRuntimeEnterEL0(dispatch_boot_probe,
        proof, stack_top);

    if (GET_THIS_TASK == boot_task.task) {
#if defined(__AROSEXEC_SMP__)
        exec_TaskRemoveRunning(boot_task.task);
        TLS_SET(ThisTask, NULL);
#else
        SET_THIS_TASK(NULL);
#endif
        boot_task.task->tc_State = TS_READY;
        Enqueue(&SysBase->TaskReady, &boot_task.task->tc_Node);
        RemTask(boot_task.task);
    }

    return proof->return_status == AROS_APPLE_DISPATCH_STATUS_PASS &&
        proof->status == AROS_APPLE_DISPATCH_STATUS_PASS &&
        AROSAppleRuntimeLocal()->transition_active == 0 &&
        GET_THIS_TASK == NULL &&
        IsListEmpty(&SysBase->TaskReady);
}
