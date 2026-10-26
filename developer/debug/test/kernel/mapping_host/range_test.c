/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mapping.h"
#define PAGE 4096U
#define BASE 0x100000U
static unsigned installs,removals;
static int install(void *context,uintptr_t va,uintptr_t pa,unsigned protection)
{ (void)context;(void)va;(void)pa;(void)protection; installs++;return 1; }
static int remove_translation(void *context,uintptr_t va,uintptr_t pa)
{ (void)context; assert(va>=BASE && pa>=0x800000); removals++;return 1; }
static const struct KrnMappingOps ops={NULL,install,remove_translation};
static void fill(struct KrnMappingObject *m,size_t page)
{
    struct KrnMappingRequest request;
    assert(KrnMappingFault(m,m->start+page*PAGE,KMAP_READ,1,&request,&ops)==KMAP_FAULT_QUEUE);
    assert(KrnMappingComplete(&request,0x800000+page*PAGE,0,&ops));
}
static void release_pages(struct KrnMappingObject *m,size_t first,size_t count)
{
    assert(KrnMappingBeginClose(m));
    for (size_t i=first;i<first+count;++i)
        if (m->pages[i].physical)
            assert(KrnMappingReleasePage(m,i,&ops));
}
int main(void)
{
    struct KrnMappingArena arena;
    struct KrnMappingObject object,right,hole,neighbor;
    struct KrnMappingPage pages[8],rpages[8],hpages[2],neighbor_pages[1];
    assert(KrnMappingArenaInitialize(&arena,BASE,64*PAGE,PAGE));
    assert(KrnMappingReserve(&arena,&object,6*PAGE,3*PAGE,KMAP_READ|KMAP_WRITE,0,(void *)7,pages,8));
    assert(KrnMappingReserve(&arena,&neighbor,PAGE,0,KMAP_READ,0,(void *)11,neighbor_pages,1));
    for (size_t i=0;i<6;++i) fill(&object,i);
    assert(KrnMappingProtectPage(&object,1,KMAP_READ,&ops));
    struct KrnMappingRequest request;
    assert(KrnMappingFault(&object,BASE+4*PAGE,KMAP_WRITE,1,&request,&ops)==KMAP_FAULT_RETRY);
    assert(pages[4].state==KMAP_DIRTY);
    assert(KrnMappingBeginClose(&object));
    assert(KrnMappingCut(&arena,&object,BASE+2*PAGE,2*PAGE,&right,rpages,8)==KMAP_CUT_INVALID);
    assert(object.resident==6 && object.next==&neighbor);
    release_pages(&object,2,2);
    unsigned installed=installs,removed=removals;
    assert(KrnMappingCut(&arena,&object,BASE+2*PAGE,2*PAGE,&right,rpages,8)==KMAP_CUT_SPLIT);
    assert(installs==installed && removals==removed);
    assert(object.length==2*PAGE && object.resident==2 && !object.closing);
    assert(object.offset==3*PAGE && object.pages[1].protection==KMAP_READ);
    assert(object.next==&right && right.next==&neighbor);
    assert(right.start==BASE+4*PAGE && right.length==2*PAGE && right.offset==7*PAGE);
    assert(right.owner==(void *)7 && right.resident==2 && !right.closing);
    assert(right.pages[0].physical==0x804000 && right.pages[0].state==KMAP_DIRTY);
    assert(right.pages[1].physical==0x805000);
    assert(KrnMappingLookup(&arena,BASE+PAGE)==&object);
    assert(KrnMappingLookup(&arena,BASE+3*PAGE)==NULL);
    assert(KrnMappingLookup(&arena,BASE+4*PAGE)==&right);
    assert(KrnMappingReserve(&arena,&hole,2*PAGE,0,KMAP_READ,0,(void *)9,hpages,2));
    assert(hole.start==BASE+2*PAGE && hole.resident==0);
    /* Front trim moves only metadata and advances the file offset. */
    release_pages(&right,0,1);
    assert(KrnMappingCut(&arena,&right,BASE+4*PAGE,PAGE,NULL,NULL,0)==KMAP_CUT_TRIMMED);
    assert(right.start==BASE+5*PAGE && right.offset==8*PAGE && right.length==PAGE);
    assert(right.resident==1 && right.pages[0].physical==0x805000);
    /* Tail trim and full removal leave the unrelated hole/new mapping live. */
    release_pages(&object,1,1);
    assert(KrnMappingCut(&arena,&object,BASE+PAGE,PAGE,NULL,NULL,0)==KMAP_CUT_TRIMMED);
    assert(object.length==PAGE && object.resident==1 && object.next==&hole);
    release_pages(&object,0,1);
    assert(KrnMappingCut(&arena,&object,BASE,PAGE,NULL,NULL,0)==KMAP_CUT_RELEASED);
    assert(arena.first==&hole && hole.next==&right && object.next==NULL);
    /* Invalid storage or live I/O cannot change any mapping metadata. */
    struct KrnMappingObject lazy,extra;
    struct KrnMappingPage lazy_pages[8],extra_pages[8];
    assert(KrnMappingReserve(&arena,&lazy,8*PAGE,0,KMAP_READ,0,(void *)3,lazy_pages,8));
    assert(KrnMappingBeginClose(&lazy));
    uintptr_t start=lazy.start;
    assert(KrnMappingCut(&arena,&lazy,start+2*PAGE,2*PAGE,&extra,extra_pages,3)==KMAP_CUT_INVALID);
    assert(lazy.length==8*PAGE && lazy.closing && !lazy.resident);
    assert(KrnMappingCut(&arena,&lazy,start+2*PAGE,2*PAGE,&extra,lazy_pages+1,8)==KMAP_CUT_INVALID);
    lazy.in_flight=1;
    assert(KrnMappingCut(&arena,&lazy,start+2*PAGE,2*PAGE,&extra,extra_pages,8)==KMAP_CUT_INVALID);
    lazy.in_flight=0;
    lazy_pages[7].generation=UINT64_MAX;
    assert(KrnMappingCut(&arena,&lazy,start+2*PAGE,2*PAGE,&extra,extra_pages,8)==KMAP_CUT_INVALID);
    lazy_pages[7].generation=0;
    installed=installs;removed=removals;
    assert(KrnMappingCut(&arena,&lazy,start+2*PAGE,2*PAGE,&extra,extra_pages,8)==KMAP_CUT_SPLIT);
    assert(installs==installed && removals==removed && !lazy.resident && !extra.resident);
    assert(lazy.length==2*PAGE && extra.length==4*PAGE && extra.offset==4*PAGE);
    /* A completed request from old indices cannot claim a later page load. */
    struct KrnMappingRequest stale={&extra,0,0},live;
    assert(KrnMappingFault(&extra,extra.start,KMAP_READ,1,&live,&ops)==KMAP_FAULT_QUEUE);
    assert(!KrnMappingComplete(&stale,0x900000,0,&ops));
    assert(KrnMappingComplete(&live,0x900000,0,&ops));
    struct KrnMappingObject front;
    struct KrnMappingPage front_pages[4];
    assert(KrnMappingReserve(&arena,&front,4*PAGE,10*PAGE,KMAP_READ,0,(void *)13,front_pages,4));
    front_pages[0].generation=6;
    front_pages[2].generation=5;
    assert(KrnMappingBeginClose(&front));
    assert(KrnMappingCut(&arena,&front,front.start,2*PAGE,NULL,NULL,0)==KMAP_CUT_TRIMMED);
    assert(front.offset==12*PAGE && front.page_count==2);
    stale=(struct KrnMappingRequest){&front,0,6};
    assert(KrnMappingFault(&front,front.start,KMAP_READ,1,&live,&ops)==KMAP_FAULT_QUEUE);
    assert(!KrnMappingComplete(&stale,0xa00000,0,&ops));
    assert(KrnMappingComplete(&live,0xa00000,0,&ops));
    /* A real pending page-in must drain before a cut. Cancelling it must
     * publish no PTE, and requests from before the cut stay stale on refault. */
    struct KrnMappingObject cancelled, cancelled_right;
    struct KrnMappingPage cancelled_pages[3], cancelled_right_pages[1];
    struct KrnMappingRequest pending_left,pending_right;
    assert(KrnMappingReserve(&arena,&cancelled,3*PAGE,20*PAGE,KMAP_READ,0,
        (void *)15,cancelled_pages,3));
    uintptr_t cancelled_start=cancelled.start;
    assert(KrnMappingFault(&cancelled,cancelled_start,KMAP_READ,1,&pending_left,&ops)==KMAP_FAULT_QUEUE);
    assert(KrnMappingFault(&cancelled,cancelled_start+2*PAGE,KMAP_READ,1,&pending_right,&ops)==KMAP_FAULT_QUEUE);
    assert(!KrnMappingBeginClose(&cancelled));
    assert(KrnMappingCut(&arena,&cancelled,cancelled_start+PAGE,PAGE,
        &cancelled_right,cancelled_right_pages,1)==KMAP_CUT_INVALID);
    installed=installs;removed=removals;
    assert(!KrnMappingComplete(&pending_left,0xb00000,0,&ops));
    assert(!KrnMappingComplete(&pending_right,0xb02000,0,&ops));
    assert(!cancelled.in_flight && !cancelled.resident);
    assert(installs==installed && removals==removed);
    assert(KrnMappingCut(&arena,&cancelled,cancelled_start+PAGE,PAGE,
        &cancelled_right,cancelled_right_pages,1)==KMAP_CUT_SPLIT);
    assert(cancelled.length==PAGE && cancelled_right.length==PAGE);
    assert(cancelled_right.offset==22*PAGE && cancelled_right.owner==(void *)15);
    assert(!KrnMappingComplete(&pending_right,0xb02000,0,&ops));
    assert(KrnMappingFault(&cancelled,cancelled.start,KMAP_READ,1,&live,&ops)==KMAP_FAULT_QUEUE);
    assert(!KrnMappingComplete(&pending_left,0xb00000,0,&ops));
    assert(cancelled.in_flight==1 && !cancelled.resident);
    assert(KrnMappingComplete(&live,0xb00000,0,&ops));
    assert(cancelled.resident==1 && installs==installed+1);
    puts("mapping cut: middle/front/tail/full / offsets / ownership / lazy / stale generation PASS");
    return 0;
}
