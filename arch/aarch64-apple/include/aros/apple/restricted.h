/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_RESTRICTED_H
#define AROS_RESTRICTED_H
#include <stddef.h>
#include <stdint.h>
/* Experimental native control API. Payloads use the scalar SVC ABI only;
 * native libraries and this control table are inaccessible in their root. */
#define AROS_RESTRICTED_KATTR_API UINT32_C(0x83f10021)
#define AROS_RESTRICTED_CODE UINT64_C(0x100000000)
#define AROS_RESTRICTED_DATA UINT64_C(0x100001000)
#define AROS_RESTRICTED_STACK UINT64_C(0x100003000)
#define AROS_RESTRICTED_FAULT_STACK UINT64_C(0x100005000)
#define AROS_RESTRICTED_TLS UINT64_C(0x100007000)
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
#define AROS_RESTRICTED_RUNNING 2
#define AROS_RESTRICTED_CANCELLED 4
struct AROSRestrictedResult {
    int64_t status;
    uint64_t value, esr, far, pc, yields, denied, replies, rejected, callbacks, resumed, cancelled, close_waits, drained, worker_retirements;
    uint64_t cpus, migrations, snapshot_valid;
    /* Present only when snapshot_valid: sole legacy owner or last closing
     * managed context, after all writers and worker leases drain. */
    unsigned char data[4096];
};
struct AROSRestrictedWork {
    /* Generation-tagged per-thread service channel, not an address-space handle. */
    uint64_t channel,token,length;
    unsigned char bytes[256];
};
/* pages includes all retained backing and metadata; metadata_pages is a subset.
 * Both remain charged until the release acknowledgement. */
struct AROSRestrictedLifetime {
    uint64_t retired,reaped,live,pending,pages,domains,shared_objects,metadata_pages;
};
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
    uint64_t (*create_domain)(const void *code,size_t bytes,uint64_t seed);
    int (*run_domain)(uint64_t handle,uint64_t seed,struct AROSRestrictedResult *);
    int (*close_domain)(uint64_t handle);
    /* Controller-only private anonymous memory. Reserve an aligned fixed range;
     * commit/decommit/protect operate on one 4096-byte page. Read=1, write=2;
     * zero means committed no-access. Release requires the reservation base.
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
};
#endif
