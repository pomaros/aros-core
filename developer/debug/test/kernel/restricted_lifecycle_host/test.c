/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <aros/apple/restricted.h>
#define R_PAGE 4096U
#define KBUF_SPACES 4
#define AROS_RESTRICTED_CHANNELS 16
#define R_TABLES 1024
#define R_COOKIE UINT64_C(0x7265737472696374)
#define R_CLAIM 0xa538
#define R_PUBLISH 0xa539
#define R_RELEASED 0xa53a
#define R_LIFETIME 0xa53b
struct Task { int id; void *tc_SPLower,*tc_SPUpper; };
struct ExceptionContext { int id; };
struct AROSAppleRuntimeFrame { uintptr_t x[31]; };
struct AROSRestrictedSpace { unsigned stopped,drained,closes; };
static void AROSRestrictedSpaceStop(struct AROSRestrictedSpace *s)
{ ++s->stopped; }
static int AROSRestrictedSpaceClose(struct AROSRestrictedSpace *s)
{ ++s->closes;return s->drained; }
struct AROSRestrictedService { unsigned drained,closes; };
static int AROSRestrictedServiceClose(struct AROSRestrictedService *s)
{ ++s->closes;return s->drained; }
enum RestrictedState { R_FREE,R_ALLOCATING,R_LIVE,R_REAPING };
struct RestrictedDomain {
    struct AROSRestrictedSpace space;
    enum RestrictedState state;
    void *pool,*code,*data;
    unsigned threads;
    size_t code_bytes;
};
struct Restricted {
    struct RestrictedDomain *domain;
    enum RestrictedState state;
    void *releaser_context;
    uint64_t cookie;
    struct Task *owner;
    void *owner_context;
    unsigned abandoned, fault_active;
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
static struct Restricted restricted_slots[AROS_RESTRICTED_CHANNELS];
static struct RestrictedDomain restricted_domains[KBUF_SPACES];
static uint64_t owners_retired,owners_reaped;
#include "actual.inc"
static struct Task owner,other;
static struct ExceptionContext context,foreign;
static uintptr_t call(unsigned svc,uintptr_t arg,struct Task *task,struct ExceptionContext *ctx)
{
    struct AROSAppleRuntimeFrame frame={.x={arg}};
    assert(r_lifecycle(&frame,task,ctx,svc));return frame.x[0];
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
    restricted_slots[0].domain=&restricted_domains[0];
    restricted_domains[0].state=R_REAPING;restricted_domains[0].threads=1;
    assert(call(R_CLAIM,0,&owner,&context)==(uintptr_t)&restricted_slots[1]);
    assert(restricted_slots[0].state==R_REAPING);
    assert(restricted_slots[1].domain==&restricted_domains[1]);
    memset(restricted_slots,0,sizeof(restricted_slots));
    memset(restricted_domains,0,sizeof(restricted_domains));

    for (unsigned i=0;i<AROS_RESTRICTED_CHANNELS;++i)
        restricted_slots[i].state=R_ALLOCATING;
    assert(!call(R_CLAIM,0,&owner,&context));
    for (unsigned i=0;i<KBUF_SPACES;++i)
        assert(restricted_domains[i].state==R_FREE && !restricted_domains[i].threads);
    memset(restricted_slots,0,sizeof(restricted_slots));
    restricted_domains[0]=(struct RestrictedDomain){.state=R_LIVE,
        .pool=&owner,.code=&owner,.data=&owner,.threads=2};
    for (unsigned i=0;i<2;++i)
        restricted_slots[i]=(struct Restricted){.state=R_LIVE,.domain=&restricted_domains[0],
            .stack=&owner,.fault_stack=&owner};
    struct AROSRestrictedLifetime shared_life;
    assert(call(R_LIFETIME,(uintptr_t)&shared_life,&owner,&context));
    assert(shared_life.live==2 && shared_life.pages==1030);
    restricted_slots[0].service.drained=1;restricted_domains[0].space.drained=1;
    assert(!r_close(&restricted_slots[0]));
    assert(!restricted_domains[0].space.stopped && !restricted_domains[0].space.closes);
    assert(!restricted_slots[0].service.closes && restricted_domains[0].state==R_LIVE);
    /* Channel acknowledgement alone cannot authorize domain release while
     * its mappings still retain backing. Exercise the actual close helper. */
    restricted_domains[0].threads=1;restricted_domains[0].space.drained=0;
    assert(!r_close(&restricted_slots[0]));
    assert(restricted_domains[0].state==R_LIVE);
    restricted_domains[0].space.drained=1;
    assert(r_close(&restricted_slots[0]) && restricted_domains[0].state==R_REAPING);
    memset(restricted_slots,0,sizeof(restricted_slots));
    memset(restricted_domains,0,sizeof(restricted_domains));

    struct Restricted *slots[KBUF_SPACES];
    for (unsigned i=0;i<KBUF_SPACES;++i) {
        slots[i]=(void *)call(R_CLAIM,0,&owner,&context);
        assert(slots[i]==&restricted_slots[i] && slots[i]->state==R_ALLOCATING);
        assert(slots[i]->stack_address==AROS_RESTRICTED_STACK+i*AROS_RESTRICTED_THREAD_STRIDE);
        assert(slots[i]->fault_address==AROS_RESTRICTED_FAULT_STACK+i*AROS_RESTRICTED_THREAD_STRIDE);
        assert(slots[i]->tls_address==AROS_RESTRICTED_TLS+i*AROS_RESTRICTED_THREAD_STRIDE);
    }
    for (unsigned i=0;i<KBUF_SPACES;++i) {
        for (unsigned j=0;j<KBUF_SPACES;++j)
            assert(r_stack_sp(slots[i],slots[j]->stack_address+R_PAGE)==(i==j));
        slots[i]->fault_active=1;
        for (unsigned j=0;j<KBUF_SPACES;++j)
            assert(r_stack_sp(slots[i],slots[j]->fault_address+R_PAGE)==(i==j));
        slots[i]->fault_active=0;
    }
    assert(!call(R_CLAIM,0,&other,&foreign));
    assert(!r_slot((uintptr_t)slots[0]+1) && !r_slot(UINTPTR_MAX));
    assert(!r_claim_reap(&foreign));
    struct AROSRestrictedLifetime life;
    assert(call(R_LIFETIME,(uintptr_t)&life,&other,&foreign));
    assert(life.live==4 && !life.pages && !life.pending);
    for (unsigned i=0;i<KBUF_SPACES;++i) {
        struct RestrictedPublication p={.slot=slots[i],.pool=&owner,
            .code=&owner,.data=&owner,.stack=&owner,.fault_stack=&owner,.tls=&owner,.seed=i,.code_bytes=16};
        assert(!call(R_PUBLISH,(uintptr_t)&p,&other,&context));
        assert(!call(R_PUBLISH,(uintptr_t)&p,&owner,&foreign));
        assert(call(R_PUBLISH,(uintptr_t)&p,&owner,&context));
        assert(!call(R_PUBLISH,(uintptr_t)&p,&owner,&context));
        assert(slots[i]->state==R_LIVE && slots[i]->seed==i);
        assert(slots[i]->domain==&restricted_domains[i] && slots[i]->domain->state==R_LIVE);
        assert(!call(R_RELEASED,(uintptr_t)slots[i],&owner,&context));
        slots[i]->domain->space.drained=1;
        slots[i]->abandoned=1;++owners_retired;
    }
    assert(!r_claim_reap(&foreign)); /* Outstanding worker lease. */
    for (unsigned i=0;i<KBUF_SPACES;++i) {
        slots[i]->service.drained=1;
        assert(r_claim_reap(&context)==slots[i]);
        unsigned closes=slots[i]->service.closes;
        assert(!r_claim_reap(&foreign)); /* No duplicate claimant. */
        assert(slots[i]->service.closes==closes);
        assert(!call(R_RELEASED,(uintptr_t)slots[i],&other,&foreign));
        assert(!call(R_CLAIM,0,&other,&foreign));
        assert(call(R_LIFETIME,(uintptr_t)&life,&owner,&context));
        assert(life.live==4 && life.pages==(4-i)*1029 && life.pending==4-i);
        slots[i]->domain->threads=2;
        assert(!call(R_RELEASED,(uintptr_t)slots[i],&owner,&context));
        assert(slots[i]->state==R_REAPING && slots[i]->domain->threads==2);
        slots[i]->domain->threads=1;
        assert(call(R_RELEASED,(uintptr_t)slots[i],&owner,&context));
        assert(!call(R_RELEASED,(uintptr_t)slots[i],&owner,&context));
        /* Reuse only after release acknowledgement, never while freeing. */
        assert(call(R_CLAIM,0,&owner,&context)==(uintptr_t)slots[i]);
    }
    assert(owners_reaped==4);
    return 0;
}
