/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <aros/apple/restricted.h>
#define R_PAGE 4096U
#define R_DOMAINS 16
#define R_THREADS 16
#define R_TABLES 1024
#define R_COOKIE UINT64_C(0x7265737472696374)
#define R_CLAIM 0xa538
#define R_PUBLISH 0xa539
#define R_RELEASED 0xa53a
#define R_LIFETIME 0xa53b
#define R_DOMAIN_CLAIM 0xa560
#define R_DOMAIN_PUBLISH 0xa561
#define R_DOMAIN_PREPARE 0xa562
#define R_DOMAIN_CLOSE 0xa563
#define R_DOMAIN_RELEASED 0xa564
#define R_DOMAIN_REAP 0xa565
#define R_DOMAIN_ABORT 0xa566
#define R_SHARED 0xa568
struct Task { int id; void *tc_SPLower,*tc_SPUpper; };
struct ExceptionContext { int id; };
struct AROSAppleRuntimeFrame { uintptr_t x[31],entry_ttbr0; };
struct RuntimeLocal { uintptr_t trusted_ttbr0; };
static struct RuntimeLocal local={123};
static struct RuntimeLocal *AROSAppleRuntimeLocal(void) { return &local; }
struct AROSRestrictedSpace { unsigned stopped,drained,closes; uint64_t handle; };
static void AROSRestrictedSpaceStop(struct AROSRestrictedSpace *s)
{ ++s->stopped; }
static int AROSRestrictedSpaceClose(struct AROSRestrictedSpace *s)
{ ++s->closes;if (s->drained) s->handle=0;return s->drained; }
struct AROSRestrictedService { unsigned drained,closes; };
static int AROSRestrictedServiceClose(struct AROSRestrictedService *s)
{ ++s->closes;return s->drained; }
struct AROSRestrictedSharedBacking { uintptr_t *pages; size_t count,views; unsigned closing; };
struct AROSRestrictedRegion {
    struct AROSRestrictedSpace *space; unsigned drained;
    struct { uintptr_t start; } mapping;
    struct AROSRestrictedSharedBacking *shared;
};
static int AROSRestrictedSharedInitialize(struct AROSRestrictedSharedBacking *b,uintptr_t *pages,size_t count)
{ if (b->pages || !pages || !count) return 0;b->pages=pages;b->count=count;return 1; }
static int AROSRestrictedSharedClose(struct AROSRestrictedSharedBacking *b)
{ b->closing=1;if (b->views) return 0;b->pages=NULL;return 1; }
static int AROSRestrictedRegionAttachShared(struct AROSRestrictedRegion *r,struct AROSRestrictedSharedBacking *b)
{ if (r->shared || b->closing) return 0;r->shared=b;++b->views;return 1; }
struct RestrictedShared {
    struct RestrictedShared *next;
    struct AROSRestrictedSharedBacking backing;
    void *owner_context,*releaser_context;
    uint64_t handle;size_t count;uintptr_t *pages;
};
struct RestrictedSharedOperation {
    unsigned operation;uint64_t handle,domain;uintptr_t address;
    struct RestrictedShared *record;
};
static struct RestrictedShared *restricted_shared;
static uint64_t shared_generation;
static int AROSRestrictedRegionDetach(struct AROSRestrictedRegion *r)
{ if (!r->drained) return 0;r->space=NULL;return 1; }
struct RestrictedAllocation { struct RestrictedAllocation *next; struct AROSRestrictedRegion region; };
enum RestrictedState { R_FREE,R_ALLOCATING,R_LIVE,R_REAPING };
struct RestrictedDomain {
    struct RestrictedDomain *next;
    size_t storage_bytes;
    struct AROSRestrictedSpace space;
    enum RestrictedState state;
    void *pool,*code,*data;
    unsigned threads,managed,closing;
    void *owner_context,*releaser_context;
    size_t code_bytes,memory_pages;
    struct RestrictedAllocation *allocations;
};
struct Restricted {
    struct RestrictedDomain *domain;
    enum RestrictedState state;
    void *releaser_context;
    uint64_t cookie;
    struct Task *owner;
    void *owner_context;
    unsigned abandoned, fault_active, snapshot_data;
    void *stack,*fault_stack,*tls;
    uint64_t stack_address,fault_address,tls_address;
    uint64_t seed;
    struct AROSRestrictedService service;
};
struct RestrictedPublication {
    struct Restricted *slot;
    void *pool,*code,*data,*stack,*fault_stack,*tls;
    uint64_t seed;
    size_t code_bytes;
};
static struct Restricted restricted_slots[R_THREADS];
static struct RestrictedDomain *restricted_domains;
static struct RestrictedDomain domain_storage[64];
static uint64_t owners_retired,owners_reaped;
struct RestrictedDomainPublication {
    struct RestrictedDomain *domain;
    void *pool,*code,*data;
    size_t code_bytes;
};
static int unmap_allowed=1;
static int r_unmap_thread(struct Restricted *r) { (void)r;return unmap_allowed; }
static uint64_t handle_generation;
static int r_prepare_domain(struct RestrictedDomain *d,uint64_t root)
{
    assert(root==123 && !d->space.handle);
    d->space.handle=(++handle_generation<<32)|1;d->space.drained=1;return 1;
}
static void r_shared_lifetime(struct AROSRestrictedLifetime *state) { (void)state; }
#include "actual.inc"
static struct Task owner,other;
static struct ExceptionContext context,foreign;
static struct RestrictedDomain *new_domain(void)
{
    for (unsigned i=0;i<64;++i) if (domain_storage[i].state==R_FREE) {
        domain_storage[i].storage_bytes=R_PAGE;return &domain_storage[i];
    }
    return NULL;
}
static uintptr_t call(unsigned svc,uintptr_t arg,struct Task *task,struct ExceptionContext *ctx)
{
    struct AROSAppleRuntimeFrame frame={.x={arg,(svc==R_CLAIM && !arg) ? (uintptr_t)new_domain() : 0}};
    assert(r_lifecycle(&frame,task,ctx,svc));return frame.x[0];
}
static uintptr_t domain_call(unsigned svc,uintptr_t argument,void *ctx,uintptr_t *status)
{
    if (svc==R_DOMAIN_CLAIM && !argument) argument=(uintptr_t)new_domain();
    struct AROSAppleRuntimeFrame frame={.x={argument},.entry_ttbr0=123};
    assert(r_domain_lifecycle(&frame,ctx,svc));
    if (status) *status=frame.x[1];return frame.x[0];
}
static uint64_t managed_create(void)
{
    struct RestrictedDomain *d=(void *)domain_call(R_DOMAIN_CLAIM,0,&context,NULL);
    assert(d && d->managed && d->state==R_ALLOCATING && !d->threads);
    struct RestrictedDomainPublication p={d,&owner,&owner,&owner,32};
    assert(!domain_call(R_DOMAIN_PUBLISH,(uintptr_t)&p,&foreign,NULL));
    assert(d->state==R_ALLOCATING);
    assert(domain_call(R_DOMAIN_PUBLISH,(uintptr_t)&p,&context,NULL));
    assert(!domain_call(R_DOMAIN_PREPARE,(uintptr_t)d,&foreign,NULL));
    uint64_t handle=domain_call(R_DOMAIN_PREPARE,(uintptr_t)d,&context,NULL);
    assert(handle && r_domain_lookup(handle)==d);return handle;
}
static void managed_tests(void)
{
    memset(restricted_slots,0,sizeof(restricted_slots));
    memset(domain_storage,0,sizeof(domain_storage));restricted_domains=NULL;
    uint64_t handle=managed_create();struct RestrictedDomain *d=r_domain_lookup(handle);
    uintptr_t status;
    assert(!r_domain_slot((uintptr_t)d+1) && !r_domain_slot(UINTPTR_MAX));
    struct Restricted *a=(void *)call(R_CLAIM,handle,&owner,&context);
    struct Restricted *b=(void *)call(R_CLAIM,handle,&other,&foreign);
    assert(a && b && a!=b && a->domain==d && b->domain==d && d->threads==2);
    struct RestrictedPublication pa={.slot=a,.stack=&owner,.fault_stack=&owner,.tls=&owner};
    struct RestrictedPublication pb={.slot=b,.stack=&other,.fault_stack=&other,.tls=&other};
    pa.pool=&other;assert(!call(R_PUBLISH,(uintptr_t)&pa,&owner,&context));
    assert(d->pool==&owner && a->state==R_ALLOCATING);pa.pool=NULL;
    assert(call(R_PUBLISH,(uintptr_t)&pa,&owner,&context));
    assert(call(R_PUBLISH,(uintptr_t)&pb,&other,&foreign));
    assert(!r_close(a) && !d->closing && !d->space.stopped);
    a->service.drained=1;
    assert(r_close(a) && d->state==R_LIVE && !d->space.stopped && d->threads==2 && !a->snapshot_data);
    a->state=R_REAPING;a->releaser_context=&context;
    assert(call(R_RELEASED,(uintptr_t)a,&owner,&context));
    assert(d->threads==1 && b->state==R_LIVE && r_domain_lookup(handle)==d);
    b->service.drained=1;assert(r_close(b) && !b->snapshot_data);
    assert(!domain_call(R_DOMAIN_CLOSE,handle,&foreign,&status) && status==UINTPTR_MAX && !d->closing);
    assert(!domain_call(R_DOMAIN_CLOSE,handle,&context,&status) && !status && d->closing);
    assert(!call(R_CLAIM,handle,&owner,&context) && d->threads==1);
    assert(!domain_call(R_DOMAIN_RELEASED,(uintptr_t)d,&context,NULL));
    b->service.drained=1;assert(r_close(b) && d->threads==1 && b->snapshot_data);
    b->state=R_REAPING;b->releaser_context=&foreign;
    assert(call(R_RELEASED,(uintptr_t)b,&other,&foreign));
    assert(!d->threads && d->state==R_LIVE);
    struct RestrictedAllocation allocation={.region={.space=&d->space}};
    d->allocations=&allocation;d->memory_pages=2;
    unsigned closes=d->space.closes;
    assert(!domain_call(R_DOMAIN_CLOSE,handle,&context,&status) && !status &&
        d->state==R_LIVE && d->space.closes==closes && allocation.region.space);
    struct AROSRestrictedLifetime retained;
    assert(call(R_LIFETIME,(uintptr_t)&retained,&owner,&context) && retained.pages==1029);
    allocation.region.drained=1;
    assert(domain_call(R_DOMAIN_CLOSE,handle,&context,&status)==(uintptr_t)d && status==1);
    assert(!r_domain_lookup(handle) && d->state==R_REAPING);
    assert(!domain_call(R_DOMAIN_RELEASED,(uintptr_t)d,&foreign,NULL));
    assert(domain_call(R_DOMAIN_RELEASED,(uintptr_t)d,&context,NULL) && d->state==R_FREE);
    uint64_t next=managed_create();assert(next!=handle && !r_domain_lookup(handle));
    assert(!call(R_CLAIM,handle,&owner,&context));
    assert(!domain_call(R_DOMAIN_CLOSE,handle,&context,&status) && status==UINTPTR_MAX);
    d=r_domain_lookup(next);assert(d && !d->closing);
    d->owner_context=NULL;
    assert(domain_call(R_DOMAIN_REAP,0,&foreign,NULL)==(uintptr_t)d);
    assert(domain_call(R_DOMAIN_RELEASED,(uintptr_t)d,&foreign,NULL));
    struct AROSRestrictedLifetime life;
    assert(call(R_LIFETIME,(uintptr_t)&life,&owner,&context));
    assert(!life.live && !life.domains && !life.pages);
}
static uintptr_t shared_call(struct RestrictedSharedOperation *op,void *ctx)
{
    struct AROSAppleRuntimeFrame frame={.x={(uintptr_t)op}};
    assert(r_shared_operation(&frame,ctx,R_SHARED));return frame.x[0];
}
static void shared_tests(void)
{
    struct RestrictedShared b={.count=1};uintptr_t page=123;b.pages=&page;
    struct RestrictedSharedOperation op={.record=&b};
    uint64_t handle=shared_call(&op,&context);assert(handle && b.owner_context==&context);
    op=(struct RestrictedSharedOperation){.operation=2,.handle=handle};
    assert(!shared_call(&op,&foreign) && !b.backing.closing);
    op.handle=handle+1;assert(!shared_call(&op,&context) && !b.backing.closing);op.handle=handle;
    b.backing.views=1;assert(!shared_call(&op,&context) && b.backing.closing && !b.releaser_context);
    b.backing.views=0;assert(shared_call(&op,&context) && b.releaser_context==&context && op.record==&b);
    assert(!shared_call(&op,&context));
    op.operation=3;assert(!shared_call(&op,&foreign) && restricted_shared==&b);
    assert(shared_call(&op,&context) && !restricted_shared);
    assert(!shared_call(&op,&context));
    memset(&b,0,sizeof(b));b.count=1;b.pages=&page;op=(struct RestrictedSharedOperation){.record=&b};
    uint64_t next=shared_call(&op,&context);assert(next>handle);
    op=(struct RestrictedSharedOperation){.operation=2,.handle=handle};assert(!shared_call(&op,&context));
    b.owner_context=NULL;b.backing.views=1;op.operation=4;
    assert(!shared_call(&op,&foreign));b.backing.views=0;
    assert(shared_call(&op,&foreign) && b.releaser_context==&foreign);
    op.operation=3;op.handle=next;assert(shared_call(&op,&foreign) && !restricted_shared);
}
static void domain_growth(void)
{
    uint64_t handles[32];
    for (unsigned i=0;i<32;++i) handles[i]=managed_create();
    struct AROSRestrictedLifetime life;
    assert(call(R_LIFETIME,(uintptr_t)&life,&owner,&context));
    assert(life.domains==32 && life.pages==32*1027 && !life.live);
    for (unsigned parity=0;parity<2;++parity) for (unsigned i=parity;i<32;i+=2) {
        struct RestrictedDomain *d=r_domain_lookup(handles[i]);assert(d);
        assert(!domain_call(R_DOMAIN_CLAIM,(uintptr_t)d,&context,NULL));
        assert(domain_call(R_DOMAIN_CLOSE,handles[i],&context,NULL)==(uintptr_t)d);
        assert(domain_call(R_DOMAIN_RELEASED,(uintptr_t)d,&context,NULL));
        assert(!r_domain_lookup(handles[i]) && !r_domain_slot((uintptr_t)d));
    }
    assert(!restricted_domains);
    assert(call(R_LIFETIME,(uintptr_t)&life,&owner,&context));assert(!life.domains && !life.pages);
}
int main(void)
{
    struct Restricted stack_owner={.owner=&owner,.stack_address=AROS_RESTRICTED_STACK,
        .fault_address=AROS_RESTRICTED_FAULT_STACK,.tls_address=AROS_RESTRICTED_TLS};
    assert(r_stack_sp(&stack_owner,AROS_RESTRICTED_STACK+R_PAGE));
    assert(!r_stack_sp(&stack_owner,AROS_RESTRICTED_STACK));
    assert(!r_stack_sp(&stack_owner,AROS_RESTRICTED_STACK+1));
    assert(!r_stack_sp(&stack_owner,AROS_RESTRICTED_STACK+R_PAGE+16));
    assert(!r_stack_sp(&stack_owner,AROS_RESTRICTED_FAULT_STACK+R_PAGE));
    stack_owner.fault_active=1;r_stack_bounds(&stack_owner,1);
    assert(r_stack_sp(&stack_owner,(uintptr_t)owner.tc_SPUpper));
    assert(!r_stack_sp(&stack_owner,AROS_RESTRICTED_STACK+R_PAGE));
    assert(!r_stack_sp(&stack_owner,AROS_RESTRICTED_FAULT_STACK));
    assert(!r_stack_sp(&stack_owner,AROS_RESTRICTED_FAULT_STACK+1));
    assert(!r_stack_sp(&stack_owner,AROS_RESTRICTED_FAULT_STACK+R_PAGE+16));
    assert((uintptr_t)owner.tc_SPLower==AROS_RESTRICTED_FAULT_STACK);
    stack_owner.fault_active=0;r_stack_bounds(&stack_owner,0);
    assert((uintptr_t)owner.tc_SPLower==AROS_RESTRICTED_STACK);
    assert(r_stack_sp(&stack_owner,(uintptr_t)owner.tc_SPUpper));

    /* Independent pools: a free domain does not authorize reuse of a context
     * whose native backing release is still in progress. */
    restricted_slots[0].state=R_REAPING;
    restricted_slots[0].domain=&domain_storage[0];
    domain_storage[0].state=R_REAPING;domain_storage[0].threads=1;restricted_domains=&domain_storage[0];
    assert(call(R_CLAIM,0,&owner,&context)==(uintptr_t)&restricted_slots[1]);
    assert(restricted_slots[0].state==R_REAPING);
    assert(restricted_slots[1].domain==&domain_storage[1]);
    memset(restricted_slots,0,sizeof(restricted_slots));
    memset(domain_storage,0,sizeof(domain_storage));restricted_domains=NULL;

    for (unsigned i=0;i<R_THREADS;++i)
        restricted_slots[i].state=R_ALLOCATING;
    assert(!call(R_CLAIM,0,&owner,&context));
    for (unsigned i=0;i<R_DOMAINS;++i)
        assert(domain_storage[i].state==R_FREE && !domain_storage[i].threads);
    memset(restricted_slots,0,sizeof(restricted_slots));
    domain_storage[0]=(struct RestrictedDomain){.state=R_LIVE,
        .pool=&owner,.code=&owner,.data=&owner,.threads=2};
    restricted_domains=&domain_storage[0];
    for (unsigned i=0;i<2;++i)
        restricted_slots[i]=(struct Restricted){.state=R_LIVE,.domain=&domain_storage[0],
            .stack=&owner,.fault_stack=&owner};
    struct AROSRestrictedLifetime shared_life;
    assert(call(R_LIFETIME,(uintptr_t)&shared_life,&owner,&context));
    assert(shared_life.live==2 && shared_life.pages==1030);
    restricted_slots[0].service.drained=1;domain_storage[0].space.drained=1;
    assert(!r_close(&restricted_slots[0]));
    assert(!domain_storage[0].space.stopped && !domain_storage[0].space.closes);
    assert(!restricted_slots[0].service.closes && domain_storage[0].state==R_LIVE);
    /* Channel acknowledgement alone cannot authorize domain release while
     * its mappings still retain backing. Exercise the actual close helper. */
    domain_storage[0].threads=1;domain_storage[0].space.drained=0;
    assert(!r_close(&restricted_slots[0]));
    assert(domain_storage[0].state==R_LIVE);
    domain_storage[0].space.drained=1;
    unmap_allowed=0;
    assert(!r_close(&restricted_slots[0]) && domain_storage[0].state==R_LIVE);
    unmap_allowed=1;
    assert(r_close(&restricted_slots[0]) && domain_storage[0].state==R_REAPING);
    memset(restricted_slots,0,sizeof(restricted_slots));
    memset(domain_storage,0,sizeof(domain_storage));restricted_domains=NULL;

    struct Restricted *slots[R_DOMAINS];
    for (unsigned i=0;i<R_DOMAINS;++i) {
        slots[i]=(void *)call(R_CLAIM,0,&owner,&context);
        assert(slots[i]==&restricted_slots[i] && slots[i]->state==R_ALLOCATING);
        assert(slots[i]->stack_address==AROS_RESTRICTED_STACK+i*AROS_RESTRICTED_THREAD_STRIDE);
        assert(slots[i]->fault_address==AROS_RESTRICTED_FAULT_STACK+i*AROS_RESTRICTED_THREAD_STRIDE);
        assert(slots[i]->tls_address==AROS_RESTRICTED_TLS+i*AROS_RESTRICTED_THREAD_STRIDE);
    }
    for (unsigned i=0;i<R_DOMAINS;++i) {
        for (unsigned j=0;j<R_DOMAINS;++j)
            assert(r_stack_sp(slots[i],slots[j]->stack_address+R_PAGE)==(i==j));
        slots[i]->fault_active=1;
        for (unsigned j=0;j<R_DOMAINS;++j)
            assert(r_stack_sp(slots[i],slots[j]->fault_address+R_PAGE)==(i==j));
        slots[i]->fault_active=0;
    }
    assert(!call(R_CLAIM,0,&other,&foreign));
    assert(!r_slot((uintptr_t)slots[0]+1) && !r_slot(UINTPTR_MAX));
    assert(!r_claim_reap(&foreign));
    struct AROSRestrictedLifetime life;
    assert(call(R_LIFETIME,(uintptr_t)&life,&other,&foreign));
    assert(life.live==R_DOMAINS && life.pages==R_DOMAINS && !life.pending);
    for (unsigned i=0;i<R_DOMAINS;++i) {
        struct RestrictedPublication p={.slot=slots[i],.pool=&owner,
            .code=&owner,.data=&owner,.stack=&owner,.fault_stack=&owner,.tls=&owner,.seed=i,.code_bytes=16};
        assert(!call(R_PUBLISH,(uintptr_t)&p,&other,&context));
        assert(!call(R_PUBLISH,(uintptr_t)&p,&owner,&foreign));
        assert(call(R_PUBLISH,(uintptr_t)&p,&owner,&context));
        assert(!call(R_PUBLISH,(uintptr_t)&p,&owner,&context));
        assert(slots[i]->state==R_LIVE && slots[i]->seed==i);
        assert(slots[i]->domain==&domain_storage[i] && slots[i]->domain->state==R_LIVE);
        assert(!call(R_RELEASED,(uintptr_t)slots[i],&owner,&context));
        slots[i]->domain->space.drained=1;
        slots[i]->abandoned=1;++owners_retired;
    }
    assert(!r_claim_reap(&foreign)); /* Outstanding worker lease. */
    for (unsigned i=0;i<R_DOMAINS;++i) {
        slots[i]->service.drained=1;
        assert(r_claim_reap(&context)==slots[i]);
        unsigned closes=slots[i]->service.closes;
        assert(!r_claim_reap(&foreign)); /* No duplicate claimant. */
        assert(slots[i]->service.closes==closes);
        assert(!call(R_RELEASED,(uintptr_t)slots[i],&other,&foreign));
        assert(!call(R_CLAIM,0,&other,&foreign));
        assert(call(R_LIFETIME,(uintptr_t)&life,&owner,&context));
        assert(life.live==R_DOMAINS && life.pages==(R_DOMAINS-i)*1030+i && life.pending==R_DOMAINS-i);
        slots[i]->domain->threads=2;
        assert(!call(R_RELEASED,(uintptr_t)slots[i],&owner,&context));
        assert(slots[i]->state==R_REAPING && slots[i]->domain->threads==2);
        slots[i]->domain->threads=1;
        assert(call(R_RELEASED,(uintptr_t)slots[i],&owner,&context));
        assert(!call(R_RELEASED,(uintptr_t)slots[i],&owner,&context));
        /* Reuse only after release acknowledgement, never while freeing. */
        assert(call(R_CLAIM,0,&owner,&context)==(uintptr_t)slots[i]);
    }
    assert(owners_reaped==R_DOMAINS);
    managed_tests();
    shared_tests();
    domain_growth();
    return 0;
}
