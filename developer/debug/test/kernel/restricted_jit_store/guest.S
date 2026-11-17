/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
.section .rodata.restricted,"a"
.arch armv8.4-a
.balign 16
.global StoreEntry, StoreEntryEnd, StorePayload, StorePayloadEnd
.global UnsupportedPayload, UnsupportedPayloadEnd
StoreEntry:
    mov x19,x0
    adr x0,RepairCalled
    svc #0xa545
    cbz x0,RegisterFailed
    add x4,x19,#4096
    movz x20,#0xffee
    movk x20,#0xffee,lsl #16
    movk x20,#0x6655,lsl #32
    movk x20,#0x8877,lsl #48
    blr x19
    svc #0xa541
RepairCalled:
    mov x0,#91
    svc #0xa541
RegisterFailed:
    mov x0,#92
    svc #0xa541
StoreEntryEnd:
.balign 16
StorePayload:
    stlurb w20,[x4,#1]
    stlurh w20,[x4,#2]
    stlur w20,[x4,#8]
    stlur x20,[x4,#16]
    mov x0,#43
    ret
StorePayloadEnd:
.balign 16
UnsupportedPayload:
    str w20,[x4,#8]
    mov x0,#43
    ret
UnsupportedPayloadEnd:
