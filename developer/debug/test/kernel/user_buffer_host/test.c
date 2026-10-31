/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include "user_buffer.h"
#include "user_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PAGE 256U
#define START 0x1000U
#define CHECK(x, name) do { if (!(x)) { puts("FAIL " name); exit(1); } } while (0)
struct Fixture {
    struct KrnBufferRegistry registry;
    struct KrnMappingDomain domain;
    struct KrnMappingArena arena;
    struct KrnMappingObject mapping;
    struct KrnMappingPage pages[2];
    unsigned char data[2][PAGE] __attribute__((aligned(PAGE)));
    uintptr_t mapped[2]; unsigned protection[2];
    struct KrnMappingOps mapops;
    struct KrnBufferOps bufops;
    uint64_t handle;
};
static int install(void *arg, uintptr_t va, uintptr_t pa, unsigned protection)
{
    struct Fixture *f = arg;
    if (va < START || va >= START + 2*PAGE) return 0;
    size_t i = (va - START)/PAGE;
    f->mapped[i] = pa; f->protection[i] = protection; return 1;
}
static int remove_page(void *arg, uintptr_t va, uintptr_t pa)
{
    struct Fixture *f = arg; size_t i = (va - START)/PAGE;
    if (i >= 2 || f->mapped[i] != pa) return 0;
    f->mapped[i] = 0; return 1;
}
static unsigned char *admit(void *arg, const struct KrnBufferSpace *space,
    uintptr_t va, uintptr_t pa, size_t size, unsigned access)
{
    struct Fixture *f = arg;
    if (space->domain != &f->domain || size != PAGE || va < START || va >= START+2*PAGE) return NULL;
    size_t i = (va-START)/PAGE;
    return f->mapped[i] == pa && (f->protection[i]&access) == access ? (void *)pa : NULL;
}
static void init(struct Fixture *f)
{
    memset(f,0,sizeof(*f));
    f->mapops = (struct KrnMappingOps){f,install,remove_page};
    f->bufops = (struct KrnBufferOps){f,admit};
    CHECK(KrnMappingArenaInitialize(&f->arena,START,4*PAGE,PAGE),"arena");
    CHECK(KrnBufferRegister(&f->registry,17,&f->domain,&f->arena,&f->handle),"register");
    struct KrnBufferSpace *owner = KrnBufferLookup(&f->registry,17,f->handle);
    CHECK(owner,"lookup");
    CHECK(KrnMappingReserve(&f->arena,&f->mapping,2*PAGE,0,3,0,owner,f->pages,2),"reserve");
    for (unsigned i=0;i<2;++i) {
        struct KrnMappingRequest request;
        CHECK(KrnMappingFault(&f->mapping,START+i*PAGE,KMAP_WRITE,1,&request,&f->mapops)==KMAP_FAULT_QUEUE,"fault");
        CHECK(KrnMappingComplete(&request,(uintptr_t)f->data[i],0,&f->mapops),"page-in");
        CHECK(KrnMappingFault(&f->mapping,START+i*PAGE,KMAP_WRITE,1,&request,&f->mapops)==KMAP_FAULT_RETRY,"dirty");
        memset(f->data[i],0x31+i,PAGE);
    }
}
static int pin(struct Fixture *f, uintptr_t va, size_t len, unsigned access, struct KrnBufferLease *l)
{
    return KrnBufferPin(&f->registry,17,f->handle,va,len,access,&f->bufops,l);
}
static void clean(struct Fixture *f)
{
    CHECK(KrnBufferClose(&f->registry,17,f->handle),"close");
    for (unsigned i=0;i<2;++i) CHECK(KrnMappingReleasePage(&f->mapping,i,&f->mapops)==(uintptr_t)f->data[i],"release-page");
    CHECK(KrnMappingRelease(&f->arena,&f->mapping),"release-mapping");
    CHECK(KrnBufferUnregister(&f->registry,17,f->handle),"unregister");
    CHECK(KrnMappingDomainCanReclaim(&f->domain),"reclaim");
}
static void copies(void)
{
    struct Fixture f; init(&f); struct KrnBufferLease l={0}; unsigned char out[16];
    CHECK(pin(&f,START+PAGE-8,16,KMAP_READ,&l),"read-pin");
    CHECK(f.mapping.in_flight==2,"all-pages-pinned");
    CHECK(KrnBufferRead(&l,17,out,sizeof(out),&f.bufops),"read");
    CHECK(out[0]==0x31 && out[7]==0x31 && out[8]==0x32 && out[15]==0x32,"cross-page-read");
    memset(out,0xa5,sizeof(out));
    CHECK(!KrnBufferRead(&l,18,out,sizeof(out),&f.bufops) && out[0]==0xa5,"lease-owner");
    struct KrnBufferLease clone=l;
    CHECK(!KrnBufferRelease(&clone) && f.mapping.in_flight==2,"noncopyable-lease");
    CHECK(KrnBufferRelease(&l) && !KrnBufferRelease(&l),"release-once");
    CHECK(pin(&f,START+PAGE-8,16,KMAP_WRITE,&l),"write-pin");
    CHECK(KrnBufferWrite(&l,17,out,16,&f.bufops),"write");
    CHECK(f.data[0][PAGE-9]==0x31 && f.data[0][PAGE-1]==0xa5 && f.data[1][0]==0xa5 && f.data[1][8]==0x32,"cross-page-write");
    CHECK(KrnBufferRelease(&l),"write-release");clean(&f);
}
static void refusal(void)
{
    struct Fixture f;init(&f);struct KrnBufferLease l={0};
    CHECK(!KrnBufferPin(&f.registry,18,f.handle,START,1,KMAP_READ,&f.bufops,&l),"domain-owner");
    CHECK(!KrnBufferPin(&f.registry,17,f.handle+(UINT64_C(1)<<32),START,1,KMAP_READ,&f.bufops,&l),"domain-generation");
    CHECK(!pin(&f,UINTPTR_MAX-3,8,KMAP_READ,&l),"overflow");
    CHECK(!pin(&f,START,0,KMAP_READ,&l),"zero-length");
    CHECK(!pin(&f,START,KBUF_BYTES+1,KMAP_READ,&l),"bound");
    CHECK(!pin(&f,START+2*PAGE-4,8,KMAP_READ,&l) && !l.space && !f.mapping.in_flight,"unmapped-tail-atomic");
    void *owner=f.mapping.owner; f.mapping.owner=&f.domain;
    CHECK(!pin(&f,START,8,KMAP_READ,&l),"foreign-mapping");f.mapping.owner=owner;
    f.pages[1].protection=0;
    CHECK(!pin(&f,START+PAGE-4,8,KMAP_READ,&l) && !f.mapping.in_flight,"guard-tail-atomic");
    f.pages[1].protection=KMAP_READ;
    CHECK(!pin(&f,START+PAGE-4,8,KMAP_WRITE,&l),"readonly-output");
    f.pages[1].protection=3;
    f.protection[1]=KMAP_READ;
    CHECK(!pin(&f,START+PAGE-4,8,KMAP_WRITE,&l),"hardware-permission");f.protection[1]=3;
    f.mapping.shared=1;CHECK(!pin(&f,START,8,KMAP_READ,&l),"shared-refused");f.mapping.shared=0;
    f.mapping.in_flight=SIZE_MAX-1;
    CHECK(!pin(&f,START+PAGE-4,8,KMAP_READ,&l) && f.mapping.in_flight==SIZE_MAX-1,"pin-count-overflow");
    f.mapping.in_flight=0;
    CHECK(f.mapping.in_flight==0 && f.domain.members==&f.registry.spaces[0].retention && !f.domain.members->next,"refusal-no-leaks");
    clean(&f);
}
static void revalidation(void)
{
    struct Fixture f;init(&f);struct KrnBufferLease l={0};unsigned char bytes[16];memset(bytes,0xa5,16);
    CHECK(pin(&f,START+PAGE-8,16,KMAP_WRITE,&l),"revalidate-pin");
    f.protection[1]=KMAP_READ;
    CHECK(!KrnBufferWrite(&l,17,bytes,16,&f.bufops) && f.data[0][PAGE-1]==0x31,"hardware-recheck-atomic");f.protection[1]=3;
    f.pages[1].protection=KMAP_READ;
    CHECK(!KrnBufferWrite(&l,17,bytes,16,&f.bufops) && f.data[0][PAGE-1]==0x31,"permission-recheck-atomic");f.pages[1].protection=3;
    ++f.pages[1].generation;
    CHECK(!KrnBufferWrite(&l,17,bytes,16,&f.bufops) && f.data[0][PAGE-1]==0x31,"page-generation");--f.pages[1].generation;
    CHECK(!KrnMappingBeginClose(&f.mapping),"close-waits-pins");
    CHECK(!KrnMappingReleasePage(&f.mapping,0,&f.mapops),"no-early-backing-release");
    CHECK(!KrnBufferWrite(&l,17,bytes,16,&f.bufops) && f.data[0][PAGE-1]==0x31,"no-late-write");
    CHECK(KrnBufferRelease(&l),"closed-lease-drain");clean(&f);
}
static void domain_lifetime(void)
{
    struct Fixture f;init(&f);struct KrnBufferLease l={0};unsigned char out[8];memset(out,0xa5,8);
    CHECK(pin(&f,START,8,KMAP_READ,&l),"domain-pin");
    CHECK(KrnBufferClose(&f.registry,17,f.handle),"domain-close");
    CHECK(!KrnBufferRead(&l,17,out,sizeof(out),&f.bufops) && out[0]==0xa5,"closed-domain-copy");
    CHECK(!KrnBufferUnregister(&f.registry,17,f.handle),"live-unregister");
    CHECK(!KrnMappingDomainCanReclaim(&f.domain),"live-domain");
    CHECK(KrnBufferRelease(&l),"domain-release");uint64_t old=f.handle;clean(&f);
    struct KrnMappingDomain d={0};
    CHECK(KrnBufferRegister(&f.registry,17,&d,&f.arena,&f.handle),"reuse-space");
    CHECK(f.handle!=old && !KrnBufferLookup(&f.registry,17,old),"stale-domain-handle");
    CHECK(KrnBufferClose(&f.registry,17,f.handle) && KrnBufferUnregister(&f.registry,17,f.handle),"reuse-close");
    for(unsigned i=0;i<KBUF_SPACES;++i)f.registry.spaces[i].generation=UINT32_MAX;
    struct KrnMappingDomain next={0};
    CHECK(!KrnBufferRegister(&f.registry,17,&next,&f.arena,&f.handle),"generation-exhaustion");
}
static void adjacent_owners(void)
{
    struct Fixture f;init(&f);
    CHECK(KrnMappingBeginClose(&f.mapping),"adjacent-retire");
    for(unsigned i=0;i<2;++i)CHECK(KrnMappingReleasePage(&f.mapping,i,&f.mapops),"adjacent-release-old");
    CHECK(KrnMappingRelease(&f.arena,&f.mapping),"adjacent-unlink");
    struct KrnMappingObject second={0};struct KrnMappingObject *maps[2]={&f.mapping,&second};
    void *owner=KrnBufferLookup(&f.registry,17,f.handle);
    for(unsigned i=0;i<2;++i){
        struct KrnMappingRequest request;
        CHECK(KrnMappingReserve(&f.arena,maps[i],PAGE,0,3,0,owner,&f.pages[i],1),"adjacent-reserve");
        CHECK(KrnMappingFault(maps[i],START+i*PAGE,KMAP_WRITE,1,&request,&f.mapops)==KMAP_FAULT_QUEUE,"adjacent-fault");
        CHECK(KrnMappingComplete(&request,(uintptr_t)f.data[i],0,&f.mapops),"adjacent-populate");
        CHECK(KrnMappingFault(maps[i],START+i*PAGE,KMAP_WRITE,1,&request,&f.mapops)==KMAP_FAULT_RETRY,"adjacent-dirty");
    }
    struct KrnBufferLease l={0};second.owner=&f.domain;
    CHECK(!pin(&f,START+PAGE-4,8,KMAP_WRITE,&l) && !f.mapping.in_flight && !second.in_flight,"foreign-second-object");
    second.owner=owner;
    CHECK(pin(&f,START+PAGE-4,8,KMAP_WRITE,&l) && f.mapping.in_flight==1 && second.in_flight==1,"adjacent-pins");
    unsigned char bytes[8]={9,8,7,6,5,4,3,2};
    CHECK(KrnBufferWrite(&l,17,bytes,8,&f.bufops) && f.data[0][PAGE-1]==6 && f.data[1][0]==5,"adjacent-write");
    CHECK(KrnBufferRelease(&l),"adjacent-drain");
    CHECK(KrnBufferClose(&f.registry,17,f.handle),"adjacent-close");
    for(unsigned i=0;i<2;++i){
        CHECK(KrnMappingReleasePage(maps[i],0,&f.mapops),"adjacent-release");
        CHECK(KrnMappingRelease(&f.arena,maps[i]),"adjacent-remove");
    }
    CHECK(KrnBufferUnregister(&f.registry,17,f.handle) && KrnMappingDomainCanReclaim(&f.domain),"adjacent-reclaim");
}
static void service_cases(void)
{
    /* Normal, cancel queued, cancel running, unmap running, close running,
       unmap completed, close completed: deterministic completion races. */
    for(unsigned mode=0;mode<7;++mode) {
        struct Fixture f;init(&f);
        struct KrnServiceQueue q={0};struct KrnBufferService r={0};
        CHECK(KrnServiceInitialize(&q,&f.domain),"service-init");
        CHECK(KrnBufferServiceSubmit(&f.registry,17,f.handle,17,&q,START,START+PAGE-4,8,&f.bufops,&r),"service-submit");
        uint64_t token=r.token;
        unsigned char stale_bytes[8];size_t stale_n=0;
        CHECK(!KrnBufferServiceTake(&r,token+(UINT64_C(1)<<32),stale_bytes,sizeof(stale_bytes),&stale_n),"stale-worker-take");
        CHECK(f.mapping.in_flight==2,"service-output-pins");
        CHECK(!KrnBufferServiceCancel(&r,18,token) && !KrnBufferServiceCancel(&r,17,token+1),"service-token-owner");
        if(mode==1) {
            CHECK(KrnBufferServiceCancel(&r,17,token) && !f.mapping.in_flight,"cancel-before-take");
        } else {
            unsigned char bytes[8];size_t n=0;
            CHECK(KrnBufferServiceTake(&r,token,bytes,sizeof(bytes),&n) && n==8,"service-take");
            CHECK(!KrnBufferServiceComplete(&r,token+(UINT64_C(1)<<32),bytes,n),"stale-worker-complete");
            for(unsigned i=0;i<8;++i)bytes[i]^=0x5a;
            if(mode==2)CHECK(KrnBufferServiceCancel(&r,17,token) && f.mapping.in_flight==2,"service-cancel-pins");
            if(mode==3)CHECK(!KrnMappingBeginClose(&f.mapping),"service-unmap-wait");
            if(mode==4)CHECK(KrnBufferClose(&f.registry,17,f.handle),"service-domain-close");
            if(mode==3 || mode==4)CHECK(!KrnMappingReleasePage(&f.mapping,0,&f.mapops),"service-retired-backing");
            CHECK(KrnBufferServiceComplete(&r,token,bytes,n),"service-complete");
            if(mode==5)CHECK(!KrnMappingBeginClose(&f.mapping),"ready-unmap");
            if(mode==6)CHECK(KrnBufferClose(&f.registry,17,f.handle),"ready-close");
            CHECK(KrnBufferServiceCollect(&r,17,token,&f.bufops)==(mode==0),"service-publication");
        }
        if(mode==0) CHECK(f.data[0][PAGE-1]==(0x31^0x5a) && f.data[1][0]==(0x31^0x5a),"service-bytes");
        else CHECK(f.data[0][PAGE-1]==0x31 && f.data[1][0]==0x32,"service-no-late-write");
        CHECK(!r.queue && !r.output.space && !f.mapping.in_flight && KrnServiceDrained(&q),"service-drain");
        KrnServiceClose(&q);clean(&f);
    }
    struct Fixture f;init(&f);struct KrnServiceQueue q={0};struct KrnBufferService r={0};
    CHECK(KrnServiceInitialize(&q,&f.domain),"submit-refusal-init");
    CHECK(!KrnBufferServiceSubmit(&f.registry,17,f.handle,17,&q,START,START+2*PAGE-4,8,&f.bufops,&r) && !f.mapping.in_flight,"submit-refusal-drain");
    KrnServiceClose(&q);clean(&f);
}
static void cross_queue_reuse(void)
{
    struct Fixture a,b;init(&a);init(&b);
    struct KrnServiceQueue qa={0},qb={0};struct KrnBufferService r={0};
    CHECK(KrnServiceInitialize(&qa,&a.domain) && KrnServiceInitialize(&qb,&b.domain),"cross-queue-init");
    CHECK(KrnBufferServiceSubmit(&a.registry,17,a.handle,17,&qa,START,START,8,&a.bufops,&r),"cross-queue-first");
    uint64_t stale=r.token,queue_token=r.service_token;
    CHECK(KrnBufferServiceCancel(&r,17,stale),"cross-queue-cancel");
    CHECK(KrnBufferServiceSubmit(&b.registry,17,b.handle,17,&qb,START,START,8,&b.bufops,&r),"cross-queue-second");
    CHECK(r.service_token==queue_token && r.token!=stale,"cross-queue-generation");
    unsigned char bytes[8];size_t n=0;
    CHECK(KrnBufferServiceTake(&r,r.token,bytes,sizeof(bytes),&n),"cross-queue-take");
    CHECK(!KrnBufferServiceComplete(&r,stale,bytes,n),"cross-queue-stale-worker");
    uint64_t current=r.token;
    CHECK(KrnBufferServiceCancel(&r,17,current) && KrnBufferServiceComplete(&r,current,bytes,n),"cross-queue-drain");
    r.token=UINT64_MAX;
    CHECK(!KrnBufferServiceSubmit(&b.registry,17,b.handle,17,&qb,START,START,8,&b.bufops,&r),"record-generation-exhaustion");
    clean(&a);clean(&b);
}
static void shared_domain_requesters(void)
{
    struct Fixture f;init(&f);
    struct KrnMappingDomainMember threads[2]={{0},{0}};
    struct KrnServiceQueue q={0};struct KrnBufferService a={0},b={0};
    CHECK(KrnMappingDomainJoin(&f.domain,&threads[0],&a) &&
        KrnMappingDomainJoin(&f.domain,&threads[1],&b),"threads-join");
    CHECK(KrnServiceInitialize(&q,&f.domain),"threads-queue");
    CHECK(!KrnBufferServiceSubmit(&f.registry,17,f.handle,0,&q,START,START+32,8,&f.bufops,&a),"thread-zero-owner");
    CHECK(KrnBufferServiceSubmit(&f.registry,17,f.handle,101,&q,START,START+32,8,&f.bufops,&a) &&
        KrnBufferServiceSubmit(&f.registry,17,f.handle,102,&q,START,START+64,8,&f.bufops,&b),"threads-submit");
    CHECK(a.principal==101 && b.principal==102 && a.buffer_principal==17 && b.buffer_principal==17,"thread-identities");
    unsigned char bytes[8];size_t n=0;uint64_t at=a.token,bt=b.token;
    CHECK(KrnBufferServiceTake(&a,at,bytes,sizeof(bytes),&n),"thread-a-take");
    CHECK(!KrnBufferServiceCancel(&a,102,at),"thread-foreign-cancel");
    CHECK(KrnBufferServiceCancel(&a,101,at),"thread-a-cancel");
    CHECK(KrnMappingDomainLeave(&threads[0]) && !f.domain.closing,"thread-a-leave");
    memset(bytes,0x77,sizeof(bytes));
    CHECK(KrnBufferServiceComplete(&a,at,bytes,n) && !a.queue,"thread-a-drain");
    CHECK(f.data[0][32]==0x31 && b.queue==&q && !f.domain.closing,"thread-a-no-write");
    CHECK(KrnBufferServiceTake(&b,bt,bytes,sizeof(bytes),&n),"thread-b-take");
    memset(bytes,0x88,sizeof(bytes));
    CHECK(KrnBufferServiceComplete(&b,bt,bytes,n),"thread-b-complete");
    CHECK(!KrnBufferServiceCollect(&b,101,bt,&f.bufops),"thread-foreign-collect");
    CHECK(KrnBufferServiceCollect(&b,102,bt,&f.bufops) && f.data[0][64]==0x88,"thread-buffer-authority");
    CHECK(!f.mapping.in_flight && KrnServiceDrained(&q),"threads-drained");
    CHECK(KrnMappingDomainLeave(&threads[1]) && !f.domain.closing,"thread-last-leave-retained-space");
    KrnServiceClose(&q);clean(&f);
}

int main(void)
{
    copies();refusal();revalidation();domain_lifetime();adjacent_owners();service_cases();cross_queue_reuse();shared_domain_requesters();
    puts("PASS owned buffers bounds permissions generations lifetime");return 0;
}
