/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Seed callee-saved GPRs, x18 and q19, switch, and report restored
 *       state. Native x18 is ordinary task state and must survive dispatch.
 */

    .text
    .balign 16
    .global AROSAppleDispatchSwitchState
    .type AROSAppleDispatchSwitchState, %function
AROSAppleDispatchSwitchState:
    sub     sp, sp, #96
    stp     x19, x20, [sp, #0]
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    stp     x25, x26, [sp, #48]
    stp     x27, x28, [sp, #64]
    str     x5, [sp, #80]
    mov     x19, x0
    add     x20, x0, #1
    add     x21, x0, #2
    add     x22, x0, #3
    add     x23, x0, #4
    add     x24, x0, #5
    add     x25, x0, #6
    add     x26, x0, #7
    add     x27, x0, #8
    add     x28, x0, #9
    add     x18, x0, #10
    fmov    d19, x1
    ins     v19.d[1], x2
    svc     #0xa602
    ldr     x7, [sp, #80]
    str     x18, [x7]
    stp     x19, x20, [x3, #0]
    stp     x21, x22, [x3, #16]
    stp     x23, x24, [x3, #32]
    stp     x25, x26, [x3, #48]
    stp     x27, x28, [x3, #64]
    fmov    x5, d19
    umov    x6, v19.d[1]
    stp     x5, x6, [x4]
    ldp     x27, x28, [sp, #64]
    ldp     x25, x26, [sp, #48]
    ldp     x23, x24, [sp, #32]
    ldp     x21, x22, [sp, #16]
    ldp     x19, x20, [sp, #0]
    add     sp, sp, #96
    ret
    .size AROSAppleDispatchSwitchState, . - AROSAppleDispatchSwitchState
