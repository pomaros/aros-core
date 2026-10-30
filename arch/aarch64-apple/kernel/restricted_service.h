/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_RESTRICTED_SERVICE_H
#define AROS_RESTRICTED_SERVICE_H
#include <aros/apple/restricted.h>
#include "../../aarch64-all/kernel/mapping.h"
#include "../../../rom/kernel/mapping/user_service.h"
struct Task;
struct AROSRestrictedService {
    struct KrnMappingDomain domain;
    struct KrnMappingDomainMember creator;
    struct KrnMappingArena arena;
    struct KrnMappingObject mapping;
    struct KrnMappingPage page;
    struct KrnServiceQueue queue;
    struct KrnBufferService request;
    struct AArch64ProtectionOps hardware;
    struct KrnMappingOps mapops;
    struct KrnBufferOps bufferops;
    struct Task *worker;
    uint64_t root,tcr,principal,handle;
    uintptr_t backing;
};
/* All entry points run at EL1 under the same UP exception exclusion. No
 * allocation, blocking, guest pointer dereference or callback into tasks.
 * SMP admission remains disabled until a shared lock/quiescence protocol. */
int AROSRestrictedServiceInit(struct AROSRestrictedService *, struct Task *,
    uint64_t root,uint64_t tcr,uintptr_t backing,const struct AArch64ProtectionOps *);
int AROSRestrictedServiceProtect(struct AROSRestrictedService *, unsigned);
int AROSRestrictedServiceSubmit(struct AROSRestrictedService *,uintptr_t,uintptr_t,size_t);
int AROSRestrictedServiceCollect(struct AROSRestrictedService *,uint64_t);
int AROSRestrictedServiceTake(struct Task *,struct AROSRestrictedWork *);
unsigned AROSRestrictedServiceState(struct Task *,const struct AROSRestrictedWork *);
int AROSRestrictedServiceComplete(struct Task *,const struct AROSRestrictedWork *);
/* Cancels requests and returns zero while an acknowledged worker lease is
 * outstanding. Caller keeps service, tables and backing alive and retries
 * from native task context; it must never wait on the exception stack. */
int AROSRestrictedServiceClose(struct AROSRestrictedService *);
#endif
