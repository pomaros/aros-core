/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
.section .rodata.restricted,"a"
.balign 16
/* Distinct full-width sentinels for the task, callback and fault handler.
 * Cover caller-saved, partially callee-saved and high vector registers.
 * Checks use only integer scratch registers so they cannot repair corruption. */
.macro RegisterSeed stage
 add x9, x23, #\stage
 dup v0.2d, x9
 add x9, x9, #1
 dup v8.2d, x9
 add x9, x9, #1
 dup v31.2d, x9
.endm
.macro VectorCheck reg, failure=RestrictedFailure
 umov x10, \reg\().d[0]
 cmp x10, x9
 b.ne \failure
 umov x10, \reg\().d[1]
 cmp x10, x9
 b.ne \failure
 add x9, x9, #1
.endm
.macro RegisterCheck stage, failure=RestrictedFailure
 cmp x18, x20
 b.ne \failure
 mrs x9, tpidr_el0
 cmp x9, x20
 b.ne \failure
 ldr x11, [x20]
 cmp x11, x23
 b.ne \failure
 add x9, x23, #\stage
 VectorCheck v0, \failure
 VectorCheck v8, \failure
 VectorCheck v31, \failure
.endm
.global RestrictedCode, RestrictedCodeEnd
RestrictedCode:
 mov x19, x1
 mov x21, x0
 mov x23, x0
 lsr x22, x0, #2
 mrs x20, tpidr_el0
 ldr x9, [x20]
 cbnz x9, RestrictedFailure
 str x23, [x20]
 RegisterSeed 0
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
 RegisterCheck 0
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
 RegisterCheck 0
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
 /* Native lifecycle SVCs cannot be reached from a restricted root. */
 svc #0xa538
 cmn x0, #1
 b.ne RestrictedFailure
 svc #0xa539
 cmn x0, #1
 b.ne RestrictedFailure
 svc #0xa53a
 cmn x0, #1
 b.ne RestrictedFailure
 svc #0xa53b
 cmn x0, #1
 b.ne RestrictedFailure
 svc #0xa560
 cmn x0, #1
 b.ne RestrictedFailure
 svc #0xa561
 cmn x0, #1
 b.ne RestrictedFailure
 svc #0xa562
 cmn x0, #1
 b.ne RestrictedFailure
 svc #0xa563
 cmn x0, #1
 b.ne RestrictedFailure
 svc #0xa564
 cmn x0, #1
 b.ne RestrictedFailure
 svc #0xa565
 cmn x0, #1
 b.ne RestrictedFailure
 svc #0xa566
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
 RegisterCheck 0
 RegisterSeed 256
 cmp x24, #100
 b.ne 6f
 mov x0, x19
 mov x1, #1
 svc #0xa546
 cbz x0, RestrictedFailure
 mov x9, #0x1234
RestrictedWriteProbe:
 str x9, [x19, #384]
 RegisterCheck 256
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
 RegisterCheck 256
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
 /* Dedicated stack ends one page below private TLS, with absent neighbors.
  * Exercise stack memory across the worker's scheduling points. */
 mov x9, sp
 mrs x10, tpidr_el0
 sub x10, x10, #1, lsl #12
 cmp x9, x10
 b.ne RestrictedFailure
 stp x0, x1, [sp, #-32]!
 stp x2, x3, [sp, #16]
 ldp x0, x1, [sp]
 ldp x2, x3, [sp, #16]
 /* Preserve the delivered x0-x3 fault arguments while checking context. */
 RegisterCheck 256
 RegisterSeed 512
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
 RegisterCheck 512
 ldr x9, [x19, #448]
 mov x26, #0x5a5a
 movk x26, #0x5a5a, lsl #16
 movk x26, #0x5a5a, lsl #32
 movk x26, #0x5a5a, lsl #48
 eor x9, x9, x26
 cmp x9, x27
 b.ne RestrictedFailure
 ldp x9, x10, [sp, #16]
 cmp x9, x24
 b.ne RestrictedFailure
 cmp x10, x25
 b.ne RestrictedFailure
 add sp, sp, #32
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

.balign 16
.global RestrictedBadStack, RestrictedBadStackEnd
RestrictedBadStack:
 /* Aligned but outside the owned stack. Each scheduling SVC must terminate
  * this domain and restore the native continuation without locking twice. */
 mov sp, x1
 cbz x0, BadStackYield
 cmp x0, #1
 b.eq BadStackCollect
 mov x0, #0
 svc #0xa549
 b .
BadStackCollect:
 mov x0, #0
 svc #0xa543
 b .
BadStackYield:
 svc #0xa540
 b .
RestrictedBadStackEnd:

.balign 16
.global RestrictedFaultPolicy, RestrictedFaultPolicyEnd
RestrictedFaultPolicy:
 mov x19, x1
 mov x20, x0
 adr x0, RestrictedPolicyHandler
 svc #0xa545
 cbz x0, RestrictedPolicyFailure
 cbnz x20, 1f
 /* An idle handler can be removed; the subsequent fault must terminate. */
 mov x0, #0
 svc #0xa545
 cbz x0, RestrictedPolicyFailure
1:
 mov x0, x19
 mov x1, #1
 svc #0xa546
 cbz x0, RestrictedPolicyFailure
RestrictedPolicyStore:
 str x20, [x19, #512]
 cmp x20, #4
 b.ne RestrictedPolicyFailure
 mov x0, #0
 svc #0xa541
 b RestrictedPolicyFailure
RestrictedPolicyHandler:
 mov x21, x2
 mov x22, x3
 mov x9, sp
 mrs x10, tpidr_el0
 sub x10, x10, #1, lsl #12
 cmp x9, x10
 b.ne RestrictedPolicyFailure
 cmp x20, #1
 b.eq RestrictedPolicyNested
 cmp x20, #2
 b.eq RestrictedPolicyLower
 cmp x20, #3
 b.eq RestrictedPolicyUpper
 cmp x20, #4
 b.ne RestrictedPolicyFailure
 /* Active-handler removal must fail without invalidating this continuation. */
 mov x0, #0
 svc #0xa545
 cbnz x0, RestrictedPolicyFailure
 mov x0, x19
 mov x1, #3
 svc #0xa546
 cbz x0, RestrictedPolicyFailure
 mov x0, x21
 mov x1, x22
 svc #0xa547
 b RestrictedPolicyFailure
RestrictedPolicyNested:
 str x20, [x19, #520]
 b RestrictedPolicyFailure
RestrictedPolicyLower:
 sub x9, sp, #1, lsl #12
 str x20, [x9, #-16]
 b RestrictedPolicyFailure
RestrictedPolicyUpper:
 str x20, [sp]
RestrictedPolicyFailure:
 mov x0, #-9
 svc #0xa541
 b RestrictedPolicyFailure
RestrictedFaultPolicyEnd:

.balign 16
.global RestrictedShared, RestrictedSharedEnd
RestrictedShared:
 mov x19, x1
 mov x23, x0
 and x21, x0, #1
 bic x24, x0, #1
 ldr x9, [x19]
 cmp x9, x24
 b.ne RestrictedSharedFailure
 mrs x20, tpidr_el0
 ldr x9, [x20]
 cbnz x9, RestrictedSharedFailure
 str x23, [x20]
 RegisterSeed 0
 add x9, x19, #64
 str x23, [x9, x21, lsl #3]
 dmb ish
1:
 ldr x9, [x19, #64]
 ldr x10, [x19, #72]
 cbz x9, 2f
 cbz x10, 2f
 cmp x9, x24
 b.ne RestrictedSharedFailure
 add x11, x24, #1
 cmp x10, x11
 b.ne RestrictedSharedFailure
 b 3f
2:
 svc #0xa540
 RegisterCheck 0, RestrictedSharedFailure
 b 1b
3:
 mov x22, #512
 lsl x22, x22, x21
4:
 svc #0xa540
 RegisterCheck 0, RestrictedSharedFailure
 ldr x9, [x19]
 cmp x9, x24
 b.ne RestrictedSharedFailure
 subs x22, x22, #1
 b.ne 4b
 add x25, x19, #512
 add x25, x25, x21, lsl #6
 str x23, [x25]
 mov x0, x25
 add x1, x25, #8
 mov x2, #8
 mov x3, #0
 svc #0xa542
 cbz x0, RestrictedSharedFailure
 mov x26, x0
5:
 mov x0, x26
 svc #0xa543
 cbz x0, 5b
 RegisterCheck 0, RestrictedSharedFailure
 mov x9, #0x5a5a
 movk x9, #0x5a5a, lsl #16
 movk x9, #0x5a5a, lsl #32
 movk x9, #0x5a5a, lsl #48
 eor x9, x9, x23
 ldr x10, [x25, #8]
 cmp x9, x10
 b.ne RestrictedSharedFailure
 mov x0, #0
 svc #0xa541
 b RestrictedSharedFailure
RestrictedSharedFailure:
 mov x0, #-9
 svc #0xa541
 b RestrictedSharedFailure
RestrictedSharedEnd:

.balign 16
.global RestrictedSpin, RestrictedSpinEnd
RestrictedSpin:
 mov x19, x1
 mov x9, #0xe4
 str x9, [x19, #512]
 mov x9, #0xa5a5
 movk x9, #0xa5a5, lsl #16
 movk x9, #0xa5a5, lsl #32
 movk x9, #0xa5a5, lsl #48
 str x9, [x19, #544]
 add x0, x19, #512
 add x1, x19, #544
 mov x2, #8
 mov x3, #0
 svc #0xa542
 cbz x0, RestrictedSpinFailure
 /* No syscall or cooperative yield after publication. Cancellation must
  * reach this context through scheduler save, with a positive spin count. */
1:
 ldr x9, [x19, #640]
 add x9, x9, #1
 str x9, [x19, #640]
 b 1b
RestrictedSpinFailure:
 mov x0, #-9
 svc #0xa541
 b RestrictedSpinFailure
RestrictedSpinEnd:

.balign 16
.global RestrictedMemory, RestrictedMemoryEnd
RestrictedMemory:
 mov x19, x1
 mov x20, #2
 lsl x20, x20, #32
 ldr x9, [x20]
 str x9, [x19]
 add x9, x9, #1
 str x9, [x20]
 /* The native memory packet entry must never dereference a guest pointer. */
 mov x0, #-1
 svc #0xa567
 cmn x0, #1
 b.ne RestrictedMemoryFailure
 mov x0, #-1
 svc #0xa568
 cmn x0, #1
 b.ne RestrictedMemoryFailure
 mov x0, #-1
 svc #0xa569
 cmn x0, #1
 b.ne RestrictedMemoryFailure
 mov x0, #0
 svc #0xa541
RestrictedMemoryFailure:
 mov x0, #-9
 svc #0xa541
 b RestrictedMemoryFailure
RestrictedMemoryEnd:

.balign 16
.global RestrictedRegionFault, RestrictedRegionFaultEnd
RestrictedRegionFault:
 mov x19, x1
 mov x20, #2
 lsl x20, x20, #32
 adr x0, RestrictedRegionHandler
 svc #0xa545
 cbz x0, RestrictedRegionFailure
 ldr x9, [x20]
 cbnz x9, RestrictedRegionFailure
 mov x0, x20
 mov x1, #1
 svc #0xa54a
 cbz x0, RestrictedRegionFailure
 mov x9, #0x77
 str x9, [x20]
 ldr x10, [x20]
 cmp x10, #0x77
 b.ne RestrictedRegionFailure
 str x10, [x19]
 mov x0, #0
 svc #0xa541
 b RestrictedRegionFailure
RestrictedRegionHandler:
 cmp x0, x20
 b.ne RestrictedRegionFailure
 mov x21, x2
 mov x22, x3
 mov x9, sp
 mrs x10, tpidr_el0
 sub x10, x10, #1, lsl #12
 cmp x9, x10
 b.ne RestrictedRegionFailure
 /* The saved access must be repaired before its continuation is accepted. */
 mov x0, x21
 mov x1, x22
 svc #0xa547
 cmn x0, #1
 b.ne RestrictedRegionFailure
 mov x0, x20
 mov x1, #3
 svc #0xa54a
 cbz x0, RestrictedRegionFailure
 mov x0, x21
 mov x1, x22
 svc #0xa547
RestrictedRegionFailure:
 mov x0, #-9
 svc #0xa541
 b RestrictedRegionFailure
RestrictedRegionFaultEnd:

.balign 16
.global RestrictedReplacedFault, RestrictedReplacedFaultEnd
RestrictedReplacedFault:
 mov x19, x1
 mov x20, #2
 lsl x20, x20, #32
 adr x0, RestrictedReplacedHandler
 svc #0xa545
 cbz x0, RestrictedReplacedFailure
 ldr x9, [x20]
 b RestrictedReplacedFailure
RestrictedReplacedHandler:
 mov x21, x2
 mov x22, x3
 mov x9, #0x71
 str x9, [x19, #512]
 add x0, x19, #512
 add x1, x19, #520
 mov x2, #8
 mov x3, #0
 svc #0xa542
 cbz x0, RestrictedReplacedFailure
 mov x23, x0
1:
 mov x0, x23
 svc #0xa543
 cbz x0, 1b
 mov x0, x21
 mov x1, x22
 svc #0xa547
 cmn x0, #1
 b.ne RestrictedReplacedFailure
 mov x0, #0
 svc #0xa541
 b RestrictedReplacedFailure
RestrictedReplacedFailure:
 mov x0, #-9
 svc #0xa541
 b RestrictedReplacedFailure
RestrictedReplacedFaultEnd:

.balign 16
.global RestrictedThreadGrowth, RestrictedThreadGrowthEnd
RestrictedThreadGrowth:
 mov x23, x0
 mrs x20, tpidr_el0
 ldr x9, [x20]
 cbnz x9, RestrictedThreadGrowthFailure
 str x23, [x20]
 RegisterSeed 0
 add x25, x1, #512
 add x25, x25, x23, lsl #4
 str x23, [x25]
 mov x0, x25
 add x1, x25, #8
 mov x2, #8
 mov x3, #0
 svc #0xa542
 cbz x0, RestrictedThreadGrowthFailure
 mov x26, x0
1:
 svc #0xa540
 RegisterCheck 0, RestrictedThreadGrowthFailure
 mov x0, x26
 svc #0xa543
 cbz x0, 1b
 RegisterCheck 0, RestrictedThreadGrowthFailure
 ldr x9, [x25, #8]
 cmp x9, x23
 b.ne RestrictedThreadGrowthFailure
 mov x0, #0
 svc #0xa541
RestrictedThreadGrowthFailure:
 mov x0, #-9
 svc #0xa541
 b RestrictedThreadGrowthFailure
RestrictedThreadGrowthEnd:

.balign 16
.global RestrictedLargeStacks, RestrictedLargeStacksEnd
RestrictedLargeStacks:
 mov x19, x1
 mov x20, x0
 mov x21, sp
 sub sp, sp, #16, lsl #12
 sub sp, sp, #4080
 mov x9, sp
 mov x10, #17
1:
 str x10, [x9]
 add x9, x9, #1, lsl #12
 subs x10, x10, #1
 b.ne 1b
 svc #0xa540
 ldr x9, [sp]
 cmp x9, #17
 b.ne LargeStacksFailure
 cmp x20, #1
 b.eq LargeStacksLower
 cmp x20, #2
 b.eq LargeStacksUpper
 adr x0, LargeStacksHandler
 svc #0xa545
 cbz x0, LargeStacksFailure
 mov x0, x19
 mov x1, #1
 svc #0xa546
 cbz x0, LargeStacksFailure
 str x20, [x19]
 ldr x9, [sp]
 cmp x9, #17
 b.ne LargeStacksFailure
 mov x0, #0
 svc #0xa541
LargeStacksLower:
 sub x9, sp, #24
 ldr x9, [x9]
 b LargeStacksFailure
LargeStacksUpper:
 ldr x9, [x21]
 b LargeStacksFailure
LargeStacksHandler:
 mov x22, x2
 mov x23, x3
 mov x24, sp
 sub sp, sp, #2, lsl #12
 sub sp, sp, #4080
 mov x9, #123
 str x9, [sp]
 svc #0xa540
 ldr x9, [sp]
 cmp x9, #123
 b.ne LargeStacksFailure
 cmp x20, #3
 b.eq LargeStacksHandlerLower
 cmp x20, #4
 b.eq LargeStacksHandlerUpper
 mov x0, x19
 mov x1, #3
 svc #0xa546
 cbz x0, LargeStacksFailure
 mov x0, x22
 mov x1, x23
 svc #0xa547
 b LargeStacksFailure
LargeStacksHandlerLower:
 sub x9, sp, #24
 ldr x9, [x9]
 b LargeStacksFailure
LargeStacksHandlerUpper:
 ldr x9, [x24]
LargeStacksFailure:
 mov x0, #-9
 svc #0xa541
 b LargeStacksFailure
RestrictedLargeStacksEnd:

.balign 4096
.global RestrictedLargeCode, RestrictedLargeCodeEnd
RestrictedLargeCode:
 b LargeCodeBody
.balign 4096
LargeCodeBody:
 mov x19, x1
 mov x20, x0
 cmp x20, #1
 b.eq LargeCodeWrite
 adr x0, LargeCodeHandler
 svc #0xa545
 cbz x0, LargeCodeFailure
 mov x0, x19
 mov x1, #1
 svc #0xa546
 cbz x0, LargeCodeFailure
 str x20, [x19]
 mov x0, #0
 svc #0xa541
LargeCodeWrite:
 adr x9, RestrictedLargeCode
 str xzr, [x9]
 b LargeCodeFailure
.balign 4096
LargeCodeHandler:
 mov x22, x2
 mov x23, x3
 mov x0, x19
 mov x1, #3
 svc #0xa546
 cbz x0, LargeCodeFailure
 mov x0, x22
 mov x1, x23
 svc #0xa547
LargeCodeFailure:
 mov x0, #-9
 svc #0xa541
 b LargeCodeFailure
RestrictedLargeCodeEnd:

.balign 16
.global RestrictedDynamicCode, RestrictedDynamicCodeEnd
RestrictedDynamicCode:
 mov x19, x1
 mov x23, x0
 movz x20, #2, lsl #32
 cbz x23, DynamicCodeWriteTest
 cmp x23, #1
 b.eq DynamicCodeScalarTest
 bl DynamicCodeStore
 mov x27, #1
 bl DynamicCodeRequest
 blr x20
 cmp x0, x23
 b.ne DynamicCodeFailure
 mov x27, #2
 bl DynamicCodeRequest
 add x23, x23, #1
 bl DynamicCodeStore
 sub x23, x23, #1
 mov x27, #3
 bl DynamicCodeRequest
 blr x20
 add x9, x23, #1
 cmp x0, x9
 b.ne DynamicCodeFailure
 mov x0, #0
 svc #0xa541
DynamicCodeStore:
 mov x9, #0x52800000
 orr x9, x9, x23, lsl #5
 movz x10, #0x03c0, lsl #32
 movk x10, #0xd65f, lsl #48
 orr x9, x9, x10
 str x9, [x20]
 ret
DynamicCodeRequest:
 stp x27, x23, [x19]
 mov x0, x19
 add x1, x19, #32
 mov x2, #16
 mov x3, #0
 svc #0xa542
 cbz x0, DynamicCodeFailure
 mov x26, x0
1:
 mov x0, x26
 svc #0xa543
 cbz x0, 1b
 ldp x9, x10, [x19, #32]
 cmp x9, x27
 b.ne DynamicCodeFailure
 cmp x10, x23
 b.ne DynamicCodeFailure
 /* Parent fixes affinity before completing the request. Force a dispatch
  * through that mask before executing the newly published instructions. */
 svc #0xa540
 ret
DynamicCodeScalarTest:
 mov x0, x20
 mov x1, #3
 svc #0xa54a
 cbnz x0, DynamicCodeFailure
 mov x0, #0
 svc #0xa541
DynamicCodeWriteTest:
 str xzr, [x20]
DynamicCodeFailure:
 mov x0, #-9
 svc #0xa541
 b DynamicCodeFailure
RestrictedDynamicCodeEnd:

/* Copy position-independent bodies into three private pages. The native
 * controller publishes them before any indirect branch or handler admission. */
.balign 16
.global RestrictedDynamicContinuation, RestrictedDynamicContinuationEnd
RestrictedDynamicContinuation:
 mov x19, x1
 mov x23, x0
 mov x22, x23
 cmp x23, #6
 b.ne 1f
 mov x22, #4
1:
 cmp x23, #7
 b.ne 1f
 mov x22, #2
1:
 cmp x23, #8
 b.ne 1f
 mov x22, #1
1:
 cmp x23, #9
 b.ne 1f
 mov x22, #5
1:
 movz x20, #2, lsl #32
 adr x9, ContinuationBody
 adr x10, ContinuationBodyEnd
 mov x11, x20
1:
 ldr w12, [x9], #4
 str w12, [x11], #4
 cmp x9, x10
 b.lo 1b
 adr x9, ContinuationHandler
 adr x10, ContinuationHandlerEnd
 add x11, x20, #1, lsl #12
1:
 ldr w12, [x9], #4
 str w12, [x11], #4
 cmp x9, x10
 b.lo 1b
 adr x9, ContinuationCallback
 adr x10, ContinuationCallbackEnd
 add x11, x20, #2, lsl #12
1:
 ldr w12, [x9], #4
 str w12, [x11], #4
 cmp x9, x10
 b.lo 1b
 mov x27, #0
 bl ContinuationRequest
 add x0, x20, #1, lsl #12
 svc #0xa545
 cbz x0, ContinuationFailure
 cmp x22, #3
 b.hs 2f
 cmp x22, #2
 b.ne 1f
 mov x27, #1
 bl ContinuationRequest
1:
 mov x0, x19
 mov x1, #1
 svc #0xa546
 cbz x0, ContinuationFailure
2:
 br x20
ContinuationRequest:
 stp x27, x23, [x19]
 mov x0, x19
 add x1, x19, #32
 mov x2, #16
 mov x3, #0
 svc #0xa542
 cbz x0, ContinuationFailure
 mov x26, x0
1:
 mov x0, x26
 svc #0xa543
 cbz x0, 1b
 svc #0xa540
 ret
ContinuationFailure:
 mov x0, #-9
 svc #0xa541
 b ContinuationFailure

.balign 16
ContinuationBody:
 cmp x22, #3
 b.hs 1f
 str xzr, [x19]
 mov x0, #0
 svc #0xa541
1:
 mov x27, #3
 stp x27, x23, [x19]
 mov x0, x19
 add x1, x19, #32
 mov x2, #16
 add x3, x20, #2, lsl #12
 svc #0xa542
 cbz x0, ContinuationBodyFailure
 mov x26, x0
1:
 mov x0, x26
 svc #0xa543
 cbz x0, 1b
 mov x0, #0
 svc #0xa541
ContinuationBodyFailure:
 mov x0, #-9
 svc #0xa541
 b ContinuationBodyFailure
ContinuationBodyEnd:

.balign 16
ContinuationHandler:
 mov x24, x2
 mov x25, x3
 mov x0, x19
 mov x1, #3
 svc #0xa546
 cbz x0, ContinuationHandlerFailure
 cmp x22, #1
 b.ne 2f
 mov x27, #2
 stp x27, x23, [x19]
 mov x0, x19
 add x1, x19, #32
 mov x2, #16
 mov x3, #0
 svc #0xa542
 cbz x0, ContinuationHandlerFailure
 mov x26, x0
1:
 mov x0, x26
 svc #0xa543
 cbz x0, 1b
 svc #0xa540
2:
 mov x0, x24
 mov x1, x25
 svc #0xa547
 /* Only the deliberately stale continuation may return from RESUME. */
 cmp x22, #1
 b.ne ContinuationHandlerFailure
 cmn x0, #1
 b.ne ContinuationHandlerFailure
 mov x0, #0
 svc #0xa541
ContinuationHandlerFailure:
 mov x0, #-9
 svc #0xa541
 b ContinuationHandlerFailure
ContinuationHandlerEnd:

.balign 16
ContinuationCallback:
 cmp x22, #5
 b.ne 2f
 mov x27, #4
 stp x27, x23, [x19]
 mov x0, x19
 add x1, x19, #32
 mov x2, #16
 mov x3, #0
 svc #0xa542
 cbz x0, ContinuationCallbackFailure
 mov x26, x0
1:
 mov x0, x26
 svc #0xa543
 cbz x0, 1b
2:
 mov x0, #0
 svc #0xa544
ContinuationCallbackFailure:
 mov x0, #-9
 svc #0xa541
 b ContinuationCallbackFailure
ContinuationCallbackEnd:
.if ContinuationBodyEnd-ContinuationBody > 4096
.error "dynamic continuation body exceeds one page"
.endif
.if ContinuationHandlerEnd-ContinuationHandler > 4096
.error "dynamic continuation handler exceeds one page"
.endif
.if ContinuationCallbackEnd-ContinuationCallback > 4096
.error "dynamic continuation callback exceeds one page"
.endif
RestrictedDynamicContinuationEnd:

.balign 16
.global RestrictedSharedCode, RestrictedSharedCodeEnd
RestrictedSharedCode:
 mov x19, x1
 mov x23, x0
 movz x20, #2, lsl #32
 cmp x23, #2
 b.eq SharedCodeWriteTest
 cmp x23, #3
 b.eq SharedCodeScalarTest
 cbnz x23, 1f
 mov x24, #51
 bl SharedCodeStore
1:
 mov x27, #1
 bl SharedCodeRequest
 blr x20
 cmp x0, #51
 b.ne SharedCodeFailure
 mov x27, #2
 bl SharedCodeRequest
 cbnz x23, 1f
 mov x24, #77
 bl SharedCodeStore
1:
 mov x27, #3
 bl SharedCodeRequest
 blr x20
 cmp x0, #77
 b.ne SharedCodeFailure
 mov x0, #0
 svc #0xa541
SharedCodeStore:
 mov x9, #0x52800000
 orr x9, x9, x24, lsl #5
 movz x10, #0x03c0, lsl #32
 movk x10, #0xd65f, lsl #48
 orr x9, x9, x10
 str x9, [x20]
 ret
SharedCodeRequest:
 stp x27, x23, [x19]
 mov x0, x19
 add x1, x19, #32
 mov x2, #16
 mov x3, #0
 svc #0xa542
 cbz x0, SharedCodeFailure
 mov x26, x0
1:
 mov x0, x26
 svc #0xa543
 cbz x0, 1b
 ldp x9, x10, [x19, #32]
 cmp x9, x27
 b.ne SharedCodeFailure
 cmp x10, x23
 b.ne SharedCodeFailure
 svc #0xa540
 ret
SharedCodeScalarTest:
 mov x0, x20
 mov x1, #3
 svc #0xa54a
 cbnz x0, SharedCodeFailure
 mov x0, #0
 svc #0xa541
SharedCodeWriteTest:
 str xzr, [x20]
SharedCodeFailure:
 mov x0, #-9
 svc #0xa541
 b SharedCodeFailure
RestrictedSharedCodeEnd:

.balign 16
.global RestrictedReservedFault, RestrictedReservedFaultEnd
RestrictedReservedFault:
 mov x19, x1
 mov x23, x0
 movz x20, #2, lsl #32
 adr x0, ReservedFaultHandler
 svc #0xa545
 cbz x0, ReservedFaultFailure
 mov x9, #0x77
 cmp x23, #3
 b.eq ReservedFaultWrite
 ldr x10, [x20]
 cbnz x10, ReservedFaultFailure
ReservedFaultWrite:
 str x9, [x20]
 ldr x10, [x20]
 cmp x10, #0x77
 b.ne ReservedFaultFailure
 str x10, [x19]
 mov x0, #0
 svc #0xa541
ReservedFaultHandler:
 cmp x0, x20
 b.ne ReservedFaultFailure
 mov x21, x2
 mov x22, x3
 mov x9, sp
 mrs x10, tpidr_el0
 sub x10, x10, #1, lsl #12
 cmp x9, x10
 b.ne ReservedFaultFailure
 mov x0, x21
 mov x1, x22
 svc #0xa547
 cmn x0, #1
 b.ne ReservedFaultFailure
 str x23, [x19, #512]
 add x0, x19, #512
 add x1, x19, #520
 mov x2, #8
 mov x3, #0
 svc #0xa542
 cbz x0, ReservedFaultFailure
 mov x24, x0
1:
 mov x0, x24
 svc #0xa543
 cbz x0, 1b
 svc #0xa540
 mov x0, x21
 mov x1, x22
 svc #0xa547
 cmn x0, #1
 b.ne ReservedFaultFailure
 cmp x23, #1
 b.eq 1f
 cmp x23, #2
 b.ne ReservedFaultFailure
1:
 mov x0, #0
 svc #0xa541
ReservedFaultFailure:
 mov x0, #-9
 svc #0xa541
 b ReservedFaultFailure
RestrictedReservedFaultEnd:

.balign 16
/* Save and compare every preserved register, using only x0 as scratch until
 * all GP registers have been checked. x0 is the specified service result. */
.macro FullWaitCheck trap, failure
 mov x25, sp
 mov x9, #0x999
 mov x10, #0xaaa
 movi v0.16b, #1
 movi v1.16b, #2
 movi v2.16b, #3
 movi v3.16b, #4
 movi v4.16b, #5
 movi v5.16b, #6
 movi v6.16b, #7
 movi v7.16b, #8
 movi v8.16b, #9
 movi v9.16b, #10
 movi v10.16b, #11
 movi v11.16b, #12
 movi v12.16b, #13
 movi v13.16b, #14
 movi v14.16b, #15
 movi v15.16b, #16
 movi v16.16b, #17
 movi v17.16b, #18
 movi v18.16b, #19
 movi v19.16b, #20
 movi v20.16b, #21
 movi v21.16b, #22
 movi v22.16b, #23
 movi v23.16b, #24
 movi v24.16b, #25
 movi v25.16b, #26
 movi v26.16b, #27
 movi v27.16b, #28
 movi v28.16b, #29
 movi v29.16b, #30
 movi v30.16b, #31
 movi v31.16b, #32
 mov x11, #0xa0000000
 msr nzcv, x11
 mov x11, #0x400000
 msr fpcr, x11
 mov x11, #0x10
 msr fpsr, x11
 sub sp, sp, #832
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
 stp q0, q1, [sp, #256]
 stp q2, q3, [sp, #288]
 stp q4, q5, [sp, #320]
 stp q6, q7, [sp, #352]
 stp q8, q9, [sp, #384]
 stp q10, q11, [sp, #416]
 stp q12, q13, [sp, #448]
 stp q14, q15, [sp, #480]
 stp q16, q17, [sp, #512]
 stp q18, q19, [sp, #544]
 stp q20, q21, [sp, #576]
 stp q22, q23, [sp, #608]
 stp q24, q25, [sp, #640]
 stp q26, q27, [sp, #672]
 stp q28, q29, [sp, #704]
 stp q30, q31, [sp, #736]
 mrs x9, nzcv
 str x9, [sp, #768]
 mrs x9, fpcr
 str x9, [sp, #776]
 mrs x9, fpsr
 str x9, [sp, #784]
 mrs x9, tpidr_el0
 str x9, [sp, #792]
 ldr x9, [sp, #72]
 svc #\trap
 str x0, [sp, #800]
 mrs x0, nzcv
 str x0, [sp, #808]
 ldr x0, [sp, #8]
 cmp x1, x0
 b.ne \failure
 ldr x0, [sp, #16]
 cmp x2, x0
 b.ne \failure
 ldr x0, [sp, #24]
 cmp x3, x0
 b.ne \failure
 ldr x0, [sp, #32]
 cmp x4, x0
 b.ne \failure
 ldr x0, [sp, #40]
 cmp x5, x0
 b.ne \failure
 ldr x0, [sp, #48]
 cmp x6, x0
 b.ne \failure
 ldr x0, [sp, #56]
 cmp x7, x0
 b.ne \failure
 ldr x0, [sp, #64]
 cmp x8, x0
 b.ne \failure
 ldr x0, [sp, #72]
 cmp x9, x0
 b.ne \failure
 ldr x0, [sp, #80]
 cmp x10, x0
 b.ne \failure
 ldr x0, [sp, #88]
 cmp x11, x0
 b.ne \failure
 ldr x0, [sp, #96]
 cmp x12, x0
 b.ne \failure
 ldr x0, [sp, #104]
 cmp x13, x0
 b.ne \failure
 ldr x0, [sp, #112]
 cmp x14, x0
 b.ne \failure
 ldr x0, [sp, #120]
 cmp x15, x0
 b.ne \failure
 ldr x0, [sp, #128]
 cmp x16, x0
 b.ne \failure
 ldr x0, [sp, #136]
 cmp x17, x0
 b.ne \failure
 ldr x0, [sp, #144]
 cmp x18, x0
 b.ne \failure
 ldr x0, [sp, #152]
 cmp x19, x0
 b.ne \failure
 ldr x0, [sp, #160]
 cmp x20, x0
 b.ne \failure
 ldr x0, [sp, #168]
 cmp x21, x0
 b.ne \failure
 ldr x0, [sp, #176]
 cmp x22, x0
 b.ne \failure
 ldr x0, [sp, #184]
 cmp x23, x0
 b.ne \failure
 ldr x0, [sp, #192]
 cmp x24, x0
 b.ne \failure
 ldr x0, [sp, #200]
 cmp x25, x0
 b.ne \failure
 ldr x0, [sp, #208]
 cmp x26, x0
 b.ne \failure
 ldr x0, [sp, #216]
 cmp x27, x0
 b.ne \failure
 ldr x0, [sp, #224]
 cmp x28, x0
 b.ne \failure
 ldr x0, [sp, #232]
 cmp x29, x0
 b.ne \failure
 ldr x0, [sp, #240]
 cmp x30, x0
 b.ne \failure
 add x0, sp, #832
 cmp x0, x25
 b.ne \failure
 ldr x0, [sp, #800]
 cmp x0, x27
 b.ne \failure
 ldr x0, [sp, #256]
 umov x1, v0.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #264]
 umov x1, v0.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #272]
 umov x1, v1.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #280]
 umov x1, v1.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #288]
 umov x1, v2.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #296]
 umov x1, v2.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #304]
 umov x1, v3.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #312]
 umov x1, v3.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #320]
 umov x1, v4.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #328]
 umov x1, v4.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #336]
 umov x1, v5.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #344]
 umov x1, v5.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #352]
 umov x1, v6.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #360]
 umov x1, v6.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #368]
 umov x1, v7.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #376]
 umov x1, v7.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #384]
 umov x1, v8.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #392]
 umov x1, v8.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #400]
 umov x1, v9.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #408]
 umov x1, v9.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #416]
 umov x1, v10.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #424]
 umov x1, v10.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #432]
 umov x1, v11.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #440]
 umov x1, v11.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #448]
 umov x1, v12.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #456]
 umov x1, v12.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #464]
 umov x1, v13.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #472]
 umov x1, v13.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #480]
 umov x1, v14.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #488]
 umov x1, v14.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #496]
 umov x1, v15.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #504]
 umov x1, v15.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #512]
 umov x1, v16.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #520]
 umov x1, v16.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #528]
 umov x1, v17.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #536]
 umov x1, v17.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #544]
 umov x1, v18.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #552]
 umov x1, v18.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #560]
 umov x1, v19.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #568]
 umov x1, v19.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #576]
 umov x1, v20.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #584]
 umov x1, v20.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #592]
 umov x1, v21.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #600]
 umov x1, v21.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #608]
 umov x1, v22.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #616]
 umov x1, v22.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #624]
 umov x1, v23.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #632]
 umov x1, v23.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #640]
 umov x1, v24.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #648]
 umov x1, v24.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #656]
 umov x1, v25.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #664]
 umov x1, v25.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #672]
 umov x1, v26.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #680]
 umov x1, v26.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #688]
 umov x1, v27.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #696]
 umov x1, v27.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #704]
 umov x1, v28.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #712]
 umov x1, v28.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #720]
 umov x1, v29.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #728]
 umov x1, v29.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #736]
 umov x1, v30.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #744]
 umov x1, v30.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #752]
 umov x1, v31.d[0]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #760]
 umov x1, v31.d[1]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #768]
 ldr x1, [sp, #808]
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #776]
 mrs x1, fpcr
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #784]
 mrs x1, fpsr
 cmp x0, x1
 b.ne \failure
 ldr x0, [sp, #792]
 mrs x1, tpidr_el0
 cmp x0, x1
 b.ne \failure
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
 ldr x1, [sp, #8]
 ldr x30, [sp, #240]
 ldr x0, [sp, #768]
 msr nzcv, x0
 ldr x0, [sp, #800]
 add sp, sp, #832
 ret
.endm
.global RestrictedWait, RestrictedWaitEnd
RestrictedWait:
 mov x23, x0
 and x9, x23, #1
 add x19, x1, x9, lsl #8
 mov x28, x18
 svc #0xa54b
 mov x20, x0
 mov x21, x1
 tbnz x23, #0, WaitPeer
 mov x22, #0
 bl WaitRequest
 mov x0, x21
 mov x27, #0
 bl WaitChecked
 mov x22, #1
WaitNext:
 svc #0xa54b
 mov x21, x1
 bl WaitRequest
 mov x0, x21
 mov x27, #1
 bl WaitChecked
 add x22, x22, #1
 cmp x22, #4
 b.lo WaitNext
 b WaitFailure
WaitPeer:
 mov x22, #0
 bl WaitRequest
 ldp x24, x25, [x19, #80]
 mov x0, x24
 mov x1, x24
 svc #0xa56a
 cmn x0, #1
 b.ne WaitFailure
 mov x0, x24
 svc #0xa56b
 cmn x0, #1
 b.ne WaitFailure
 mov x0, x25
 svc #0xa54d
 cbnz x0, WaitFailure
 mov x0, x24
 svc #0xa54d
 cmp x0, #1
 b.ne WaitFailure
 mov x0, #0
 svc #0xa541
 b WaitFailure
WaitRequest:
 stp x22, x23, [x19]
 stp x20, x21, [x19, #16]
 mov x0, x19
 add x1, x19, #64
 mov x2, #32
 mov x3, #0
 svc #0xa542
 cbz x0, WaitFailure
 mov x26, x0
1:
 mov x0, x26
 svc #0xa543
 cbz x0, 1b
 ldp x9, x10, [x19, #64]
 cmp x9, x22
 b.ne WaitFailure
 cmp x10, x23
 b.ne WaitFailure
 svc #0xa540
 ret
WaitChecked:
 FullWaitCheck 0xa54c, WaitFailure
WaitFailure:
 mov x0, #-9
 svc #0xa541
 b WaitFailure
RestrictedWaitEnd:

.balign 16
.global RestrictedSuspend, RestrictedSuspendEnd
.global RestrictedSuspendNoCalls, RestrictedSuspendNoCallsEnd
RestrictedSuspend:
 mov x23, x0
 mov x19, x1
 mov x28, x18
 movz x20, #2, lsl #32
 tbnz x23, #0, SuspendWriterReady
 b SuspendRequest
SuspendWriterReady:
 add x21, x20, #8
1:
 ldar x9, [x21]
 cbz x9, 1b
SuspendRequest:
 str x23, [x19]
 mov x0, x19
 add x1, x19, #64
 mov x2, #8
 mov x3, #0
 svc #0xa542
 cbz x0, SuspendFailure
 mov x26, x0
 tbnz x23, #0, SuspendCollect
 mov x25, sp
 mov x9, #0x999
 movi v8.16b, #0x5a
 movi v31.16b, #0xa5
 mov x11, #0xa0000000
 msr nzcv, x11
 mov x11, #0x400000
 msr fpcr, x11
 mov x11, #0x10
 msr fpsr, x11
 add x21, x20, #8
 mov x11, #1
RestrictedSuspendNoCalls:
 stlr x11, [x21]
 mov x24, #0
SuspendPureLoop:
 add x24, x24, #1
 ldar x10, [x20]
 cbz x10, SuspendPureLoop
RestrictedSuspendNoCallsEnd:
 mrs x11, nzcv
 mov x12, #0xa0000000
 cmp x11, x12
 b.ne SuspendFailure
 cmp x9, #0x999
 b.ne SuspendFailure
 cmp x10, #1
 b.ne SuspendFailure
 cmp x18, x28
 b.ne SuspendFailure
 mov x11, sp
 cmp x11, x25
 b.ne SuspendFailure
 umov x11, v8.d[1]
 mov x12, #0x5a5a
 orr x12, x12, x12, lsl #16
 orr x12, x12, x12, lsl #32
 cmp x11, x12
 b.ne SuspendFailure
 umov x11, v31.d[0]
 mov x12, #0xa5a5
 orr x12, x12, x12, lsl #16
 orr x12, x12, x12, lsl #32
 cmp x11, x12
 b.ne SuspendFailure
 mrs x11, fpcr
 cmp x11, #0x400000
 b.ne SuspendFailure
 mrs x11, fpsr
 cmp x11, #0x10
 b.ne SuspendFailure
SuspendCollect:
 mov x0, x26
 svc #0xa543
 cbz x0, SuspendCollect
 ldr x9, [x19, #64]
 cmp x9, x23
 b.ne SuspendFailure
 tbz x23, #0, SuspendExit
 svc #0xa56c
 cmn x0, #1
 b.ne SuspendFailure
 svc #0xa56d
 cmn x0, #1
 b.ne SuspendFailure
 mov x9, #1
 stlr x9, [x20]
SuspendExit:
 mov x0, #0
 svc #0xa541
SuspendFailure:
 mov x0, #-9
 svc #0xa541
 b SuspendFailure
RestrictedSuspendEnd:

.balign 16
.global RestrictedTimedWait, RestrictedTimedWaitEnd
RestrictedTimedWait:
 mov x23, x0
 mov x19, x1
 mov x28, x18
 svc #0xa54b
 mov x20, x0
 mov x21, x1
 mov x0, x21
 mov x1, #0
 svc #0xa54e
 cmp x0, #2
 b.ne TimedFailure
 mov x0, x21
 mov x1, #-1
 svc #0xa54e
 cbnz x0, TimedFailure
 stp x23, x20, [x19]
 stp x21, xzr, [x19, #16]
 mov x0, x19
 add x1, x19, #64
 mov x2, #32
 mov x3, #0
 svc #0xa542
 cbz x0, TimedFailure
 mov x26, x0
1:
 mov x0, x26
 svc #0xa543
 cbz x0, 1b
 svc #0xa54f
 cmn x0, #1
 b.eq TimedFailure
 mov x24, x0
 mov x22, #0x86a0
 movk x22, #1, lsl #16 // 100000 us, ordinary expiration.
 cbz x23, 2f
 mov x22, #0x4240
 movk x22, #0xf, lsl #16 // 1000000 us for suspension/expiry handshake.
 cmp x23, #2
 b.eq 2f
 mov x22, #0x9680
 movk x22, #0x98, lsl #16 // 10000000 us, controller wake or close.
2:
 mov x0, x21
 mov x1, x22
 mov x27, #2
 cmp x23, #1
 b.ne 3f
 mov x27, #1
3:
 bl TimedChecked
 svc #0xa54f
 cmp x0, x24
 b.lo TimedFailure
 sub x0, x0, x24
 cmp x23, #1
 b.eq 4f
 cmp x0, x22
 b.lo TimedFailure
4:
 mov x0, #0
 svc #0xa541
 b TimedFailure
TimedChecked:
 FullWaitCheck 0xa54e, TimedFailure
TimedFailure:
 mov x0, #-9
 svc #0xa541
 b TimedFailure
RestrictedTimedWaitEnd:
