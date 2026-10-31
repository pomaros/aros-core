/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_RESTRICTED_SERVICE_H
#define AROS_RESTRICTED_SERVICE_H
#include <aros/apple/restricted.h>
#include "../../aarch64-all/kernel/mapping.h"
#include "../../../rom/kernel/mapping/user_service.h"
struct Task;
struct AROSRestrictedRegion;
struct AROSRestrictedSharedBacking {
    struct AROSRestrictedSharedBacking *next;
    uintptr_t *pages;
    size_t count,views;
    struct AROSRestrictedRegion *publication;
    unsigned closing;
};
#define AROS_RESTRICTED_DYNAMIC_BASE UINT64_C(0x200000000)
#define AROS_RESTRICTED_DYNAMIC_END UINT64_C(0x400000000)
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
    uintptr_t *backing;
    struct AROSRestrictedSharedBacking *shared;
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
int AROSRestrictedRegionAttachShared(struct AROSRestrictedRegion *,struct AROSRestrictedSharedBacking *);
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
 * require the native code-publication protocol below, never ordinary commit. */
int AROSRestrictedRegionReserve(struct AROSRestrictedSpace *,struct AROSRestrictedRegion *,
    uintptr_t,size_t,struct KrnMappingPage *,uintptr_t *,size_t);
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
 * or 4 (execute, hardware read-only and UXN clear).
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
int AROSRestrictedServiceRetireWorker(void *);
unsigned AROSRestrictedServiceState(struct Task *,const struct AROSRestrictedWork *);
int AROSRestrictedServiceComplete(struct Task *,const struct AROSRestrictedWork *);
/* Cancels requests and returns zero while an acknowledged worker lease is
 * outstanding. Caller keeps service, tables and backing alive and retries
 * from native task context; it must never wait on the exception stack. */
int AROSRestrictedServiceClose(struct AROSRestrictedService *);
#endif
