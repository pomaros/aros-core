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

static void lookup_lifetime_tests(void)
{
    enum { COUNT=192 };
    struct KrnMappingArena a;
    struct KrnMappingObject m[COUNT],right;
    struct KrnMappingPage pages[COUNT][3],rp[1];
    uintptr_t base=0x10000;
    assert(KrnMappingArenaInitialize(&a,base,(COUNT*4+4)*4096,4096));
    for (unsigned i=0;i<COUNT;++i) {
        assert(KrnMappingReserveAt(&a,&m[i],base+i*4*4096,3*4096,0,3,0,&a,pages[i],3));
        assert(KrnMappingLookup(&a,m[i].start+4095)==&m[i]);
    }
    assert(KrnMappingLookup(&a,m[0].start)==&m[0]); /* more objects than cache entries */
    assert(KrnMappingLookup(&a,m[COUNT-1].start)==&m[COUNT-1]);
    uintptr_t old=m[COUNT-1].start;
    assert(KrnMappingBeginClose(&m[COUNT-1]) && KrnMappingRelease(&a,&m[COUNT-1]));
    /* Reusing released storage must not make an unlinked object visible. */
    m[COUNT-1].start=old;m[COUNT-1].length=4096;
    assert(!KrnMappingLookup(&a,old));
    assert(KrnMappingReserveAt(&a,&m[COUNT-1],old,3*4096,0,3,0,&a,pages[COUNT-1],3));
    assert(KrnMappingLookup(&a,old+4096)==&m[COUNT-1]);
    assert(KrnMappingBeginClose(&m[COUNT-1]));
    assert(KrnMappingCut(&a,&m[COUNT-1],old+4096,4096,&right,rp,1)==KMAP_CUT_SPLIT);
    assert(KrnMappingLookup(&a,old)==&m[COUNT-1]);
    assert(!KrnMappingLookup(&a,old+4096));
    assert(KrnMappingLookup(&a,old+8192)==&right);
    assert(KrnMappingBeginClose(&right));
    assert(KrnMappingCut(&a,&right,old+8192,4096,NULL,NULL,0)==KMAP_CUT_RELEASED);
    right.length=4096; /* retired storage cannot survive in the cache */
    assert(!KrnMappingLookup(&a,old+8192));
    for (unsigned i=0;i<COUNT;++i)
        assert(KrnMappingBeginClose(&m[i]) && KrnMappingRelease(&a,&m[i]));
    assert(!a.first && !KrnMappingLookup(&a,base));
}

static unsigned check_lookup_index(struct KrnMappingObject *m, uintptr_t lo,
    uintptr_t hi, unsigned *count)
{
    if (!m) return 0;
    assert(m->start>=lo && m->start<hi && m->length<=hi-m->start);
    unsigned l=check_lookup_index(m->lookup_left,lo,m->start,count);
    unsigned r=check_lookup_index(m->lookup_right,m->start+m->length,hi,count);
    assert(l<=r+1 && r<=l+1);
    assert(m->lookup_height==1+(l>r ? l : r));
    ++*count;return m->lookup_height;
}

static void lookup_index_tests(void)
{
    enum { COUNT=384 };
    struct KrnMappingArena a;
    struct KrnMappingObject m[COUNT];
    struct KrnMappingPage pages[COUNT];
    unsigned char live[COUNT]={0};
    uintptr_t base=0x10000;
    assert(KrnMappingArenaInitialize(&a,base,COUNT*8192,4096));
    /* Sorted insertion is the worst case for an unbalanced search tree. */
    for (unsigned i=0;i<COUNT;++i) {
        assert(KrnMappingReserveAt(&a,&m[i],base+i*8192,4096,0,1,0,&a,&pages[i],1));
        live[i]=1;
    }
    uint32_t random=0x12345678;
    for (unsigned step=0;step<4000;++step) {
        random^=random<<13;random^=random>>17;random^=random<<5;
        unsigned i=random%COUNT;
        /* Repeated root deletion exercises replacement with two children. */
        if (!(step%7) && a.lookup_root) i=(unsigned)(a.lookup_root-m);
        if (live[i]) {
            assert(KrnMappingBeginClose(&m[i]) && KrnMappingRelease(&a,&m[i]));
            live[i]=0;
            assert(!m[i].lookup_left && !m[i].lookup_right && !m[i].lookup_height);
        } else {
            assert(KrnMappingReserveAt(&a,&m[i],base+i*8192,4096,0,1,0,&a,&pages[i],1));
            live[i]=1;
        }
        unsigned count=0,expected=0;
        check_lookup_index(a.lookup_root,base,base+COUNT*8192,&count);
        for (unsigned j=0;j<COUNT;++j) expected+=live[j]!=0;
        assert(count==expected);
        for (unsigned q=0;q<16;++q) {
            random^=random<<13;random^=random>>17;random^=random<<5;
            unsigned slot=random%COUNT,offset=(random>>12)%8192;
            struct KrnMappingObject *want=live[slot] && offset<4096 ? &m[slot] : NULL;
            assert(KrnMappingLookup(&a,base+slot*8192+offset)==want);
        }
    }
    for (unsigned i=0;i<COUNT;++i) if (live[i])
        assert(KrnMappingBeginClose(&m[i]) && KrnMappingRelease(&a,&m[i]));
    assert(!a.first && !a.lookup_root && !KrnMappingLookup(&a,base));
}

static void lookup_index_cut_tests(void)
{
    struct KrnMappingArena a;
    struct KrnMappingObject m[4];
    struct KrnMappingPage pages[4][8];
    const uintptr_t base=0x10000,page=4096;
    assert(KrnMappingArenaInitialize(&a,base,64*page,page));
    for (unsigned i=0;i<3;++i)
        assert(KrnMappingReserveAt(&a,&m[i],base+i*16*page,8*page,0,1,0,&a,pages[i],8));
    uintptr_t start=m[1].start;
    assert(KrnMappingBeginClose(&m[1]));
    assert(KrnMappingCut(&a,&m[1],start,2*page,NULL,NULL,0)==KMAP_CUT_TRIMMED);
    memset(a.recent,0,sizeof(a.recent));
    assert(!KrnMappingLookup(&a,start) && KrnMappingLookup(&a,start+2*page)==&m[1]);
    assert(KrnMappingBeginClose(&m[1]));
    assert(KrnMappingCut(&a,&m[1],start+6*page,2*page,NULL,NULL,0)==KMAP_CUT_TRIMMED);
    assert(KrnMappingBeginClose(&m[1]));
    assert(KrnMappingCut(&a,&m[1],start+3*page,page,NULL,NULL,0)==KMAP_CUT_INVALID);
    assert(KrnMappingCut(&a,&m[1],start+3*page,page,&m[3],pages[3],8)==KMAP_CUT_SPLIT);
    memset(a.recent,0,sizeof(a.recent));
    assert(KrnMappingLookup(&a,start+2*page)==&m[1]);
    assert(!KrnMappingLookup(&a,start+3*page));
    assert(KrnMappingLookup(&a,start+4*page)==&m[3]);
    unsigned count=0;
    check_lookup_index(a.lookup_root,base,base+64*page,&count);
    assert(count==4);
    for (unsigned i=0;i<4;++i)
        assert(KrnMappingBeginClose(&m[i]) && KrnMappingRelease(&a,&m[i]));
    assert(!a.first && !a.lookup_root);
}

int main(void)
{
    object_tests(); reservation_tests(); close_completion_tests(); populate_tests(); fixed_reservation_tests();
    lookup_lifetime_tests();
    lookup_index_tests();
    lookup_index_cut_tests();
    puts("mapping-host PASS coalesced=1000 stale-completion=1 writeback=1 close-drain-no-pte=1 populate-preflight=1");
    return 0;
}
