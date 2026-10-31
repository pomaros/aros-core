/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "restricted_service.h"
#define PAGE 4096U
_Static_assert(KS_RUNNING==AROS_RESTRICTED_RUNNING && KS_CANCELLED==AROS_RESTRICTED_CANCELLED,"public work states");
static struct KrnBufferRegistry registry;
static struct AROSRestrictedSpace *spaces[KBUF_SPACES];
static struct AROSRestrictedSharedBacking *shared_backings;
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
static int physical_in_use(uintptr_t pa)
{
    for (struct AROSRestrictedSharedBacking *b=shared_backings;b;b=b->next)
        for (size_t i=0;i<b->count;++i) if (b->pages[i]==pa) return 1;
    for (unsigned i=0;i<KBUF_SPACES;++i) {
        struct AROSRestrictedSpace *s=spaces[i];if (!s) continue;
        if (s->backing==pa) return 1;
        for (struct AROSRestrictedRegion *r=s->regions;r;r=r->next)
            for (size_t page=0;page<r->mapping.page_count;++page)
                if (r->backing[page]==pa) return 1;
    }
    return 0;
}
static int shared_registered(struct AROSRestrictedSharedBacking *backing)
{
    for (struct AROSRestrictedSharedBacking *b=shared_backings;b;b=b->next)
        if (b==backing) return 1;
    return 0;
}
int AROSRestrictedSharedInitialize(struct AROSRestrictedSharedBacking *b,uintptr_t *pages,size_t count)
{
    if (!b || !pages || !count || b->pages || b->next || shared_registered(b)) return 0;
    for (size_t i=0;i<count;++i) {
        if (!pages[i] || (pages[i]&(PAGE-1)) || physical_in_use(pages[i])) return 0;
        for (size_t j=0;j<i;++j) if (pages[j]==pages[i]) return 0;
    }
    b->pages=pages;b->count=count;b->views=0;b->closing=0;
    b->next=shared_backings;shared_backings=b;return 1;
}
int AROSRestrictedSharedClose(struct AROSRestrictedSharedBacking *b)
{
    if (!b || !shared_registered(b)) return 0;
    b->closing=1;if (b->views) return 0;
    struct AROSRestrictedSharedBacking **link=&shared_backings;
    while (*link!=b) link=&(*link)->next;
    *link=b->next;b->next=NULL;b->pages=NULL;b->count=0;return 1;
}
int AROSRestrictedRegionAttachShared(struct AROSRestrictedRegion *r,struct AROSRestrictedSharedBacking *b)
{
    if (!r || !r->space || r->space->domain.closing || r->mapping.closing ||
        r->shared || !b || !shared_registered(b) || b->closing ||
        b->views==SIZE_MAX || b->count!=r->mapping.page_count) return 0;
    for (size_t i=0;i<r->mapping.page_count;++i)
        if (r->backing[i] || r->mapping.pages[i].state!=KMAP_RESERVED) return 0;
    r->shared=b;++b->views;return 1;
}
int AROSRestrictedRegionCommitShared(struct AROSRestrictedRegion *r,size_t page,unsigned access)
{
    if (!r || !r->space || r->space->domain.closing || !r->shared ||
        !shared_registered(r->shared) || r->shared->closing ||
        page>=r->mapping.page_count || r->backing[page] || (access&~3U)) return 0;
    uintptr_t pa=r->shared->pages[page];r->backing[page]=pa;
    if (KrnMappingCommitResidentPage(&r->mapping,page,pa,access,&r->space->mapops)) return 1;
    r->backing[page]=0;return 0;
}
static int owns_page(struct AROSRestrictedSpace *s,uintptr_t va,uintptr_t pa)
{
    if (!pa || (va&(PAGE-1))) return 0;
    if (va==AROS_RESTRICTED_DATA) return pa==s->backing;
    for (struct AROSRestrictedRegion *r=s->regions;r;r=r->next)
        if (va>=r->mapping.start && va-r->mapping.start<r->mapping.length)
            return r->space==s && r->backing[(va-r->mapping.start)/PAGE]==pa;
    return 0;
}
static unsigned char *admit(void *opaque,const struct KrnBufferSpace *space,
    uintptr_t va,uintptr_t pa,size_t size,unsigned access)
{
    struct AROSRestrictedSpace *s=opaque;uint64_t actual;
    if (space->domain!=&s->domain || size!=PAGE || !owns_page(s,va,pa) || !AArch64UserPagePhysical(&s->hardware,s->root,s->tcr,
            va,access,1,&actual) || actual!=pa) return NULL;
    return (unsigned char *)pa;
}
static int install(void *opaque,uintptr_t va,uintptr_t pa,unsigned protection)
{
    struct AROSRestrictedSpace *s=opaque;uint64_t actual;
    if (!owns_page(s,va,pa)) return 0;
    unsigned access=protection|AARCH64_PROTECT_USER;
    if (AArch64MappingPhysical(&s->hardware,s->root,s->tcr,va,&actual))
        return actual==pa && AArch64RemapPage(&s->hardware,s->root,s->tcr,va,pa,access);
    return AArch64MapPage(&s->hardware,s->root,s->tcr,va,pa,access,1);
}
static int remove_page(void *opaque,uintptr_t va,uintptr_t pa)
{
    struct AROSRestrictedSpace *s=opaque;
    return owns_page(s,va,pa) &&
        AArch64UnmapPage(&s->hardware,s->root,s->tcr,va,pa);
}
int AROSRestrictedSpaceInit(struct AROSRestrictedSpace *s,uint64_t principal,
    uint64_t root,uint64_t tcr,uintptr_t backing,const struct AArch64ProtectionOps *hardware)
{
    if (!s || !principal || !root || !backing || !hardware || s->root || physical_in_use(backing)) return 0;
    s->root=root;s->tcr=tcr;s->principal=principal;s->backing=backing;s->hardware=*hardware;
    s->mapops=(struct KrnMappingOps){s,install,remove_page};
    s->bufferops=(struct KrnBufferOps){s,admit};
    if (!KrnMappingDomainJoin(&s->domain,&s->creator,s) ||
        !KrnMappingArenaInitialize(&s->arena,AROS_RESTRICTED_DATA,
            AROS_RESTRICTED_DYNAMIC_END-AROS_RESTRICTED_DATA,PAGE) ||
        !KrnBufferRegister(&registry,s->principal,&s->domain,&s->arena,&s->handle)) return 0;
    spaces[(uint32_t)s->handle-1]=s;
    void *identity=KrnBufferLookup(&registry,s->principal,s->handle);
    if (!KrnMappingReserve(&s->arena,&s->mapping,PAGE,0,KMAP_READ|KMAP_WRITE,0,
        identity,&s->page,1)) return 0;
    struct KrnMappingRequest request;
    return KrnMappingFault(&s->mapping,AROS_RESTRICTED_DATA,KMAP_WRITE,1,&request,&s->mapops)==KMAP_FAULT_QUEUE &&
        KrnMappingComplete(&request,backing,0,&s->mapops) &&
        KrnMappingFault(&s->mapping,AROS_RESTRICTED_DATA,KMAP_WRITE,1,&request,&s->mapops)==KMAP_FAULT_RETRY;
}
int AROSRestrictedRegionReserve(struct AROSRestrictedSpace *s,struct AROSRestrictedRegion *r,
    uintptr_t address,size_t length,struct KrnMappingPage *pages,uintptr_t *backing,size_t capacity)
{
    if (!s || !r || !pages || !backing || !s->handle || s->domain.closing ||
        r->space || r->next || address<AROS_RESTRICTED_DYNAMIC_BASE ||
        address>=AROS_RESTRICTED_DYNAMIC_END || !length || (length&(PAGE-1)) ||
        length>AROS_RESTRICTED_DYNAMIC_END-address || length/PAGE>capacity) return 0;
    for (size_t i=0;i<length/PAGE;++i) if (backing[i]) return 0;
    void *identity=KrnBufferLookup(&registry,s->principal,s->handle);
    if (!identity || !KrnMappingReserveVirtual(&s->arena,&r->mapping,address,length,
        identity,pages,capacity)) return 0;
    r->space=s;r->backing=backing;r->next=s->regions;s->regions=r;return 1;
}
int AROSRestrictedRegionCommit(struct AROSRestrictedRegion *r,size_t page,uintptr_t pa,unsigned access)
{
    if (!r || !r->space || r->space->domain.closing || page>=r->mapping.page_count ||
        r->shared || !pa || (pa&(PAGE-1)) || (access&~(KMAP_READ|KMAP_WRITE)) ||
        r->backing[page] || physical_in_use(pa)) return 0;
    r->backing[page]=pa;
    if (KrnMappingCommitResidentPage(&r->mapping,page,pa,access,&r->space->mapops)) return 1;
    r->backing[page]=0;return 0;
}
int AROSRestrictedRegionDecommit(struct AROSRestrictedRegion *r,size_t page,uintptr_t *released)
{
    if (!r || !r->space || r->space->domain.closing || page>=r->mapping.page_count || !released) return 0;
    if (!KrnMappingDecommitPage(&r->mapping,page,&r->space->mapops,released)) return 0;
    r->backing[page]=0;if (r->shared) *released=0;return 1;
}
int AROSRestrictedRegionProtect(struct AROSRestrictedRegion *r,size_t page,unsigned access)
{
    return r && r->space && !r->space->domain.closing && !(access&~(KMAP_READ|KMAP_WRITE)) &&
        KrnMappingProtectPage(&r->mapping,page,access,&r->space->mapops);
}
int AROSRestrictedRegionDetach(struct AROSRestrictedRegion *r)
{
    if (!r || !r->space) return 0;
    struct AROSRestrictedSpace *s=r->space;
    struct AROSRestrictedRegion **link=&s->regions;
    while (*link && *link!=r) link=&(*link)->next;
    if (!*link || !KrnMappingBeginClose(&r->mapping)) return 0;
    for (size_t i=0;i<r->mapping.page_count;++i)
        if (r->mapping.pages[i].physical &&
            KrnMappingReleasePage(&r->mapping,i,&s->mapops)!=r->backing[i]) return 0;
    if (!KrnMappingRelease(&s->arena,&r->mapping)) return 0;
    if (r->shared) {
        for (size_t i=0;i<r->shared->count;++i) r->backing[i]=0;
        --r->shared->views;r->shared=NULL;
    }
    *link=r->next;r->next=NULL;r->space=NULL;return 1;
}
int AROSRestrictedSpaceProtect(struct AROSRestrictedSpace *s,unsigned access)
{
    if (access!=KMAP_READ && access!=(KMAP_READ|KMAP_WRITE)) return 0;
    return s->handle && !s->domain.closing &&
        KrnMappingProtectPage(&s->mapping,0,access,&s->mapops);
}
int AROSRestrictedSpacePageIdentity(struct AROSRestrictedSpace *s,uintptr_t address,
    unsigned access,uintptr_t *physical,uint64_t *generation)
{
    if (!s || !s->handle || s->domain.closing || !physical || !generation || access>2) return 0;
    struct KrnMappingObject *m=KrnMappingLookup(&s->arena,address);
    if (!m || m->closing) return 0;
    size_t index=(address-m->start)/PAGE;struct KrnMappingPage *p=&m->pages[index];
    uintptr_t va=address&~(uintptr_t)(PAGE-1);uint64_t actual;
    if (!p->physical || (p->state!=KMAP_CLEAN && p->state!=KMAP_DIRTY) ||
        (access && !(p->protection&access)) || !owns_page(s,va,p->physical)) return 0;
    int ok=access ? AArch64UserPagePhysical(&s->hardware,s->root,s->tcr,va,access,1,&actual) :
        AArch64MappingPhysical(&s->hardware,s->root,s->tcr,va,&actual);
    if (!ok || actual!=p->physical) return 0;
    *physical=p->physical;*generation=p->generation;return 1;
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
    if (s->regions) return 0;
    if (s->handle) {
        if (s->arena.first) {
            if (!KrnMappingBeginClose(&s->mapping)) return 0;
            if (s->page.physical && KrnMappingReleasePage(&s->mapping,0,&s->mapops)!=s->backing) return 0;
            if (!KrnMappingRelease(&s->arena,&s->mapping)) return 0;
        }
        if (!KrnBufferUnregister(&registry,s->principal,s->handle)) return 0;
        spaces[(uint32_t)s->handle-1]=NULL;
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
