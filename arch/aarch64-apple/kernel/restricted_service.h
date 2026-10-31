/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_RESTRICTED_SERVICE_H
#define AROS_RESTRICTED_SERVICE_H
#include <aros/apple/restricted.h>
#include "../../aarch64-all/kernel/mapping.h"
#include "../../../rom/kernel/mapping/user_service.h"
struct Task;
struct AROSRestrictedSpace {
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
};
#define AROS_RESTRICTED_CHANNELS 16U
struct AROSRestrictedService {
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
