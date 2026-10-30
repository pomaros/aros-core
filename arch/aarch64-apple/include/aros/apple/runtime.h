/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#ifndef AROS_APPLE_RUNTIME_H
#define AROS_APPLE_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include <aros/apple/runtime_local.h>

#define AROS_APPLE_RUNTIME_FRAME_SIZE UINT32_C(864)
#define AROS_APPLE_DISPATCH_CONTEXT_SIZE UINT32_C(816)
#define AROS_APPLE_DISPATCH_SIMD_OFFSET UINT32_C(288)
#define AROS_APPLE_EXCEPTION_CONTEXT_SIZE UINT32_C(280)
#define AROS_APPLE_SIMD_STATE_SIZE UINT32_C(528)
#define AROS_APPLE_RUNTIME_VECTOR_COUNT UINT32_C(16)
#define AROS_APPLE_RUNTIME_VECTOR_TABLE_SIZE UINT32_C(2048)
#define AROS_APPLE_RUNTIME_PROOF_VERSION UINT32_C(2)
#define AROS_APPLE_RUNTIME_PROOF_EL1_CALLS UINT32_C(14)
#define AROS_APPLE_RUNTIME_PROOF_EL0_CALLS UINT32_C(17)
#define AROS_APPLE_RUNTIME_PROOF_SUPERVISORS UINT32_C(2)
#define AROS_APPLE_RUNTIME_PROOF_REJECTIONS UINT32_C(4)
#define AROS_APPLE_DISPATCH_PROOF_VERSION UINT32_C(3)
#define AROS_APPLE_DISPATCH_X18_OFFSET UINT64_C(10)
#define AROS_APPLE_RUNTIME_STACK_MIN UINT64_C(65536)
#define AROS_APPLE_DISPATCH_TASK_STACK_SIZE UINT64_C(65536)

#define AROS_APPLE_RUNTIME_SVC_IS_SUPER UINT32_C(0xa500)
#define AROS_APPLE_RUNTIME_SVC_SUPERVISOR UINT32_C(0xa501)
#define AROS_APPLE_RUNTIME_SVC_RETURN_EL1 UINT32_C(0xa502)
#define AROS_APPLE_RUNTIME_SVC_CLI UINT32_C(0xa503)
#define AROS_APPLE_RUNTIME_SVC_STI UINT32_C(0xa504)
#define AROS_APPLE_RUNTIME_SVC_DEBUG_PUT_CHAR UINT32_C(0xa507)
#define AROS_APPLE_RUNTIME_SVC_IRQ_SAVE UINT32_C(0xa508)
#define AROS_APPLE_RUNTIME_SVC_IRQ_RESTORE UINT32_C(0xa509)
#define AROS_APPLE_RUNTIME_SVC_INVALID_TEST UINT32_C(0xa5ff)
#define AROS_APPLE_RUNTIME_SVC_KERNEL_BASE UINT32_C(0xa600)
#define AROS_APPLE_RUNTIME_SVC_ERROR UINT64_MAX

#define AROS_APPLE_DISPATCH_STATUS_PASS UINT64_C(0xd15ca11d15ca11)
#define AROS_APPLE_DISPATCH_SEQUENCE_COUNT UINT32_C(7)
#define AROS_APPLE_DISPATCH_GPR_COUNT UINT32_C(10)
#define AROS_APPLE_DISPATCH_BOOT_GPR_BASE UINT64_C(0xb007100000000000)
#define AROS_APPLE_DISPATCH_TASK_A_GPR_BASE UINT64_C(0xa11c100000000000)
#define AROS_APPLE_DISPATCH_TASK_B_GPR_BASE UINT64_C(0xb11c100000000000)
#define AROS_APPLE_DISPATCH_BOOT_SIMD_LOW UINT64_C(0xb007f00db007f00d)
#define AROS_APPLE_DISPATCH_BOOT_SIMD_HIGH UINT64_C(0xb007faceb007face)
#define AROS_APPLE_DISPATCH_TASK_A_SIMD_LOW UINT64_C(0xa11cf00da11cf00d)
#define AROS_APPLE_DISPATCH_TASK_A_SIMD_HIGH UINT64_C(0xa11cfacea11cface)
#define AROS_APPLE_DISPATCH_TASK_B_SIMD_LOW UINT64_C(0xb11cf00db11cf00d)
#define AROS_APPLE_DISPATCH_TASK_B_SIMD_HIGH UINT64_C(0xb11cfaceb11cface)

#define AROS_APPLE_RUNTIME_STATUS_EMPTY UINT64_C(0)
#define AROS_APPLE_RUNTIME_STATUS_HANDLED UINT64_C(1)
#define AROS_APPLE_RUNTIME_STATUS_REJECTED UINT64_C(2)

struct __attribute__((aligned(16))) AROSAppleRuntimeFrame {
    uint64_t x[31];
    uint64_t sp;
    uint64_t elr;
    uint64_t spsr;
    uint64_t esr;
    uint64_t far;
    uint64_t vector;
    uint64_t service;
    uint64_t status;
    uint64_t task_tls;
    uint64_t simd[66];
    /* Entry is evidence; return is selected by the kernel dispatcher. */
    uint64_t return_ttbr0;
    uint64_t entry_ttbr0;
};

struct __attribute__((aligned(16))) AROSAppleRuntimeProof {
    uint32_t version;
    uint32_t size;
    uint32_t frame_size;
    uint32_t vector_table_size;
    uint64_t vectors;
    uint64_t user_stack_lower;
    uint64_t user_stack_upper;
    uint64_t el1_service_count;
    uint64_t el0_service_count;
    uint64_t supervisor_count;
    uint64_t rejected_count;
    uint64_t el1_is_super;
    uint64_t el0_is_super;
    uint64_t el1_supervisor_result;
    uint64_t el0_supervisor_result;
    uint64_t el1_invalid_result;
    uint64_t el0_invalid_result;
    uint64_t return_status;
    uint64_t el0_stack_observed;
    uint64_t el1_kernel_result;
};

struct __attribute__((aligned(16))) AROSAppleDispatchProof {
    uint32_t version;
    uint32_t size;
    uint32_t context_size;
    uint32_t frame_size;
    uint64_t boot_task;
    uint64_t task_a;
    uint64_t task_b;
    uint64_t context_a;
    uint64_t context_b;
    uint64_t stack_a_lower;
    uint64_t stack_a_upper;
    uint64_t stack_a_observed;
    uint64_t stack_b_lower;
    uint64_t stack_b_upper;
    uint64_t stack_b_observed;
    uint64_t dispatch_count_before;
    uint64_t dispatch_count_after;
    uint64_t sequence_count;
    uint64_t boot_switches;
    uint64_t task_a_switches;
    uint64_t task_b_switches;
    uint64_t cleanup_count;
    uint64_t sequence[AROS_APPLE_DISPATCH_SEQUENCE_COUNT];
    uint64_t boot_gpr_after[AROS_APPLE_DISPATCH_GPR_COUNT];
    uint64_t task_a_gpr_after[AROS_APPLE_DISPATCH_GPR_COUNT];
    uint64_t task_b_gpr_after[AROS_APPLE_DISPATCH_GPR_COUNT];
    uint64_t return_status;
    uint64_t status;
    uint64_t boot_simd_after[2];
    uint64_t task_a_simd_after[2];
    uint64_t task_b_simd_after[2];
    uint64_t boot_x18_after;
    uint64_t task_a_x18_after;
    uint64_t task_b_x18_after;
};

_Static_assert(offsetof(struct AROSAppleRuntimeFrame, x[30]) == 240,
    "runtime frame x30 offset");
_Static_assert(offsetof(struct AROSAppleRuntimeFrame, sp) == 248,
    "runtime frame SP offset");
_Static_assert(offsetof(struct AROSAppleRuntimeFrame, elr) == 256,
    "runtime frame ELR offset");
_Static_assert(offsetof(struct AROSAppleRuntimeFrame, spsr) == 264,
    "runtime frame SPSR offset");
_Static_assert(offsetof(struct AROSAppleRuntimeFrame, esr) == 272,
    "runtime frame ESR offset");
_Static_assert(offsetof(struct AROSAppleRuntimeFrame, far) == 280,
    "runtime frame FAR offset");
_Static_assert(offsetof(struct AROSAppleRuntimeFrame, vector) == 288,
    "runtime frame vector offset");
_Static_assert(offsetof(struct AROSAppleRuntimeFrame, service) == 296,
    "runtime frame service offset");
_Static_assert(offsetof(struct AROSAppleRuntimeFrame, status) == 304,
    "runtime frame status offset");
_Static_assert(offsetof(struct AROSAppleRuntimeFrame, simd) == 320,
    "runtime frame SIMD offset");
_Static_assert(offsetof(struct AROSAppleRuntimeFrame, task_tls) == 312,
    "runtime task TLS offset");
_Static_assert(offsetof(struct AROSAppleRuntimeFrame, return_ttbr0) == 848,
    "runtime destination root offset");
_Static_assert(offsetof(struct AROSAppleRuntimeFrame, entry_ttbr0) == 856,
    "runtime entry root offset");
_Static_assert(sizeof(struct AROSAppleRuntimeFrame) ==
    AROS_APPLE_RUNTIME_FRAME_SIZE, "runtime frame size");
_Static_assert(sizeof(struct AROSAppleRuntimeProof) == 144,
    "runtime proof size");
_Static_assert(sizeof(struct AROSAppleDispatchProof) == 544,
    "dispatch proof size");

int AROSAppleRuntimeIsSuper(void);
void AROSAppleRuntimeCli(void);
void AROSAppleRuntimeSti(void);
int AROSAppleRuntimeDebugPutChar(int chr);
/* CPU-local IRQ/FIQ exclusion, usable from EL0; restore in nesting order. */
uint64_t AROSAppleRuntimeIRQSave(void);
void AROSAppleRuntimeIRQRestore(uint64_t mask);
void AROSAppleRuntimeSetIRQReady(int ready);
void AROSAppleRuntimeSetFIQReady(int ready);
uintptr_t AROSAppleRuntimeSupervisor(uintptr_t (*callback)(void));
int AROSAppleRuntimeHandleException(struct AROSAppleRuntimeFrame *frame);
_Noreturn void AROSAppleRuntimeReturnEL1(uint64_t status);
uint64_t AROSAppleRuntimeEnterEL0(
    void (*entry)(struct AROSAppleRuntimeProof *),
    struct AROSAppleRuntimeProof *proof,
    uintptr_t stack_top);

int AROSAppleKernelProveRuntime(
    uintptr_t user_stack, uint64_t user_stack_size,
    struct AROSAppleRuntimeProof *proof);
int AROSAppleKernelProveDispatch(
    uintptr_t user_stack, uint64_t user_stack_size,
    struct AROSAppleDispatchProof *proof);

extern char AROSAppleRuntimeVectors[];

#endif /* AROS_APPLE_RUNTIME_H */
