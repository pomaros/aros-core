/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../../../rom/kernel/mapping/mapping.h"

struct Memory { unsigned installs, removes, protection, fail; uintptr_t physical; };
static int install(void *v, uintptr_t va, uintptr_t pa, unsigned prot)
{
    struct Memory *m=v;
    assert(va && pa);
    if (m->fail) return 0;
    ++m->installs; m->protection=prot; m->physical=pa; return 1;
}
static int remove_page(void *v, uintptr_t va, uintptr_t pa)
{
    struct Memory *m=v;
    assert(va && pa==m->physical);
    if (m->fail) return 0;
    ++m->removes; return 1;
}
static void object_tests(void)
{
    struct KrnMappingObject m;
    struct KrnMappingPage pages[3];
    struct KrnMappingRequest r, duplicate, wb;
    struct Memory memory={0};
    struct KrnMappingOps ops={&memory, install, remove_page};
    assert(KrnMappingInitialize(&m, 0x100000, 3*4096, 4096, 0, 3, 1,
        &m, pages, 3));
    assert(!m.resident && !m.in_flight && !memory.installs);
    assert(KrnMappingFault(&m, m.start, 1, 0, &r, &ops)==KMAP_FAULT_FATAL);
    assert(KrnMappingFault(&m, m.start, 1, 1, &r, &ops)==KMAP_FAULT_QUEUE);
    for (unsigned i=0;i<1000;++i)
        assert(KrnMappingFault(&m, m.start, 1, 1, &duplicate, &ops)==KMAP_FAULT_WAIT);
    assert(m.in_flight==1 && !m.resident);
    assert(KrnMappingComplete(&r, 0x200000, 0, &ops));
    assert(!KrnMappingComplete(&duplicate, 0x300000, 0, &ops));
    assert(m.resident==1 && !m.in_flight && memory.protection==1);
    assert(KrnMappingProtectPage(&m,0,1,&ops));
    assert(KrnMappingFault(&m,m.start,2,1,&r,&ops)==KMAP_FAULT_TRAP);
    assert(KrnMappingProtectPage(&m,0,3,&ops));
    assert(memory.protection==1); /* Clean tracking survives mprotect RW. */
    assert(KrnMappingFault(&m, m.start, 2, 1, &r, &ops)==KMAP_FAULT_RETRY);
    assert(pages[0].state==KMAP_DIRTY && memory.protection==3);
    assert(KrnMappingBeginWriteback(&m, 0, &wb, &ops));
    assert(memory.protection==1);
    assert(KrnMappingFault(&m, m.start, 2, 1, &r, &ops)==KMAP_FAULT_WAIT);
    assert(!KrnMappingComplete(&r, 0x300000, 0, &ops));
    assert(KrnMappingEndWriteback(&wb, -5));
    assert(KrnMappingFault(&m, m.start, 2, 1, &r, &ops)==KMAP_FAULT_RETRY);
    assert(memory.protection==3);
    assert(KrnMappingBeginWriteback(&m, 0, &r, &ops));
    assert(!KrnMappingEndWriteback(&wb, 0)); /* old generation */
    assert(!KrnMappingBeginClose(&m));
    assert(!KrnMappingReleasePage(&m, 0, &ops));
    assert(KrnMappingEndWriteback(&r, -5));
    assert(KrnMappingBeginClose(&m));
    assert(!KrnMappingReleasePage(&m, 0, &ops)); /* no lost dirty write */
    assert(KrnMappingBeginWriteback(&m, 0, &r, &ops));
    assert(KrnMappingEndWriteback(&r, 0));
    memory.fail=1;
    assert(!KrnMappingReleasePage(&m, 0, &ops));
    assert(m.resident==1);
    memory.fail=0;
    assert(KrnMappingReleasePage(&m, 0, &ops)==0x200000);
    assert(!m.resident && memory.removes==1);
    assert(KrnMappingFault(&m, m.start, 1, 1, &r, &ops)==KMAP_FAULT_TRAP);

    assert(KrnMappingInitialize(&m, 0x100000, 4096, 4096, 0, 3, 0,&m,pages,3));
    assert(KrnMappingFault(&m, m.start, 1, 1, &r, &ops)==KMAP_FAULT_QUEUE);
    assert(!KrnMappingComplete(&r, 0x200000, -5, &ops));
    assert(KrnMappingFault(&m, m.start, 1, 1, &r, &ops)==KMAP_FAULT_TRAP);
    assert(!m.resident && !m.in_flight);
    assert(!KrnMappingInitialize(&m, UINTPTR_MAX-4095, 8192,4096,0,1,0,&m,pages,3));
    assert(!KrnMappingInitialize(&m, 0x100000,4096,4096,UINT64_MAX-4095,1,0,&m,pages,3));
}

static void reservation_tests(void)
{
    struct KrnMappingArena arena;
    struct KrnMappingObject a,b,c;
    struct KrnMappingPage pa[2],pb[2],pc[2];
    assert(KrnMappingArenaInitialize(&arena,0x100000,4*4096,4096));
    assert(KrnMappingReserve(&arena,&a,2*4096,0,3,0,&a,pa,2));
    assert(KrnMappingReserve(&arena,&b,2*4096,0,1,0,&b,pb,2));
    assert(!KrnMappingReserve(&arena,&c,4096,0,1,0,&c,pc,2));
    assert(!KrnMappingReserve(&arena,&a,4096,0,1,0,&a,pa,2));
    assert(a.start==0x100000 && b.start==0x102000);
    assert(KrnMappingLookup(&arena,0x101fff)==&a);
    assert(KrnMappingLookup(&arena,0x102000)==&b);
    assert(!KrnMappingLookup(&arena,0x104000));
    assert(KrnMappingBeginClose(&a));
    assert(KrnMappingRelease(&arena,&a));
    assert(KrnMappingReserve(&arena,&c,4096,0,1,0,&c,pc,2));
    assert(c.start==0x100000 && !KrnMappingLookup(&arena,0x101000));
    assert(KrnMappingReserve(&arena,&a,4096,0,3,0,&a,pa,2));
    assert(a.start==0x101000);
    assert(!a.resident && !b.resident && !c.resident);
}

static void close_completion_tests(void)
{
    struct KrnMappingArena arena;
    struct KrnMappingObject m;
    struct KrnMappingPage pages[2];
    struct KrnMappingRequest first, second, stale;
    struct Memory memory={0};
    struct KrnMappingOps ops={&memory,install,remove_page};
    assert(KrnMappingArenaInitialize(&arena,0x100000,2*4096,4096));
    assert(KrnMappingReserve(&arena,&m,2*4096,0,3,0,&m,pages,2));
    assert(KrnMappingFault(&m,m.start,1,1,&first,&ops)==KMAP_FAULT_QUEUE);
    assert(KrnMappingFault(&m,m.start+4096,1,1,&second,&ops)==KMAP_FAULT_QUEUE);
    stale=first;
    assert(m.in_flight==2 && !m.resident);
    assert(!KrnMappingBeginClose(&m));
    assert(!KrnMappingRelease(&arena,&m));
    /* Closing wins the race after page fill but before PTE publication. The
     * rejected completion leaves its physical page with the pager, no install. */
    assert(!KrnMappingComplete(&first,0x200000,0,&ops));
    assert(!memory.installs && !m.resident && m.in_flight==1);
    assert(pages[0].state==KMAP_ABSENT && !pages[0].physical);
    assert(!KrnMappingComplete(&stale,0x300000,0,&ops));
    assert(m.in_flight==1); /* duplicate cannot drain another page */
    assert(KrnMappingFault(&m,m.start,1,1,&stale,&ops)==KMAP_FAULT_TRAP);
    /* Cancellation also drains a failed page-in with no PTE callback. */
    assert(!KrnMappingComplete(&second,0,-5,NULL));
    assert(!m.in_flight && !m.resident && !memory.installs);
    assert(pages[1].state==KMAP_ABSENT && !pages[1].physical);
    assert(!KrnMappingComplete(&second,0,-5,NULL));
    assert(KrnMappingBeginClose(&m));
    assert(KrnMappingRelease(&arena,&m));
    assert(arena.first==NULL && !KrnMappingLookup(&arena,0x100000));
}

static void populate_tests(void)
{
    struct KrnMappingObject m;
    struct KrnMappingPage pages[3];
    struct KrnMappingRequest r, stale;
    struct Memory memory={0};
    struct KrnMappingOps ops={&memory,install,remove_page};
    size_t resident=99,pending=99; int error=99;
    assert(KrnMappingInitialize(&m,0x100000,3*4096,4096,0,3,0,&m,pages,3));
    pages[2].generation=UINT64_MAX;
    assert(!KrnMappingPopulate(&m,m.start,m.length,1,&resident,&pending,&error));
    assert(error && !m.in_flight && pages[0].state==KMAP_ABSENT);
    assert(KrnMappingPopulate(&m,m.start,m.length,0,&resident,&pending,&error));
    assert(!resident && !pending); /* validation alone never queues */
    pages[2].generation=0;
    assert(KrnMappingPopulate(&m,m.start,m.length,1,&resident,&pending,&error));
    assert(m.in_flight==3 && pending==3 && !resident);
    assert(KrnMappingFault(&m,m.start,1,1,&r,&ops)==KMAP_FAULT_WAIT);
    stale=r; ++stale.generation;
    assert(!KrnMappingComplete(&stale,0x200000,0,&ops));
    assert(m.in_flight==3 && !memory.installs);
    assert(KrnMappingComplete(&r,0x200000,0,&ops));
    assert(KrnMappingPopulate(&m,m.start,m.length,1,&resident,&pending,&error));
    assert(m.in_flight==2 && resident==1 && pending==2); /* no double queue */
    assert(!KrnMappingBeginClose(&m));
    for (size_t i=1;i<3;i++) {
        assert(KrnMappingFault(&m,m.start+i*4096,1,1,&r,&ops)==KMAP_FAULT_TRAP);
        r=(struct KrnMappingRequest){&m,i,pages[i].generation};
        assert(!KrnMappingComplete(&r,0x200000+i*4096,0,&ops));
    }
    assert(!m.in_flight && m.resident==1 && memory.installs==1);
    assert(KrnMappingReleasePage(&m,0,&ops)==0x200000);
    assert(!m.resident && memory.removes==1);
}

static void fixed_reservation_tests(void)
{
    const uintptr_t base=0x100000000ULL;
    struct KrnMappingArena arena, other;
    struct KrnMappingObject middle, first, last, reject, snapshot, peer;
    struct KrnMappingPage mp[2], fp[2], lp[2], rp[2], saved[2], pp[2];
    assert(KrnMappingArenaInitialize(&arena,base,8*4096,4096));
    assert(KrnMappingArenaInitialize(&other,base,8*4096,4096));
    assert(KrnMappingReserveAt(&arena,&middle,base+3*4096,2*4096,0,3,0,&arena,mp,2));
    assert(KrnMappingReserveAt(&arena,&last,base+6*4096,2*4096,0,1,0,&arena,lp,2));
    assert(KrnMappingReserveAt(&arena,&first,base,2*4096,0,0,0,&arena,fp,2));
    assert(arena.first==&first && first.next==&middle && middle.next==&last && !last.next);
    assert(!KrnMappingLookup(&arena,base+2*4096));
    assert(KrnMappingLookup(&arena,base+4*4096)==&middle);
    assert(KrnMappingReserveAt(&other,&peer,base+3*4096,2*4096,0,3,0,&other,pp,2));
    assert(peer.owner!=middle.owner && peer.pages!=middle.pages);
    memset(&reject,0xa5,sizeof(reject));snapshot=reject;
    memset(rp,0x5a,sizeof(rp));memcpy(saved,rp,sizeof(saved));
    assert(!KrnMappingReserveAt(&arena,&reject,base+2*4096,2*4096,0,3,0,&arena,rp,2));
    assert(!memcmp(&reject,&snapshot,sizeof(reject)) && !memcmp(rp,saved,sizeof(rp)));
    /* Range crossing the arena boundary must be refused before publication. */
    assert(!KrnMappingReserveAt(&other,&reject,base+7*4096,2*4096,0,3,0,&other,rp,2));
    assert(!KrnMappingReserveAt(&arena,&reject,base+2*4096+1,4096,0,3,0,&arena,rp,2));
    assert(!KrnMappingReserveAt(&arena,&reject,base+2*4096,4096,0,7,0,&arena,rp,2));
    assert(!KrnMappingReserveAt(&arena,&reject,base+2*4096,4096,0,3,0,&arena,rp,0));
    assert(!KrnMappingReserveAt(&arena,&last,base+2*4096,4096,0,3,0,&arena,rp,2));
    assert(!memcmp(&reject,&snapshot,sizeof(reject)) && !memcmp(rp,saved,sizeof(rp)));
    assert(last.start==base+6*4096 && middle.next==&last);
    assert(KrnMappingBeginClose(&middle) && KrnMappingRelease(&arena,&middle));
    assert(KrnMappingReserveAt(&arena,&reject,base+2*4096,3*4096,0,3,0,&arena,rp,2)==0);
    assert(KrnMappingReserveAt(&arena,&reject,base+2*4096,2*4096,0,3,0,&arena,rp,2));
    assert(first.next==&reject && reject.next==&last);
    assert(rp[0].state==KMAP_ABSENT && !rp[0].physical && !reject.resident);
    struct KrnMappingArena high;
    uintptr_t highbase=(UINTPTR_MAX & ~(uintptr_t)4095)-4*4096;
    assert(KrnMappingArenaInitialize(&high,highbase,4*4096,4096));
    assert(!KrnMappingReserveAt(&high,&middle,highbase+3*4096,4*4096,0,3,0,&high,mp,2));
    assert(!high.first);
}

int main(void)
{
    object_tests(); reservation_tests(); close_completion_tests(); populate_tests(); fixed_reservation_tests();
    puts("mapping-host PASS coalesced=1000 stale-completion=1 writeback=1 close-drain-no-pte=1 populate-preflight=1");
    return 0;
}
