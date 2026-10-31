/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#ifndef AROS_KERNEL_MAPPING_OBJECT_H
#define AROS_KERNEL_MAPPING_OBJECT_H

#include <stdint.h>
#include <stddef.h>

enum KrnMappingAccess { KMAP_READ = 1, KMAP_WRITE = 2, KMAP_EXEC = 4 };
enum KrnMappingPageState {
    KMAP_ABSENT, KMAP_LOADING, KMAP_CLEAN, KMAP_DIRTY,
    KMAP_WRITEBACK, KMAP_ERROR, KMAP_RESERVED
};
enum KrnMappingCutResult {
    KMAP_CUT_INVALID, KMAP_CUT_TRIMMED, KMAP_CUT_SPLIT, KMAP_CUT_RELEASED
};
enum KrnMappingFaultResult {
    KMAP_FAULT_OUTSIDE, KMAP_FAULT_RETRY, KMAP_FAULT_WAIT,
    KMAP_FAULT_QUEUE, KMAP_FAULT_TRAP, KMAP_FAULT_FATAL
};
struct KrnMappingPage {
    uintptr_t physical;
    uint64_t generation;
    unsigned state;
    int error;
    unsigned protection;
};
struct KrnMappingObject {
    uintptr_t start;
    size_t length, page_size, page_count;
    uint64_t offset;
    void *owner;
    struct KrnMappingPage *pages;
    unsigned protection, shared, closing;
    size_t resident, in_flight;
    struct KrnMappingObject *next;
    unsigned virtual_reservation;
};
struct KrnMappingArena {
    uintptr_t start;
    size_t length, page_size;
    struct KrnMappingObject *first;
};
struct KrnMappingRequest {
    struct KrnMappingObject *mapping;
    size_t page;
    uint64_t generation;
};

/* Storage is allocated before publication. All functions run under the same
 * mapping lock. No function blocks, allocates or performs filesystem I/O.
 * The caller enqueues QUEUE requests exactly once, atomically registers a
 * waiter on WAIT/QUEUE before releasing the lock, then suspends that task.
 * Completion wakes every waiter; each retries the original instruction.
 * fault(wait) must NOT invoke Wait() on an exception stack.
 *
 * install publishes a physical page read-only for CLEAN and writable for
 * DIRTY, according to mapping protection; remove removes the translation.
 * They synchronously quiesce other CPUs and acknowledge TLB invalidation.
 * Callbacks run under the mapping lock and cannot call this engine again.
 */
struct KrnMappingOps {
    void *context;
    int (*install)(void *, uintptr_t virtual_address, uintptr_t physical,
        unsigned protection);
    int (*remove)(void *, uintptr_t virtual_address, uintptr_t physical);
};
int KrnMappingInitialize(struct KrnMappingObject *, uintptr_t, size_t,
    size_t page_size, uint64_t offset, unsigned protection, int shared,
    void *owner, struct KrnMappingPage *, size_t capacity);
/* The platform supplies a verified unbacked VA arena. Reservation allocates
 * no physical pages and never displaces an existing mapping. All arena and
 * object operations share the mapping lock. Object/page storage remains
 * alive through drain + release; requests cannot outlive that storage. */
int KrnMappingArenaInitialize(struct KrnMappingArena *, uintptr_t, size_t,
    size_t page_size);
int KrnMappingReserve(struct KrnMappingArena *, struct KrnMappingObject *,
    size_t length, uint64_t offset, unsigned protection, int shared,
    void *owner, struct KrnMappingPage *, size_t capacity);
/* Exact-address reservation, never replacement. Address/range must be aligned
 * and wholly inside the verified arena. Collision or invalid input leaves the
 * arena, destination record and page metadata unchanged. Caller supplies
 * unpublished record/page storage and holds the same mapping lock. */
int KrnMappingReserveAt(struct KrnMappingArena *, struct KrnMappingObject *,
    uintptr_t address, size_t length, uint64_t offset, unsigned protection,
    int shared, void *owner, struct KrnMappingPage *, size_t capacity);
/* Anonymous virtual reservations start uncommitted, unlike demand-paged file
 * mappings. address==0 selects first fit; nonzero is exact and never replaces.
 * Commit changes only RESERVED pages to ABSENT with the requested protection;
 * existing committed pages are unchanged. It authorizes zero-backed page-in,
 * not immediate physical allocation: the coordinator supplies KPAGER_ZERO.
 * Decommit requires all mapping I/O/buffer pins drained, then removes the PTE
 * with acknowledged invalidation before returning backing to the caller.
 * It preserves the virtual reservation and refuses file/shared objects.
 * A failed operation leaves metadata/output unchanged. released is a distinct
 * trusted output, never an alias of mapping/page storage. No allocation or wait
 * occurs here; a range coordinator owns rollback of multi-page PTE operations. */
int KrnMappingReserveVirtual(struct KrnMappingArena *, struct KrnMappingObject *,
    uintptr_t address, size_t length, void *owner, struct KrnMappingPage *, size_t capacity);
int KrnMappingCommitRange(struct KrnMappingObject *, uintptr_t address,
    size_t length, unsigned protection);
int KrnMappingDecommitPage(struct KrnMappingObject *, size_t page,
    const struct KrnMappingOps *, uintptr_t *released);
struct KrnMappingObject *KrnMappingLookup(struct KrnMappingArena *, uintptr_t);
int KrnMappingRelease(struct KrnMappingArena *, struct KrnMappingObject *);
/* Retire a drained, already-unmapped subrange. BeginClose and the caller's
 * acknowledged translation removal/backing release precede this operation.
 * Every page in the cut must have physical==0; the remaining pages are
 * transferred as metadata, never copied or reinstalled. For a middle cut,
 * right/right_pages are unpublished caller-owned storage of sufficient size.
 * TRIMMED retains m, SPLIT retains m and publishes right, RELEASED detaches
 * m for the caller to free after dropping the lock. Failure changes nothing.
 * No outstanding request or waiter may retain page indices across a cut.
 */
enum KrnMappingCutResult KrnMappingCut(struct KrnMappingArena *,
    struct KrnMappingObject *, uintptr_t address, size_t length,
    struct KrnMappingObject *right, struct KrnMappingPage *right_pages,
    size_t right_capacity);
enum KrnMappingFaultResult KrnMappingFault(struct KrnMappingObject *,
    uintptr_t address, unsigned access, int may_block,
    struct KrnMappingRequest *, const struct KrnMappingOps *);
/* Pager owns its temporary page until completion returns 1. Error completion
 * never takes ownership. No data is published until filling has succeeded.
 * A matching completion after BeginClose drains its request and returns 0
 * without installing a PTE or retaining physical backing (ops may be NULL).
 * The caller must still release any temporary page. A repeated/stale completion
 * has no effect. */
int KrnMappingComplete(const struct KrnMappingRequest *, uintptr_t physical,
    int error, const struct KrnMappingOps *);
int KrnMappingBeginWriteback(struct KrnMappingObject *, size_t page,
    struct KrnMappingRequest *, const struct KrnMappingOps *);
int KrnMappingEndWriteback(const struct KrnMappingRequest *, int error);
/* Freeze an ordered batch of dirty pages before one filesystem transaction.
 * Requests initially contain each current DIRTY generation and must be strictly
 * increasing by virtual address (including across objects). Preflight rejects
 * stale/duplicate/nonshared pages before changing any PTE. All callbacks must
 * acknowledge RO on every CPU. On failure, completed freezes revert to DIRTY
 * with advanced generation and RO PTEs; a later write fault grants write again.
 * End preflights the entire batch before publishing any completion. Caller
 * holds the mapping lock throughout each operation and wakes waiters afterward.
 * This does not discover/freeze other aliases: the native coordinator must
 * include every alias and pin their lifetimes through the whole transaction.
 */
int KrnMappingBeginWritebackBatch(struct KrnMappingRequest *, size_t count,
    const struct KrnMappingOps *);
int KrnMappingEndWritebackBatch(const struct KrnMappingRequest *, size_t count,
    int error);

int KrnMappingProtectPage(struct KrnMappingObject *, size_t page,
    unsigned protection, const struct KrnMappingOps *);
/* BeginClose prevents new faults and returns 0 until pager I/O has drained.
 * Dirty shared pages must be successfully synced before releasing backing.
 * ReleasePage returns physical ownership only after translation removal. */
int KrnMappingBeginClose(struct KrnMappingObject *);
uintptr_t KrnMappingReleasePage(struct KrnMappingObject *, size_t page,
    const struct KrnMappingOps *);

/* Preflight a whole page-aligned subrange, then optionally queue absent pages.
 * No permissions are changed. Existing backing stays resident until unmap.
 * Queue refuses stored page errors/generation exhaustion before mutation;
 * validation alone checks live coverage even for inaccessible/error pages. */
int KrnMappingPopulate(struct KrnMappingObject *, uintptr_t address, size_t length,
    int queue, size_t *resident, size_t *pending, int *page_error);
#endif
