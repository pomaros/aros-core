/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_RESTRICTED_SERVICE_H
#define AROS_RESTRICTED_SERVICE_H
#include <aros/apple/restricted.h>
#include <aros/apple/page_arena.h>
#include "../../aarch64-all/kernel/mapping.h"
#include "../../../rom/kernel/mapping/user_service.h"
struct Task;
struct AROSRestrictedRegion;
struct AROSRestrictedSharedBacking {
    struct AROSRestrictedSharedBacking *next;
    uintptr_t *pages;
    /* Only attached guest views of this object, retained through detach retry. */
    struct AROSRestrictedRegion *regions;
    size_t count,views;
    struct AROSRestrictedRegion *publication;
    unsigned closing;
    /* API 35: the controller's native view, counted in views. Pending while
     * it is being mapped or unmapped; both refuse executable publication. */
    uintptr_t native_view;
    unsigned native_pending;
};
/* Low fixed mappings exclude null and the complete bootstrap/thread window.
 * The low window reaches the dynamic window, so a domain sees one contiguous
 * range from 64 KiB to the code window; physical RAM identity pages inside it
 * become private on first use. */
#define AROS_RESTRICTED_LOW_BASE UINT64_C(0x10000)
#define AROS_RESTRICTED_LOW_END UINT64_C(0x200000000)
#define AROS_RESTRICTED_DYNAMIC_BASE UINT64_C(0x200000000)
#define AROS_RESTRICTED_DYNAMIC_END UINT64_C(0x400000000)
/* API 34: a high window from the end of the code window to the private
 * thread windows, admitted per domain only when the kernel template leaves it
 * unmapped. */
#define AROS_RESTRICTED_HIGH_BASE UINT64_C(0x500000000)
#define AROS_RESTRICTED_HIGH_END UINT64_C(0x2000000000)
/* API 36: the wide window, 1 TiB to 64 TiB (the native code window follows), beyond every physical identity
 * mapping of a 40-bit physical space; admitted when the template leaves it
 * unmapped. Linux programs see a full 47-bit user address space. */
#define AROS_RESTRICTED_WIDE_BASE UINT64_C(0x10000000000)
#define AROS_RESTRICTED_WIDE_END UINT64_C(0x400000000000) /* native code window above */
static inline int AROSRestrictedRangeValid(uintptr_t address,size_t length)
{
    if (!length || ((address|length)&4095)) return 0;
    if (address>=AROS_RESTRICTED_LOW_BASE && address<AROS_RESTRICTED_LOW_END)
        return length<=AROS_RESTRICTED_LOW_END-address;
    if (address>=AROS_RESTRICTED_HIGH_BASE && address<AROS_RESTRICTED_HIGH_END)
        return length<=AROS_RESTRICTED_HIGH_END-address;
    if (address>=AROS_RESTRICTED_WIDE_BASE && address<AROS_RESTRICTED_WIDE_END)
        return length<=AROS_RESTRICTED_WIDE_END-address;
    return address>=AROS_RESTRICTED_DYNAMIC_BASE && address<AROS_RESTRICTED_DYNAMIC_END &&
        length<=AROS_RESTRICTED_DYNAMIC_END-address;
}
/* Slots of the service's index of private and shared backing: both come from
 * the page arena, so twice its page capacity keeps the load at most one half. */
#define AROS_RESTRICTED_INDEX_SLOTS (2U*(size_t)(AROS_APPLE_PAGE_ARENA_MAX_BYTES/4096U))
struct AROSRestrictedSpace {
    struct AROSRestrictedSpace *next;
    struct KrnBufferSpace registration;
    struct KrnMappingDomain domain;
    struct KrnMappingDomainMember creator;
    struct KrnMappingArena arena;
    struct KrnMappingObject mapping;
    struct KrnMappingPage page;
    struct AArch64ProtectionOps hardware;
    struct KrnMappingOps mapops;
    struct KrnBufferOps bufferops;
    uint64_t root,tcr,principal,handle;
    uintptr_t backing;
    unsigned channels;
    struct AROSRestrictedRegion *regions;
};
struct AROSRestrictedRegion {
    struct AROSRestrictedSpace *space;
    struct AROSRestrictedRegion *next;
    struct KrnMappingObject mapping;
    /* Backing ledger in chunks parallel to mapping.chunks: one array of
     * mapping.chunk_pages physical addresses per attached chunk. */
    uintptr_t **backing;
    struct AROSRestrictedSharedBacking *shared;
    struct AROSRestrictedRegion *shared_next;
    /* API 36: object page shown at view page 0 */
    size_t shared_first;
    uint64_t identity;
    unsigned publication_pending;
    size_t publication_page;
};
/* Shared anonymous backing is registered once, independently of any space.
 * Initialize transfers ownership of fresh zeroed pages; close stops admission
 * and returns ownership only after every attached view has detached. A region
 * retains the whole object even when its pages are decommitted. These are
 * anonymous views, not file-backed mappings with writeback semantics.
 * Executable publication refuses any writable peer view. New writable views
 * are refused while an executable peer exists. A pending transaction blocks
 * object mutation until retry or detachment of the publishing view. */
int AROSRestrictedSharedInitialize(struct AROSRestrictedSharedBacking *,uintptr_t *,size_t);
int AROSRestrictedSharedClose(struct AROSRestrictedSharedBacking *);
/* Serialized controller copies: no backing alias in native buffer, no write
 * while a touched page has an executable alias, no access while closing or
 * publishing. Aligned eight-byte transfers are atomic and fully fenced. */
int AROSRestrictedSharedRead(struct AROSRestrictedSharedBacking *,size_t,void *,size_t);
int AROSRestrictedSharedWrite(struct AROSRestrictedSharedBacking *,size_t,const void *,size_t);
/* API 35 native view, in three coordinator steps around an uncoordinated
 * mapping. Begin admits one view of an open object with no executable page
 * and no publication, counts it and makes it pending; Commit records the
 * mapped address; Abort ends a pending view, mapped or not. End starts the
 * teardown of a committed view: it returns the address and leaves the view
 * pending until Abort, once the mapping is gone. A pending or committed view
 * refuses executable publication, keeps the object open and refuses copies
 * through a native buffer inside it. */
int AROSRestrictedSharedViewBegin(struct AROSRestrictedSharedBacking *);
int AROSRestrictedSharedViewCommit(struct AROSRestrictedSharedBacking *,uintptr_t);
int AROSRestrictedSharedViewAbort(struct AROSRestrictedSharedBacking *);
int AROSRestrictedSharedViewEnd(struct AROSRestrictedSharedBacking *,uintptr_t *);
/* API 36 owner copy; see restricted_service.c. At most
 * AROS_RESTRICTED_SHARED_COPY_MAX bytes per call. */
int AROSRestrictedSpaceCopy(struct AROSRestrictedSpace *,uintptr_t,void *,size_t,unsigned);
int AROSRestrictedRegionAttachShared(struct AROSRestrictedRegion *,struct AROSRestrictedSharedBacking *);
/* API 36: attach object pages [first, first + region pages) */
int AROSRestrictedRegionAttachSharedAt(struct AROSRestrictedRegion *,struct AROSRestrictedSharedBacking *,size_t first);
int AROSRestrictedRegionCommitShared(struct AROSRestrictedRegion *,size_t,unsigned);
/* Native owned-page adapter. The caller supplies zeroed record/backing arrays,
 * then freshly allocated and zero-filled pages; no guest PA is accepted.
 * Reservation/commit/decommit/protection are coordinator-only and nonblocking.
 * Commit transfers backing on success, retains caller ownership on failure.
 * Decommit returns a page only after acknowledged unmap. Detach closes and
 * drains the whole region, removes translations and returns ownership of ALL
 * nonzero backing entries, without freeing any native memory itself. A zero
 * detach result may follow partial unmapping: retain ALL backing and metadata
 * and retry until success; it is not an ownership-transfer acknowledgement.
 * Record/page/backing arrays stay alive through detach. SpaceClose refuses
 * attached regions: the domain coordinator must detach/free its owned list.
 * Shared views never transfer page ownership on decommit/detach: decommit
 * returns zero and successful detach clears borrowed backing entries. Close
 * the backing object separately to reclaim its pages. Executable permissions
 * require the native code-publication protocol below, never ordinary commit,
 * except the JIT class (KMAP_JIT_CLASS): a private page committed or published
 * writable and executable when the platform's JIT mode allows it, which the
 * caller checks. Shared views never take the JIT class. */
/* A region's page records and backing ledger are chunked (KrnMappingReserveVirtualChunked):
 * chunks and backing each have one zeroed slot per chunk_pages pages of the
 * capacity, and stay alive with the region. A commit (private or shared) of a
 * page whose chunk is missing is refused: the caller attaches chunk_pages
 * zeroed page records and backing entries first (AttachChunk, after
 * ChunkMissing). Private backing pages enter the service's one alias index,
 * sized by the page arena; a region carries no index of its own. */
int AROSRestrictedRegionReserve(struct AROSRestrictedSpace *,struct AROSRestrictedRegion *,
    uintptr_t,size_t,struct KrnMappingPage **chunks,uintptr_t **backing,size_t chunk_pages,
    size_t capacity);
int AROSRestrictedRegionChunkMissing(const struct AROSRestrictedRegion *,size_t page);
int AROSRestrictedRegionAttachChunk(struct AROSRestrictedRegion *,size_t page,
    struct KrnMappingPage *,uintptr_t *backing);
int AROSRestrictedRegionCommit(struct AROSRestrictedRegion *,size_t,uintptr_t,unsigned);
int AROSRestrictedRegionDecommit(struct AROSRestrictedRegion *,size_t,uintptr_t *);
int AROSRestrictedRegionProtect(struct AROSRestrictedRegion *,size_t,unsigned);
/* Owned private/shared-page publication transaction. Caller holds the mapping/coordinator
 * locks and has quiesced all CPUs using any alias for the entire call. Callback
 * must update every native alias synchronously, without allocation or waiting.
 * A transaction advances page generation once, including on later failure;
 * retries retain that generation. RX publication uses native RO/NX; non-executable targets keep it while any
 * shared peer executes, otherwise restore native RW/NX.
 * Failure after guest revocation retains a no-access page and pending state.
 * Retry the same page or detach the whole region; ordinary mutation is refused
 * meanwhile. This helper does not itself establish native quiescence. */
struct AROSRestrictedPublicationOps {
    void *context;
    int (*protect_native)(void *,uintptr_t physical,unsigned protection);
};
int AROSRestrictedRegionPublish(struct AROSRestrictedRegion *,size_t page,
    unsigned protection,const struct AROSRestrictedPublicationOps *);
int AROSRestrictedRegionDetach(struct AROSRestrictedRegion *);
struct AROSRestrictedService {
    struct AROSRestrictedService *next;
    struct AROSRestrictedSpace *space;
    struct KrnMappingDomainMember member;
    struct KrnServiceQueue queue;
    struct KrnBufferService request;
    struct Task *worker;
    void *worker_context;
    uint64_t principal, channel, worker_retirements;
    unsigned closing;
};
/* All entry points share the EL1 coordinator lock. No allocation, blocking,
 * guest pointer dereference or callback into tasks. Spaces own mappings;
 * channels retain a space independently and close without retiring it. */
int AROSRestrictedSpaceInit(struct AROSRestrictedSpace *, uint64_t principal,
    uint64_t root,uint64_t tcr,uintptr_t backing,const struct AArch64ProtectionOps *);
void AROSRestrictedSpaceStop(struct AROSRestrictedSpace *);
int AROSRestrictedSpaceClose(struct AROSRestrictedSpace *);
int AROSRestrictedSpaceProtect(struct AROSRestrictedSpace *, unsigned);
/* access is 0 (owned resident page, including a guard), 1 (read), 2 (write)
 * or 4 (execute: hardware read-only and UXN clear, or the hardware JIT leaf
 * of a JIT-class page).
 * Validate both the backing ledger and live PTE; return page identity/generation and reservation incarnation
 * without dereferencing an untrusted guest VA. */
int AROSRestrictedSpacePageIdentity(struct AROSRestrictedSpace *,uintptr_t,unsigned,
    uintptr_t *,uint64_t *,uint64_t *);
/* Fault admission also accepts an owned, unmapped RESERVED page. physical=0
 * identifies it: a valid repair must preserve the reservation incarnation and
 * perform exactly one commit (generation+1) before resumption. */
int AROSRestrictedSpaceFaultIdentity(struct AROSRestrictedSpace *,uintptr_t,
    uintptr_t *,uint64_t *,uint64_t *);
int AROSRestrictedServiceInit(struct AROSRestrictedService *,
    struct AROSRestrictedSpace *, uint64_t requester);
int AROSRestrictedServiceSubmit(struct AROSRestrictedService *,uintptr_t,uintptr_t,size_t);
int AROSRestrictedServiceCollect(struct AROSRestrictedService *,uint64_t);
int AROSRestrictedServiceTake(struct Task *,void *,struct AROSRestrictedWork *);
/* Kernel-selected space only. Refusal never leases another space's request. */
int AROSRestrictedServiceTakeSpace(struct Task *,void *,struct AROSRestrictedSpace *,struct AROSRestrictedWork *);
int AROSRestrictedServiceRetireWorker(void *);
unsigned AROSRestrictedServiceState(struct Task *,const struct AROSRestrictedWork *);
int AROSRestrictedServiceComplete(struct Task *,const struct AROSRestrictedWork *);
/* Cancels requests and returns zero while an acknowledged worker lease is
 * outstanding. Caller keeps service, tables and backing alive and retries
 * from native task context; it must never wait on the exception stack. */
int AROSRestrictedServiceClose(struct AROSRestrictedService *);
#endif
