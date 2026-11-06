/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "restricted_service.h"
#include <string.h>
#define PAGE 4096U
_Static_assert(KS_RUNNING==AROS_RESTRICTED_RUNNING && KS_CANCELLED==AROS_RESTRICTED_CANCELLED,"public work states");
static struct KrnBufferRegistry registry;
static struct AROSRestrictedSpace *spaces;
static struct AROSRestrictedSharedBacking *shared_backings;
static uint64_t reservation_generation;
/* Generation storage outlives every service object and never resets on close. */
static struct AROSRestrictedService *active;
static uint64_t channel_generation;
static struct AROSRestrictedService *channel_lookup(uint64_t handle)
{
    if (!handle) return NULL;
    for (struct AROSRestrictedService *s=active;s;s=s->next)
        if (s->channel==handle) return s;
    return NULL;
}
/* Index of every private backing page of every region, for constant-time
 * alias refusal: open addressing with linear probing, deletion by backward
 * shift, so a lookup ends at the first empty slot. Shared views never enter it. */
#define INDEX_MASK (AROS_RESTRICTED_INDEX_SLOTS-1)
_Static_assert((AROS_RESTRICTED_INDEX_SLOTS&INDEX_MASK)==0,"index slots are a power of two");
static uintptr_t backing_index[AROS_RESTRICTED_INDEX_SLOTS];
static size_t index_home(uintptr_t pa)
{ return (size_t)(((uint64_t)(pa>>12)*UINT64_C(0x9e3779b97f4a7c15))>>32)&INDEX_MASK; }
static int index_contains(uintptr_t pa)
{
    for (size_t i=index_home(pa);backing_index[i];i=(i+1)&INDEX_MASK)
        if (backing_index[i]==pa) return 1;
    return 0;
}
static void index_insert(uintptr_t pa)
{
    size_t i=index_home(pa);
    while (backing_index[i]) i=(i+1)&INDEX_MASK;
    backing_index[i]=pa;
}
static void index_remove(uintptr_t pa)
{
    size_t i=index_home(pa);
    while (backing_index[i]!=pa) { if (!backing_index[i]) return;i=(i+1)&INDEX_MASK; }
    for (size_t j=(i+1)&INDEX_MASK;backing_index[j];j=(j+1)&INDEX_MASK) {
        size_t home=index_home(backing_index[j]);
        /* Move j into the hole unless its home lies cyclically in (i,j]. */
        if (((j-home)&INDEX_MASK)>=((j-i)&INDEX_MASK)) { backing_index[i]=backing_index[j];i=j; }
    }
    backing_index[i]=0;
}
/* Backing entry of a page, NULL while its chunk is missing. */
static uintptr_t *backing_at(const struct AROSRestrictedRegion *r,size_t page)
{
    if (page>=r->mapping.page_count) return NULL;
    uintptr_t *chunk=r->backing[page/r->mapping.chunk_pages];
    return chunk ? &chunk[page%r->mapping.chunk_pages] : NULL;
}
static uintptr_t backing_of(const struct AROSRestrictedRegion *r,size_t page)
{
    uintptr_t *entry=backing_at(r,page);
    return entry ? *entry : 0;
}
static int physical_in_use(uintptr_t pa)
{
    for (struct AROSRestrictedSharedBacking *b=shared_backings;b;b=b->next)
        for (size_t i=0;i<b->count;++i) if (b->pages[i]==pa) return 1;
    for (struct AROSRestrictedSpace *s=spaces;s;s=s->next)
        if (s->backing==pa) return 1;
    return index_contains(pa);
}
static int shared_registered(struct AROSRestrictedSharedBacking *backing)
{
    for (struct AROSRestrictedSharedBacking *b=shared_backings;b;b=b->next)
        if (b==backing) return 1;
    return 0;
}
/* All aliases share the coordinator. Refuse conflicting permissions before
 * changing any descriptor; a failed transaction blocks mutation of its object. */
static int shared_publication_policy(struct AROSRestrictedRegion *r,size_t page,
    unsigned access,unsigned *other_execute)
{
    *other_execute=0;
    if (!r->shared) return 1;
    struct AROSRestrictedSharedBacking *b=r->shared;
    size_t op=r->shared_first+page;
    if (!shared_registered(b) || (b->closing && (access&KMAP_EXEC)) || op>=b->count ||
        (b->publication && b->publication!=r)) return 0;
    /* A native view is a writable peer of every page */
    if ((access&KMAP_EXEC) && (b->native_view || b->native_pending)) return 0;
    for (struct AROSRestrictedSpace *s=spaces;s;s=s->next)
        for (struct AROSRestrictedRegion *v=s->regions;v;v=v->next) {
            if (v==r || v->shared!=b || op<v->shared_first || op-v->shared_first>=v->mapping.page_count) continue;
            size_t vp=op-v->shared_first;
            uintptr_t backing=backing_of(v,vp);
            if (!backing) continue;
            struct KrnMappingPage *p=KrnMappingPageAt(&v->mapping,vp);
            if (!p || p->physical!=b->pages[op] || p->physical!=backing) return 0;
            if ((access&KMAP_EXEC) && (p->protection&KMAP_WRITE)) return 0;
            if ((access&KMAP_WRITE) && (p->protection&KMAP_EXEC)) return 0;
            if (p->protection&KMAP_EXEC) *other_execute=1;
        }
    return 1;
}
int AROSRestrictedSharedInitialize(struct AROSRestrictedSharedBacking *b,uintptr_t *pages,size_t count)
{
    if (!b || !pages || !count || b->pages || b->next || shared_registered(b)) return 0;
    for (size_t i=0;i<count;++i) {
        if (!pages[i] || (pages[i]&(PAGE-1)) || physical_in_use(pages[i])) return 0;
        for (size_t j=0;j<i;++j) if (pages[j]==pages[i]) return 0;
    }
    b->pages=pages;b->count=count;b->views=0;b->closing=0;b->publication=NULL;
    b->native_view=0;b->native_pending=0;
    b->next=shared_backings;shared_backings=b;return 1;
}
/* Coordinator-serialized access through owned backing, never a guest VA.
 * Validate every touched page before changing either destination. */
static int shared_copy(struct AROSRestrictedSharedBacking *b,size_t offset,
    void *buffer,size_t length,int write)
{
    if (!b || !shared_registered(b) || b->publication || b->closing ||
        b->count>SIZE_MAX/PAGE || offset>b->count*PAGE ||
        length>b->count*PAGE-offset || length>AROS_RESTRICTED_SHARED_COPY_MAX ||
        (length && (!buffer || (uintptr_t)buffer>UINTPTR_MAX-length))) return 0;
    if (!length) return 1;
    uintptr_t begin=(uintptr_t)buffer,end=begin+length;
    /* Native scratch storage must not alias any registered shared backing,
     * through its pages or a native view. */
    for (struct AROSRestrictedSharedBacking *other=shared_backings;other;other=other->next) {
        for (size_t i=0;i<other->count;++i)
            if (begin<other->pages[i]+PAGE && other->pages[i]<end) return 0;
        if (other->native_view && begin<other->native_view+other->count*PAGE &&
            other->native_view<end) return 0;
    }
    size_t first=offset/PAGE,last=(offset+length-1)/PAGE;
    for (size_t page=first;page<=last;++page) {
        if (!b->pages[page] || (b->pages[page]&(PAGE-1))) return 0;
        for (struct AROSRestrictedSpace *space=spaces;space;space=space->next)
            for (struct AROSRestrictedRegion *r=space->regions;r;r=r->next) {
                if (r->shared!=b || page<r->shared_first || page-r->shared_first>=r->mapping.page_count) continue;
                uintptr_t backing=backing_of(r,page-r->shared_first);
                if (!backing) continue;
                struct KrnMappingPage *p=KrnMappingPageAt(&r->mapping,page-r->shared_first);
                if (!p || p->physical!=b->pages[page] || backing!=b->pages[page] ||
                    (write && (p->protection&KMAP_EXEC))) return 0;
            }
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (length==8 && !(offset&7)) {
        uint64_t value;unsigned char *bytes=(unsigned char *)&value;
        uint64_t *address=(void *)(b->pages[first]+offset%PAGE);
        if (write) {
            for (unsigned i=0;i<8;++i) bytes[i]=((unsigned char *)buffer)[i];
            __atomic_store_n(address,value,__ATOMIC_SEQ_CST);
        } else {
            value=__atomic_load_n(address,__ATOMIC_SEQ_CST);
            for (unsigned i=0;i<8;++i) ((unsigned char *)buffer)[i]=bytes[i];
        }
    } else {
        for (size_t i=0;i<length;++i) {
            size_t at=offset+i;unsigned char *address=(void *)(b->pages[at/PAGE]+at%PAGE);
            if (write) *address=((unsigned char *)buffer)[i];
            else ((unsigned char *)buffer)[i]=*address;
        }
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return 1;
}
int AROSRestrictedSharedRead(struct AROSRestrictedSharedBacking *b,size_t offset,void *buffer,size_t length)
{return shared_copy(b,offset,buffer,length,0);}
int AROSRestrictedSharedWrite(struct AROSRestrictedSharedBacking *b,size_t offset,const void *buffer,size_t length)
{return shared_copy(b,offset,(void *)buffer,length,1);}
int AROSRestrictedSharedClose(struct AROSRestrictedSharedBacking *b)
{
    if (!b || !shared_registered(b)) return 0;
    b->closing=1;if (b->views) return 0;
    struct AROSRestrictedSharedBacking **link=&shared_backings;
    while (*link!=b) link=&(*link)->next;
    *link=b->next;b->next=NULL;b->pages=NULL;b->count=0;return 1;
}
int AROSRestrictedSharedViewBegin(struct AROSRestrictedSharedBacking *b)
{
    if (!b || b->closing || !shared_registered(b) || b->publication ||
        b->native_view || b->native_pending || b->views==SIZE_MAX) return 0;
    /* No page may be executable through any domain view */
    for (struct AROSRestrictedSpace *space=spaces;space;space=space->next)
        for (struct AROSRestrictedRegion *r=space->regions;r;r=r->next) {
            if (r->shared!=b) continue;
            for (size_t page=0;page<r->mapping.page_count;++page)
                if (backing_of(r,page) && (KrnMappingPageAt(&r->mapping,page)->protection&KMAP_EXEC)) return 0;
        }
    b->native_pending=1;++b->views;return 1;
}
int AROSRestrictedSharedViewCommit(struct AROSRestrictedSharedBacking *b,uintptr_t address)
{
    if (!b || !shared_registered(b) || !b->native_pending || b->native_view ||
        !address || (address&(PAGE-1))) return 0;
    b->native_pending=0;b->native_view=address;return 1;
}
int AROSRestrictedSharedViewAbort(struct AROSRestrictedSharedBacking *b)
{
    if (!b || !shared_registered(b) || !b->native_pending || b->native_view || !b->views) return 0;
    b->native_pending=0;--b->views;return 1;
}
int AROSRestrictedSharedViewEnd(struct AROSRestrictedSharedBacking *b,uintptr_t *address)
{
    if (!b || !address || !shared_registered(b) || !b->native_view || b->native_pending) return 0;
    *address=b->native_view;b->native_view=0;b->native_pending=1;return 1;
}
int AROSRestrictedRegionAttachShared(struct AROSRestrictedRegion *r,struct AROSRestrictedSharedBacking *b)
{
    if (!r || !b || b->count!=r->mapping.page_count) return 0;
    return AROSRestrictedRegionAttachSharedAt(r,b,0);
}
int AROSRestrictedRegionAttachSharedAt(struct AROSRestrictedRegion *r,struct AROSRestrictedSharedBacking *b,size_t first)
{
    if (!r || !r->space || r->space->domain.closing || r->mapping.closing ||
        r->shared || !b || !shared_registered(b) || b->closing || b->publication ||
        b->views==SIZE_MAX || first>b->count || r->mapping.page_count>b->count-first) return 0;
    for (size_t i=0;i<r->mapping.page_count;++i) {
        const struct KrnMappingPage *p=KrnMappingPageAt(&r->mapping,i);
        if (backing_of(r,i) || (p && p->state!=KMAP_RESERVED)) return 0;
    }
    r->shared=b;r->shared_first=first;++b->views;return 1;
}
int AROSRestrictedRegionCommitShared(struct AROSRestrictedRegion *r,size_t page,unsigned access)
{
    if (!r || !r->space || r->space->domain.closing || !r->shared ||
        !shared_registered(r->shared) || r->shared->closing ||
        page>=r->mapping.page_count || (access&~3U)) return 0;
    uintptr_t *backing=backing_at(r,page);
    if (!backing || *backing) return 0;
    unsigned other_execute;
    if (r->shared->publication || !shared_publication_policy(r,page,access,&other_execute)) return 0;
    uintptr_t pa=r->shared->pages[r->shared_first+page];*backing=pa;
    if (KrnMappingCommitResidentPage(&r->mapping,page,pa,access,&r->space->mapops)) return 1;
    *backing=0;return 0;
}
/* The space publishes only its bootstrap mapping and embedded region mappings.
 * Recover the region from that trusted mapping instead of rescanning the
 * region list on every stack check and buffer page admission. */
static struct AROSRestrictedRegion *mapping_region(struct AROSRestrictedSpace *s,
    struct KrnMappingObject *m)
{
    if (!m || m==&s->mapping) return NULL;
    struct AROSRestrictedRegion *r=(void *)((unsigned char *)m-
        offsetof(struct AROSRestrictedRegion,mapping));
    return r->space==s ? r : NULL;
}
static int owns_page(struct AROSRestrictedSpace *s,uintptr_t va,uintptr_t pa)
{
    if (!pa || (va&(PAGE-1))) return 0;
    if (va==AROS_RESTRICTED_DATA) return pa==s->backing;
    struct AROSRestrictedRegion *r=mapping_region(s,KrnMappingLookup(&s->arena,va));
    return r && backing_of(r,(va-r->mapping.start)/PAGE)==pa;
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
    /* The engine's JIT class becomes the hardware JIT class; every other
     * protection keeps its W^X meaning at EL0. */
    unsigned access=protection==KMAP_JIT_CLASS ?
        AARCH64_PROTECT_JIT|AARCH64_PROTECT_READ|AARCH64_PROTECT_WRITE|
            AARCH64_PROTECT_EXEC|AARCH64_PROTECT_USER :
        protection|AARCH64_PROTECT_USER;
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
        !KrnMappingArenaInitialize(&s->arena,AROS_RESTRICTED_LOW_BASE,
            AROS_RESTRICTED_WIDE_END-AROS_RESTRICTED_LOW_BASE,PAGE) ||
        !KrnBufferRegister(&registry,&s->registration,s->principal,&s->domain,&s->arena,&s->handle)) return 0;
    s->next=spaces;spaces=s;
    void *identity=KrnBufferLookup(&registry,s->principal,s->handle);
    if (!KrnMappingReserveAt(&s->arena,&s->mapping,AROS_RESTRICTED_DATA,PAGE,0,KMAP_READ|KMAP_WRITE,0,
        identity,&s->page,1)) return 0;
    struct KrnMappingRequest request;
    return KrnMappingFault(&s->mapping,AROS_RESTRICTED_DATA,KMAP_WRITE,1,&request,&s->mapops)==KMAP_FAULT_QUEUE &&
        KrnMappingComplete(&request,backing,0,&s->mapops) &&
        KrnMappingFault(&s->mapping,AROS_RESTRICTED_DATA,KMAP_WRITE,1,&request,&s->mapops)==KMAP_FAULT_RETRY;
}
int AROSRestrictedRegionReserve(struct AROSRestrictedSpace *s,struct AROSRestrictedRegion *r,
    uintptr_t address,size_t length,struct KrnMappingPage **chunks,uintptr_t **backing,
    size_t chunk_pages,size_t capacity)
{
    if (!s || !r || !chunks || !backing || !chunk_pages || !s->handle ||
        s->domain.closing || r->space || r->next || reservation_generation==UINT64_MAX ||
        !AROSRestrictedRangeValid(address,length) || length/PAGE>capacity) return 0;
    for (size_t i=0;i<(length/PAGE+chunk_pages-1)/chunk_pages;++i) if (backing[i]) return 0;
    void *identity=KrnBufferLookup(&registry,s->principal,s->handle);
    if (!identity || !KrnMappingReserveVirtualChunked(&s->arena,&r->mapping,address,length,
        identity,chunks,chunk_pages)) return 0;
    r->identity=++reservation_generation;
    r->space=s;r->backing=backing;r->next=s->regions;s->regions=r;return 1;
}
int AROSRestrictedRegionChunkMissing(const struct AROSRestrictedRegion *r,size_t page)
{
    return r && r->space && KrnMappingChunkMissing(&r->mapping,page);
}
int AROSRestrictedRegionAttachChunk(struct AROSRestrictedRegion *r,size_t page,
    struct KrnMappingPage *pages,uintptr_t *backing)
{
    if (!pages || !backing || !AROSRestrictedRegionChunkMissing(r,page) ||
        r->space->domain.closing || r->backing[page/r->mapping.chunk_pages]) return 0;
    for (size_t i=0;i<r->mapping.chunk_pages;++i) if (backing[i]) return 0;
    if (!KrnMappingAttachChunk(&r->mapping,page,pages)) return 0;
    r->backing[page/r->mapping.chunk_pages]=backing;return 1;
}
int AROSRestrictedRegionCommit(struct AROSRestrictedRegion *r,size_t page,uintptr_t pa,unsigned access)
{
    if (!r || !r->space || r->space->domain.closing || page>=r->mapping.page_count ||
        r->shared || r->publication_pending || !pa || (pa&(PAGE-1)) ||
        ((access&~(KMAP_READ|KMAP_WRITE)) && access!=KMAP_JIT_CLASS)) return 0;
    uintptr_t *backing=backing_at(r,page);
    if (!backing || *backing || physical_in_use(pa)) return 0;
    /* A JIT page is installed as data first: execute-permission cache
     * maintenance goes through the owned native alias, which exists only
     * once the page is resident. The fresh page has no other user. */
    unsigned initial=access==KMAP_JIT_CLASS ? KMAP_READ|KMAP_WRITE : access;
    *backing=pa;
    if (KrnMappingCommitResidentPage(&r->mapping,page,pa,initial,&r->space->mapops)) {
        uintptr_t released=0;
        if (access!=KMAP_JIT_CLASS ||
            KrnMappingProtectPage(&r->mapping,page,access,&r->space->mapops)) {
            index_insert(pa);return 1;
        }
        if (!KrnMappingDecommitPage(&r->mapping,page,&r->space->mapops,&released) || released!=pa)
            for (;;) __asm__ volatile("wfe"); /* a fresh leaf cannot refuse */
    }
    *backing=0;return 0;
}
int AROSRestrictedRegionDecommit(struct AROSRestrictedRegion *r,size_t page,uintptr_t *released)
{
    if (!r || !r->space || r->space->domain.closing || page>=r->mapping.page_count || !released || r->publication_pending ||
        (r->shared && r->shared->publication)) return 0;
    if (!KrnMappingDecommitPage(&r->mapping,page,&r->space->mapops,released)) return 0;
    uintptr_t *backing=backing_at(r,page);
    if (backing) *backing=0;
    if (r->shared) *released=0;
    else if (*released) index_remove(*released);
    return 1;
}
int AROSRestrictedRegionProtect(struct AROSRestrictedRegion *r,size_t page,unsigned access)
{
    unsigned other_execute;
    return r && r->space && !r->space->domain.closing && !r->publication_pending &&
        page<r->mapping.page_count && KrnMappingPageAt(&r->mapping,page) &&
        !(KrnMappingPageAt(&r->mapping,page)->protection&KMAP_EXEC) &&
        !(access&~(KMAP_READ|KMAP_WRITE)) &&
        (!r->shared || !r->shared->publication) &&
        shared_publication_policy(r,page,access,&other_execute) &&
        KrnMappingProtectPage(&r->mapping,page,access,&r->space->mapops);
}
int AROSRestrictedRegionPublish(struct AROSRestrictedRegion *r,size_t page,
    unsigned access,const struct AROSRestrictedPublicationOps *ops)
{
    if (!r || !r->space || r->space->domain.closing ||
        r->mapping.closing || r->mapping.in_flight || !r->mapping.virtual_reservation ||
        page>=r->mapping.page_count || !ops || !ops->protect_native ||
        !KrnMappingProtectionValid(access,!r->shared) ||
        (r->publication_pending && r->publication_page!=page)) return 0;
    unsigned other_execute;
    if (!shared_publication_policy(r,page,access,&other_execute)) return 0;
    struct KrnMappingPage *p=KrnMappingPageAt(&r->mapping,page);
    if (!p || !p->physical || p->physical!=backing_of(r,page) ||
        (r->shared && p->physical!=r->shared->pages[r->shared_first+page]) ||
        (p->state!=KMAP_CLEAN && p->state!=KMAP_DIRTY) ||
        (!r->publication_pending && p->generation==UINT64_MAX)) return 0;
    /* No CPU may observe this intermediate state: native caller owns the
     * rendezvous. Keep metadata non-accessible if any later step refuses. */
    if (!KrnMappingProtectPage(&r->mapping,page,0,&r->space->mapops)) return 0;
    if (!r->publication_pending) ++p->generation;
    r->publication_pending=1;r->publication_page=page;
    if (r->shared) r->shared->publication=r;
    /* A JIT page is private: its native alias stays writable like data. */
    unsigned native=access==KMAP_JIT_CLASS ? KMAP_READ|KMAP_WRITE :
        ((access&KMAP_EXEC) || other_execute) ? KMAP_READ : KMAP_READ|KMAP_WRITE;
    if (!ops->protect_native(ops->context,p->physical,native) ||
        !KrnMappingProtectPage(&r->mapping,page,access,&r->space->mapops)) return 0;
    if (r->shared) r->shared->publication=NULL;
    r->publication_pending=0;r->publication_page=0;return 1;
}
int AROSRestrictedRegionDetach(struct AROSRestrictedRegion *r)
{
    if (!r || !r->space) return 0;
    struct AROSRestrictedSpace *s=r->space;
    struct AROSRestrictedRegion **link=&s->regions;
    while (*link && *link!=r) link=&(*link)->next;
    if (!*link || !KrnMappingBeginClose(&r->mapping)) return 0;
    for (size_t i=0;i<r->mapping.page_count;++i) {
        const struct KrnMappingPage *p=KrnMappingPageAt(&r->mapping,i);
        if (!p || !p->physical) continue;
        uintptr_t released=KrnMappingReleasePage(&r->mapping,i,&s->mapops);
        if (released!=backing_of(r,i)) return 0;
        if (!r->shared) index_remove(released);
    }
    if (!KrnMappingRelease(&s->arena,&r->mapping)) return 0;
    if (r->shared) {
        if (r->shared->publication==r) r->shared->publication=NULL;
        for (size_t i=0;i<r->mapping.page_count;++i) {
            uintptr_t *backing=backing_at(r,i);
            if (backing) *backing=0;
        }
        --r->shared->views;r->shared=NULL;r->shared_first=0;
    }
    *link=r->next;r->next=NULL;r->space=NULL;
    r->publication_pending=0;r->publication_page=0;return 1;
}
int AROSRestrictedSpaceProtect(struct AROSRestrictedSpace *s,unsigned access)
{
    if (access!=KMAP_READ && access!=(KMAP_READ|KMAP_WRITE)) return 0;
    return s->handle && !s->domain.closing &&
        KrnMappingProtectPage(&s->mapping,0,access,&s->mapops);
}
int AROSRestrictedSpacePageIdentity(struct AROSRestrictedSpace *s,uintptr_t address,
    unsigned access,uintptr_t *physical,uint64_t *generation,uint64_t *reservation)
{
    if (!s || !s->handle || s->domain.closing || !physical || !generation || !reservation || (access!=0 && access!=KMAP_READ && access!=KMAP_WRITE && access!=KMAP_EXEC)) return 0;
    struct KrnMappingObject *m=KrnMappingLookup(&s->arena,address);
    if (!m || m->closing) return 0;
    size_t index=(address-m->start)/PAGE;struct KrnMappingPage *p=KrnMappingPageAt(m,index);
    uintptr_t va=address&~(uintptr_t)(PAGE-1);uint64_t actual;
    if (!p || !p->physical || (p->state!=KMAP_CLEAN && p->state!=KMAP_DIRTY) ||
        (access && !(p->protection&access)) || !owns_page(s,va,p->physical)) return 0;
    /* Execute identity of a JIT page proves the JIT leaf: the page is owned
     * and current, its contents are the guest's to change. */
    unsigned hardware=access==KMAP_EXEC && p->protection==KMAP_JIT_CLASS ?
        AARCH64_PROTECT_JIT : access;
    int ok=access ? AArch64UserPagePhysical(&s->hardware,s->root,s->tcr,va,hardware,1,&actual) :
        AArch64MappingPhysical(&s->hardware,s->root,s->tcr,va,&actual);
    if (!ok || actual!=p->physical) return 0;
    uint64_t identity=0;
    if (m==&s->mapping) identity=s->handle;
    else {
        struct AROSRestrictedRegion *r=mapping_region(s,m);
        if (r) identity=r->identity;
    }
    if (!identity) return 0;
    *physical=p->physical;*generation=p->generation;*reservation=identity;return 1;
}
int AROSRestrictedSpaceFaultIdentity(struct AROSRestrictedSpace *s,uintptr_t address,
    uintptr_t *physical,uint64_t *generation,uint64_t *reservation)
{
    if (AROSRestrictedSpacePageIdentity(s,address,0,physical,generation,reservation)) return 1;
    if (!s || !s->handle || s->domain.closing || !physical || !generation || !reservation) return 0;
    struct KrnMappingObject *m=KrnMappingLookup(&s->arena,address);
    if (!m || m->closing || !m->virtual_reservation) return 0;
    size_t index=(address-m->start)/PAGE;struct KrnMappingPage *p=KrnMappingPageAt(m,index);
    /* A page without a chunk is reserved at the reservation's generation. */
    if (p && (p->state!=KMAP_RESERVED || p->physical || p->generation==UINT64_MAX)) return 0;
    uint64_t actual;
    if (AArch64MappingPhysical(&s->hardware,s->root,s->tcr,address&~(uintptr_t)(PAGE-1),&actual)) return 0;
    for (struct AROSRestrictedRegion *r=s->regions;r;r=r->next)
        if (&r->mapping==m) {
            if (!r->identity || backing_of(r,index) || r->publication_pending ||
                (r->shared && r->shared->publication)) return 0;
            *physical=0;*generation=p ? p->generation : m->reserved_generation;
            *reservation=r->identity;return 1;
        }
    return 0;
}
int AROSRestrictedServiceInit(struct AROSRestrictedService *s,
    struct AROSRestrictedSpace *space,uint64_t requester)
{
    if (!s || !space || !requester || s->space || s->channel || s->queue.domain ||
        s->next || s->member.domain || s->closing || !space->handle || space->domain.closing ||
        space->channels==(unsigned)-1 || channel_generation==UINT64_MAX) return 0;
    struct AROSRestrictedService **link=&active;
    while (*link) {
        if (*link==s) return 0;
        link=&(*link)->next;
    }
    if (!KrnMappingDomainJoin(&space->domain,&s->member,s)) return 0;
    if (!KrnServiceInitialize(&s->queue,&space->domain)) {
        KrnMappingDomainLeave(&s->member);return 0;
    }
    s->space=space;s->principal=requester;++space->channels;
    s->channel=++channel_generation;*link=s;return 1;
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
static int service_take(struct Task *worker,void *context,struct AROSRestrictedSpace *selected,struct AROSRestrictedWork *work)
{
    if (!worker || !context || !work) return 0;
    for (struct AROSRestrictedService *s=active;s;s=s->next) {
        if ((selected && s->space!=selected) || s->closing || s->worker || s->space->domain.closing || s->request.state!=KS_QUEUED) continue;
        size_t length;
        if (!KrnBufferServiceTake(&s->request,s->request.token,work->bytes,sizeof(work->bytes),&length)) return 0;
        s->worker=worker;s->worker_context=context;work->channel=s->channel;work->token=s->request.token;work->length=length;
        return 1;
    }
    return 0;
}
int AROSRestrictedServiceTake(struct Task *worker,void *context,struct AROSRestrictedWork *work)
{
    return service_take(worker,context,NULL,work);
}
int AROSRestrictedServiceTakeSpace(struct Task *worker,void *context,
    struct AROSRestrictedSpace *space,struct AROSRestrictedWork *work)
{
    return space && service_take(worker,context,space,work);
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
    for (struct AROSRestrictedService *s=active;s;s=s->next) {
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
    struct AROSRestrictedService **link=&active;
    while (*link!=s) link=&(*link)->next;
    *link=s->next;s->next=NULL;
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
        struct AROSRestrictedSpace **link=&spaces;
        while (*link && *link!=s) link=&(*link)->next;
        if (!*link) return 0;
        *link=s->next;s->next=NULL;
        s->handle=0;
    }
    if (s->creator.domain && !KrnMappingDomainLeave(&s->creator)) return 0;
    return !s->root || KrnMappingDomainCanReclaim(&s->domain);
}
/* The tested portable mechanisms retain their single source of truth. */
/* API 36: a copy between native memory and committed, accessible pages of
 * the domain, for its owner. Every page of the range is validated first
 * (owned, committed, mapped with the requested access, same backing), then
 * the bytes move through the kernel's identity view of the backing. A write
 * needs a writable leaf, so published code is refused by the access check. */
int AROSRestrictedSpaceCopy(struct AROSRestrictedSpace *s,uintptr_t address,void *buffer,size_t length,unsigned access)
{
    if (!s || !s->handle || s->domain.closing || !buffer || (access!=KMAP_READ && access!=KMAP_WRITE)) return 0;
    if (!length) return 1;
    if (length>AROS_RESTRICTED_SHARED_COPY_MAX || address+length<address) return 0;
    for (unsigned pass=0;pass<2;++pass) {
        uintptr_t cursor=address;size_t left=length;unsigned char *native=buffer;
        while (left) {
            uintptr_t va=cursor&~(uintptr_t)(PAGE-1);size_t offset=cursor-va,n=PAGE-offset;
            if (n>left) n=left;
            struct KrnMappingObject *m=KrnMappingLookup(&s->arena,va);
            if (!m || m->closing) return 0;
            struct KrnMappingPage *page=KrnMappingPageAt(m,(va-m->start)/PAGE);uint64_t actual;
            if (!page || !page->physical || (page->state!=KMAP_CLEAN && page->state!=KMAP_DIRTY) ||
                !(page->protection&access) || !owns_page(s,va,page->physical) ||
                !AArch64UserPagePhysical(&s->hardware,s->root,s->tcr,va,access,1,&actual) ||
                actual!=page->physical) return 0;
            if (pass) {
                unsigned char *backing=(unsigned char *)page->physical+offset;
                if (access==KMAP_WRITE) memcpy(backing,native,n); else memcpy(native,backing,n);
            }
            cursor+=n;native+=n;left-=n;
        }
    }
    return 1;
}
#include "../../../rom/kernel/mapping/mapping.c"
#include "../../../rom/kernel/mapping/domain.c"
#include "../../../rom/kernel/mapping/service.c"
#include "../../../rom/kernel/mapping/user_buffer.c"
#include "../../../rom/kernel/mapping/user_service.c"
