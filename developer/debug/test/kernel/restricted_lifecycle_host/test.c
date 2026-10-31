/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <aros/apple/restricted.h>
#define R_PAGE 4096U
#define R_DOMAINS 16
#define R_THREADS 64
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
#define R_TABLE_GROW 0xa569
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
struct RestrictedTableChunk { struct RestrictedTableChunk *next; void *pages; size_t count,used; };
struct RestrictedDomain {
    struct RestrictedDomain *next;
    size_t storage_bytes;
    struct AROSRestrictedSpace space;
    enum RestrictedState state;
    void *code,*data;
    unsigned threads,managed,closing;
    void *owner_context,*releaser_context;
    size_t code_bytes,memory_pages,memory_metadata_pages;
    struct RestrictedTableChunk *tables;
    size_t table_pages,table_used,table_metadata_pages,table_shortage;
    struct RestrictedAllocation *allocations;
};
struct Restricted {
    struct Restricted *next;
    size_t storage_bytes;
    struct RestrictedDomain *domain;
    enum RestrictedState state;
    void *releaser_context;
    uint64_t cookie;
    struct Task *owner;
    void *owner_context;
    unsigned abandoned, fault_active, snapshot_data;
    void *stack,*fault_stack,*tls;
    uint64_t stack_address,fault_address,tls_address;
    size_t stack_bytes,fault_bytes,address_span;
    uint64_t seed;
    struct AROSRestrictedService service;
};
struct RestrictedPublication {
    struct Restricted *slot;
    void *code,*data,*stack,*fault_stack,*tls;
    uint64_t seed;
    size_t code_bytes;
};
static struct Restricted thread_storage[R_THREADS];
static struct Restricted *restricted_slots;
static struct RestrictedDomain *restricted_domains;
static struct RestrictedDomain domain_storage[64];
static uint64_t owners_retired,owners_reaped;
struct RestrictedDomainPublication {
    struct RestrictedDomain *domain;
    void *code,*data;
    size_t code_bytes;
};
static int unmap_allowed=1;
static int r_unmap_thread(struct Restricted *r) { (void)r;return unmap_allowed; }
static uint64_t handle_generation;
static int r_prepare_domain(struct RestrictedDomain *d,uint64_t root)
{
    assert(root==123 && !d->space.handle);d->table_pages=1024;
    d->space.handle=(++handle_generation<<32)|1;d->space.drained=1;return 1;
}
static void r_shared_lifetime(struct AROSRestrictedLifetime *state) { (void)state; }
struct RestrictedTableTransfer { uint64_t handle; struct RestrictedDomain *domain; struct Restricted *thread; struct RestrictedTableChunk *chunk; };
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
static struct Restricted *new_thread(void)
{
    for (unsigned i=0;i<R_THREADS;++i) if (thread_storage[i].state==R_FREE) {
        thread_storage[i].stack_bytes=R_PAGE;thread_storage[i].fault_bytes=R_PAGE;
        thread_storage[i].storage_bytes=R_PAGE;return &thread_storage[i];
    }
    return NULL;
}
static void refuse_reclaim(struct Restricted *r)
{
    struct AROSAppleRuntimeFrame frame={.x={0,(uintptr_t)new_domain(),(uintptr_t)r}};
    assert(r_lifecycle(&frame,&owner,&context,R_CLAIM) && !frame.x[0]);
}
static uintptr_t call(unsigned svc,uintptr_t arg,struct Task *task,struct ExceptionContext *ctx)
{
    struct AROSAppleRuntimeFrame frame={.x={arg,(svc==R_CLAIM && !arg) ? (uintptr_t)new_domain() : 0,svc==R_CLAIM ? (uintptr_t)new_thread() : 0}};
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
    struct RestrictedDomainPublication p={d,&owner,&owner,32};
    assert(!domain_call(R_DOMAIN_PUBLISH,(uintptr_t)&p,&foreign,NULL));
    assert(d->state==R_ALLOCATING);
    assert(domain_call(R_DOMAIN_PUBLISH,(uintptr_t)&p,&context,NULL));
    assert(!domain_call(R_DOMAIN_PREPARE,(uintptr_t)d,&foreign,NULL));
    uint64_t handle=domain_call(R_DOMAIN_PREPARE,(uintptr_t)d,&context,NULL);
    assert(handle && r_domain_lookup(handle)==d);return handle;
}
static void managed_tests(void)
{
    memset(thread_storage,0,sizeof(thread_storage));restricted_slots=NULL;
    memset(domain_storage,0,sizeof(domain_storage));restricted_domains=NULL;
    uint64_t handle=managed_create();struct RestrictedDomain *d=r_domain_lookup(handle);
    uintptr_t status;
    assert(!r_domain_slot((uintptr_t)d+1) && !r_domain_slot(UINTPTR_MAX));
    struct Restricted *a=(void *)call(R_CLAIM,handle,&owner,&context);
    struct Restricted *b=(void *)call(R_CLAIM,handle,&other,&foreign);
    assert(a && b && a!=b && a->domain==d && b->domain==d && d->threads==2);
    struct RestrictedPublication pa={.slot=a,.stack=&owner,.fault_stack=&owner,.tls=&owner};
    struct RestrictedPublication pb={.slot=b,.stack=&other,.fault_stack=&other,.tls=&other};
    pa.code=&other;assert(!call(R_PUBLISH,(uintptr_t)&pa,&owner,&context));
    assert(d->code==&owner && a->state==R_ALLOCATING);pa.code=NULL;
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
static void thread_growth(void)
{
    uint64_t handle=managed_create();struct RestrictedDomain *d=r_domain_lookup(handle);
    struct Restricted *threads[32];
    for (unsigned i=0;i<32;++i) {
        threads[i]=(void *)call(R_CLAIM,handle,&owner,&context);assert(threads[i]);
        assert(threads[i]->stack_address==AROS_RESTRICTED_STACK+i*AROS_RESTRICTED_THREAD_STRIDE);
        assert(threads[i]->fault_address==AROS_RESTRICTED_FAULT_STACK+i*AROS_RESTRICTED_THREAD_STRIDE);
        assert(threads[i]->tls_address==AROS_RESTRICTED_TLS+i*AROS_RESTRICTED_THREAD_STRIDE);
        for (unsigned j=0;j<i;++j) assert(!r_stack_sp(threads[j],threads[i]->stack_address+R_PAGE));
    }
    assert(d->threads==32);
    /* A claimed reaper still owns its virtual stack/TLS interval. */
    threads[0]->state=R_REAPING;
    struct Restricted *extra=(void *)call(R_CLAIM,handle,&owner,&context);
    assert(extra && extra->stack_address==AROS_RESTRICTED_STACK+32*AROS_RESTRICTED_THREAD_STRIDE);
    extra->service.drained=1;assert(r_close(extra));extra->state=R_REAPING;extra->releaser_context=&context;
    assert(call(R_RELEASED,(uintptr_t)extra,&owner,&context));
    threads[0]->state=R_ALLOCATING;
    struct AROSRestrictedLifetime life;
    assert(call(R_LIFETIME,(uintptr_t)&life,&owner,&context));
    assert(life.live==32 && life.metadata_pages==33 && life.pages==1059);
    for (unsigned parity=0;parity<2;++parity) for (unsigned i=parity;i<32;i+=2) {
        struct Restricted *r=threads[i];r->service.drained=1;
        assert(r_close(r));r->state=R_REAPING;r->releaser_context=&context;
        refuse_reclaim(r);
        assert(call(R_RELEASED,(uintptr_t)r,&owner,&context));
        assert(!r_slot((uintptr_t)r));
    }
    assert(!restricted_slots && !d->threads);
    struct Restricted *r=(void *)call(R_CLAIM,handle,&owner,&context);
    assert(r && r->stack_address==AROS_RESTRICTED_STACK);r->service.drained=1;
    assert(r_close(r));r->state=R_REAPING;r->releaser_context=&context;
    assert(call(R_RELEASED,(uintptr_t)r,&owner,&context));
    assert(domain_call(R_DOMAIN_CLOSE,handle,&context,NULL)==(uintptr_t)d);
    assert(domain_call(R_DOMAIN_RELEASED,(uintptr_t)d,&context,NULL));
    assert(call(R_LIFETIME,(uintptr_t)&life,&owner,&context));
    assert(!life.live && !life.domains && !life.pages && !life.metadata_pages);
}
static uintptr_t table_transfer(struct RestrictedTableTransfer *p,void *ctx)
{
    struct AROSAppleRuntimeFrame f={.x={(uintptr_t)p}};
    assert(r_table_transfer(&f,ctx,R_TABLE_GROW));return f.x[0];
}
static void table_growth(void)
{
    uint64_t handle=managed_create();struct RestrictedDomain *d=r_domain_lookup(handle);
    d->table_pages=0;
    struct RestrictedTableChunk a={.pages=(void *)0x10000,.count=3};
    struct RestrictedTableTransfer p={.handle=handle,.chunk=&a};
    assert(!table_transfer(&p,&foreign) && !d->tables);
    assert(table_transfer(&p,&context) && d->table_pages==3 && d->table_metadata_pages==1);
    assert(!table_transfer(&p,&context));
    struct RestrictedTableChunk overlap={.pages=(void *)0x11000,.count=3};p.chunk=&overlap;
    assert(!table_transfer(&p,&context) && !overlap.next);
    assert(!r_allocate(d,14) && !d->table_used);
    assert(!r_tables_need(d,5) && d->table_shortage==2);
    for (unsigned i=0;i<3;++i) assert(r_allocate(d,12)==0x10000+i*R_PAGE);
    assert(!r_allocate(d,12) && d->table_used==3);
    struct RestrictedTableChunk extra={.pages=(void *)0x20000,.count=2048};p.chunk=&extra;
    assert(table_transfer(&p,&context));
    assert(r_tables_need(d,2048) && !d->table_shortage);
    for (unsigned i=0;i<2048;++i) assert(r_allocate(d,12)==0x20000+i*R_PAGE);
    assert(!r_allocate(d,12) && d->table_pages==2051 && d->table_used==2051);
    struct AROSRestrictedLifetime life;assert(call(R_LIFETIME,(uintptr_t)&life,&owner,&context));
    assert(life.table_pages==2051 && life.metadata_pages==3 && life.pages==2056);
    d->closing=1;struct RestrictedTableChunk denied={.pages=(void *)0x1000000,.count=1};p.chunk=&denied;
    assert(!table_transfer(&p,&context));
    assert(domain_call(R_DOMAIN_CLOSE,handle,&context,NULL)==(uintptr_t)d);
    assert(domain_call(R_DOMAIN_RELEASED,(uintptr_t)d,&context,NULL));
    assert(call(R_LIFETIME,(uintptr_t)&life,&owner,&context) && !life.pages && !life.table_pages);
}
static void variable_stacks(void)
{
    struct RestrictedDomain d={0},other_domain={0};
    struct Restricted a={.domain=&d,.owner=&owner,.stack_bytes=17*R_PAGE,.fault_bytes=3*R_PAGE};
    struct Restricted b={.domain=&d,.stack_bytes=R_PAGE,.fault_bytes=R_PAGE};
    assert(r_thread_addresses(&a,&d));
    assert(a.stack_address==AROS_RESTRICTED_STACK && a.address_span==2*AROS_RESTRICTED_THREAD_STRIDE);
    assert(a.fault_address==a.stack_address+18*R_PAGE && a.tls_address==a.fault_address+4*R_PAGE);
    restricted_slots=&a;a.state=R_REAPING;
    a.stack=&owner;a.fault_stack=&owner;a.tls=&owner;
    struct AROSRestrictedLifetime life;
    assert(call(R_LIFETIME,(uintptr_t)&life,&owner,&context));
    assert(life.pages==21);
    assert(r_thread_addresses(&b,&d) && b.stack_address==a.stack_address+a.address_span);
    assert(r_thread_addresses(&b,&other_domain) && b.stack_address==a.stack_address);
    assert(r_stack_sp(&a,a.stack_address+a.stack_bytes));
    assert(!r_stack_sp(&a,a.stack_address+a.stack_bytes+16));
    r_stack_bounds(&a,0);assert((uintptr_t)owner.tc_SPUpper==a.stack_address+a.stack_bytes);
    a.fault_active=1;r_stack_bounds(&a,1);
    assert((uintptr_t)owner.tc_SPUpper==a.fault_address+a.fault_bytes);
    assert(r_stack_sp(&a,a.fault_address+a.fault_bytes));
    assert(!r_stack_sp(&a,a.fault_address+a.fault_bytes+16));
    b.stack_bytes=SIZE_MAX;assert(!r_thread_addresses(&b,&d));
    b.stack_bytes=0;assert(!r_thread_addresses(&b,&d));
    b.stack_bytes=R_PAGE+1;assert(!r_thread_addresses(&b,&d));
    b.stack_bytes=R_PAGE;b.fault_bytes=SIZE_MAX;assert(!r_thread_addresses(&b,&d));
    restricted_slots=NULL;b.fault_bytes=R_PAGE;
    assert(r_thread_addresses(&b,&d) && b.stack_address==AROS_RESTRICTED_STACK);
    assert(r_range_tables(0x1ff000,2*R_PAGE)==4);
    assert(r_range_tables(0x3ffff000,2*R_PAGE)==5);
    assert(r_range_tables(0x7ffffff000ULL,2*R_PAGE)==6);
}
static void code_images(void)
{
    unsigned char image[3*R_PAGE];
    struct RestrictedDomain d={.state=R_LIVE,.code=image,.code_bytes=2*R_PAGE+4};
    struct Restricted r={.domain=&d};
    assert(r_code_storage(d.code_bytes)==3*R_PAGE);
    assert(!r_code_storage(0) && !r_code_storage(SIZE_MAX) && !r_code_storage(UINT32_MAX));
    assert(r_code_alias(&d,AROS_RESTRICTED_CODE+R_PAGE,R_PAGE)==image+R_PAGE);
    assert(r_code_alias(&d,AROS_RESTRICTED_CODE+2*R_PAGE,R_PAGE)==image+2*R_PAGE);
    assert(!r_code_alias(&d,AROS_RESTRICTED_CODE-1,R_PAGE));
    assert(!r_code_alias(&d,AROS_RESTRICTED_CODE+3*R_PAGE,R_PAGE));
    assert(r_code_pc(&r,AROS_RESTRICTED_CODE+2*R_PAGE));
    assert(!r_code_pc(&r,AROS_RESTRICTED_CODE+2*R_PAGE+4));
    assert(!r_code_pc(&r,AROS_RESTRICTED_CODE+1));
    restricted_domains=&d;
    struct AROSRestrictedLifetime life;assert(call(R_LIFETIME,(uintptr_t)&life,&owner,&context));
    assert(life.pages==3);restricted_domains=NULL;
}
int main(void)
{
    struct Restricted stack_owner={.owner=&owner,.stack_address=AROS_RESTRICTED_STACK,
        .fault_address=AROS_RESTRICTED_FAULT_STACK,.tls_address=AROS_RESTRICTED_TLS,
        .stack_bytes=R_PAGE,.fault_bytes=R_PAGE};
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
    restricted_slots=&thread_storage[0];thread_storage[0].state=R_REAPING;
    thread_storage[0].domain=&domain_storage[0];
    domain_storage[0].state=R_REAPING;domain_storage[0].threads=1;restricted_domains=&domain_storage[0];
    assert(call(R_CLAIM,0,&owner,&context)==(uintptr_t)&thread_storage[1]);
    assert(thread_storage[0].state==R_REAPING);
    assert(thread_storage[1].domain==&domain_storage[1]);
    memset(thread_storage,0,sizeof(thread_storage));restricted_slots=NULL;
    memset(domain_storage,0,sizeof(domain_storage));restricted_domains=NULL;

    domain_storage[0]=(struct RestrictedDomain){.state=R_LIVE,
        .table_pages=1024,.code=&owner,.data=&owner,.code_bytes=32,.threads=2};
    restricted_domains=&domain_storage[0];
    for (unsigned i=0;i<2;++i)
        thread_storage[i]=(struct Restricted){.state=R_LIVE,.domain=&domain_storage[0],
            .stack=&owner,.fault_stack=&owner,.stack_bytes=R_PAGE,.fault_bytes=R_PAGE};
    restricted_slots=&thread_storage[0];thread_storage[0].next=&thread_storage[1];
    struct AROSRestrictedLifetime shared_life;
    assert(call(R_LIFETIME,(uintptr_t)&shared_life,&owner,&context));
    assert(shared_life.live==2 && shared_life.pages==1030);
    thread_storage[0].service.drained=1;domain_storage[0].space.drained=1;
    assert(!r_close(&thread_storage[0]));
    assert(!domain_storage[0].space.stopped && !domain_storage[0].space.closes);
    assert(!thread_storage[0].service.closes && domain_storage[0].state==R_LIVE);
    /* Channel acknowledgement alone cannot authorize domain release while
     * its mappings still retain backing. Exercise the actual close helper. */
    domain_storage[0].threads=1;domain_storage[0].space.drained=0;
    assert(!r_close(&thread_storage[0]));
    assert(domain_storage[0].state==R_LIVE);
    domain_storage[0].space.drained=1;
    unmap_allowed=0;
    assert(!r_close(&thread_storage[0]) && domain_storage[0].state==R_LIVE);
    unmap_allowed=1;
    assert(r_close(&thread_storage[0]) && domain_storage[0].state==R_REAPING);
    memset(thread_storage,0,sizeof(thread_storage));restricted_slots=NULL;
    memset(domain_storage,0,sizeof(domain_storage));restricted_domains=NULL;

    struct Restricted *slots[R_DOMAINS];
    for (unsigned i=0;i<R_DOMAINS;++i) {
        slots[i]=(void *)call(R_CLAIM,0,&owner,&context);
        assert(slots[i]==&thread_storage[i] && slots[i]->state==R_ALLOCATING);
        assert(slots[i]->stack_address==AROS_RESTRICTED_STACK);
        assert(slots[i]->fault_address==AROS_RESTRICTED_FAULT_STACK);
        assert(slots[i]->tls_address==AROS_RESTRICTED_TLS);
    }
    for (unsigned i=0;i<R_DOMAINS;++i) {
        for (unsigned j=0;j<R_DOMAINS;++j)
            assert(r_stack_sp(slots[i],slots[j]->stack_address+R_PAGE)==1);
        slots[i]->fault_active=1;
        for (unsigned j=0;j<R_DOMAINS;++j)
            assert(r_stack_sp(slots[i],slots[j]->fault_address+R_PAGE)==1);
        slots[i]->fault_active=0;
    }
    refuse_reclaim(slots[0]);
    assert(!r_slot((uintptr_t)slots[0]+1) && !r_slot(UINTPTR_MAX));
    assert(!r_claim_reap(&foreign));
    struct AROSRestrictedLifetime life;
    assert(call(R_LIFETIME,(uintptr_t)&life,&other,&foreign));
    assert(life.live==R_DOMAINS && life.pages==2*R_DOMAINS && !life.pending);
    for (unsigned i=0;i<R_DOMAINS;++i) {
        struct RestrictedPublication p={.slot=slots[i],
            .code=&owner,.data=&owner,.stack=&owner,.fault_stack=&owner,.tls=&owner,.seed=i,.code_bytes=16};
        assert(!call(R_PUBLISH,(uintptr_t)&p,&other,&context));
        assert(!call(R_PUBLISH,(uintptr_t)&p,&owner,&foreign));
        assert(call(R_PUBLISH,(uintptr_t)&p,&owner,&context));
        assert(!call(R_PUBLISH,(uintptr_t)&p,&owner,&context));
        assert(slots[i]->state==R_LIVE && slots[i]->seed==i);
        assert(slots[i]->domain==&domain_storage[i] && slots[i]->domain->state==R_LIVE);
        slots[i]->domain->table_pages=1024;
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
        refuse_reclaim(slots[i]);
        assert(call(R_LIFETIME,(uintptr_t)&life,&owner,&context));
        assert(life.live==R_DOMAINS && life.pages==(R_DOMAINS-i)*1031+2*i && life.pending==R_DOMAINS-i);
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
    thread_growth();
    variable_stacks();
    code_images();
    shared_tests();
    domain_growth();
    table_growth();
    return 0;
}
