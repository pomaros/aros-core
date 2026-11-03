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
struct Backing { unsigned allocations,removes,fail_remove,protection;uintptr_t mapped;unsigned char bytes[PAGE] __attribute__((aligned(PAGE))); };
static int allocate(void *opaque,size_t size,struct KrnPagerPage *page)
{
    struct Backing *b=opaque;CHECK(size==PAGE,"page-size");++b->allocations;
    *page=(struct KrnPagerPage){(uintptr_t)b->bytes,b->bytes};return 1;
}
static void release(void *opaque,struct KrnPagerPage *page,size_t size)
{ (void)opaque;(void)page;(void)size; }
static int install(void *opaque,uintptr_t va,uintptr_t pa,unsigned protection)
{
    struct Backing *b=opaque;
    CHECK(va>=BASE && va<BASE+4*PAGE && pa==(uintptr_t)b->bytes &&
        (!(protection&KMAP_EXEC) || protection==KMAP_JIT_CLASS),"install-input");
    b->mapped=pa;b->protection=protection;return 1;
}
static int remove_page(void *opaque,uintptr_t va,uintptr_t pa)
{
    struct Backing *b=opaque;CHECK(va>=BASE && va<BASE+4*PAGE && pa==b->mapped,"remove-input");
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
    /* JIT class: resident pages of the anonymous reservation only. */
    CHECK(!KrnMappingCommitRange(&m,BASE+PAGE,PAGE,KMAP_JIT_CLASS),"jit-lazy-refused");
    CHECK(!KrnMappingCommitResidentPage(&m,1,(uintptr_t)b.bytes,KMAP_JIT|3,&maps) &&
        !KrnMappingCommitResidentPage(&m,1,(uintptr_t)b.bytes,7,&maps) &&
        !KrnMappingCommitResidentPage(&m,1,(uintptr_t)b.bytes,KMAP_JIT_CLASS|16,&maps) && !b.mapped,"jit-partial-refused");
    CHECK(KrnMappingCommitResidentPage(&m,1,(uintptr_t)b.bytes,KMAP_JIT_CLASS,&maps) &&
        b.protection==KMAP_JIT_CLASS && p[1].state==KMAP_DIRTY,"jit-commit");
    CHECK(KrnMappingProtectPage(&m,1,3,&maps) && b.protection==3,"jit-to-data");
    CHECK(KrnMappingProtectPage(&m,1,KMAP_JIT_CLASS,&maps) && b.protection==KMAP_JIT_CLASS,"data-to-jit");
    CHECK(KrnMappingDecommitPage(&m,1,&maps,&released) && released==(uintptr_t)b.bytes,"jit-decommit");
    CHECK(!KrnMappingProtectPage(&m,1,KMAP_JIT_CLASS,&maps),"jit-reserved-refused");
    struct KrnMappingObject file;struct KrnMappingPage fp[1];
    CHECK(!KrnMappingInitialize(&file,BASE+2*PAGE,PAGE,PAGE,0,KMAP_JIT_CLASS,0,&b,fp,1),"jit-object-refused");
    CHECK(KrnMappingReserveAt(&arena,&file,BASE+2*PAGE,PAGE,0,3,0,&b,fp,1),"file-reserve");
    CHECK(KrnMappingFault(&file,BASE+2*PAGE,KMAP_WRITE,1,&request,&maps)==KMAP_FAULT_QUEUE &&
        KrnMappingComplete(&request,(uintptr_t)b.bytes,0,&maps),"file-resident");
    CHECK(!KrnMappingProtectPage(&file,0,KMAP_JIT_CLASS,&maps) && b.protection!=KMAP_JIT_CLASS,"jit-file-refused");
    CHECK(KrnMappingBeginClose(&file) && KrnMappingReleasePage(&file,0,&maps)==(uintptr_t)b.bytes &&
        KrnMappingRelease(&arena,&file),"file-release");
    CHECK(KrnMappingBeginClose(&m) && KrnMappingRelease(&arena,&m) && !arena.first,"release-reservation");
    /* Chunked reservation: no page record until its chunk is attached. */
    struct KrnMappingObject c;struct KrnMappingPage *chunks[2]={0},c0[2],c1[2];
    CHECK(!KrnMappingReserveVirtualChunked(&arena,&c,BASE,3*PAGE,&b,NULL,2) &&
        !KrnMappingReserveVirtualChunked(&arena,&c,BASE,3*PAGE,&b,chunks,0),"chunked-invalid");
    chunks[0]=c0;
    CHECK(!KrnMappingReserveVirtualChunked(&arena,&c,BASE,3*PAGE,&b,chunks,2) && !arena.first,"chunked-dirty-table");
    chunks[0]=NULL;
    CHECK(KrnMappingReserveVirtualChunked(&arena,&c,BASE,3*PAGE,&b,chunks,2) && c.page_count==3 &&
        c.chunk_pages==2 && !c.pages && c.virtual_reservation,"chunked-reserve");
    CHECK(KrnMappingChunkMissing(&c,0) && KrnMappingChunkMissing(&c,2) && !KrnMappingChunkMissing(&c,3) &&
        !KrnMappingPageAt(&c,0) && !KrnMappingPageAt(&c,3),"chunked-missing");
    CHECK(KrnMappingFault(&c,BASE,KMAP_READ,1,&request,&maps)==KMAP_FAULT_TRAP && !c.in_flight,"chunked-fault-trap");
    CHECK(!KrnMappingCommitResidentPage(&c,0,(uintptr_t)b.bytes,3,&maps) && !b.mapped && !c.resident,"chunked-commit-needs-chunk");
    CHECK(!KrnMappingProtectPage(&c,0,3,&maps),"chunked-protect-missing");
    released=123;
    CHECK(KrnMappingDecommitPage(&c,0,&maps,&released) && !released,"chunked-decommit-missing");
    CHECK(!KrnMappingCommitRange(&c,BASE,PAGE,3) &&
        !KrnMappingPopulate(&c,BASE,PAGE,0,&resident,&pending,&error),"chunked-flat-only");
    CHECK(!KrnMappingAttachChunk(&c,3,c0) && !KrnMappingAttachChunk(&c,0,NULL) && KrnMappingChunkMissing(&c,0),"chunked-attach-invalid");
    CHECK(KrnMappingAttachChunk(&c,1,c0) && !KrnMappingAttachChunk(&c,0,c1) && KrnMappingPageAt(&c,1)==&c0[1] &&
        c0[0].state==KMAP_RESERVED && !c0[0].physical && !KrnMappingChunkMissing(&c,0) && KrnMappingChunkMissing(&c,2),"chunked-attach");
    CHECK(KrnMappingCommitResidentPage(&c,1,(uintptr_t)b.bytes,3,&maps) && c0[1].state==KMAP_DIRTY &&
        c.resident==1 && b.mapped==(uintptr_t)b.bytes,"chunked-commit");
    CHECK(KrnMappingFault(&c,BASE+PAGE,KMAP_WRITE,1,&request,&maps)==KMAP_FAULT_RETRY,"chunked-fault-resident");
    CHECK(KrnMappingProtectPage(&c,1,1,&maps) && b.protection==1,"chunked-protect");
    CHECK(KrnMappingAttachChunk(&c,2,c1) && KrnMappingPageAt(&c,2)==&c1[0] && !KrnMappingChunkMissing(&c,2),"chunked-attach-tail");
    CHECK(KrnMappingBeginClose(&c) && !KrnMappingRelease(&arena,&c) &&
        KrnMappingCut(&arena,&c,BASE+2*PAGE,PAGE,NULL,NULL,0)==KMAP_CUT_INVALID,"chunked-no-cut");
    CHECK(!KrnMappingReleasePage(&c,0,&maps) && KrnMappingReleasePage(&c,1,&maps)==(uintptr_t)b.bytes &&
        KrnMappingRelease(&arena,&c) && !arena.first,"chunked-release");
    puts("PASS virtual reserve commit decommit zero reuse");return 0;
}
