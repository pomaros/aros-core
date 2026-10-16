/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * PSCI enters at EL1 with MMU off, x0 = physical handoff. Identity mapping. */
    .text
    .balign 64
    .global AROSCPUProbeEntry
    .type AROSCPUProbeEntry,%function
AROSCPUProbeEntry:
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
    adr x1,AROSCPUProbeVectors
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
    bl AROSCPUProbeMain
.Lfault:
    wfe
    b .Lfault
    .balign 2048
AROSCPUProbeVectors:
    b .Lfault
    .balign 128
    b .Lfault
    .balign 128
    b .Lfault
    .balign 128
    b .Lfault
    .balign 128
    b .Lfault
    .balign 128
    b .Lprobe_irq
    .balign 128
    b .Lfault
    .balign 128
    b .Lfault
    .balign 128
    b .Lfault
    .balign 128
    b .Lfault
    .balign 128
    b .Lfault
    .balign 128
    b .Lfault
    .balign 128
    b .Lfault
    .balign 128
    b .Lfault
    .balign 128
    b .Lfault
    .balign 128
    b .Lfault
    .balign 128

.Lprobe_irq:
    sub sp,sp,#272
    stp x0,x1,[sp,#0]
    stp x2,x3,[sp,#16]
    stp x4,x5,[sp,#32]
    stp x6,x7,[sp,#48]
    stp x8,x9,[sp,#64]
    stp x10,x11,[sp,#80]
    stp x12,x13,[sp,#96]
    stp x14,x15,[sp,#112]
    stp x16,x17,[sp,#128]
    stp x18,x19,[sp,#144]
    stp x20,x21,[sp,#160]
    stp x22,x23,[sp,#176]
    stp x24,x25,[sp,#192]
    stp x26,x27,[sp,#208]
    stp x28,x29,[sp,#224]
    str x30,[sp,#240]
    mrs x0,elr_el1
    mrs x1,spsr_el1
    stp x0,x1,[sp,#248]
    bl AROSCPUProbeHandleIRQ
    ldp x0,x1,[sp,#248]
    msr elr_el1,x0
    msr spsr_el1,x1
    ldp x0,x1,[sp,#0]
    ldp x2,x3,[sp,#16]
    ldp x4,x5,[sp,#32]
    ldp x6,x7,[sp,#48]
    ldp x8,x9,[sp,#64]
    ldp x10,x11,[sp,#80]
    ldp x12,x13,[sp,#96]
    ldp x14,x15,[sp,#112]
    ldp x16,x17,[sp,#128]
    ldp x18,x19,[sp,#144]
    ldp x20,x21,[sp,#160]
    ldp x22,x23,[sp,#176]
    ldp x24,x25,[sp,#192]
    ldp x26,x27,[sp,#208]
    ldp x28,x29,[sp,#224]
    ldr x30,[sp,#240]
    add sp,sp,#272
    eret
    .global AROSCPUProbeEntryEnd
AROSCPUProbeEntryEnd:
