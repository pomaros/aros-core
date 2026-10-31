/* Copyright (C) 2026 John KNIPPER.
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "restricted_service.h"
#include <aros/apple/restricted.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define R_PAGE 4096U
#define MEMF_CLEAR 1U
static unsigned exclusion,attempts,fail_at,live;
static int publish;
static void *owned;
static struct { void *p;size_t bytes; } allocations[128];
static void Forbid(void) { ++exclusion; }
static void Permit(void) { assert(exclusion);--exclusion; }
static void *AROSApplePageAllocate(void *address,size_t bytes,unsigned flags)
{
    assert(exclusion==1 && !address && flags==MEMF_CLEAR && bytes && !(bytes&(R_PAGE-1)));
    if (++attempts==fail_at) return NULL;
    for (unsigned i=0;i<128;++i) if (!allocations[i].p) {
        void *p=aligned_alloc(R_PAGE,bytes);assert(p);memset(p,0,bytes);
        allocations[i].p=p;allocations[i].bytes=bytes;++live;return p;
    }
    abort();
}
static int AROSApplePageFree(void *p,size_t bytes)
{
    assert(exclusion==1);
    for (unsigned i=0;i<128;++i) if (allocations[i].p==p) {
        assert(allocations[i].bytes==bytes);allocations[i].p=NULL;--live;free(p);return 1;
    }
    abort();
}
static void *AROSApplePageTableAllocate(size_t bytes) { return AROSApplePageAllocate(NULL,bytes,MEMF_CLEAR); }
static int AROSApplePageFreeRaw(void *p,size_t bytes) { return AROSApplePageFree(p,bytes); }
struct RestrictedDomain;struct Restricted;
enum RestrictedState { R_FREE,R_ALLOCATING,R_LIVE,R_REAPING };
#include "records.inc"
static uintptr_t table_publish(struct RestrictedTableTransfer *p)
{
    assert(exclusion==1 && p->handle==77 && p->chunk && p->chunk->pages);
    if (publish) owned=p->chunk;
    return publish;
}
static int r_memory_call(struct RestrictedMemoryOperation *op)
{
    assert(exclusion==1 && op->operation==0);
    if (publish) owned=op->allocation;
    return publish;
}
static uintptr_t r_shared_call(struct RestrictedSharedOperation *op)
{
    assert(exclusion==1);
    if (op->operation==3) { owned=NULL;return 1; }
    assert(op->operation==0);
    if (publish) { owned=op->record;op->record->handle=77;return 77; }
    return 0;
}
#include "actual.inc"
int main(void)
{
    Forbid();
    assert(!r_supply_tables(77,NULL,NULL,0));
    assert(!r_supply_tables(77,NULL,NULL,SIZE_MAX));
    for (unsigned failure=1;failure<=2;++failure) {
        attempts=0;fail_at=failure;publish=1;owned=NULL;
        assert(!r_supply_tables(77,NULL,NULL,2048) && !live && !owned);
    }
    fail_at=0;publish=0;
    assert(!r_supply_tables(77,NULL,NULL,2048) && !live && !owned);
    publish=1;assert(r_supply_tables(77,NULL,NULL,2048) && live==2 && owned);
    struct RestrictedTableChunk *c=owned;assert(c->count==2048 && !c->used && !c->next);
    assert(r_supply_tables(77,NULL,NULL,3) && live==4);
    struct RestrictedTableChunk *second=owned;second->next=c;second->used=1;c->used=2048;
    struct RestrictedDomain domain={.tables=second};r_free_domain_backing(&domain);owned=NULL;
    assert(!live);
    domain=(struct RestrictedDomain){.code_bytes=2*R_PAGE+4};
    domain.code=AROSApplePageAllocate(NULL,r_code_storage(domain.code_bytes),MEMF_CLEAR);
    domain.data=AROSApplePageAllocate(NULL,R_PAGE,MEMF_CLEAR);assert(live==2);
    r_free_domain_backing(&domain);
    Permit();assert(!live && !exclusion);
    assert(!r_record_size(SIZE_MAX,1,1));
    assert(!r_record_size(16,SIZE_MAX,8));
    assert(r_record_size(16,1,8)==R_PAGE && r_record_size(16,512,8)==2*R_PAGE);
    for (unsigned failure=1;failure<=9;++failure) {
        attempts=0;fail_at=failure;publish=1;owned=NULL;
        assert(!r_create_shared(8*R_PAGE) && !live && !owned && !exclusion);
    }
    attempts=0;fail_at=0;publish=0;
    assert(!r_create_shared(8*R_PAGE) && !live && !exclusion);
    publish=1;assert(r_create_shared(8*R_PAGE)==77 && live==9 && owned && !exclusion);
    Forbid();r_release_shared(owned);Permit();assert(!live && !owned && !exclusion);
    attempts=0;fail_at=1;
    assert(!r_reserve_memory(1,AROS_RESTRICTED_DYNAMIC_BASE,R_PAGE) && !live && !exclusion);
    fail_at=0;publish=0;
    assert(!r_reserve_memory(1,AROS_RESTRICTED_DYNAMIC_BASE,R_PAGE) && !live && !exclusion);
    publish=1;assert(r_reserve_memory(1,AROS_RESTRICTED_DYNAMIC_BASE,2048*R_PAGE) && live==1 && !exclusion);
    struct RestrictedAllocation *a=owned;
    assert(a->count==2048 && a->storage_bytes>R_PAGE && !a->backing[2047]);
    Forbid();r_free_allocation(a);Permit();owned=NULL;
    assert(!live && !exclusion);puts("PASS native allocation failure rollback and ownership");return 0;
}
