/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "restricted_service.h"
#define PAGE 4096U
_Static_assert(KS_RUNNING==AROS_RESTRICTED_RUNNING && KS_CANCELLED==AROS_RESTRICTED_CANCELLED,"public work states");
static struct KrnBufferRegistry registry;
/* Generation storage outlives every service object and never resets on close. */
static struct {
    struct AROSRestrictedService *service;
    uint32_t generation;
} active[AROS_RESTRICTED_CHANNELS];
static struct AROSRestrictedService *channel_lookup(uint64_t handle)
{
    uint32_t index=(uint32_t)handle;
    if (!index || index>AROS_RESTRICTED_CHANNELS) return NULL;
    return active[index-1].generation==(uint32_t)(handle>>32) ?
        active[index-1].service : NULL;
}
static unsigned char *admit(void *opaque,const struct KrnBufferSpace *space,
    uintptr_t va,uintptr_t pa,size_t size,unsigned access)
{
    struct AROSRestrictedSpace *s=opaque;uint64_t actual;
    if (space->domain!=&s->domain || size!=PAGE || va!=AROS_RESTRICTED_DATA ||
        pa!=s->backing || !AArch64UserPagePhysical(&s->hardware,s->root,s->tcr,
            va,access,1,&actual) || actual!=pa) return NULL;
    return (unsigned char *)pa;
}
static int install(void *opaque,uintptr_t va,uintptr_t pa,unsigned protection)
{
    struct AROSRestrictedSpace *s=opaque;uint64_t actual;
    if (va!=AROS_RESTRICTED_DATA || pa!=s->backing) return 0;
    unsigned access=protection|AARCH64_PROTECT_USER;
    if (AArch64MappingPhysical(&s->hardware,s->root,s->tcr,va,&actual))
        return actual==pa && AArch64RemapPage(&s->hardware,s->root,s->tcr,va,pa,access);
    return AArch64MapPage(&s->hardware,s->root,s->tcr,va,pa,access,1);
}
static int remove_page(void *opaque,uintptr_t va,uintptr_t pa)
{
    struct AROSRestrictedSpace *s=opaque;
    return va==AROS_RESTRICTED_DATA && pa==s->backing &&
        AArch64UnmapPage(&s->hardware,s->root,s->tcr,va,pa);
}
int AROSRestrictedSpaceInit(struct AROSRestrictedSpace *s,uint64_t principal,
    uint64_t root,uint64_t tcr,uintptr_t backing,const struct AArch64ProtectionOps *hardware)
{
    if (!s || !principal || !root || !backing || !hardware || s->root) return 0;
    s->root=root;s->tcr=tcr;s->principal=principal;s->backing=backing;s->hardware=*hardware;
    s->mapops=(struct KrnMappingOps){s,install,remove_page};
    s->bufferops=(struct KrnBufferOps){s,admit};
    if (!KrnMappingDomainJoin(&s->domain,&s->creator,s) ||
        !KrnMappingArenaInitialize(&s->arena,AROS_RESTRICTED_DATA,PAGE,PAGE) ||
        !KrnBufferRegister(&registry,s->principal,&s->domain,&s->arena,&s->handle)) return 0;
    void *identity=KrnBufferLookup(&registry,s->principal,s->handle);
    if (!KrnMappingReserve(&s->arena,&s->mapping,PAGE,0,KMAP_READ|KMAP_WRITE,0,
        identity,&s->page,1)) return 0;
    struct KrnMappingRequest request;
    return KrnMappingFault(&s->mapping,AROS_RESTRICTED_DATA,KMAP_WRITE,1,&request,&s->mapops)==KMAP_FAULT_QUEUE &&
        KrnMappingComplete(&request,backing,0,&s->mapops) &&
        KrnMappingFault(&s->mapping,AROS_RESTRICTED_DATA,KMAP_WRITE,1,&request,&s->mapops)==KMAP_FAULT_RETRY;
}
int AROSRestrictedSpaceProtect(struct AROSRestrictedSpace *s,unsigned access)
{
    if (access!=KMAP_READ && access!=(KMAP_READ|KMAP_WRITE)) return 0;
    return s->handle && !s->domain.closing &&
        KrnMappingProtectPage(&s->mapping,0,access,&s->mapops);
}
int AROSRestrictedServiceInit(struct AROSRestrictedService *s,
    struct AROSRestrictedSpace *space,uint64_t requester)
{
    if (!s || !space || !requester || s->space || s->channel || s->queue.domain ||
        s->member.domain || s->closing || !space->handle || space->domain.closing)
        return 0;
    for (unsigned i=0;i<AROS_RESTRICTED_CHANNELS;++i) {
        if (active[i].service || active[i].generation==UINT32_MAX) continue;
        if (!KrnMappingDomainJoin(&space->domain,&s->member,s)) return 0;
        if (!KrnServiceInitialize(&s->queue,&space->domain)) {
            KrnMappingDomainLeave(&s->member);return 0;
        }
        s->space=space;s->principal=requester;++space->channels;
        ++active[i].generation;
        s->channel=((uint64_t)active[i].generation<<32)|(i+1);
        active[i].service=s;return 1;
    }
    return 0;
}
int AROSRestrictedServiceSubmit(struct AROSRestrictedService *s,uintptr_t input,uintptr_t output,size_t length)
{
    if (!s || !s->space || s->closing) return 0;
    struct AROSRestrictedSpace *space=s->space;
    return KrnBufferServiceSubmit(&registry,space->principal,space->handle,s->principal,
        &s->queue,input,output,length,&space->bufferops,&s->request);
}
int AROSRestrictedServiceCollect(struct AROSRestrictedService *s,uint64_t token)
{
    return s && s->space && !s->closing &&
        KrnBufferServiceCollect(&s->request,s->principal,token,&s->space->bufferops);
}
int AROSRestrictedServiceTake(struct Task *worker,void *context,struct AROSRestrictedWork *work)
{
    if (!worker || !context || !work) return 0;
    for (unsigned i=0;i<AROS_RESTRICTED_CHANNELS;++i) {
        struct AROSRestrictedService *s=active[i].service;
        if (!s || s->closing || s->worker || s->space->domain.closing || s->request.state!=KS_QUEUED) continue;
        size_t length;
        if (!KrnBufferServiceTake(&s->request,s->request.token,work->bytes,sizeof(work->bytes),&length)) return 0;
        s->worker=worker;s->worker_context=context;work->channel=s->channel;work->token=s->request.token;work->length=length;
        return 1;
    }
    return 0;
}
unsigned AROSRestrictedServiceState(struct Task *worker,const struct AROSRestrictedWork *work)
{
    if (!worker || !work) return 0;
    struct AROSRestrictedService *s=channel_lookup(work->channel);
    if (!s || s->worker!=worker ||
        s->request.token!=work->token) return 0;
    return s->request.state;
}
int AROSRestrictedServiceComplete(struct Task *worker,const struct AROSRestrictedWork *work)
{
    if (!worker || !work || work->length>sizeof(work->bytes)) return 0;
    struct AROSRestrictedService *s=channel_lookup(work->channel);
    if (!s || s->worker!=worker ||
        !KrnBufferServiceComplete(&s->request,work->token,work->bytes,work->length)) return 0;
    s->worker=NULL;s->worker_context=NULL;return 1;
}
int AROSRestrictedServiceRetireWorker(void *context)
{
    /* Exec has made this task terminal before deleting its context. Cancel
     * and acknowledge its retained staging lease without reading its stack or
     * publishing a reply. The native caller frees context storage afterward. */
    if (!context) return 1;
    for (unsigned i=0;i<AROS_RESTRICTED_CHANNELS;++i) {
        struct AROSRestrictedService *s=active[i].service;
        if (!s || s->worker_context!=context) continue;
        uint64_t token=s->request.token;
        if (!KrnBufferServiceCancel(&s->request,s->principal,token) ||
            !KrnBufferServiceComplete(&s->request,token,NULL,0)) return 0;
        s->worker=NULL;s->worker_context=NULL;++s->worker_retirements;
    }
    return 1;
}
int AROSRestrictedServiceClose(struct AROSRestrictedService *s)
{
    if (!s) return 0;
    if (!s->space) return !s->channel && !s->member.domain;
    s->closing=1;
    if (s->request.queue && !KrnBufferServiceCancel(&s->request,s->principal,s->request.token)) return 0;
    if (s->worker || s->request.queue) return 0;
    /* KrnServiceClose stops the entire domain; a channel owns only its request. */
    if (!KrnServiceDrained(&s->queue) || channel_lookup(s->channel)!=s ||
        !s->space->channels || !KrnMappingDomainLeave(&s->member)) return 0;
    --s->space->channels;
    active[(uint32_t)s->channel-1].service=NULL;
    s->queue.domain=NULL;s->channel=0;s->space=NULL;return 1;
}
void AROSRestrictedSpaceStop(struct AROSRestrictedSpace *s)
{
    if (s->handle) (void)KrnBufferClose(&registry,s->principal,s->handle);
    else if (s->root) KrnMappingDomainStop(&s->domain);
}
int AROSRestrictedSpaceClose(struct AROSRestrictedSpace *s)
{
    AROSRestrictedSpaceStop(s);
    if (s->channels) return 0;
    if (s->handle) {
        if (s->arena.first) {
            if (!KrnMappingBeginClose(&s->mapping)) return 0;
            if (s->page.physical && KrnMappingReleasePage(&s->mapping,0,&s->mapops)!=s->backing) return 0;
            if (!KrnMappingRelease(&s->arena,&s->mapping)) return 0;
        }
        if (!KrnBufferUnregister(&registry,s->principal,s->handle)) return 0;
        s->handle=0;
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
