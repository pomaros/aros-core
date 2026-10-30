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
 mov x0, x19
 svc #0xa545
 cbnz x0, RestrictedFailure
 adr x0, RestrictedFaultHandler
 svc #0xa545
 cbz x0, RestrictedFailure
 sub x0, x19, #1, lsl #12
 mov x1, #3
 svc #0xa546
 cbnz x0, RestrictedFailure
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
 /* Running cancellation: worker holds its copied request until cancelled. */
 mov x9, #0xa5a5
 movk x9, #0xa5a5, lsl #16
 movk x9, #0xa5a5, lsl #32
 movk x9, #0xa5a5, lsl #48
 str x9, [x19, #512]
 str x9, [x19, #544]
 mov x9, #0xe2
 str x9, [x19, #480]
 add x0, x19, #480
 add x1, x19, #512
 mov x2, #8
 mov x3, #0
 svc #0xa542
 cbz x0, RestrictedFailure
 mov x25, x0
8:
 mov x0, x25
 svc #0xa549
 cmp x0, #2
 b.ne 8b
 mov x0, x25
 svc #0xa548
 cmp x0, #1
 b.ne RestrictedFailure
10:
 mov x0, x25
 svc #0xa549
 cbnz x0, 10b
 mov x0, x25
 svc #0xa543
 cbnz x0, RestrictedFailure
 ldr x9, [x19, #512]
 mov x10, #0xa5a5
 movk x10, #0xa5a5, lsl #16
 movk x10, #0xa5a5, lsl #32
 movk x10, #0xa5a5, lsl #48
 cmp x9, x10
 b.ne RestrictedFailure
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
 mov x0, #1
 svc #0xa532
 cmn x0, #1
 b.ne RestrictedFailure
 svc #0xa533
 cmn x0, #1
 b.ne RestrictedFailure
 mov x0, #0
 svc #0xa544
 cmn x0, #1
 b.ne RestrictedFailure
 mov x0, #1
 svc #0xa535
 cmn x0, #1
 b.ne RestrictedFailure
 mov x0, #1
 svc #0xa537
 cmn x0, #1
 b.ne RestrictedFailure
 svc #0xa536
 cmn x0, #1
 b.ne RestrictedFailure
 cmp x23, #1, lsl #12
 b.ne 2f
 /* Terminate while a worker lease is live. Native close must cancel/drain. */
 mov x9, #0xe3
 str x9, [x19, #480]
 add x0, x19, #480
 add x1, x19, #544
 mov x2, #8
 mov x3, #0
 svc #0xa542
 cbz x0, RestrictedFailure
 mov x25, x0
11:
 mov x0, x25
 svc #0xa549
 cmp x0, #2
 b.ne 11b
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
 cmp x24, #100
 b.ne 6f
 mov x0, x19
 mov x1, #1
 svc #0xa546
 cbz x0, RestrictedFailure
 mov x9, #0x1234
RestrictedWriteProbe:
 str x9, [x19, #384]
 ldr x9, [x19, #384]
 mov x10, #0x1234
 cmp x9, x10
 b.ne RestrictedFailure
 add x0, x19, #320
 add x1, x19, #352
 mov x2, #8
 adr x3, RestrictedCallback
 svc #0xa542
 cbnz x0, RestrictedFailure
6:
 mov x0, x23
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
RestrictedFaultHandler:
 add x9, x19, #384
 cmp x0, x9
 b.ne RestrictedFailure
 ubfx x9, x1, #26, #6
 cmp x9, #0x24
 b.ne RestrictedFailure
 tbz x1, #6, RestrictedFailure
 adr x9, RestrictedWriteProbe
 cmp x2, x9
 b.ne RestrictedFailure
 mov x24, x2
 mov x25, x3
 /* Valid PC/token cannot resume until the page is writable again. */
 mov x0, x24
 mov x1, x25
 svc #0xa547
 cmn x0, #1
 b.ne RestrictedFailure
 mov x0, x19
 mov x1, #3
 svc #0xa546
 cbz x0, RestrictedFailure
 /* Neither a forged PC nor token may replace the saved continuation. */
 mov x0, x19
 mov x1, x25
 svc #0xa547
 cmn x0, #1
 b.ne RestrictedFailure
 mov x0, x24
 add x1, x25, #1
 svc #0xa547
 cmn x0, #1
 b.ne RestrictedFailure
 add x27, x23, #911
 str x27, [x19, #416]
 add x0, x19, #416
 add x1, x19, #448
 mov x2, #8
 mov x3, #0
 svc #0xa542
 cbz x0, RestrictedFailure
 mov x28, x0
7:
 mov x0, x28
 svc #0xa543
 cbz x0, 7b
 ldr x9, [x19, #448]
 mov x26, #0x5a5a
 movk x26, #0x5a5a, lsl #16
 movk x26, #0x5a5a, lsl #32
 movk x26, #0x5a5a, lsl #48
 eor x9, x9, x26
 cmp x9, x27
 b.ne RestrictedFailure
 mov x0, x24
 mov x1, x25
 svc #0xa547
 b RestrictedFailure
RestrictedCodeEnd:

.balign 16
.global RestrictedQueued, RestrictedQueuedEnd
RestrictedQueued:
 mov x19, x1
 mov x9, #0xe1
 str x9, [x19, #480]
 mov x9, #0xa5a5
 movk x9, #0xa5a5, lsl #16
 movk x9, #0xa5a5, lsl #32
 movk x9, #0xa5a5, lsl #48
 str x9, [x19, #512]
 add x0, x19, #480
 add x1, x19, #512
 mov x2, #8
 mov x3, #0
 svc #0xa542
 cbz x0, QueuedFailure
 mov x25, x0
 svc #0xa548
 cmp x0, #1
 b.ne QueuedFailure
 mov x0, x25
 svc #0xa549
 cbnz x0, QueuedFailure
 ldr x9, [x19, #512]
 mov x10, #0xa5a5
 movk x10, #0xa5a5, lsl #16
 movk x10, #0xa5a5, lsl #32
 movk x10, #0xa5a5, lsl #48
 cmp x9, x10
 b.ne QueuedFailure
 mov x0, #0
 svc #0xa541
 b .
QueuedFailure:
 mov x0, #9
 svc #0xa541
 b .
RestrictedQueuedEnd:

.balign 16
.global RestrictedWorkerDeath, RestrictedWorkerDeathEnd
RestrictedWorkerDeath:
 mov x19, x1
 mov x9, #0xe4
 str x9, [x19, #480]
 mov x9, #0xa5a5
 movk x9, #0xa5a5, lsl #16
 movk x9, #0xa5a5, lsl #32
 movk x9, #0xa5a5, lsl #48
 str x9, [x19, #512]
 add x0, x19, #480
 add x1, x19, #512
 mov x2, #8
 mov x3, #0
 svc #0xa542
 cbz x0, WorkerDeathFailure
 mov x25, x0
12:
 mov x0, x25
 svc #0xa549
 cbnz x0, 12b
 mov x0, #0
 svc #0xa541
 b .
WorkerDeathFailure:
 mov x0, #9
 svc #0xa541
 b .
RestrictedWorkerDeathEnd:
