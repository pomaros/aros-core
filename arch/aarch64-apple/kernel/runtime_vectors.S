/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Apple AArch64 runtime exception vectors and EL0 transition.
 */

#include <aros/apple/runtime_local.h>

.arch armv8-a+simd

#define FRAME_SIZE 864
#define FRAME_SP 248
#define FRAME_ELR 256
#define FRAME_SPSR 264
#define FRAME_ESR 272
#define FRAME_FAR 280
#define FRAME_VECTOR 288
#define FRAME_SERVICE 296
#define FRAME_STATUS 304
#define FRAME_TLS 312
#define FRAME_ROOT 848
#define FRAME_ENTRY_ROOT 856
#define FRAME_SIMD 320
#define FRAME_FPCR 832
#define FRAME_FPSR 840
/* The ASID field of a TTBR0 value: 8-bit ASIDs (TCR_EL1.AS clear). */
#define TTBR_ASID_MASK 0x00ff000000000000

/*
 * Translation roots and ASIDs.
 *
 * Native tasks and restricted domains both run at EL0. A CPU's trusted root
 * (AROSAppleRuntimeLocal()->trusted_ttbr0) is the native identity map, with
 * ASID 0. A restricted domain's root is a private clone of it with EL0
 * access withdrawn from native pages, plus the domain's own pages; its TTBR0
 * value carries the domain's ASID in bits 55:48 (restricted.c). An exception
 * taken under a domain root switches to the trusted root before any C runs;
 * the return switches to the frame's root.
 *
 * The switches need no TLB invalidation, because:
 *  1. Every leaf of every root is not global. Stage two builds the native
 *     map so (stage2_mmu.c); KrnMapGlobal, the code window, AArch64MapPage
 *     and the root clone write nG leaves; protection changes and block
 *     splits keep the bit. A TLB entry thus belongs to one ASID only.
 *  2. ASID 0 is the native root's. A nonzero ASID belongs to one live
 *     domain root (restricted.c, under the coordinator). An entry tagged
 *     with an ASID therefore comes from that one root and is used again only
 *     under it: switching between roots of different ASIDs leaves nothing
 *     usable that the new root does not map the same way, and no two entries
 *     for one address and ASID can coexist.
 *  3. A released ASID is given again only after a broadcast TLBI VMALLE1IS
 *     issued after the release, when no CPU can use the old root any more.
 *  4. Every change of a root's tables invalidates by address for all ASIDs
 *     (TLBI VAAE1IS) or everything (TLBI VMALLE1IS), broadcast to every CPU,
 *     a parked one included. None relies on these switches.
 *  5. A domain root of ASID 0 (all 255 others live) shares the native ASID:
 *     switching to or from it empties the local TLB, as every switch did
 *     before ASIDs. So does every switch while AROSAppleRestrictedSharedRoots
 *     is set: a global native leaf was found, rule 1 does not hold.
 *
 * TTBR0 holds the ASID and the table address in one register, so a single
 * MSR changes both, and the ISB after it makes later translations use them.
 */

    .macro RUNTIME_VECTOR number
    .balign 128
    sub     sp, sp, #FRAME_SIZE
    stp     x0, x1, [sp, #0]
    mov     x0, #\number
    b       .Lruntime_exception_common
    .endm

    .section .text.runtime_vectors,"ax"
    .balign 2048
    .global AROSAppleRuntimeVectors
    .type AROSAppleRuntimeVectors, %object
AROSAppleRuntimeVectors:
    RUNTIME_VECTOR 0
    RUNTIME_VECTOR 1
    RUNTIME_VECTOR 2
    RUNTIME_VECTOR 3
    RUNTIME_VECTOR 4
    RUNTIME_VECTOR 5
    RUNTIME_VECTOR 6
    RUNTIME_VECTOR 7
    RUNTIME_VECTOR 8
    RUNTIME_VECTOR 9
    RUNTIME_VECTOR 10
    RUNTIME_VECTOR 11
    RUNTIME_VECTOR 12
    RUNTIME_VECTOR 13
    RUNTIME_VECTOR 14
    RUNTIME_VECTOR 15
    .balign 128
    .size AROSAppleRuntimeVectors, . - AROSAppleRuntimeVectors

    /* Keep entry and return in the same pinnable executable section as the
     * vectors. No C, dynamic module or user pointer is accessed before the
     * trusted root is active. */
    .balign 16
.Lruntime_exception_common:
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]
    stp     q0, q1, [sp, #FRAME_SIMD + 0]
    stp     q2, q3, [sp, #FRAME_SIMD + 32]
    stp     q4, q5, [sp, #FRAME_SIMD + 64]
    stp     q6, q7, [sp, #FRAME_SIMD + 96]
    stp     q8, q9, [sp, #FRAME_SIMD + 128]
    stp     q10, q11, [sp, #FRAME_SIMD + 160]
    stp     q12, q13, [sp, #FRAME_SIMD + 192]
    stp     q14, q15, [sp, #FRAME_SIMD + 224]
    stp     q16, q17, [sp, #FRAME_SIMD + 256]
    stp     q18, q19, [sp, #FRAME_SIMD + 288]
    stp     q20, q21, [sp, #FRAME_SIMD + 320]
    stp     q22, q23, [sp, #FRAME_SIMD + 352]
    stp     q24, q25, [sp, #FRAME_SIMD + 384]
    stp     q26, q27, [sp, #FRAME_SIMD + 416]
    stp     q28, q29, [sp, #FRAME_SIMD + 448]
    stp     q30, q31, [sp, #FRAME_SIMD + 480]
    mrs     x1, FPCR
    str     x1, [sp, #FRAME_FPCR]
    mrs     x1, FPSR
    str     x1, [sp, #FRAME_FPSR]
    cmp     x0, #8
    b.lo    .Lruntime_current_sp
    mrs     x1, SP_EL0
    b       .Lruntime_sp_ready
.Lruntime_current_sp:
    add     x1, sp, #FRAME_SIZE
.Lruntime_sp_ready:
    str     x1, [sp, #FRAME_SP]
    mrs     x1, ELR_EL1
    str     x1, [sp, #FRAME_ELR]
    mrs     x1, SPSR_EL1
    str     x1, [sp, #FRAME_SPSR]
    mrs     x1, ESR_EL1
    str     x1, [sp, #FRAME_ESR]
    mrs     x1, FAR_EL1
    str     x1, [sp, #FRAME_FAR]
    str     x0, [sp, #FRAME_VECTOR]
    stp     xzr, xzr, [sp, #FRAME_SERVICE]
    mrs     x1, tpidr_el0
    str     x1, [sp, #FRAME_TLS]
    mrs     x1, ttbr0_el1
    str     x1, [sp, #FRAME_ROOT]
    str     x1, [sp, #FRAME_ENTRY_ROOT]
    mrs     x2, tpidrro_el0
    cbz     x2, .Lruntime_trusted_ready
    ldr     x3, [x2, #AROS_CPU_TRUSTED_ROOT_OFFSET]
    cbz     x3, .Lruntime_trusted_ready
    cmp     x1, x3
    b.eq    .Lruntime_trusted_ready
    dsb     ish
    msr     ttbr0_el1, x3
    isb
    /* Under the trusted root now. Rule 5: a root of ASID 0 left, or shared
     * roots, empty the local TLB; otherwise nothing (see above). */
    tst     x1, #TTBR_ASID_MASK
    b.eq    .Lruntime_entry_flush
    adrp    x4, AROSAppleRestrictedSharedRoots
    add     x4, x4, :lo12:AROSAppleRestrictedSharedRoots
    ldr     w4, [x4]
    cbz     w4, .Lruntime_entry_switched
.Lruntime_entry_flush:
    tlbi    vmalle1
    dsb     ish
    isb
.Lruntime_entry_switched:
    ldr     x1, [x2, #AROS_CPU_KERNEL_TLS_OFFSET]
    msr     tpidr_el0, x1
    isb
.Lruntime_trusted_ready:
    mov     x0, sp
    bl      AROSAppleRuntimeHandleException
    cbz     w0, .Lruntime_exception_halt

    ldr     x1, [sp, #FRAME_ELR]
    msr     ELR_EL1, x1
    ldr     x1, [sp, #FRAME_SPSR]
    msr     SPSR_EL1, x1
    and     x2, x1, #0xf
    cbnz    x2, .Lruntime_sp_restored
    ldr     x2, [sp, #FRAME_SP]
    msr     SP_EL0, x2
.Lruntime_sp_restored:
    ldr     x1, [sp, #FRAME_FPCR]
    msr     FPCR, x1
    ldr     x1, [sp, #FRAME_FPSR]
    msr     FPSR, x1
    ldp     q0, q1, [sp, #FRAME_SIMD + 0]
    ldp     q2, q3, [sp, #FRAME_SIMD + 32]
    ldp     q4, q5, [sp, #FRAME_SIMD + 64]
    ldp     q6, q7, [sp, #FRAME_SIMD + 96]
    ldp     q8, q9, [sp, #FRAME_SIMD + 128]
    ldp     q10, q11, [sp, #FRAME_SIMD + 160]
    ldp     q12, q13, [sp, #FRAME_SIMD + 192]
    ldp     q14, q15, [sp, #FRAME_SIMD + 224]
    ldp     q16, q17, [sp, #FRAME_SIMD + 256]
    ldp     q18, q19, [sp, #FRAME_SIMD + 288]
    ldp     q20, q21, [sp, #FRAME_SIMD + 320]
    ldp     q22, q23, [sp, #FRAME_SIMD + 352]
    ldp     q24, q25, [sp, #FRAME_SIMD + 384]
    ldp     q26, q27, [sp, #FRAME_SIMD + 416]
    ldp     q28, q29, [sp, #FRAME_SIMD + 448]
    ldp     q30, q31, [sp, #FRAME_SIMD + 480]
    /* All remaining loads use the pinned exception stack. The frame belongs
     * to EL1; guest registers never select a root or kernel metadata pointer. */
    ldr     x1, [sp, #FRAME_ROOT]
    mrs     x2, ttbr0_el1
    cmp     x1, x2
    b.eq    .Lruntime_return_root_ready
    /* Decide under the trusted root, which maps the flag (rule 5). */
    tst     x1, #TTBR_ASID_MASK
    cset    w3, eq
    adrp    x2, AROSAppleRestrictedSharedRoots
    add     x2, x2, :lo12:AROSAppleRestrictedSharedRoots
    ldr     w2, [x2]
    orr     w3, w3, w2
    dsb     ish
    msr     ttbr0_el1, x1
    isb
    cbz     w3, .Lruntime_return_root_ready
    tlbi    vmalle1
    dsb     ish
    isb
.Lruntime_return_root_ready:
    ldr     x1, [sp, #FRAME_TLS]
    msr     tpidr_el0, x1
    isb
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30, [sp, #240]
    add     sp, sp, #FRAME_SIZE
    eret

.Lruntime_exception_halt:
    wfe
    b       .Lruntime_exception_halt

    .balign 16
    .global AROSAppleRuntimeEnterEL0
    .type AROSAppleRuntimeEnterEL0, %function
AROSAppleRuntimeEnterEL0:
    stp     x29, x30, [sp, #-96]!
    stp     x19, x20, [sp, #16]
    stp     x21, x22, [sp, #32]
    stp     x23, x24, [sp, #48]
    stp     x25, x26, [sp, #64]
    stp     x27, x28, [sp, #80]
    mov     x29, sp
    mrs     x3, tpidrro_el0
    cbz     x3, .Lruntime_exception_halt
    adr     x4, .Lruntime_el1_return
    str     x4, [x3, #AROS_CPU_RETURN_PC_OFFSET]
    mov     w4, #1
    str     w4, [x3, #AROS_CPU_TRANSITION_OFFSET]
    msr     SP_EL0, x2
    msr     ELR_EL1, x0
    mov     x0, x1
    /* EL0t, SError, IRQ and FIQ masked; D clear, as in every frame that
     * returns to EL0 (runtime.c, runtime_exit_policy). */
    mov     x3, #0x1c0
    msr     SPSR_EL1, x3
    isb
    eret
.Lruntime_el1_return:
    ldp     x27, x28, [sp, #80]
    ldp     x25, x26, [sp, #64]
    ldp     x23, x24, [sp, #48]
    ldp     x21, x22, [sp, #32]
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #96
    ret
    .size AROSAppleRuntimeEnterEL0, . - AROSAppleRuntimeEnterEL0
