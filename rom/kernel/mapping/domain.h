/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#ifndef AROS_KERNEL_MAPPING_DOMAIN_H
#define AROS_KERNEL_MAPPING_DOMAIN_H

#include <stddef.h>

struct KrnMappingDomain;
struct KrnMappingDomainMember {
    struct KrnMappingDomain *domain;
    struct KrnMappingDomainMember *next;
    void *identity;
};
enum KrnMappingDomainPinKind { KDOMAIN_MAPPING = 1, KDOMAIN_IO = 2 };
struct KrnMappingDomainPin {
    struct KrnMappingDomain *domain;
    struct KrnMappingDomainPin *next, *parent;
    size_t requests;
    unsigned kind;
};
struct KrnMappingDomain {
    struct KrnMappingDomainMember *members;
    struct KrnMappingDomainPin *pins;
    unsigned closing;
};

/* Internal lifetime primitive, independent of Task, POSIX bases and handles.
 * All operations share the coordinator's mapping lock. They neither allocate,
 * block, perform I/O nor access member identities. Before first use every
 * domain/member/pin is zero-initialized. Initialize is NOT a reset operation
 * on published storage. A member identifies a retained execution participant;
 * the caller pins its identity until Leave, including across task retirement.
 * Domain/member/pin storage must remain alive while linked or referenced.
 * Raw pointer values do not constitute validation of untrusted native handles.
 *
 * Joining a worker retains the domain before making the worker runnable.
 * Leaving the creating worker does not retire other members' mappings. The
 * last Leave closes admission; Stop closes it explicitly even with members.
 * Closing requests asynchronous cancellation/cleanup by the coordinator. This
 * module never frees backing or assumes that a task has abandoned its stack.
 */
void KrnMappingDomainInitialize(struct KrnMappingDomain *);
int KrnMappingDomainJoin(struct KrnMappingDomain *,
    struct KrnMappingDomainMember *, void *identity);
int KrnMappingDomainLeave(struct KrnMappingDomainMember *);
void KrnMappingDomainStop(struct KrnMappingDomain *);

/* Each mapping owns a distinct pin. Each in-flight I/O owns another distinct
 * pin retaining both the domain and its mapping. Duplicate releases fail.
 * Mapping release fails until every child I/O completes. A caller may create
 * cleanup I/O after Stop (for dirty writeback); fresh fault I/O is refused.
 * The coordinator still owns per-request generations and cancellation, pins
 * file/ops/buffer storage separately and performs the final publication check.
 */
int KrnMappingDomainRetainMapping(struct KrnMappingDomain *,
    struct KrnMappingDomainPin *);
int KrnMappingDomainBeginIo(struct KrnMappingDomainPin *mapping,
    struct KrnMappingDomainPin *request, int cleanup);
int KrnMappingDomainRelease(struct KrnMappingDomainPin *);
/* True only after admission closes, every participant detaches and all owned
 * mapping/I/O pins drain. The caller must also withdraw external references
 * before freeing the domain storage after dropping the mapping lock. */
int KrnMappingDomainCanReclaim(const struct KrnMappingDomain *);

#endif
