/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: TLS variables reached through AArch64 TLS descriptors, written by
          hand until the compiler emits the sequence for AROS
*/

    .section .tdata, "awT", %progbits
    .balign 8
    .globl  tls_init
tls_init:
    .quad   0x1122334455667788
    .quad   tls_global              /* an initialiser the loader relocates */

    .section .tbss, "awT", %nobits
    .balign 64
    .globl  tls_zero
tls_zero:
    .zero   256

    .data
    .globl  tls_global
tls_global:
    .quad   0

    .text

/* void *tls_addr_init(void), void *tls_addr_zero(void): the AAELF64 sequence */
    .balign 4
    .globl  tls_addr_init
    .type   tls_addr_init, %function
tls_addr_init:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    adrp    x0, :tlsdesc:tls_init
    ldr     x1, [x0, #:tlsdesc_lo12:tls_init]
    add     x0, x0, #:tlsdesc_lo12:tls_init
    .tlsdesccall tls_init
    blr     x1
    mrs     x1, tpidr_el0
    add     x0, x0, x1
    ldp     x29, x30, [sp], #16
    ret
    .size   tls_addr_init, . - tls_addr_init

    .balign 4
    .globl  tls_addr_zero
    .type   tls_addr_zero, %function
tls_addr_zero:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    adrp    x0, :tlsdesc:tls_zero
    ldr     x1, [x0, #:tlsdesc_lo12:tls_zero]
    add     x0, x0, #:tlsdesc_lo12:tls_zero
    .tlsdesccall tls_zero
    blr     x1
    mrs     x1, tpidr_el0
    add     x0, x0, x1
    ldp     x29, x30, [sp], #16
    ret
    .size   tls_addr_zero, . - tls_addr_zero

/*
 * void tls_preserve(UQUAD out[48]): seed x2-x18 and v0-v31, resolve
 * tls_zero, store every register after the call. The resolver may change
 * only x0 (and the flags).
 */
    .balign 4
    .globl  tls_preserve
    .type   tls_preserve, %function
tls_preserve:
    stp     x29, x30, [sp, #-32]!
    mov     x29, sp
    stp     x19, x20, [sp, #16]
    mov     x19, x0
    movz    x20, #0x5a00
    add     x2, x20, #2
    add     x3, x20, #3
    add     x4, x20, #4
    add     x5, x20, #5
    add     x6, x20, #6
    add     x7, x20, #7
    add     x8, x20, #8
    add     x9, x20, #9
    add     x10, x20, #10
    add     x11, x20, #11
    add     x12, x20, #12
    add     x13, x20, #13
    add     x14, x20, #14
    add     x15, x20, #15
    add     x16, x20, #16
    add     x17, x20, #17
    add     x18, x20, #18
    .irp    n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    add     x0, x20, #(0x100 + \n)
    dup     v\n\().2d, x0
    .endr
    adrp    x0, :tlsdesc:tls_zero
    ldr     x1, [x0, #:tlsdesc_lo12:tls_zero]
    add     x0, x0, #:tlsdesc_lo12:tls_zero
    .tlsdesccall tls_zero
    blr     x1
    stp     x2, x3, [x19, #0]
    stp     x4, x5, [x19, #16]
    stp     x6, x7, [x19, #32]
    stp     x8, x9, [x19, #48]
    stp     x10, x11, [x19, #64]
    stp     x12, x13, [x19, #80]
    stp     x14, x15, [x19, #96]
    stp     x16, x17, [x19, #112]
    str     x18, [x19, #128]
    add     x19, x19, #136
    .irp    n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    str     d\n, [x19], #8
    .endr
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #32
    ret
    .size   tls_preserve, . - tls_preserve
