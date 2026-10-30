/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#ifndef AROS_KERNEL_USER_BUFFER_H
#define AROS_KERNEL_USER_BUFFER_H
#include "mapping.h"
#include "domain.h"
#define KBUF_SPACES 4U
#define KBUF_PAGES 4U
#define KBUF_BYTES 256U
struct KrnBufferSpace {
    struct KrnMappingDomain *domain;
    struct KrnMappingArena *arena;
    struct KrnMappingDomainMember retention;
    uint64_t principal;
    uint32_t generation;
    unsigned closing;
    size_t leases;
};
struct KrnBufferRegistry { struct KrnBufferSpace spaces[KBUF_SPACES]; };
struct KrnBufferPart {
    struct KrnMappingObject *mapping;
    size_t page, offset, length;
    uintptr_t physical;
    uint64_t generation;
};
struct KrnBufferLease {
    struct KrnBufferSpace *space;
    struct KrnMappingDomainMember retention;
    uint32_t generation;
    unsigned access, count;
    size_t length;
    struct KrnBufferPart parts[KBUF_PAGES];
};
/* admit verifies CURRENT hardware translation, EL0 access and expected PA for
 * the complete page, returning a permanently resident trusted byte alias.
 * It must reject device/non-owned memory and table-level permission denial.
 * No dereference of the supplied guest VA is permitted. Caller pins the alias
 * and backing until release. Callback is mandatory, read-only, nonblocking and
 * non-reentrant; it must preserve the caller's current root/interrupt state.
 * An alias is NOT authority: ledger ownership/protection is checked separately.
 */
struct KrnBufferOps {
    void *context;
    unsigned char *(*admit)(void *, const struct KrnBufferSpace *, uintptr_t va,
        uintptr_t physical, size_t page_size, unsigned access);
};
/* All APIs share the address-space/coordinator lock with mapping mutations.
 * Registry, domains, arenas and leases are zeroed before first publication.
 * Registry storage/generation counters are never reset while handles may exist.
 * principal is nonzero KERNEL-derived caller identity, never an untrusted field.
 * Each registered space owns an initially empty arena; mapping.owner must be
 * the validated KrnBufferSpace pointer. Registry and domains must outlive all
 * users. No pointer in these structs is exposed to an untrusted task.
 * Anonymous resident mappings only; no shared/file/writeback support here.
 */
int KrnBufferRegister(struct KrnBufferRegistry *, uint64_t principal,
    struct KrnMappingDomain *, struct KrnMappingArena *, uint64_t *handle);
struct KrnBufferSpace *KrnBufferLookup(struct KrnBufferRegistry *,
    uint64_t principal, uint64_t handle);
int KrnBufferClose(struct KrnBufferRegistry *, uint64_t principal, uint64_t handle);
/* Requires closed, empty arena and no leases. Detaches registry retention;
 * external workers/mappings can still retain the domain: check CanReclaim. */
int KrnBufferUnregister(struct KrnBufferRegistry *, uint64_t principal, uint64_t handle);
/* Nonempty ranges <= KBUF_BYTES, at most KBUF_PAGES; read or write only.
 * Preflights EVERY page before changing counts or output lease. Adds mapping
 * in_flight references, so existing ReleasePage/Cut/Release refuse reclamation.
 * Leases are caller-owned, noncopyable kernel records; backing is not freed by
 * Release. Closing/protecting a mapping invalidates copy permission immediately
 * even while its pinned backing remains alive. */
int KrnBufferPin(struct KrnBufferRegistry *, uint64_t principal, uint64_t handle,
    uintptr_t address, size_t length, unsigned access,
    const struct KrnBufferOps *, struct KrnBufferLease *);
int KrnBufferRead(const struct KrnBufferLease *, uint64_t principal,
    void *kernel_output, size_t capacity, const struct KrnBufferOps *);
int KrnBufferWrite(const struct KrnBufferLease *, uint64_t principal,
    const void *kernel_input, size_t length, const struct KrnBufferOps *);
/* Trusted kernel buffers must not overlap registry/lease/mapping storage or
 * pinned aliases. Both copies revalidate all parts before touching bytes;
 * rejection leaves output unchanged. Writes require exact admitted length.
 * Callers perform no potentially blocking operation with this lock held. */
int KrnBufferRelease(struct KrnBufferLease *);
#endif
