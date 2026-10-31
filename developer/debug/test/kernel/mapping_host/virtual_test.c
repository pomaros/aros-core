/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "mapping.h"
#include "pager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PAGE 256U
#define BASE 0x10000U
#define CHECK(x,n) do { if (!(x)) { puts("FAIL " n);exit(1); } } while (0)
struct Backing { unsigned allocations,removes,fail_remove;uintptr_t mapped;unsigned char bytes[PAGE] __attribute__((aligned(PAGE))); };
static int allocate(void *opaque,size_t size,struct KrnPagerPage *page)
{
    struct Backing *b=opaque;CHECK(size==PAGE,"page-size");++b->allocations;
    *page=(struct KrnPagerPage){(uintptr_t)b->bytes,b->bytes};return 1;
}
static void release(void *opaque,struct KrnPagerPage *page,size_t size)
{ (void)opaque;(void)page;(void)size; }
static int install(void *opaque,uintptr_t va,uintptr_t pa,unsigned protection)
{
    struct Backing *b=opaque;CHECK(va==BASE && pa==(uintptr_t)b->bytes && !(protection&KMAP_EXEC),"install-input");
    b->mapped=pa;return 1;
}
static int remove_page(void *opaque,uintptr_t va,uintptr_t pa)
{
    struct Backing *b=opaque;CHECK(va==BASE && pa==b->mapped,"remove-input");
    if (b->fail_remove) return 0;
    ++b->removes;b->mapped=0;return 1;
}
int main(void)
{
    struct Backing b={0};memset(b.bytes,0xa5,PAGE);
    struct KrnMappingOps maps={&b,install,remove_page};
    struct KrnPagerOps pager={&b,allocate,release,NULL,NULL};
    struct KrnMappingArena arena;struct KrnMappingObject m,collision;struct KrnMappingPage p[2],extra[1];
    CHECK(KrnMappingArenaInitialize(&arena,BASE,4*PAGE,PAGE),"arena");
    CHECK(KrnMappingReserveVirtual(&arena,&m,BASE,2*PAGE,&b,p,2),"reserve");
    CHECK(m.virtual_reservation && p[0].state==KMAP_RESERVED && !b.allocations,"reserve-no-backing");
    struct KrnMappingRequest request;
    CHECK(KrnMappingFault(&m,BASE,KMAP_READ,1,&request,&maps)==KMAP_FAULT_TRAP && !m.in_flight,"reserved-fault");
    CHECK(!KrnMappingProtectPage(&m,0,3,&maps),"reserved-protect");
    p[1].generation=UINT64_MAX;
    CHECK(!KrnMappingCommitRange(&m,BASE,2*PAGE,3) && p[0].state==KMAP_RESERVED,"commit-preflight");
    p[1].generation=0;
    CHECK(!KrnMappingCommitRange(&m,BASE+PAGE,2*PAGE,3) && !KrnMappingCommitRange(&m,BASE,PAGE,7),"commit-range-permissions");
    CHECK(KrnMappingCommitRange(&m,BASE,PAGE,3) && !b.allocations,"commit-lazy");
    size_t resident,pending;int error;
    CHECK(!KrnMappingPopulate(&m,BASE,2*PAGE,1,&resident,&pending,&error) && !m.in_flight,"populate-reserved-refusal");
    CHECK(KrnMappingFault(&m,BASE,KMAP_WRITE,1,&request,&maps)==KMAP_FAULT_QUEUE,"commit-fault");
    struct KrnMappingRequest stale=request;struct KrnPagerPage page;
    CHECK(KrnPagerPageIn(&pager,PAGE,KPAGER_ZERO,0,0,&page)==KPAGER_OK,"zero-fill-first");
    CHECK(KrnMappingComplete(&request,page.physical,0,&maps),"first-publication");
    for (unsigned i=0;i<PAGE;++i) CHECK(b.bytes[i]==0,"zero-bytes");
    CHECK(KrnMappingFault(&m,BASE,KMAP_WRITE,1,&request,&maps)==KMAP_FAULT_RETRY,"first-write");
    memset(b.bytes,0xa5,PAGE);
    CHECK(KrnMappingCommitRange(&m,BASE,PAGE,1) && p[0].protection==3 && b.bytes[0]==0xa5,"commit-idempotent");
    uintptr_t released=123;uint64_t generation=p[0].generation;
    m.in_flight=1;
    CHECK(!KrnMappingDecommitPage(&m,0,&maps,&released) && released==123 && b.mapped==page.physical,"decommit-pinned");
    m.in_flight=0;b.fail_remove=1;
    CHECK(!KrnMappingDecommitPage(&m,0,&maps,&released) && released==123 && p[0].generation==generation,"decommit-remove-refusal");
    b.fail_remove=0;
    CHECK(KrnMappingDecommitPage(&m,0,&maps,&released) && released==page.physical && !b.mapped && b.removes==1,"decommit-acknowledged");
    CHECK(!m.resident && p[0].generation>generation && KrnMappingLookup(&arena,BASE)==&m,"reservation-retained");
    CHECK(KrnMappingFault(&m,BASE,KMAP_READ,1,&request,&maps)==KMAP_FAULT_TRAP && !m.in_flight,"decommit-no-demand-reload");
    CHECK(!KrnMappingReserveAt(&arena,&collision,BASE,PAGE,0,3,0,&b,extra,1),"decommit-keeps-va");
    CHECK(!KrnMappingComplete(&stale,page.physical,0,&maps),"stale-after-decommit");
    CHECK(KrnMappingCommitRange(&m,BASE,PAGE,3),"recommit");
    CHECK(KrnMappingFault(&m,BASE,KMAP_READ,1,&request,&maps)==KMAP_FAULT_QUEUE,"recommit-fault");
    CHECK(!KrnMappingComplete(&stale,page.physical,0,&maps) && m.in_flight==1,"stale-after-recommit");
    CHECK(KrnPagerPageIn(&pager,PAGE,KPAGER_ZERO,0,0,&page)==KPAGER_OK && KrnMappingComplete(&request,page.physical,0,&maps),"recommit-publication");
    for (unsigned i=0;i<PAGE;++i) CHECK(b.bytes[i]==0,"recommit-zero-bytes");
    CHECK(KrnMappingDecommitPage(&m,0,&maps,&released),"final-decommit");
    CHECK(KrnMappingDecommitPage(&m,0,NULL,&released) && !released,"decommit-idempotent");
    CHECK(KrnMappingBeginClose(&m) && KrnMappingRelease(&arena,&m) && !arena.first,"release-reservation");
    puts("PASS virtual reserve commit decommit zero reuse");return 0;
}
