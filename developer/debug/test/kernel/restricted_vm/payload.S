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
 b.ne 9f
 add x21, x21, #1
 str x21, [x19]
 svc #0xa540
 cmp x18, x20
 b.ne 9f
 mrs x9, tpidr_el0
 cmp x9, x20
 b.ne 9f
 subs x22, x22, #1
 b.ne 1b
 mov x0, #1
 svc #0xa501
 cmn x0, #1
 b.ne 9f
 svc #0xa503
 cmn x0, #1
 b.ne 9f
 mov x0, #1
 svc #0xa530
 cmn x0, #1
 b.ne 9f
 cmp x23, #1, lsl #12
 b.ne 2f
 mov x9, #0x40080000
 ldr x0, [x9]
 b 9f
2:
 mov x0, #0
 svc #0xa541
 b .
9:
 mov x0, #9
 svc #0xa541
 b .
RestrictedCodeEnd:
