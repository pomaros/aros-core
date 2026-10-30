/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
.arch armv8-a+simd
.section .text.boot,"ax"
.global _start
_start:
 msr daifset, #15
 ldr x0, =__stack_top
 mov sp, x0
 ldr x0, =__bss_start
 ldr x1, =__bss_end
1: cmp x0, x1
 b.hs 2f
 str xzr, [x0], #8
 b 1b
2: mov x0, #0x300000
 msr cpacr_el1, x0
 isb
#if RUNTIME_ENTRY
 adr x0, AROSAppleRuntimeVectors
#else
 adr x0, vectors
#endif
 msr vbar_el1, x0
 isb
 bl main
 b .

.balign 2048
vectors:
.rept 8
 b unexpected
 .balign 128
.endr
 b lower_sync
 .balign 128
.rept 7
 b unexpected
 .balign 128
.endr
unexpected:
 mrs x0, esr_el1
 mrs x1, elr_el1
 bl unexpected_exception
 b .

lower_sync:
 sub sp, sp, #816
 stp x0, x1, [sp, #0]
 stp x2, x3, [sp, #16]
 stp x4, x5, [sp, #32]
 stp x6, x7, [sp, #48]
 stp x8, x9, [sp, #64]
 stp x10, x11, [sp, #80]
 stp x12, x13, [sp, #96]
 stp x14, x15, [sp, #112]
 stp x16, x17, [sp, #128]
 stp x18, x19, [sp, #144]
 stp x20, x21, [sp, #160]
 stp x22, x23, [sp, #176]
 stp x24, x25, [sp, #192]
 stp x26, x27, [sp, #208]
 stp x28, x29, [sp, #224]
 str x30, [sp, #240]
 mrs x0, sp_el0
 str x0, [sp, #248]
 mrs x0, elr_el1
 str x0, [sp, #256]
 mrs x0, spsr_el1
 str x0, [sp, #264]
 mrs x0, esr_el1
 str x0, [sp, #272]
 mrs x0, far_el1
 str x0, [sp, #280]
 stp q0, q1, [sp, #288]
 stp q2, q3, [sp, #320]
 stp q4, q5, [sp, #352]
 stp q6, q7, [sp, #384]
 stp q8, q9, [sp, #416]
 stp q10, q11, [sp, #448]
 stp q12, q13, [sp, #480]
 stp q14, q15, [sp, #512]
 stp q16, q17, [sp, #544]
 stp q18, q19, [sp, #576]
 stp q20, q21, [sp, #608]
 stp q22, q23, [sp, #640]
 stp q24, q25, [sp, #672]
 stp q26, q27, [sp, #704]
 stp q28, q29, [sp, #736]
 stp q30, q31, [sp, #768]
 mrs x0, fpcr
 str x0, [sp, #800]
 mrs x0, fpsr
 str x0, [sp, #808]
 mov x0, sp
#if RUNTIME_ENTRY
 bl mechanism_handle
#else
 bl handle_exception
#endif
 b restore
#if RUNTIME_ENTRY
.global fixture_enter_guest
fixture_enter_guest:
#else
.global enter_guest
enter_guest:
#endif
 sub sp, sp, #816
 mov x1, x0
 mov x0, sp
 mov x2, #816
 bl memcpy
restore:
 ldr x0, [sp, #248]
 msr sp_el0, x0
 ldr x0, [sp, #256]
 msr elr_el1, x0
 ldr x0, [sp, #264]
 msr spsr_el1, x0
 ldr x0, [sp, #800]
 msr fpcr, x0
 ldr x0, [sp, #808]
 msr fpsr, x0
 ldp q0, q1, [sp, #288]
 ldp q2, q3, [sp, #320]
 ldp q4, q5, [sp, #352]
 ldp q6, q7, [sp, #384]
 ldp q8, q9, [sp, #416]
 ldp q10, q11, [sp, #448]
 ldp q12, q13, [sp, #480]
 ldp q14, q15, [sp, #512]
 ldp q16, q17, [sp, #544]
 ldp q18, q19, [sp, #576]
 ldp q20, q21, [sp, #608]
 ldp q22, q23, [sp, #640]
 ldp q24, q25, [sp, #672]
 ldp q26, q27, [sp, #704]
 ldp q28, q29, [sp, #736]
 ldp q30, q31, [sp, #768]
 ldp x0, x1, [sp, #0]
 ldp x2, x3, [sp, #16]
 ldp x4, x5, [sp, #32]
 ldp x6, x7, [sp, #48]
 ldp x8, x9, [sp, #64]
 ldp x10, x11, [sp, #80]
 ldp x12, x13, [sp, #96]
 ldp x14, x15, [sp, #112]
 ldp x16, x17, [sp, #128]
 ldp x18, x19, [sp, #144]
 ldp x20, x21, [sp, #160]
 ldp x22, x23, [sp, #176]
 ldp x24, x25, [sp, #192]
 ldp x26, x27, [sp, #208]
 ldp x28, x29, [sp, #224]
 ldr x30, [sp, #240]
 add sp, sp, #816
 eret

.section .guest,"ax"
.balign 4096
.global guest_start, guest_end
guest_start:
 mov x20, x0
 mov x24, x2
 mov x19, #0x1000
 movk x19, #0x1000, lsl #16
 mov x21, x1
 mov x22, #1000
 mov x18, #0xfeed
 add x18, x18, x20
 mov x23, #0xbeef
 add x23, x23, x20
 dup v8.2d, x23
1:
 ldr x9, [x19]
 cmp x9, x21
 b.ne guest_fail_data
 add x21, x21, #1
 str x21, [x19]
 svc #1
 mov x9, #0xfeed
 add x9, x9, x20
 cmp x18, x9
 b.ne guest_fail_regs
 umov x9, v8.d[1]
 cmp x9, x23
 b.ne guest_fail_regs
 subs x22, x22, #1
 b.ne 1b
 svc #0xa501
 cmn x0, #1
 b.ne guest_fail_svc
 svc #0xa503
 cmn x0, #1
 b.ne guest_fail_svc
 svc #0xffff
 cmn x0, #1
 b.ne guest_fail_svc
 // Fixture service: transform four owned bytes synchronously at EL1.
 mov w9, #0x1234
 str w9, [x19, #32]
 add x0, x19, #32
 mov x1, #4
 mov x2, #0x5a
 svc #4
 cbnz x0, guest_fail_bridge
 ldr w9, [x19, #32]
 mov w10, #0x486e
 movk w10, #0x5a5a, lsl #16
 cmp w9, w10
 b.ne guest_fail_bridge
 // Kernel pointer and crossing the owned page must both be refused.
 mov x0, #0x40000000
 mov x1, #4
 svc #4
 cmn x0, #1
 b.ne guest_fail_bridge
 add x0, x19, #4094
 svc #4
 cmn x0, #1
 b.ne guest_fail_bridge
 // Direct kernel RAM read/write, guard access and execution from RW data.
 mov x9, #0x123
.global kernel_read_probe, kernel_write_probe, guard_read_probe, after_nx_probe
kernel_read_probe:
 ldr x9, [x24]
 cmp x9, #0x123
 b.ne guest_fail_kernel
kernel_write_probe:
 str xzr, [x24]
 add x10, x19, #4096
guard_read_probe:
 ldr x9, [x10]
 blr x19
after_nx_probe:
 // Protect, fault on write, resume exact store after handler repairs mapping.
 svc #2
 add x21, x21, #1
.global owned_write_probe
owned_write_probe:
 str x21, [x19]
 svc #3
 b .
guest_fail_kernel: mov x0, #5
 b guest_fail
guest_fail_data: mov x0, #1
 b guest_fail
guest_fail_regs: mov x0, #2
 b guest_fail
guest_fail_svc: mov x0, #3
 b guest_fail
guest_fail_bridge: mov x0, #4
guest_fail:
 svc #0x7f
 b .
guest_end:
