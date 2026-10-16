/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
    .text
    .balign 4
    .global AROSAppleGuardProbe
    .type AROSAppleGuardProbe, %function
AROSAppleGuardProbe:
    mov x9, sp
    mov sp, x0
    .global AROSAppleGuardProbeFault
AROSAppleGuardProbeFault:
    str xzr, [sp, #-16]!
    .global AROSAppleGuardProbeResume
AROSAppleGuardProbeResume:
    /* No C instruction runs on the deliberately underflowed stack. */
    mov sp, x9
    ret
    .size AROSAppleGuardProbe, . - AROSAppleGuardProbe
