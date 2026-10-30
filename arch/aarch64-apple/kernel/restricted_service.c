/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "restricted_service.h"
#define PAGE 4096U
static struct KrnBufferRegistry registry;
static struct AROSRestrictedService *active[KBUF_SPACES];
static unsigned char *admit(void *opaque,const struct KrnBufferSpace *space,
    uintptr_t va,uintptr_t pa,size_t size,unsigned access)
{
    struct AROSRestrictedService *s=opaque;uint64_t actual;
    if (space->domain!=&s->domain || size!=PAGE || va!=AROS_RESTRICTED_DATA ||
        pa!=s->backing || !AArch64UserPagePhysical(&s->hardware,s->root,s->tcr,
            va,access,1,&actual) || actual!=pa) return NULL;
    return (unsigned char *)pa;
}
static int install(void *opaque,uintptr_t va,uintptr_t pa,unsigned protection)
{
    struct AROSRestrictedService *s=opaque;uint64_t actual;
    if (va!=AROS_RESTRICTED_DATA || pa!=s->backing) return 0;
    unsigned access=protection|AARCH64_PROTECT_USER;
    if (AArch64MappingPhysical(&s->hardware,s->root,s->tcr,va,&actual))
        return actual==pa && AArch64RemapPage(&s->hardware,s->root,s->tcr,va,pa,access);
    return AArch64MapPage(&s->hardware,s->root,s->tcr,va,pa,access,1);
}
static int remove_page(void *opaque,uintptr_t va,uintptr_t pa)
{
    struct AROSRestrictedService *s=opaque;
    return va==AROS_RESTRICTED_DATA && pa==s->backing &&
        AArch64UnmapPage(&s->hardware,s->root,s->tcr,va,pa);
}
int AROSRestrictedServiceInit(struct AROSRestrictedService *s,struct Task *owner,
    uint64_t root,uint64_t tcr,uintptr_t backing,const struct AArch64ProtectionOps *hardware)
{
    s->root=root;s->tcr=tcr;s->principal=(uintptr_t)owner;s->backing=backing;s->hardware=*hardware;
    s->mapops=(struct KrnMappingOps){s,install,remove_page};
    s->bufferops=(struct KrnBufferOps){s,admit};
    if (!KrnMappingDomainJoin(&s->domain,&s->creator,owner) ||
        !KrnMappingArenaInitialize(&s->arena,AROS_RESTRICTED_DATA,PAGE,PAGE) ||
        !KrnBufferRegister(&registry,s->principal,&s->domain,&s->arena,&s->handle)) return 0;
    active[(uint32_t)s->handle-1]=s;
    void *identity=KrnBufferLookup(&registry,s->principal,s->handle);
    if (!KrnMappingReserve(&s->arena,&s->mapping,PAGE,0,KMAP_READ|KMAP_WRITE,0,
        identity,&s->page,1) || !KrnServiceInitialize(&s->queue,&s->domain)) return 0;
    struct KrnMappingRequest request;
    return KrnMappingFault(&s->mapping,AROS_RESTRICTED_DATA,KMAP_WRITE,1,&request,&s->mapops)==KMAP_FAULT_QUEUE &&
        KrnMappingComplete(&request,backing,0,&s->mapops) &&
        KrnMappingFault(&s->mapping,AROS_RESTRICTED_DATA,KMAP_WRITE,1,&request,&s->mapops)==KMAP_FAULT_RETRY;
}
int AROSRestrictedServiceProtect(struct AROSRestrictedService *s,unsigned access)
{
    if (access!=KMAP_READ && access!=(KMAP_READ|KMAP_WRITE)) return 0;
    return s->handle && !s->domain.closing &&
        KrnMappingProtectPage(&s->mapping,0,access,&s->mapops);
}
int AROSRestrictedServiceSubmit(struct AROSRestrictedService *s,uintptr_t input,uintptr_t output,size_t length)
{
    return KrnBufferServiceSubmit(&registry,s->principal,s->handle,&s->queue,
        input,output,length,&s->bufferops,&s->request);
}
int AROSRestrictedServiceCollect(struct AROSRestrictedService *s,uint64_t token)
{
    return KrnBufferServiceCollect(&s->request,s->principal,token,&s->bufferops);
}
int AROSRestrictedServiceTake(struct Task *worker,struct AROSRestrictedWork *work)
{
    if (!worker || !work) return 0;
    for (unsigned i=0;i<KBUF_SPACES;++i) {
        struct AROSRestrictedService *s=active[i];
        if (!s || s->worker || s->domain.closing || s->request.state!=KS_QUEUED) continue;
        size_t length;
        if (!KrnBufferServiceTake(&s->request,s->request.token,work->bytes,sizeof(work->bytes),&length)) return 0;
        s->worker=worker;work->domain=s->handle;work->token=s->request.token;work->length=length;
        return 1;
    }
    return 0;
}
int AROSRestrictedServiceComplete(struct Task *worker,const struct AROSRestrictedWork *work)
{
    if (!worker || !work || work->length>sizeof(work->bytes)) return 0;
    uint32_t index=(uint32_t)work->domain;
    if (!index || index>KBUF_SPACES) return 0;
    struct AROSRestrictedService *s=active[index-1];
    if (!s || s->handle!=work->domain || s->worker!=worker ||
        !KrnBufferServiceComplete(&s->request,work->token,work->bytes,work->length)) return 0;
    s->worker=NULL;return 1;
}
int AROSRestrictedServiceClose(struct AROSRestrictedService *s)
{
    if (s->handle) {
        if (!KrnBufferClose(&registry,s->principal,s->handle)) return 0;
        if (s->request.queue && !KrnBufferServiceCancel(&s->request,s->principal,s->request.token)) return 0;
        if (s->worker || s->request.queue) return 0;
        if (s->queue.domain) { KrnServiceClose(&s->queue);if (!KrnServiceDrained(&s->queue)) return 0; }
        if (s->arena.first) {
            if (!KrnMappingBeginClose(&s->mapping)) return 0;
            if (s->page.physical && KrnMappingReleasePage(&s->mapping,0,&s->mapops)!=s->backing) return 0;
            if (!KrnMappingRelease(&s->arena,&s->mapping)) return 0;
        }
        if (!KrnBufferUnregister(&registry,s->principal,s->handle)) return 0;
        active[(uint32_t)s->handle-1]=NULL;s->handle=0;
    }
    if (s->creator.domain && !KrnMappingDomainLeave(&s->creator)) return 0;
    return !s->root || KrnMappingDomainCanReclaim(&s->domain);
}
/* The tested portable mechanisms retain their single source of truth. */
#include "../../../rom/kernel/mapping/mapping.c"
#include "../../../rom/kernel/mapping/domain.c"
#include "../../../rom/kernel/mapping/service.c"
#include "../../../rom/kernel/mapping/user_buffer.c"
#include "../../../rom/kernel/mapping/user_service.c"
