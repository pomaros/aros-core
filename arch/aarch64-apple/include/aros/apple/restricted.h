/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_RESTRICTED_H
#define AROS_RESTRICTED_H
#include <stddef.h>
#include <stdint.h>
/* Experimental native control API. Payloads use the scalar SVC ABI only;
 * native libraries and this control table are inaccessible in their root. */
#define AROS_RESTRICTED_KATTR_API UINT32_C(0x83f10021)
/* Immutable image window, disjoint from data, thread stacks and dynamic regions. */
#define AROS_RESTRICTED_CODE UINT64_C(0x400000000)
#define AROS_RESTRICTED_CODE_END UINT64_C(0x500000000)
/* Private control/thread window above the high allocation window and RAM. */
#define AROS_RESTRICTED_DATA UINT64_C(0x2000001000)
#define AROS_RESTRICTED_STACK UINT64_C(0x2000003000)
#define AROS_RESTRICTED_FAULT_STACK UINT64_C(0x2000005000)
#define AROS_RESTRICTED_TLS UINT64_C(0x2000007000)
#define AROS_RESTRICTED_THREAD_END UINT64_C(0x2100000000)
#define AROS_RESTRICTED_THREAD_STRIDE UINT64_C(0x10000)
#define AROS_RESTRICTED_ENTER 0xa530
#define AROS_RESTRICTED_YIELD 0xa540
#define AROS_RESTRICTED_EXIT 0xa541
#define AROS_RESTRICTED_SUBMIT 0xa542
#define AROS_RESTRICTED_COLLECT 0xa543
#define AROS_RESTRICTED_CALLBACK_RETURN 0xa544
#define AROS_RESTRICTED_FAULT_HANDLER 0xa545
#define AROS_RESTRICTED_PROTECT_DATA 0xa546
#define AROS_RESTRICTED_FAULT_RESUME 0xa547
#define AROS_RESTRICTED_CANCEL 0xa548
#define AROS_RESTRICTED_STATUS 0xa549
#define AROS_RESTRICTED_PROTECT_MEMORY 0xa54a
/* INFO returns channel in x0 and wake generation in x1; version 34 adds the
 * index of the CPU executing the call in x2 (a hint: the thread may migrate
 * right after). WAIT takes the
 * observed generation in x0: 0 means changed/exhausted/refused, 1 means woken.
 * Query again after wake; signals are coalesced by generation, not counted.
 * WAKE takes a same-domain channel in x0, returns 1 on success, else 0.
 * Closing the domain cancels parked execution through the native return. */
#define AROS_RESTRICTED_THREAD_INFO 0xa54b
#define AROS_RESTRICTED_WAIT 0xa54c
#define AROS_RESTRICTED_WAKE 0xa54d
#define AROS_RESTRICTED_WAIT_FOR 0xa54e
#define AROS_RESTRICTED_CLOCK 0xa54f
/* Version 30: restore the calling restricted thread. x0 points to this record,
 * x1 is its exact size. Source must be readable owned data or the caller's
 * normal stack. Success does not return; refusal returns x0=0 and preserves
 * all other registers. No service, wait, callback or fault may be active.
 * PC must name executable owned code; SP stays in the caller's normal stack.
 * NZCV is the only writable PSTATE field. Native TLS/root and exception state
 * are kernel-owned and unchanged. x18 is an ordinary guest register here.
 * The kernel copies before validating; callers synchronize concurrent writers.
 */
#define AROS_RESTRICTED_RESTORE_CONTEXT 0xa550
struct AROSRestrictedContext {
    uint64_t x[31],sp,pc,nzcv;
    uint64_t simd[64],fpcr,fpsr;
};
/* Version 33: synchronous exception delivery. EXCEPTION_HANDLER registers an
 * owned executable handler PC in x0 (0 unregisters); x0 returns 1, or 0 when
 * refused (active exception or repair, unowned PC). A synchronous exception
 * that the FAULT_HANDLER repair path does not take (undefined instruction,
 * instruction or data abort at any address, PC/SP alignment, FP trap, BRK)
 * enters the handler on the fault stack instead of ending the domain. The
 * kernel writes this record at the top of the fault stack and sets x0 to its
 * address, x1 to ESR, x2 to FAR, x3 to the exception generation, SP just below
 * the record and x30 to 0. The handler continues only through EXIT or
 * RESTORE_CONTEXT, whose record must lie in the normal stack or owned data and
 * whose SP must return to the normal stack; the restore clears the exception
 * state. Every other SVC is denied, and an exception, service, wait or
 * callback state while the handler runs ends the domain. Delivery needs a fault
 * stack of at least the record plus 1024 bytes and no active callback, repair,
 * wait or service request; otherwise the exception ends the domain as before. */
#define AROS_RESTRICTED_EXCEPTION_HANDLER 0xa551
/* Version 34: a FAULT_HANDLER repair that does not commit the page declines
 * with x0 = faulting PC and x1 = fault generation (as FAULT_RESUME takes):
 * the saved fault becomes a synchronous exception for the EXCEPTION_HANDLER,
 * or ends the domain when none can take it. Refusal returns x0 = UINT64_MAX.
 * FAULT_RESUME, and every stack admission, accept an owned stack as for
 * RESTORE_CONTEXT, including an owned reserved page below SP that a demand
 * commit will back. Together they let a controller commit lazily: pages are
 * backed on first access by a repair, as Windows charges commit without
 * backing it. */
#define AROS_RESTRICTED_FAULT_DECLINE 0xa552
/* Version 38, threads started with run_served only. x0 = address of a
 * 256-byte request in the thread's own committed, writable memory. The
 * thread leaves the domain and its native task, on the same CPU, receives
 * the request in the handler it gave to run_served. When the handler
 * returns, the 256 bytes it left are copied back to the same address and
 * the thread continues with x0 = 1; every other register is preserved.
 * Refusal (thread not served, a callback pending, memory not accessible)
 * returns x0 = UINT64_MAX at once. A call is admitted from the fault and
 * exception stacks too. No request is queued and nobody is woken: the
 * thread that asks is the thread that answers. A served thread's SUBMIT of
 * 256 bytes with the reply in the same buffer and no callback is taken as
 * this call, and its COLLECT succeeds at once, so programs built for the
 * queue run served without being rebuilt. */
#define AROS_RESTRICTED_CALL 0xa553
struct AROSRestrictedException {
    struct AROSRestrictedContext context;
    uint64_t esr,far,generation,reserved;
};
#define AROS_RESTRICTED_WAIT_WOKEN 1U
#define AROS_RESTRICTED_WAIT_EXPIRED 2U
/* WAIT_FOR takes generation in x0, relative microseconds in x1. Zero duration
 * expires immediately; overflow/unavailable clock/refusal returns 0. Wake is
 * 1, expiry 2. CLOCK returns monotonic microseconds, UINT64_MAX if unavailable.
 * Expiry is not earlier than the deadline; timer/scheduler granularity can
 * delay return. Wake at/after deadline cannot replace expiry with success.
 * Expiry satisfies WAIT only; SUSPEND and native Exec freeze retain ownership. */
#define AROS_RESTRICTED_THREAD_ACTIVE 1U
#define AROS_RESTRICTED_THREAD_WAITING 2U
#define AROS_RESTRICTED_THREAD_PARKED 4U
#define AROS_RESTRICTED_THREAD_SUSPENDED 8U
#define AROS_RESTRICTED_RUNNING 2
#define AROS_RESTRICTED_CANCELLED 4
struct AROSRestrictedResult {
    int64_t status;
    uint64_t value, esr, far, pc, yields, denied, replies, rejected, callbacks, resumed, cancelled, close_waits, drained, worker_retirements;
    uint64_t cpus, migrations, snapshot_valid;
    /* Synchronous exceptions delivered to the registered handler. */
    uint64_t exceptions;
    /* Present only when snapshot_valid: sole legacy owner or last closing
     * managed context, after all writers and worker leases drain. */
    unsigned char data[4096];
};
struct AROSRestrictedWork {
    /* Generation-tagged per-thread service channel, not an address-space handle. */
    uint64_t channel,token,length;
    unsigned char bytes[256];
};
/* pages includes all retained backing and metadata; metadata_pages and table_pages are disjoint subsets.
 * Both remain charged until the release acknowledgement. */
struct AROSRestrictedLifetime {
    uint64_t retired,reaped,live,pending,pages,domains,shared_objects,metadata_pages,table_pages;
    uint64_t waits,timed_waits,suspended_threads;
};
#define AROS_RESTRICTED_SHARED_COPY_MAX 65536U
struct AROSRestrictedAPI {
    uint32_t version, size;
    int (*run)(const void *code, size_t bytes, uint64_t seed,
        struct AROSRestrictedResult *result);
    int (*take)(struct AROSRestrictedWork *);
    int (*complete)(const struct AROSRestrictedWork *);
    unsigned (*state)(const struct AROSRestrictedWork *);
    void (*lifetime)(struct AROSRestrictedLifetime *);
    /* Native controller API: handles inherit the buffer-space generation.
     * run_domain attaches the calling native task synchronously. Its stacks,
     * TLS, continuation and service channel remain private to that context.
     * close_domain is controller-only: 1 reclaimed, 0 cancellation/drain pending,
     * -1 stale/foreign handle. A controller's retirement initiates cancellation.
     * Managed run results have no bulk data snapshot while peers can write;
     * snapshot_valid identifies the last closing context after all writers drain. */
    /* Copy an immutable image into the dedicated CODE window. Storage rounds
     * up to pages and zero-fills padding; callback/handler PCs stay within bytes.
     * Sizes exceeding the native protection length or available arena fail. */
    uint64_t (*create_domain)(const void *code,size_t bytes,uint64_t seed);
    int (*run_domain)(uint64_t handle,uint64_t seed,struct AROSRestrictedResult *);
    int (*close_domain)(uint64_t handle);
    /* Version 29 additionally admits [0x10000,0x100000000); the original
     * [0x200000000,0x400000000) window remains. Version 34 adds
     * [0x1000000000,0x2000000000) where the kernel's page-table template maps
     * nothing; reservations there are refused otherwise. A reservation must fit wholly
     * inside one window. Null, bootstrap/thread space and code stay excluded.
     * Controller-only private anonymous memory. Reserve an aligned fixed range;
     * commit/decommit/protect operate on one 4096-byte page. Read=1, write=2;
     * zero means committed no-access. protect_memory additionally accepts
     * read+execute (5) for committed pages through a quiesced publication
     * transaction. Shared publication requires every writable peer to be revoked;
     * writers are refused while any peer executes. Commit never grants execute,
     * except version 34's JIT pages: commit_memory and protect_memory accept
     * read+write+execute (7) on private pages when the kernel has a JIT mode
     * (AROS_APPLE_KATTR_JIT_MODE, <aros/apple/jit.h>), and refuse it otherwise.
     * Such a page is genuinely writable and executable on a VM guest (RWX
     * mode); in SPRR mode the kernel switches the thread between writing and
     * executing JIT pages on the faulting access, and a JIT-page instruction
     * that writes a JIT page faults. Shared views never become JIT pages. A
     * JIT page's execute identity proves ownership and generation, not
     * immutable contents. Dynamic code
     * can supply callbacks and fault continuations: saved code identity must
     * still match when entered/restored. Replacement or revocation fails closed.
     * Release requires the reservation base.
     * A failed release can begin draining: retain the reservation and retry.
     * All return 1 on success, 0 on refusal; no physical address is exposed. */
    int (*reserve_memory)(uint64_t handle,uintptr_t address,size_t length);
    int (*commit_memory)(uint64_t handle,uintptr_t address,unsigned protection);
    int (*decommit_memory)(uint64_t handle,uintptr_t address);
    int (*protect_memory)(uint64_t handle,uintptr_t address,unsigned protection);
    int (*release_memory)(uint64_t handle,uintptr_t address);
    /* Controller-owned shared backing. Attach requires an empty reservation of
     * exactly the object's size; commit each view page separately. Decommit
     * retains shared contents. close_shared stops admission and returns 0 while
     * views remain (also for stale/foreign handles); retry after release_memory
     * or domain close. Creator retirement closes and reaps orphan objects. */
    uint64_t (*create_shared)(size_t length);
    int (*attach_shared)(uint64_t domain,uintptr_t address,uint64_t object);
    int (*commit_shared)(uint64_t domain,uintptr_t address,unsigned protection);
    int (*close_shared)(uint64_t object);
    /* Explicit eager stack backing, each nonzero and page-aligned. Both stacks
     * have unmapped guards; their VA intervals remain owned until release ACK.
     * Failure returns 0. Other run entry points select one page per stack. */
    int (*run_domain_stacks)(uint64_t handle,uint64_t seed,size_t stack_bytes,
        size_t fault_bytes,struct AROSRestrictedResult *result);
    /* Controller-only wake of an active member identified by its channel. */
    int (*wake_thread)(uint64_t domain,uint64_t channel);
    /* Controller snapshot under the scheduler task lock. Zero means stale,
     * foreign or no active restricted execution. PARKED acknowledges saved
     * context on the wait list; external native freeze is not PARKED. */
    unsigned (*thread_state)(uint64_t domain,uint64_t channel);
    /* Controller-only counted suspension: prior count on success, -1 on
     * refusal. Suspend marks a running thread for scheduler preemption;
     * thread_state PARKED acknowledges its complete saved context. Resume
     * removes one suspension; a remaining wait still prevents execution.
     * Close cancels all restricted park reasons. Native Exec freeze is owned
     * separately and is never released by these calls. */
    int64_t (*suspend_thread)(uint64_t domain,uint64_t channel);
    int64_t (*resume_thread)(uint64_t domain,uint64_t channel);
    /* Version31: owner-only native-buffer copies through an opaque shared handle.
     * At most64KiB per call; validate the entire range before copying. Buffer
     * must not alias registered shared storage. Closing/publishing objects and
     * writes to executable shared pages are refused. No guest pointer or PA is
     * exposed. Aligned eight-byte transfers are atomic and fully fenced; larger
     * copies need caller synchronization (for example a sequence counter).
     * Return1 on success,0 on refusal. Zero length accepts NULL at a valid end.
     * The native controller is trusted to supply accessible scratch memory. */
    int (*read_shared)(uint64_t object,size_t offset,void *buffer,size_t length);
    int (*write_shared)(uint64_t object,size_t offset,const void *buffer,size_t length);
    /* Version32: copied service work from one live managed domain owned by the
     * calling Native controller. Invalid/foreign/closing domains return0 and
     * leave work unchanged. Other domains' requests remain queued. Legacy take
     * retains global worker semantics; completion binds the original worker. */
    int (*take_domain)(uint64_t domain,struct AROSRestrictedWork *work);
    /* Version 35: the owner's native view of a shared object, for a native
     * peer that must read and write the memory a domain sees as it changes,
     * such as a Venus renderer and its rings. One view per object: a native
     * address of the whole object, read-write and never executable, NULL on
     * refusal (not the owner, closing, publishing, a view already, a page
     * executable in a domain, or no window space). While the view exists,
     * no page of the object can be published executable, close_shared
     * returns 0 as for a domain view, and copies refuse a native buffer
     * inside it. The view does not follow decommit: the object keeps its
     * pages. unmap_shared ends the view (1, or 0 when there is none). A
     * controller that retires with a view leaves it to be unmapped with its
     * orphan objects. Contents are shared with the domain without
     * synchronization: the native peer validates what it reads. */
    void *(*map_shared)(uint64_t object);
    int (*unmap_shared)(uint64_t object);
    /* Version 36: the owner copies between its native memory and committed,
     * accessible pages of a live domain, up to AROS_RESTRICTED_SHARED_COPY_MAX
     * bytes per call. The whole range is validated before any byte moves:
     * owned, committed and mapped with the requested access, so a write to
     * published code is refused. 1 on success, 0 on refusal. */
    int (*read_domain)(uint64_t domain,uintptr_t address,void *buffer,size_t length);
    int (*write_domain)(uint64_t domain,uintptr_t address,const void *buffer,size_t length);
    /* Version 36: attach pages [first_page, first_page + n) of a shared object
     * to an empty reservation of exactly n pages at address. */
    int (*attach_shared_at)(uint64_t domain,uintptr_t address,uint64_t object,size_t first_page);
    /* Version 36: protect_memory for consecutive pages under one rendezvous;
     * returns the number of pages changed (stops at the first refusal). */
    size_t (*protect_range)(uint64_t domain,uintptr_t address,size_t pages,unsigned protection);
    /* Version 37: turn a committed read-only, single-page shared reservation
     * into private read-write backing containing the same bytes. Keeps the
     * reservation identity. Only the owning controller can call this; pending
     * write-fault tickets matching the old page identity are renewed exactly
     * once to the replacement identity. Other stale identities stay invalid.
     * Returns 1 on success, 0 on refusal. */
    int (*privatize_memory)(uint64_t domain,uintptr_t address);
    /* Version 38: run_domain_stacks for a thread that is served by its own
     * native task. Each AROS_RESTRICTED_CALL of the thread returns into
     * handler(context, bytes) on the calling task, outside the domain and
     * outside the kernel: the handler may block, take locks and use any API
     * of this structure as the controller that owns the domain, for this
     * domain and every other one of that controller. It answers in bytes
     * and returns nonzero to continue the thread, or zero to end it; the
     * thread's result then reports the status given in the first 8 bytes.
     * While a thread is inside its handler it executes no domain code:
     * suspend_thread counts as usual and the thread re-enters the domain
     * only when its count is back to zero. */
    int (*run_served)(uint64_t handle,uint64_t seed,size_t stack_bytes,
        size_t fault_bytes,struct AROSRestrictedResult *result,
        int (*handler)(void *context,unsigned char bytes[256]),void *context);
    /* Version 39: remove an already-decommitted, page-aligned interval within
     * one private reservation owned by this controller. Retained pages keep
     * physical backing, protections and fault/code identities; a middle cut
     * creates two independently releasable reservations. The kernel retains
     * their shared metadata until the final native release finishes. Refuses
     * live backing in the hole, shared views, publication and request leases.
     * Allocation/refusal changes nothing. Returns1 on success,0 on refusal. */
    int (*cut_memory)(uint64_t domain,uintptr_t address,size_t length);

};
#endif
