/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
.section .rodata.restricted,"a"
.balign 16
.global RestrictedCode, RestrictedCodeEnd
RestrictedCode:
 mov x19, x1
 mov x21, x0
 mov x23, x0
 lsr x22, x0, #2
 mrs x20, tpidr_el0
1:
 ldr x9, [x19]
 cmp x9, x21
 b.ne RestrictedFailure
 add x21, x21, #1
 str x21, [x19]
 svc #0xa540
 cmp x18, x20
 b.ne RestrictedFailure
 mrs x9, tpidr_el0
 cmp x9, x20
 b.ne RestrictedFailure
 subs x22, x22, #1
 b.ne 1b
 /* Reject invalid input, read-only code output, overflow and guard page. */
 mov x0, #0x40080000
 add x1, x19, #288
 mov x2, #8
 svc #0xa542
 cbnz x0, RestrictedFailure
 add x0, x19, #256
 sub x1, x19, #1, lsl #12
 mov x2, #8
 svc #0xa542
 cbnz x0, RestrictedFailure
 mov x0, #-4
 add x1, x19, #288
 mov x2, #8
 svc #0xa542
 cbnz x0, RestrictedFailure
 add x0, x19, #1, lsl #12
 add x1, x19, #288
 mov x2, #8
 svc #0xa542
 cbnz x0, RestrictedFailure
 /* Callback PC must belong to this immutable code allocation. */
 add x0, x19, #256
 add x1, x19, #288
 mov x2, #8
 mov x3, x19
 svc #0xa542
 cbnz x0, RestrictedFailure
 mov x24, #100
 mov x26, #0x5a5a
 movk x26, #0x5a5a, lsl #16
 movk x26, #0x5a5a, lsl #32
 movk x26, #0x5a5a, lsl #48
3:
 add x27, x23, x24
 str x27, [x19, #256]
 adr x3, RestrictedCallback
 add x0, x19, #256
 add x1, x19, #288
 mov x2, #8
 svc #0xa542
 cbz x0, RestrictedFailure
 mov x25, x0
4:
 mov x0, x25
 svc #0xa543
 cbz x0, 4b
 ldr x9, [x19, #288]
 eor x9, x9, x26
 cmp x9, x27
 b.ne RestrictedFailure
 subs x24, x24, #1
 b.ne 3b
 mov x0, #1
 svc #0xa501
 cmn x0, #1
 b.ne RestrictedFailure
 svc #0xa503
 cmn x0, #1
 b.ne RestrictedFailure
 mov x0, #1
 svc #0xa530
 cmn x0, #1
 b.ne RestrictedFailure
 cmp x23, #1, lsl #12
 b.ne 2f
 mov x9, #0x40080000
 ldr x0, [x9]
 b RestrictedFailure
2:
 mov x0, #0
 svc #0xa541
 b .
RestrictedFailure:
 mov x0, #9
 svc #0xa541
 b .
RestrictedCallback:
 mov x19, x1
 add x27, x0, #73
 str x27, [x19, #320]
 add x0, x19, #320
 add x1, x19, #352
 mov x2, #8
 mov x3, #0
 svc #0xa542
 cbz x0, RestrictedFailure
 mov x25, x0
5:
 mov x0, x25
 svc #0xa543
 cbz x0, 5b
 ldr x9, [x19, #352]
 mov x26, #0x5a5a
 movk x26, #0x5a5a, lsl #16
 movk x26, #0x5a5a, lsl #32
 movk x26, #0x5a5a, lsl #48
 eor x9, x9, x26
 cmp x9, x27
 b.ne RestrictedFailure
 mov x0, #0
 svc #0xa544
 b RestrictedFailure
RestrictedCodeEnd:
