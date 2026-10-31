/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Execute the production C walkers against a synthetic physical table ledger.
 * No host pointer is passed as a guest physical address.
 */
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mapping.h"

#define MAX_TABLES 16
#define GUARD (UINT64_C(1) << 58)
#define CONTIG (UINT64_C(1) << 52)
#define PXN (UINT64_C(1) << 53)
#define UXN (UINT64_C(1) << 54)
#define PA_HIGH (UINT64_C(1) << 48)
#define DATA_PA UINT64_C(0x40000000)
#define SENTINEL UINT64_C(0xdeadbeef)
struct Table { uint64_t pa, *words; };
struct Fixture {
    struct Table tables[MAX_TABLES];
    unsigned count, page, va_bits, allocate_calls, fail_at;
    unsigned clean_calls, invalidations, synchronizations;
    uint64_t tcr, root, va, next_pa, deny_pa, last_clean;
    uint64_t sync_descriptor;
    int dirty, malformed_allocation;
};
static unsigned cases;
static struct Table *find(struct Fixture *f, uint64_t pa)
{
    for (unsigned i=0;i<f->count;++i)
        if (f->tables[i].pa==pa) return &f->tables[i];
    return NULL;
}
static uint64_t new_table(struct Fixture *f)
{
    assert(f->count<MAX_TABLES);
    uint64_t pa=f->next_pa;
    f->next_pa+=UINT64_C(1)<<f->page;
    struct Table *t=&f->tables[f->count++];
    t->pa=pa;t->words=malloc((size_t)1<<f->page);assert(t->words);
    memset(t->words,0xa5,(size_t)1<<f->page);
    return pa;
}
static uint64_t *resolve(void *context, uint64_t pa)
{
    struct Fixture *f=context;
    if (pa==f->deny_pa) return NULL;
    struct Table *t=find(f,pa);return t ? t->words : NULL;
}
static uint64_t allocate(void *context, unsigned page)
{
    struct Fixture *f=context;assert(page==f->page);++f->allocate_calls;
    if (f->fail_at && f->allocate_calls==f->fail_at) return 0;
    uint64_t pa=new_table(f);
    return f->malformed_allocation ? pa|1 : pa;
}
static void clean(void *context, const void *address, size_t size)
{
    struct Fixture *f=context;int found=0;
    for (unsigned i=0;i<f->count;++i) {
        uintptr_t start=(uintptr_t)f->tables[i].words,p=(uintptr_t)address;
        if (p>=start && p-start<((size_t)1<<f->page)) {
            assert(size<=((size_t)1<<f->page)-(p-start));found=1;break;
        }
    }
    assert(found);++f->clean_calls;f->dirty=1;
    if (size==sizeof(uint64_t)) f->last_clean=*(const uint64_t *)address;
}
static void invalidate(void *context)
{
    struct Fixture *f=context;
    assert(f->dirty);f->dirty=0;++f->invalidations;
}
static uint64_t *slot_at(struct Fixture *f, uint64_t va, unsigned target)
{
    unsigned stride=f->page-3,first=3-(f->va_bits-f->page-1)/stride;
    uint64_t *table=find(f,f->root)->words;
    for (unsigned level=first;level<=target;++level) {
        unsigned shift=f->page+stride*(3-level);
        uint64_t *slot=&table[(va>>shift)&(((uint64_t)1<<stride)-1)];
        if (level==target) return slot;
        struct Table *next=find(f,*slot&UINT64_C(0x0000ffffffffc000));
        if (f->page==12) next=find(f,*slot&UINT64_C(0x0000fffffffff000));
        assert(next);table=next->words;
    }
    abort();
}
static void synchronize(void *context, uint64_t va, size_t size)
{
    struct Fixture *f=context;assert(size==((size_t)1<<f->page));
    /* Cache maintenance must use a readable, valid, NX alias before EXEC. */
    uint64_t descriptor=*slot_at(f,va,3);
    assert((descriptor&3)==3 && (descriptor&(PXN|UXN))==(PXN|UXN));
    assert(!f->dirty);f->sync_descriptor=descriptor;++f->synchronizations;
}
static struct AArch64ProtectionOps ops(struct Fixture *f)
{
    return (struct AArch64ProtectionOps){f,resolve,allocate,clean,invalidate,synchronize,NULL};
}
static void init(struct Fixture *f,unsigned page,unsigned va_bits)
{
    *f=(struct Fixture){.page=page,.va_bits=va_bits,.next_pa=UINT64_C(0x10000)};
    f->root=new_table(f);memset(find(f,f->root)->words,0,(size_t)1<<page);
    f->tcr=(64-va_bits)|(UINT64_C(5)<<32)|(page==14 ? UINT64_C(2)<<14 : 0);
    f->va=(UINT64_C(1)<<(va_bits-1))+(UINT64_C(3)<<page);
}
static void finish(struct Fixture *f)
{
    for (unsigned i=0;i<f->count;++i) free(f->tables[i].words);
    ++cases;
}
static unsigned reachable(struct Fixture *f,uint64_t pa,unsigned depth)
{
    assert(depth<4);struct Table *table=find(f,pa);assert(table);unsigned n=1;
    for (size_t i=0;i<((size_t)1<<(f->page-3));++i) {
        uint64_t d=table->words[i];
        if ((d&3)==3) {
            uint64_t mask=f->page==12 ? UINT64_C(0x0000fffffffff000):UINT64_C(0x0000ffffffffc000);
            struct Table *child=find(f,d&mask);
            if (child) n+=reachable(f,child->pa,depth+1);
        }
    }
    return n;
}
static void basic(unsigned page,unsigned bits)
{
    struct Fixture f;init(&f,page,bits);struct AArch64ProtectionOps o=ops(&f);
    unsigned rw=AARCH64_PROTECT_READ|AARCH64_PROTECT_WRITE|AARCH64_PROTECT_USER;
    assert(AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,rw,1));
    uint64_t d=*slot_at(&f,f.va,3),pa=SENTINEL;
    assert((d&3)==3 && (d&((uint64_t)7<<2))==(UINT64_C(1)<<2));
    assert((d&(PXN|UXN))==(PXN|UXN) && !(d&(UINT64_C(1)<<7)));
    assert(AArch64MappingPhysical(&o,f.root,f.tcr,f.va,&pa) && pa==DATA_PA);
    assert(AArch64MappingPhysical(&o,f.root|UINT64_C(1)|(UINT64_C(0xa5)<<48),f.tcr,f.va,&pa) && pa==DATA_PA);
    assert(AArch64TranslateResident(&o,f.root,f.tcr,f.va+123,&pa) && pa==DATA_PA+123);
    unsigned count=f.count,inv=f.invalidations;
    assert(!AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA+((uint64_t)1<<page),rw,1));
    assert(f.count==count && f.invalidations==inv && *slot_at(&f,f.va,3)==d);
    assert(!AArch64UnmapPage(&o,f.root,f.tcr,f.va,DATA_PA+((uint64_t)1<<page)));
    assert(*slot_at(&f,f.va,3)==d);
    assert(!AArch64RemapPage(&o,f.root,f.tcr,f.va,DATA_PA+((uint64_t)1<<page),0));
    assert(*slot_at(&f,f.va,3)==d);
    assert(AArch64RemapPage(&o,f.root,f.tcr,f.va,DATA_PA,AARCH64_PROTECT_USER));
    d=*slot_at(&f,f.va,3);assert(!(d&1) && (d&GUARD));
    assert(AArch64MappingPhysical(&o,f.root,f.tcr,f.va,&pa) && pa==DATA_PA);
    pa=SENTINEL;assert(!AArch64TranslateResident(&o,f.root,f.tcr,f.va,&pa) && pa==SENTINEL);
    assert(!AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,rw,1));
    assert(AArch64RemapPage(&o,f.root,f.tcr,f.va,DATA_PA,AARCH64_PROTECT_READ|AARCH64_PROTECT_USER));
    assert((*slot_at(&f,f.va,3)&(UINT64_C(1)<<7)) && !(*slot_at(&f,f.va,3)&GUARD));
    assert(AArch64RemapPage(&o,f.root,f.tcr,f.va,DATA_PA,rw));
    /* A second leaf on the same path must reuse every intermediate table. */
    uint64_t other=f.va+((uint64_t)1<<page);
    assert(AArch64MapPage(&o,f.root,f.tcr,other,DATA_PA+((uint64_t)1<<page),0,1));
    assert(f.count==count && AArch64UnmapPage(&o,f.root,f.tcr,other,DATA_PA+((uint64_t)1<<page)));
    assert(AArch64UnmapPage(&o,f.root,f.tcr,f.va,DATA_PA));
    assert(!*slot_at(&f,f.va,3) && reachable(&f,f.root,0)==f.count);
    assert(!AArch64UnmapPage(&o,f.root,f.tcr,f.va,DATA_PA));
    pa=SENTINEL;assert(!AArch64MappingPhysical(&o,f.root,f.tcr,f.va,&pa) && pa==SENTINEL);
    assert(!f.dirty && f.invalidations>0 && f.clean_calls>=f.invalidations);
    finish(&f);
}
static void permissions(unsigned page)
{
    const unsigned good[]={0,AARCH64_PROTECT_USER,AARCH64_PROTECT_READ,
        AARCH64_PROTECT_READ|AARCH64_PROTECT_WRITE,
        AARCH64_PROTECT_READ|AARCH64_PROTECT_EXEC,
        AARCH64_PROTECT_READ|AARCH64_PROTECT_EXEC|AARCH64_PROTECT_USER,
        AARCH64_PROTECT_READ|AARCH64_PROTECT_EXEC|AARCH64_PROTECT_USER|AARCH64_PROTECT_EXEC_BOTH};
    const unsigned bad[]={2,4,6,7,32,63,64,16,17,21};
    for (unsigned k=0;k<sizeof(good)/sizeof(*good);++k) {
        struct Fixture f;init(&f,page,48);struct AArch64ProtectionOps o=ops(&f);
        assert(AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,good[k],7));
        uint64_t d=*slot_at(&f,f.va,3);
        if (good[k]&AARCH64_PROTECT_EXEC) {
            assert(f.synchronizations==1);
            if (good[k]&AARCH64_PROTECT_EXEC_BOTH) assert(!(d&(PXN|UXN)));
            else if (good[k]&AARCH64_PROTECT_USER) assert((d&PXN) && !(d&UXN));
            else assert(!(d&PXN) && (d&UXN));
        }
        assert(AArch64RemapPage(&o,f.root,f.tcr,f.va,DATA_PA,0));
        assert(AArch64RemapPage(&o,f.root,f.tcr,f.va,DATA_PA,good[k]));
        assert(AArch64UnmapPage(&o,f.root,f.tcr,f.va,DATA_PA));finish(&f);
    }
    for (unsigned k=0;k<sizeof(bad)/sizeof(*bad);++k) {
        struct Fixture f;init(&f,page,48);struct AArch64ProtectionOps o=ops(&f);
        assert(!AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,bad[k],1));
        assert(f.count==1 && !f.clean_calls && !f.allocate_calls);
        assert(AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,1,1));
        uint64_t d=*slot_at(&f,f.va,3);
        assert(!AArch64RemapPage(&o,f.root,f.tcr,f.va,DATA_PA,bad[k]));
        assert(*slot_at(&f,f.va,3)==d);finish(&f);
    }
}
static void allocation_failures(unsigned page)
{
    for (unsigned failed=1;failed<=3;++failed) {
        struct Fixture f;init(&f,page,48);f.fail_at=failed;struct AArch64ProtectionOps o=ops(&f);
        assert(!AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,1,1));
        assert(f.count==failed && reachable(&f,f.root,0)==f.count && !f.dirty);
        f.fail_at=0;assert(AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,1,1));
        assert(f.count==4);assert(AArch64UnmapPage(&o,f.root,f.tcr,f.va,DATA_PA));
        assert(reachable(&f,f.root,0)==4);finish(&f);
    }
    /* Allocated but unresolvable or malformed pages never publish a parent.
     * Their allocator ledger still owns them and releases them at teardown. */
    for (unsigned failure=0;failure<3;++failure) {
        struct Fixture f;init(&f,page,48);struct AArch64ProtectionOps o=ops(&f);
        if (failure==0) f.deny_pa=f.next_pa;
        if (failure==1) f.malformed_allocation=1;
        if (failure==2) {f.next_pa=UINT64_C(1)<<36;f.tcr&=~(UINT64_C(7)<<32);}
        assert(!AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,1,1));
        assert(f.count==2 && reachable(&f,f.root,0)==1 && !f.clean_calls);finish(&f);
    }
}
static void invalid_geometry(unsigned page)
{
    struct Fixture f;init(&f,page,48);struct AArch64ProtectionOps o=ops(&f);uint64_t pa;
    const uint64_t bits[]={UINT64_C(1)<<7,UINT64_C(1)<<37,UINT64_C(1)<<39,UINT64_C(1)<<40,UINT64_C(1)<<59};
    for (unsigned k=0;k<sizeof(bits)/sizeof(*bits);++k) {
        uint64_t tcr=f.tcr|bits[k];pa=SENTINEL;
        assert(!AArch64MapPage(&o,f.root,tcr,f.va,DATA_PA,1,1));
        assert(!AArch64TranslateResident(&o,f.root,tcr,f.va,&pa) && pa==SENTINEL);
    }
    for (unsigned tg=1;tg<4;tg+=2) assert(!AArch64MapPage(&o,f.root,(f.tcr&~(UINT64_C(3)<<14))|((uint64_t)tg<<14),f.va,DATA_PA,1,1));
    for (unsigned width=16;width<=24;width+=8) assert(!AArch64MapPage(&o,f.root,(f.tcr&~UINT64_C(63))|(64-width),f.va,DATA_PA,1,1));
    assert(!AArch64MapPage(&o,f.root,(f.tcr&~UINT64_C(63))|15,f.va,DATA_PA,1,1));
    for (unsigned ips=6;ips<=7;++ips) assert(!AArch64MapPage(&o,f.root,(f.tcr&~(UINT64_C(7)<<32))|((uint64_t)ips<<32),f.va,DATA_PA,1,1));
    assert(!AArch64MapPage(&o,f.root,f.tcr,UINT64_C(1)<<48,DATA_PA,1,1));
    assert(!AArch64MapPage(&o,f.root,f.tcr,f.va+1,DATA_PA,1,1));
    assert(!AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA+1,1,1));
    assert(!AArch64MapPage(&o,f.root,f.tcr,f.va,PA_HIGH,1,1));
    assert(!AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,1,8));
    assert(!AArch64MapPage(&o,f.root|8,f.tcr,f.va,DATA_PA,1,1));
    assert(!AArch64MapPage(NULL,f.root,f.tcr,f.va,DATA_PA,1,1));
    assert(!AArch64MappingPhysical(NULL,f.root,f.tcr,f.va,&pa));
    assert(!AArch64UnmapPage(NULL,f.root,f.tcr,f.va,DATA_PA));
    assert(!AArch64RemapPage(NULL,f.root,f.tcr,f.va,DATA_PA,1));
    assert(!AArch64TranslateResident(NULL,f.root,f.tcr,f.va,&pa));
    assert(f.count==1 && !f.allocate_calls && !f.clean_calls);finish(&f);
}
static void corruption(unsigned page)
{
    const uint64_t malformed[]={PA_HIGH,UINT64_C(1)<<52,GUARD};
    for (unsigned k=0;k<sizeof(malformed)/sizeof(*malformed);++k) {
        struct Fixture f;init(&f,page,48);struct AArch64ProtectionOps o=ops(&f);
        assert(AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,1,1));
        uint64_t *slot=slot_at(&f,f.va,3);*slot|=malformed[k];uint64_t d=*slot,pa=SENTINEL;
        assert(!AArch64MappingPhysical(&o,f.root,f.tcr,f.va,&pa) && pa==SENTINEL);
        assert(!AArch64UnmapPage(&o,f.root,f.tcr,f.va,DATA_PA));assert(*slot==d);
        assert(!AArch64RemapPage(&o,f.root,f.tcr,f.va,DATA_PA,1));assert(*slot==d);
        finish(&f);
    }
    /* An IPS-invalid intermediate physical address must not be masked into
     * a different valid table. The resolver can represent it, so validation
     * rather than an accidental failed lookup must reject it. */
    for (unsigned level=0;level<3;++level) {
        struct Fixture f;init(&f,page,48);struct AArch64ProtectionOps o=ops(&f);
        assert(AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,1,1));
        uint64_t *parent=slot_at(&f,f.va,level),saved=*parent;
        uint64_t mask=page==12?UINT64_C(0x0000fffffffff000):UINT64_C(0x0000ffffffffc000);
        struct Table *t=find(&f,saved&mask);assert(t);t->pa|=UINT64_C(1)<<36;*parent|=UINT64_C(1)<<36;
        uint64_t narrow=f.tcr&~(UINT64_C(7)<<32),pa=SENTINEL;
        assert(!AArch64MappingPhysical(&o,f.root,narrow,f.va,&pa) && pa==SENTINEL);
        assert(!AArch64MapPage(&o,f.root,narrow,f.va+((uint64_t)1<<page),DATA_PA,1,1));finish(&f);
    }
    struct Fixture f;init(&f,page,48);struct AArch64ProtectionOps o=ops(&f);
    assert(AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,1,1));
    if (page==14) {
        uint64_t *slot=slot_at(&f,f.va,3);*slot|=UINT64_C(1)<<12;
        uint64_t pa=SENTINEL;assert(!AArch64MappingPhysical(&o,f.root,f.tcr,f.va,&pa));
        assert(!AArch64UnmapPage(&o,f.root,f.tcr,f.va,DATA_PA));
    }
    finish(&f);
}
static void callback_contract(unsigned page)
{
    struct Fixture f;init(&f,page,48);struct AArch64ProtectionOps o=ops(&f);
    for (unsigned k=0;k<5;++k) {
        struct AArch64ProtectionOps bad=o;
        switch (k) {
        case 0:bad.resolve=NULL;break;
        case 1:bad.allocate=NULL;break;
        case 2:bad.clean=NULL;break;
        case 3:bad.invalidate=NULL;break;
        default:bad.synchronize=NULL;break;
        }
        assert(!AArch64MapPage(&bad,f.root,f.tcr,f.va,DATA_PA,1,1));
    }
    assert(f.count==1 && !f.clean_calls);
    assert(AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,1,1));
    struct AArch64ProtectionOps query={.context=&f,.resolve=resolve};
    uint64_t pa=SENTINEL;
    assert(AArch64MappingPhysical(&query,f.root,f.tcr,f.va,&pa) && pa==DATA_PA);
    assert(AArch64TranslateResident(&query,f.root,f.tcr,f.va+1,&pa) && pa==DATA_PA+1);
    assert(!AArch64MappingPhysical(&query,f.root,f.tcr,f.va,NULL));
    assert(!AArch64TranslateResident(&query,f.root,f.tcr,f.va,NULL));
    assert(!AArch64RemapPage(&query,f.root,f.tcr,f.va,DATA_PA,0));
    assert(!AArch64UnmapPage(&query,f.root,f.tcr,f.va,DATA_PA));
    query.clean=clean;query.invalidate=invalidate;
    assert(AArch64UnmapPage(&query,f.root,f.tcr,f.va,DATA_PA));
    finish(&f);
}
static void parent_and_blocks(unsigned page)
{
    for (unsigned level=0;level<3;++level) {
        struct Fixture f;init(&f,page,48);struct AArch64ProtectionOps o=ops(&f);
        assert(AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,1,1));
        uint64_t *parent=slot_at(&f,f.va,level),saved=*parent;
        *parent|=UINT64_C(0x1f)<<59;uint64_t pa=SENTINEL;
        assert(!AArch64UnmapPage(&o,f.root,f.tcr,f.va,DATA_PA));
        assert(!AArch64MappingPhysical(&o,f.root,f.tcr,f.va,&pa));
        assert(AArch64TranslateResident(&o,f.root,f.tcr,f.va+333,&pa) && pa==DATA_PA+333);
        *parent=saved;
        uint64_t block=level==0?UINT64_C(0x800000000000):level==1?UINT64_C(0x4000000000):UINT64_C(0x80000000);
        *parent=block|1;uint64_t old=*parent;pa=SENTINEL;
        assert(!AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,1,1));
        assert(!AArch64UnmapPage(&o,f.root,f.tcr,f.va,block));
        assert(!AArch64RemapPage(&o,f.root,f.tcr,f.va,block,1));assert(*parent==old);
        int supported=level==2 || (level==1 && page==12);
        uint64_t offset=f.va&((UINT64_C(1)<<(page+(page-3)*(3-level)))-1);
        int found=AArch64TranslateResident(&o,f.root,f.tcr,f.va,&pa);
        assert(supported ? found && pa==block+offset : !found && pa==SENTINEL);
        *parent|=UINT64_C(1)<<page;
        assert(!AArch64TranslateResident(&o,f.root,f.tcr,f.va,&pa));finish(&f);
    }
}
static void address_widths(unsigned page)
{
    for (unsigned scope=0;scope<3;++scope) {
        struct Fixture f;init(&f,page,48);struct AArch64ProtectionOps o=ops(&f);
        assert(AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,1,1));
        if (scope==0) {
            struct Table *root=find(&f,f.root);assert(root);
            root->pa|=UINT64_C(1)<<36;f.root=root->pa;
            f.tcr&=~(UINT64_C(7)<<32);
        } else if (scope==1) {
            /* Without checking bits49:48 this would alias a valid table. */
            *slot_at(&f,f.va,0)|=PA_HIGH;
        } else {
            *slot_at(&f,f.va,3)|=UINT64_C(1)<<36;
            f.tcr&=~(UINT64_C(7)<<32);
        }
        uint64_t pa=SENTINEL;
        assert(!AArch64MappingPhysical(&o,f.root,f.tcr,f.va,&pa) && pa==SENTINEL);
        assert(!AArch64TranslateResident(&o,f.root,f.tcr,f.va+1,&pa) && pa==SENTINEL);
        finish(&f);
    }
    if (page==14) {
        struct Fixture f;init(&f,page,48);struct AArch64ProtectionOps o=ops(&f);
        assert(AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,1,1));
        *slot_at(&f,f.va,0)|=UINT64_C(1)<<12;
        uint64_t pa=SENTINEL;
        assert(!AArch64MappingPhysical(&o,f.root,f.tcr,f.va,&pa) && pa==SENTINEL);
        assert(!AArch64TranslateResident(&o,f.root,f.tcr,f.va,&pa) && pa==SENTINEL);
        assert(!AArch64MapPage(&o,f.root,f.tcr,f.va+((uint64_t)1<<page),DATA_PA,1,1));
        finish(&f);
    }
}
static void user_admission(unsigned page)
{
    struct Fixture f;init(&f,page,48);struct AArch64ProtectionOps o=ops(&f);
    assert(AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,11,1));
    struct AArch64ProtectionOps query={.context=&f,.resolve=resolve};
    uint64_t pa=SENTINEL,*entry=slot_at(&f,f.va,3),original=*entry;
    unsigned allocations=f.allocate_calls, cleans=f.clean_calls, invalidations=f.invalidations;
    assert(AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,1,1,&pa) && pa==DATA_PA);
    assert(AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,2,1,&pa) && pa==DATA_PA);
    *entry=original & ~(UINT64_C(1)<<6);pa=SENTINEL;
    assert(!AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,1,1,&pa) && pa==SENTINEL);
    *entry=original | (UINT64_C(1)<<7);pa=SENTINEL;
    assert(!AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,2,1,&pa) && pa==SENTINEL);
    assert(AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,1,1,&pa) && pa==DATA_PA);
    /* Execute admission checks hardware UXN and read-only state, not merely
     * the software permission ledger. Queries must not mutate page tables. */
    *entry=original;pa=SENTINEL;
    assert(!AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,4,1,&pa) && pa==SENTINEL);
    *entry=original | (UINT64_C(1)<<7);pa=SENTINEL;
    assert(!AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,4,1,&pa) && pa==SENTINEL);
    *entry=(original | (UINT64_C(1)<<7)) & ~UXN;
    assert(AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,4,1,&pa) && pa==DATA_PA);
    *entry=original & ~UXN;pa=SENTINEL;
    assert(!AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,4,1,&pa) && pa==SENTINEL);
    const uint64_t malformed[]={original & ~UINT64_C(1),original & ~(UINT64_C(1)<<10),
        original | GUARD,original | CONTIG,original | PA_HIGH,original & ~UINT64_C(0x1c)};
    for(unsigned i=0;i<sizeof(malformed)/sizeof(*malformed);++i) {
        *entry=malformed[i];pa=SENTINEL;
        assert(!AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,1,1,&pa) && pa==SENTINEL);
    }
    *entry=original;
    for(unsigned bit=59;bit<=63;++bit) {
        uint64_t *parent=slot_at(&f,f.va,0),saved=*parent;
        *parent|=UINT64_C(1)<<bit;pa=SENTINEL;
        assert(!AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,1,1,&pa) && pa==SENTINEL);
        *parent=saved;
    }
    pa=SENTINEL;
    assert(!AArch64UserPagePhysical(&query,f.root,f.tcr,f.va+1,1,1,&pa) && pa==SENTINEL);
    assert(!AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,4,1,&pa) && pa==SENTINEL);
    assert(!AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,1,8,&pa) && pa==SENTINEL);
    assert(!AArch64UserPagePhysical(NULL,f.root,f.tcr,f.va,1,1,&pa));
    assert(!AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,1,1,NULL));
    assert(f.allocate_calls==allocations && f.clean_calls==cleans && f.invalidations==invalidations);
    finish(&f);
}
static void privileged_alias(unsigned page)
{
    struct Fixture f;init(&f,page,48);struct AArch64ProtectionOps o=ops(&f);
    assert(AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,3,1));
    uint64_t *entry=slot_at(&f,f.va,3),original=*entry;
    *entry=original|(UINT64_C(1)<<6);
    assert(!AArch64RemapPrivilegedPage(&o,f.root,f.tcr,f.va,DATA_PA,5));
    assert(*entry==(original|(UINT64_C(1)<<6)));
    *entry=original&~UXN;
    assert(!AArch64RemapPrivilegedPage(&o,f.root,f.tcr,f.va,DATA_PA,3));
    assert(*entry==(original&~UXN));*entry=original;
    const unsigned invalid[]={8,11,13,29,47,7};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        assert(!AArch64RemapPrivilegedPage(&o,f.root,f.tcr,f.va,DATA_PA,invalid[i]));
        assert(*entry==original);
    }
    assert(!AArch64RemapPrivilegedPage(&o,f.root,f.tcr,f.va,DATA_PA+((uint64_t)1<<page),5));
    assert(*entry==original);
    assert(AArch64RemapPrivilegedPage(&o,f.root,f.tcr,f.va,DATA_PA,5));
    assert((*entry&(UINT64_C(1)<<7)) && !(*entry&PXN) && (*entry&UXN) && !(*entry&(UINT64_C(1)<<6)));
    assert(AArch64RemapPrivilegedPage(&o,f.root,f.tcr,f.va,DATA_PA,0));
    assert((*entry&GUARD) && !(*entry&1));
    assert(AArch64RemapPrivilegedPage(&o,f.root,f.tcr,f.va,DATA_PA,3));
    assert(!(*entry&(UINT64_C(1)<<7)) && (*entry&PXN) && (*entry&UXN) && !(*entry&(UINT64_C(1)<<6)));
    unsigned allocation_count=f.allocate_calls;
    assert(AArch64UnmapPage(&o,f.root,f.tcr,f.va,DATA_PA));
    assert(!AArch64RemapPrivilegedPage(&o,f.root,f.tcr,f.va,DATA_PA,5));
    assert(!*entry && f.allocate_calls==allocation_count);
    finish(&f);
}
/* Reproduce an inherited low identity mapping in a separately owned root.
 * The QEMU failure used 48-bit VA / 4K tables; also cover 39-bit and 16K. */
static void low_clone_eviction(unsigned page, unsigned bits)
{
    struct Fixture f;init(&f,page,bits);struct AArch64ProtectionOps o=ops(&f);
    f.va=UINT64_C(0x7ffe0000);
    uint64_t step=UINT64_C(1)<<page,source=f.root,clone=0,pa=SENTINEL;
    assert(AArch64MapPage(&o,source,f.tcr,f.va,f.va,3,1));
    assert(AArch64MapPage(&o,source,f.tcr,f.va+step,f.va+step,3,1));
    uint64_t original=*slot_at(&f,f.va,3),neighbor=*slot_at(&f,f.va+step,3);
    size_t count=0;
    assert(AArch64CountPrivilegedRoot(&o,source,f.tcr,MAX_TABLES,&count));
    assert(AArch64ClonePrivilegedRoot(&o,source,f.tcr,count,&clone));
    assert(clone!=source);f.root=clone;
    uint64_t *entry=slot_at(&f,f.va,3),saved=*entry;
    unsigned allocations=f.allocate_calls,invalidations=f.invalidations;
    assert(!AArch64MapPage(&o,clone,f.tcr,f.va,DATA_PA,11,1));
    assert(!AArch64UnmapPrivilegedPage(&o,clone,f.tcr,f.va,f.va+step));
    const uint64_t refused[]={saved|(UINT64_C(1)<<6),saved&~UXN,
        saved|CONTIG,saved|PA_HIGH,saved|GUARD};
    for (unsigned i=0;i<sizeof(refused)/sizeof(*refused);++i) {
        *entry=refused[i];
        assert(!AArch64UnmapPrivilegedPage(&o,clone,f.tcr,f.va,f.va));
        assert(*entry==refused[i]);
    }
    *entry=saved;
    assert(f.allocate_calls==allocations && f.invalidations==invalidations);
    assert(AArch64UnmapPrivilegedPage(&o,clone,f.tcr,f.va,f.va));
    assert(!*entry && f.invalidations==invalidations+1);
    assert(!AArch64UnmapPrivilegedPage(&o,clone,f.tcr,f.va,f.va));
    assert(AArch64MapPage(&o,clone,f.tcr,f.va,DATA_PA,11,1));
    assert(AArch64UserPagePhysical(&o,clone,f.tcr,f.va,2,1,&pa) && pa==DATA_PA);
    assert(!AArch64UnmapPrivilegedPage(&o,clone,f.tcr,f.va,DATA_PA));
    assert(*slot_at(&f,f.va+step,3)==neighbor);
    assert(f.allocate_calls==allocations);
    f.root=source;
    assert(*slot_at(&f,f.va,3)==original && *slot_at(&f,f.va+step,3)==neighbor);
    assert(AArch64MappingPhysical(&o,source,f.tcr,f.va,&pa) && pa==f.va);
    finish(&f);
}
int main(void)
{
    const unsigned widths[]={25,30,32,39,42,48};
    for (unsigned page=12;page<=14;page+=2) {
        for (unsigned i=0;i<sizeof(widths)/sizeof(*widths);++i) basic(page,widths[i]);
        low_clone_eviction(page,39);low_clone_eviction(page,48);
        privileged_alias(page);user_admission(page);permissions(page);allocation_failures(page);invalid_geometry(page);corruption(page);parent_and_blocks(page);callback_contract(page);address_widths(page);
    }
    printf("[AARCH64-MAPPING-HOST] result=PASS cases=%u granules=4K,16K actual-production-walks=1 native=NOT_RUN\n",cases);
    return 0;
}
