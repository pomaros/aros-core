/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Position-independent EL1 entry for the Apple second-stage loader.
 */

#include "stage2_exception.h"

    .macro STAGE2_VECTOR number
    .balign 128
    sub     sp, sp, #STAGE2_EXCEPTION_FRAME_SIZE
    stp     x0, x1, [sp, #0]
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
    str     x30, [sp, #STAGE2_EXCEPTION_X30]
    add     x1, sp, #STAGE2_EXCEPTION_FRAME_SIZE
    str     x1, [sp, #STAGE2_EXCEPTION_SP]
    mrs     x1, ELR_EL1
    str     x1, [sp, #STAGE2_EXCEPTION_ELR]
    mrs     x1, SPSR_EL1
    str     x1, [sp, #STAGE2_EXCEPTION_SPSR]
    mrs     x1, ESR_EL1
    str     x1, [sp, #STAGE2_EXCEPTION_ESR]
    mrs     x1, FAR_EL1
    str     x1, [sp, #STAGE2_EXCEPTION_FAR]
    mov     x0, #\number
    str     x0, [sp, #STAGE2_EXCEPTION_VECTOR]
    mov     x0, #1
    str     x0, [sp, #STAGE2_EXCEPTION_VALID]
    b       .Lstage2_exception_common
    .endm

    .section .vectors,"ax"
    .balign 2048
    .global AROSAppleStage2Vectors
    .type AROSAppleStage2Vectors, %object
AROSAppleStage2Vectors:
    STAGE2_VECTOR 0
    STAGE2_VECTOR 1
    STAGE2_VECTOR 2
    STAGE2_VECTOR 3
    STAGE2_VECTOR 4
    STAGE2_VECTOR 5
    STAGE2_VECTOR 6
    STAGE2_VECTOR 7
    STAGE2_VECTOR 8
    STAGE2_VECTOR 9
    STAGE2_VECTOR 10
    STAGE2_VECTOR 11
    STAGE2_VECTOR 12
    STAGE2_VECTOR 13
    STAGE2_VECTOR 14
    STAGE2_VECTOR 15
    .size AROSAppleStage2Vectors, . - AROSAppleStage2Vectors

    .section .text.entry,"ax"
    .p2align 4
    .global AROSAppleStage2Entry
    .type AROSAppleStage2Entry, %function

AROSAppleStage2Entry:
    mov     x19, x0
    mov     x20, #0

    /* Reject drift in the version-1 register and execution-state contract. */
    orr     x8, x1, x2
    orr     x8, x8, x3
    orr     x8, x8, x4
    orr     x8, x8, x5
    orr     x8, x8, x6
    orr     x8, x8, x7
    cbnz    x8, .Lstage2_fail
    mrs     x8, CurrentEL
    cmp     x8, #4
    b.ne    .Lstage2_fail
    mrs     x8, SPSel
    cmp     x8, #1
    b.ne    .Lstage2_fail
    mrs     x8, DAIF
    and     x8, x8, #0x3c0
    cmp     x8, #0x3c0
    b.ne    .Lstage2_fail
    mrs     x8, SCTLR_EL1
    tbnz    x8, #0, .Lstage2_fail
    tbnz    x8, #1, .Lstage2_fail
    tbnz    x8, #2, .Lstage2_fail
    tbnz    x8, #12, .Lstage2_fail
    tbnz    x8, #25, .Lstage2_fail
    mov     x8, sp
    tst     x8, #15
    b.ne    .Lstage2_fail


    /* The bundle allocation is zero-filled, but clear our NOLOAD state too. */
    adr     x9, __stage2_bss_start
    adr     x10, __stage2_bss_end
.Lstage2_clear_bss:
    cmp     x9, x10
    b.hs    .Lstage2_bss_done
    str     xzr, [x9], #8
    b       .Lstage2_clear_bss
.Lstage2_bss_done:

    /* Leave the unreported firmware stack before any reclaimable tag exists. */
    adr     x8, __stage2_stack_end
    mov     sp, x8

    /* Own every EL1 exception before probing firmware-described memory. */
    adr     x8, AROSAppleStage2Vectors
    msr     VBAR_EL1, x8
    isb


    adr     x8, AROSAppleStage2Phase
    mov     x9, #STAGE2_PHASE_ENTRY
    str     x9, [x8]

#if defined(AROS_APPLE_STAGE2_EXCEPTION_TEST)
    brk     #0xa205
#endif

    /* Runtime vectors and task contexts own all q0-q31 plus FPCR/FPSR. */
    mrs     x8, CPACR_EL1
    orr     x8, x8, #0x300000
    msr     CPACR_EL1, x8
    isb
    mrs     x8, CPACR_EL1
    and     x8, x8, #0x300000
    cmp     x8, #0x300000
    b.ne    .Lstage2_fail


    mov     x0, x19
    adr     x1, __stage2_image_start
    adr     x2, __stage2_image_end
    bl      AROSAppleStage2Main
    mov     w20, w0
    cbnz    w20, .Lstage2_fail




#if defined(AROS_APPLE_STAGE2_QEMU_TEST)
    adr     x1, .Lstage2_ownership_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_memory_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_mmu_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_header_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_exec_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_kernel_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_physical_memory_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_payload_identity_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_runtime_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_dispatch_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_coldstart_pass_message
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     w9, [x8, #STAGE2_COLDSTART_RESIDENT_COUNT]
    cmp     w9, #25
    b.eq    .Lstage2_framebuffer_requested
    cmp     w9, #34
    b.eq    .Lstage2_framebuffer_requested
    /* The desktop family: 54 residents and up. */
    cmp     w9, #54
    b.lo    .Lstage2_framebuffer_not_requested
.Lstage2_wanderer_requested:
    adr     x1, .Lstage2_framebuffer_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_wanderer_pass_message
    bl      .Lstage2_write0
    b       .Lstage2_framebuffer_not_requested
.Lstage2_framebuffer_requested:
    adr     x1, .Lstage2_framebuffer_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_graphics_render_pass_message
    bl      .Lstage2_write0
    cmp     w9, #34
    b.ne    .Lstage2_framebuffer_not_requested
    adr     x1, .Lstage2_intuition_pass_message
    bl      .Lstage2_write0
.Lstage2_framebuffer_not_requested:
    adr     x1, .Lstage2_filesystem_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_boot_device_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_dos_takeover_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_abi_probe_pass_message
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x9, [x8, #STAGE2_COLDSTART_WORKBENCH_REQUESTED]
    cbz     x9, .Lstage2_workbench_abi_not_requested
    adr     x1, .Lstage2_workbench_abi_pass_message
    bl      .Lstage2_write0
.Lstage2_workbench_abi_not_requested:
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x9, [x8, #STAGE2_COLDSTART_TLS_REQUESTED]
    cbz     x9, .Lstage2_tls_abi_not_requested
    adr     x1, .Lstage2_tls_abi_pass_message
    bl      .Lstage2_write0
.Lstage2_tls_abi_not_requested:
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x9, [x8, #STAGE2_COLDSTART_SMALL_REQUESTED]
    cbz     x9, .Lstage2_small_abi_not_requested
    adr     x1, .Lstage2_small_abi_pass_message
    bl      .Lstage2_write0
.Lstage2_small_abi_not_requested:
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x9, [x8, #STAGE2_COLDSTART_SETTINGS_REQUESTED]
    cbz     x9, .Lstage2_settings_not_requested
    adr     x1, .Lstage2_settings_pass_message
    bl      .Lstage2_write0
.Lstage2_settings_not_requested:
    adr     x1, .Lstage2_time_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_irq_quarantine_pass_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_pass_message
    bl      .Lstage2_write0
#endif
    b       .Lstage2_halt

.Lstage2_fail:
    /* No scheduler or interrupt handler may resume a failed boot while
     * diagnostics are emitted. This path terminates at the local park. */
    msr     daifset, #0xf
    isb
#if defined(AROS_APPLE_STAGE2_QEMU_TEST)
    adr     x1, .Lstage2_fail_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_failure_daif_label
    bl      .Lstage2_write0
    mrs     x0, DAIF
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_phase_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2Phase
    ldr     x10, [x8]
    mov     x0, x10
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_return_label
    bl      .Lstage2_write0
    mov     x0, x20
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_kernel_phase_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2KernelState
    ldr     w0, [x8, #STAGE2_KERNEL_PHASE]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_kernel_status_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2KernelState
    ldr     w0, [x8, #STAGE2_KERNEL_STATUS]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_kernel_detail_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2KernelState
    ldr     w0, [x8, #STAGE2_KERNEL_DETAIL]
    bl      .Lstage2_write_hex
    cmp     x10, #STAGE2_PHASE_DISPATCH
    b.ne    .Lstage2_coldstart_diagnostics
    adr     x1, .Lstage2_dispatch_status_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2DispatchProof
    ldr     x0, [x8, #STAGE2_DISPATCH_STATUS]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_dispatch_return_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2DispatchProof
    ldr     x0, [x8, #STAGE2_DISPATCH_RETURN_STATUS]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_dispatch_count_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2DispatchProof
    ldr     x0, [x8, #STAGE2_DISPATCH_SEQUENCE_COUNT]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_boot_switches_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2DispatchProof
    ldr     x0, [x8, #STAGE2_DISPATCH_BOOT_SWITCHES]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_task_a_switches_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2DispatchProof
    ldr     x0, [x8, #STAGE2_DISPATCH_TASK_A_SWITCHES]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_task_b_switches_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2DispatchProof
    ldr     x0, [x8, #STAGE2_DISPATCH_TASK_B_SWITCHES]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_cleanup_count_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2DispatchProof
    ldr     x0, [x8, #STAGE2_DISPATCH_CLEANUP_COUNT]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_dispatch_before_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2DispatchProof
    ldr     x0, [x8, #STAGE2_DISPATCH_COUNT_BEFORE]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_dispatch_after_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2DispatchProof
    ldr     x0, [x8, #STAGE2_DISPATCH_COUNT_AFTER]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_dispatch_sequence_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2DispatchProof
    add     x8, x8, #STAGE2_DISPATCH_SEQUENCE
    mov     x9, #0
.Lstage2_dispatch_sequence_loop:
    ldr     x0, [x8, x9, lsl #3]
    bl      .Lstage2_write_hex
    add     x9, x9, #1
    cmp     x9, #7
    b.lo    .Lstage2_dispatch_sequence_loop
    b       .Lstage2_fail_diagnostics_done
.Lstage2_coldstart_diagnostics:
    cmp     x10, #STAGE2_PHASE_COLDSTART
    b.ne    .Lstage2_fail_diagnostics_done
    adr     x1, .Lstage2_coldstart_status_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     w0, [x8, #STAGE2_COLDSTART_STATUS]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_detail_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     w0, [x8, #STAGE2_COLDSTART_DETAIL]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_resident_count_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     w0, [x8, #STAGE2_COLDSTART_RESIDENT_COUNT]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_abi_return_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_ABI_RETURN]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_abi_pages_before_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_ABI_PAGES_BEFORE]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_abi_pages_loaded_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_ABI_PAGES_LOADED]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_abi_pages_unloaded_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_ABI_PAGES_UNLOADED]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_abi_load_allocations_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_ABI_LOAD_ALLOCATIONS]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_abi_unload_frees_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_ABI_UNLOAD_FREES]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_abi_load_rx_protections_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_ABI_LOAD_RX_PROTECTIONS]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_abi_unload_rw_protections_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_ABI_UNLOAD_RW_PROTECTIONS]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_abi_bad_return_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_ABI_BAD_RETURN]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_abi_argument_size_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_ABI_ARGUMENT_SIZE]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_tls_return_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_TLS_RETURN]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_small_return_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_SMALL_RETURN]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_settings_return_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_SETTINGS_RETURN]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_aros_base_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_AROS_BASE]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_iffparse_base_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_IFFPARSE_BASE]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_locale_base_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_LOCALE_BASE]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_stdc_base_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_STDC_BASE]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_stdcio_base_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_STDCIO_BASE]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_posixc_base_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_POSIXC_BASE]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_exception_vector_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_RUNTIME_EXCEPTION_VECTOR]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_exception_esr_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_RUNTIME_EXCEPTION_ESR]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_exception_far_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_RUNTIME_EXCEPTION_FAR]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_exception_elr_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_RUNTIME_EXCEPTION_ELR]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_exception_spsr_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_RUNTIME_EXCEPTION_SPSR]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_exception_lr_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_RUNTIME_EXCEPTION_LR]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_exception_sp_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_RUNTIME_EXCEPTION_SP]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_exception_task_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    ldr     x0, [x8, #STAGE2_COLDSTART_RUNTIME_EXCEPTION_TASK]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_coldstart_exception_task_name_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2ColdStartProof
    add     x1, x8, #STAGE2_COLDSTART_RUNTIME_EXCEPTION_TASK_NAME
    bl      .Lstage2_write0
    adr     x1, .Lstage2_newline
    bl      .Lstage2_write0
.Lstage2_fail_diagnostics_done:
#endif

.Lstage2_halt:
    wfe
    b       .Lstage2_halt

    .size AROSAppleStage2Entry, . - AROSAppleStage2Entry

    .section .text.exception,"ax"
    .p2align 4
.Lstage2_exception_common:
    /* Persist the full 304-byte record outside the exception stack frame. */
    mov     x0, sp
    adr     x1, AROSAppleStage2ExceptionRecord
    mov     x2, #19
.Lstage2_exception_copy:
    ldp     x3, x4, [x0], #16
    stp     x3, x4, [x1], #16
    subs    x2, x2, #1
    b.ne    .Lstage2_exception_copy



#if defined(AROS_APPLE_STAGE2_QEMU_TEST)
    adr     x1, .Lstage2_exception_message
    bl      .Lstage2_write0
    adr     x1, .Lstage2_vector_label
    bl      .Lstage2_write0
    ldr     x0, [sp, #STAGE2_EXCEPTION_VECTOR]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_esr_label
    bl      .Lstage2_write0
    ldr     x0, [sp, #STAGE2_EXCEPTION_ESR]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_far_label
    bl      .Lstage2_write0
    ldr     x0, [sp, #STAGE2_EXCEPTION_FAR]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_elr_label
    bl      .Lstage2_write0
    ldr     x0, [sp, #STAGE2_EXCEPTION_ELR]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_spsr_label
    bl      .Lstage2_write0
    ldr     x0, [sp, #STAGE2_EXCEPTION_SPSR]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_lr_label
    bl      .Lstage2_write0
    ldr     x0, [sp, #STAGE2_EXCEPTION_X30]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_x0_label
    bl      .Lstage2_write0
    ldr     x0, [sp, #0]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_x1_label
    bl      .Lstage2_write0
    ldr     x0, [sp, #8]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_x19_label
    bl      .Lstage2_write0
    ldr     x0, [sp, #152]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_x20_label
    bl      .Lstage2_write0
    ldr     x0, [sp, #160]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_phase_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2Phase
    ldr     x0, [x8]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_kernel_base_label
    bl      .Lstage2_write0
    adr     x8, AROSAppleStage2KernelBase
    ldr     x0, [x8]
    bl      .Lstage2_write_hex
    adr     x1, .Lstage2_sctlr_label
    bl      .Lstage2_write0
    mrs     x0, SCTLR_EL1
    bl      .Lstage2_write_hex
#endif

.Lstage2_exception_halt:
    wfe
    b       .Lstage2_exception_halt

#if defined(AROS_APPLE_STAGE2_QEMU_TEST)
.Lstage2_write0:
    stp     x29, x30, [sp, #-160]!
    mov     x29, sp
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    str     x18, [sp, #144]
    mov     x0, x1
    bl      AROSVMConsoleWrite0
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldr     x18, [sp, #144]
    ldp     x29, x30, [sp], #160
    ret

/* Write x0 as a fixed-width hexadecimal line using the semihost console. */
.Lstage2_write_hex:
    adr     x1, .Lstage2_hex_buffer
    add     x2, x1, #2
    mov     x3, #60
    adr     x4, .Lstage2_hex_digits
.Lstage2_write_hex_digit:
    lsr     x5, x0, x3
    and     x5, x5, #15
    ldrb    w6, [x4, x5]
    strb    w6, [x2], #1
    subs    x3, x3, #4
    b.ge    .Lstage2_write_hex_digit
    adr     x1, .Lstage2_hex_buffer
    b       .Lstage2_write0
#endif

#if defined(AROS_APPLE_STAGE2_QEMU_TEST)
    .section .rodata,"a"
.Lstage2_memory_pass_message:
    .asciz  "[B2M] conventional-memory probe PASS\n"
.Lstage2_mmu_pass_message:
    .asciz  "[B2U] validated-RAM W^X identity MMU + caches PASS\n"
.Lstage2_ownership_pass_message:
    .asciz  "[B2O] loader allocation ownership PASS\n"
.Lstage2_header_pass_message:
    .asciz  "[B2H] memory header + resident scan PASS\n"
.Lstage2_exec_pass_message:
    .asciz  "[B2E] ExecBase preparation PASS\n"
.Lstage2_kernel_pass_message:
    .asciz  "[B2K] single-task kernel resident PASS\n"
.Lstage2_physical_memory_pass_message:
    .asciz  "[B2P] physical-memory import + reclamation PASS\n"
.Lstage2_payload_identity_pass_message:
    .asciz  "[B2L] verified payload identity retained PASS\n"
.Lstage2_runtime_pass_message:
    .asciz  "[B2V] runtime vectors + EL0 SVC PASS\n"
.Lstage2_dispatch_pass_message:
    .asciz  "[B2D] boot + two-task context dispatch PASS\n"
.Lstage2_coldstart_pass_message:
    .asciz  "[B2C] core cold-start resident layer PASS\n"
.Lstage2_framebuffer_pass_message:
    .asciz  "[G1Q] inherited framebuffer HIDD bootstrap PASS\n"
.Lstage2_graphics_render_pass_message:
    .asciz  "[G2Q] graphics.library inherited framebuffer render PASS\n"
.Lstage2_intuition_pass_message:
    .asciz  "[G3Q] Intuition screen + window render PASS\n"
.Lstage2_wanderer_pass_message:
    .asciz  "[G4Q] Wanderer RAM desktop launch PASS\n"
.Lstage2_filesystem_pass_message:
    .asciz  "[B2F] filesystem + DOS takeover readiness PASS\n"
.Lstage2_boot_device_pass_message:
    .asciz  "[B2B] retained-image block device + BootNode PASS\n"
.Lstage2_dos_takeover_pass_message:
    .asciz  "[B2S] DOS takeover + SYS namespace sentinel PASS\n"
.Lstage2_abi_probe_pass_message:
    .asciz  "[B2A] portable ABI + emulated TLS + streams + W^X/unload PASS\n"
.Lstage2_workbench_abi_pass_message:
    .asciz  "[B2W] Workbench process startup ABI PASS\n"
.Lstage2_tls_abi_pass_message:
    .asciz  "[B2X] concurrent emulated TLS task isolation PASS\n"
.Lstage2_small_abi_pass_message:
    .asciz  "[B2R] small code model page-relative relocations PASS\n"
.Lstage2_settings_pass_message:
    .asciz  "[B2G] settings by name through settings.library PASS\n"
.Lstage2_time_pass_message:
    .asciz  "[B2T] architectural counter time base PASS\n"
.Lstage2_irq_quarantine_pass_message:
    .asciz  "[B2I] pre-AIC IRQ quarantine PASS\n"
.Lstage2_pass_message:
    .asciz  "[B2Q] AROS startup contract PASS\n"
.Lstage2_fail_message:
    .asciz  "[B2Q] FAIL: stage-two startup\n"
.Lstage2_dispatch_status_label:
    .asciz  "D_STATUS="
.Lstage2_dispatch_return_label:
    .asciz  "D_RETURN="
.Lstage2_dispatch_count_label:
    .asciz  "D_SEQUENCE_COUNT="
.Lstage2_boot_switches_label:
    .asciz  "D_BOOT_SWITCHES="
.Lstage2_task_a_switches_label:
    .asciz  "D_TASK_A_SWITCHES="
.Lstage2_task_b_switches_label:
    .asciz  "D_TASK_B_SWITCHES="
.Lstage2_cleanup_count_label:
    .asciz  "D_CLEANUP="
.Lstage2_dispatch_before_label:
    .asciz  "D_DISPATCH_BEFORE="
.Lstage2_dispatch_after_label:
    .asciz  "D_DISPATCH_AFTER="
.Lstage2_dispatch_sequence_label:
    .asciz  "D_SEQUENCE=\n"
.Lstage2_coldstart_status_label:
    .asciz  "C_STATUS="
.Lstage2_coldstart_detail_label:
    .asciz  "C_DETAIL="
.Lstage2_coldstart_resident_count_label:
    .asciz  "C_RESIDENTS="
.Lstage2_coldstart_abi_return_label:
    .asciz  "C_ABI_RETURN="
.Lstage2_coldstart_abi_pages_before_label:
    .asciz  "C_ABI_PAGES_BEFORE="
.Lstage2_coldstart_abi_pages_loaded_label:
    .asciz  "C_ABI_PAGES_LOADED="
.Lstage2_coldstart_abi_pages_unloaded_label:
    .asciz  "C_ABI_PAGES_UNLOADED="
.Lstage2_coldstart_abi_load_allocations_label:
    .asciz  "C_ABI_LOAD_ALLOCATIONS="
.Lstage2_coldstart_abi_unload_frees_label:
    .asciz  "C_ABI_UNLOAD_FREES="
.Lstage2_coldstart_abi_load_rx_protections_label:
    .asciz  "C_ABI_LOAD_RX_PROTECTIONS="
.Lstage2_coldstart_abi_unload_rw_protections_label:
    .asciz  "C_ABI_UNLOAD_RW_PROTECTIONS="
.Lstage2_coldstart_abi_bad_return_label:
    .asciz  "C_ABI_BAD_RETURN="
.Lstage2_coldstart_abi_argument_size_label:
    .asciz  "C_ABI_ARGUMENT_SIZE="
.Lstage2_coldstart_tls_return_label:
    .asciz  "C_TLS_RETURN="
.Lstage2_coldstart_small_return_label:
    .asciz  "C_SMALL_RETURN="
.Lstage2_coldstart_settings_return_label:
    .asciz  "C_SETTINGS_RETURN="
.Lstage2_coldstart_aros_base_label:
    .asciz  "C_AROS_BASE="
.Lstage2_coldstart_iffparse_base_label:
    .asciz  "C_IFFPARSE_BASE="
.Lstage2_coldstart_locale_base_label:
    .asciz  "C_LOCALE_BASE="
.Lstage2_coldstart_stdc_base_label:
    .asciz  "C_STDC_BASE="
.Lstage2_coldstart_stdcio_base_label:
    .asciz  "C_STDCIO_BASE="
.Lstage2_coldstart_posixc_base_label:
    .asciz  "C_POSIXC_BASE="
.Lstage2_coldstart_exception_vector_label:
    .asciz  "C_EXCEPTION_VECTOR="
.Lstage2_coldstart_exception_esr_label:
    .asciz  "C_EXCEPTION_ESR="
.Lstage2_coldstart_exception_far_label:
    .asciz  "C_EXCEPTION_FAR="
.Lstage2_coldstart_exception_elr_label:
    .asciz  "C_EXCEPTION_ELR="
.Lstage2_coldstart_exception_spsr_label:
    .asciz  "C_EXCEPTION_SPSR="
.Lstage2_coldstart_exception_lr_label:
    .asciz  "C_EXCEPTION_LR="
.Lstage2_coldstart_exception_sp_label:
    .asciz  "C_EXCEPTION_SP="
.Lstage2_coldstart_exception_task_label:
    .asciz  "C_EXCEPTION_TASK="
.Lstage2_coldstart_exception_task_name_label:
    .asciz  "C_EXCEPTION_TASK_NAME="
.Lstage2_newline:
    .asciz  "\n"
.Lstage2_exception_message:
    .asciz  "[B2Q] EXCEPTION\n"
.Lstage2_vector_label:
    .asciz  "VECTOR="
.Lstage2_esr_label:
    .asciz  "ESR="
.Lstage2_far_label:
    .asciz  "FAR="
.Lstage2_elr_label:
    .asciz  "ELR="
.Lstage2_spsr_label:
    .asciz  "SPSR="
.Lstage2_lr_label:
    .asciz  "LR="
.Lstage2_x0_label:
    .asciz  "X0="
.Lstage2_x1_label:
    .asciz  "X1="
.Lstage2_x19_label:
    .asciz  "X19="
.Lstage2_x20_label:
    .asciz  "X20="
.Lstage2_failure_daif_label:
    .asciz "FAIL_DAIF="
.Lstage2_kernel_phase_label:
    .asciz "KERNEL_PHASE="
.Lstage2_kernel_status_label:
    .asciz "KERNEL_STATUS="
.Lstage2_kernel_detail_label:
    .asciz "KERNEL_DETAIL="
.Lstage2_phase_label:
    .asciz  "PHASE="
.Lstage2_return_label:
    .asciz  "RETURN="
.Lstage2_kernel_base_label:
    .asciz  "KBASE="
.Lstage2_sctlr_label:
    .asciz  "SCTLR="
.Lstage2_hex_digits:
    .ascii  "0123456789abcdef"

    .section .data,"aw"
.Lstage2_hex_buffer:
    .asciz  "0x0000000000000000\n"
#endif

    .section .bss.exception,"aw",%nobits
    .balign 16
    .global AROSAppleStage2ExceptionRecord
    .type AROSAppleStage2ExceptionRecord, %object
AROSAppleStage2ExceptionRecord:
    .skip STAGE2_EXCEPTION_RECORD_SIZE
    .size AROSAppleStage2ExceptionRecord, . - AROSAppleStage2ExceptionRecord
