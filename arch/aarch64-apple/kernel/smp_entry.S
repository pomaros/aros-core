/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * PSCI enters at EL1 with MMU off, x0 = physical handoff. Identity mapping. */
    .text
    .balign 64
    .global AROSAppleSMPEntry
    .type AROSAppleSMPEntry,%function
AROSAppleSMPEntry:
    msr daifset,#15
    mrs x1,CurrentEL
    cmp x1,#4
    b.ne .Lfault
    ldp x1,x2,[x0,#8]
    msr ttbr0_el1,x1
    msr ttbr1_el1,x2
    ldp x1,x2,[x0,#24]
    msr tcr_el1,x1
    msr mair_el1,x2
    adrp x1,AROSAppleRuntimeVectors
    add x1,x1,:lo12:AROSAppleRuntimeVectors
    msr vbar_el1,x1
    ldr x2,[x0,#40]
    dsb sy
    isb
    tlbi vmalle1
    dsb sy
    isb
    msr sctlr_el1,x2
    isb
    msr spsel,#1
    ldr x1,[x0]
    mov sp,x1
    ldr x1,[x0,#48]
    msr cntkctl_el1,x1
    msr tpidrro_el0,xzr
    msr tpidr_el0,xzr
    mov x1,#0x300000
    msr cpacr_el1,x1
    isb
    bl AROSAppleSMPMain
.Lfault:
    wfe
    b .Lfault
    .global AROSAppleSMPEntryEnd
AROSAppleSMPEntryEnd:
