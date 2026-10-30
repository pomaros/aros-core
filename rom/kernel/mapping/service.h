/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#ifndef AROS_KERNEL_MAPPING_SERVICE_H
#define AROS_KERNEL_MAPPING_SERVICE_H
#include <stdint.h>
#include "domain.h"
#define KRN_SERVICE_BYTES 256U
#define KRN_SERVICE_SLOTS 4U
enum KrnServiceState { KS_FREE, KS_QUEUED, KS_RUNNING, KS_READY, KS_CANCELLED };
struct KrnServiceSlot {
    struct KrnMappingDomainMember retention;
    uint32_t generation;
    unsigned state;
    uint64_t owner;
    size_t length;
    unsigned char bytes[KRN_SERVICE_BYTES];
};
struct KrnServiceQueue {
    struct KrnMappingDomain *domain;
    struct KrnServiceSlot slots[KRN_SERVICE_SLOTS];
};
/* Internal mechanism, not a public ABI. All calls require ONE caller-held
 * domain/coordinator lock. Initialize once on zeroed unpublished storage;
 * never reset generations or reuse the queue before all external references
 * have been withdrawn. One queue per domain; caller holds a domain membership
 * until Close. Queue, slots and domain remain alive until final drain.
 *
 * owner is a nonzero kernel-issued principal, NOT caller-provided identity.
 * Tokens are queue-scoped (generation,index); caller validates the domain
 * handle/ownership before selecting a queue. Each owner may have one live
 * request. Generation exhaustion permanently retires a slot rather than wrap.
 * Every buffer is trusted, resident kernel storage, non-overlapping with queue
 * storage. Validate/copy user mappings BEFORE Submit; copy replies to owned
 * staging storage in Collect, then separately validate/copy to user memory.
 * No user pointer, worker callback or scheduler operation is retained here.
 */
int KrnServiceInitialize(struct KrnServiceQueue *, struct KrnMappingDomain *);
int KrnServiceSubmit(struct KrnServiceQueue *, uint64_t owner,
    const void *input, size_t length, uint64_t *token);
/* Worker-only. Take copies input and claims a lease; failure leaves output
 * untouched. A taken request MUST receive exactly one successful Complete,
 * including after cancellation. Workers do not access slot storage directly.
 */
int KrnServiceTake(struct KrnServiceQueue *, uint64_t token,
    void *input, size_t capacity, size_t *length);
int KrnServiceComplete(struct KrnServiceQueue *, uint64_t token,
    const void *reply, size_t length);
int KrnServiceCollect(struct KrnServiceQueue *, uint64_t owner, uint64_t token,
    void *reply, size_t capacity, size_t *length);
/* Queued/ready cancellation retires immediately. Running cancellation keeps
 * retention until Complete acknowledges the worker lease; reply is discarded.
 * Cancellation is idempotent only while that cancelled lease remains alive.
 */
int KrnServiceCancel(struct KrnServiceQueue *, uint64_t owner, uint64_t token);
/* Stops the domain and cancels all requests; does NOT detach caller's member,
 * release mappings, interrupt workers or establish CPU quiescence. Call Close
 * even if another subsystem already stopped the domain, to retire queued and
 * ready requests. Drained reports queue leases only, NOT domain reclamation. */
void KrnServiceClose(struct KrnServiceQueue *);
int KrnServiceDrained(const struct KrnServiceQueue *);
#endif
