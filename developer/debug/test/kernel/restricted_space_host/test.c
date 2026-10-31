/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "restricted_service.h"
#include "pager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x,n) do { if (!(x)) { puts("FAIL " n); exit(1); } } while(0)
#define PAGE 4096U
struct Task { unsigned id; };
struct Fixture { unsigned used, invalidations, synchronizations; unsigned char tables[16][PAGE] __attribute__((aligned(PAGE))); };
static uint64_t *resolve(void *opaque,uint64_t pa)
{
    struct Fixture *f=opaque;uintptr_t start=(uintptr_t)f->tables;
    return pa>=start && pa-start<=sizeof(f->tables) && PAGE<=sizeof(f->tables)-(pa-start) ? (void *)(uintptr_t)pa : NULL;
}
static uint64_t allocate(void *opaque,unsigned bits)
{
    struct Fixture *f=opaque;
    return bits==12 && f->used<16 ? (uintptr_t)f->tables[f->used++] : 0;
}
static void clean(void *opaque,const void *address,size_t bytes)
{ (void)opaque;(void)address;(void)bytes; }
static void invalidate(void *opaque) { ++((struct Fixture *)opaque)->invalidations; }
static void sync_code(void *opaque,uint64_t va,size_t bytes)
{ ++((struct Fixture *)opaque)->synchronizations;(void)va;(void)bytes; }
static int page_allocate(void *opaque,size_t size,struct KrnPagerPage *page)
{
    CHECK(size==PAGE,"region-page-size");*page=(struct KrnPagerPage){(uintptr_t)opaque,opaque};return 1;
}
static void page_release(void *opaque,struct KrnPagerPage *page,size_t size)
{ (void)opaque;(void)page;(void)size; }
static void zero_page(void *pa)
{
    struct KrnPagerOps ops={pa,page_allocate,page_release,NULL,NULL};struct KrnPagerPage page;
    CHECK(KrnPagerPageIn(&ops,PAGE,KPAGER_ZERO,0,0,&page)==KPAGER_OK,"region-zero-pager");
}
static int deny_remove(void *opaque,uintptr_t va,uintptr_t pa)
{ (void)opaque;(void)va;(void)pa;return 0; }
static uintptr_t denied_address;
static int (*real_remove)(void *,uintptr_t,uintptr_t);
static int selective_remove(void *opaque,uintptr_t va,uintptr_t pa)
{ return va!=denied_address && real_remove(opaque,va,pa); }
static struct AROSRestrictedRegion *publishing;
static unsigned native_calls,native_policy,fail_native,fail_install;
static int (*publication_install)(void *,uintptr_t,uintptr_t,unsigned);
static uint64_t publication_leaf(struct AROSRestrictedSpace *s,uintptr_t va)
{
    uint64_t *table=(void *)(uintptr_t)s->root;
    for (unsigned shift=30;shift>12;shift-=9)
        table=(void *)(uintptr_t)(table[(va>>shift)&511]&UINT64_C(0x0000fffffffff000));
    return table[(va>>12)&511];
}
static int native_publication(void *context,uintptr_t pa,unsigned policy)
{
    (void)context;++native_calls;
    CHECK(publishing->publication_pending && !publishing->mapping.pages[0].protection &&
        !(publication_leaf(publishing->space,publishing->mapping.start)&1),"publication-revoked-before-native");
    CHECK(pa==publishing->backing[0],"publication-native-identity");
    if (fail_native) return 0;native_policy=policy;return 1;
}
static int publication_fault(void *context,uintptr_t va,uintptr_t pa,unsigned access)
{
    if (fail_install==1 || (fail_install==2 && access==5)) return 0;
    return publication_install(context,va,pa,access);
}
static void publication_tests(void)
{
    struct Fixture *f=aligned_alloc(PAGE,sizeof(*f));
    unsigned char *data=aligned_alloc(PAGE,PAGE),*page=aligned_alloc(PAGE,PAGE);
    CHECK(f && data && page,"publication-storage");memset(f,0,sizeof(*f));f->used=1;
    struct AROSRestrictedSpace space={0};
    struct AArch64ProtectionOps hardware={f,resolve,allocate,clean,invalidate,sync_code,NULL};
    CHECK(AROSRestrictedSpaceInit(&space,99,(uintptr_t)f->tables[0],25|(UINT64_C(5)<<32),(uintptr_t)data,&hardware),"publication-space");
    struct AROSRestrictedRegion region={0};struct KrnMappingPage metadata[2];uintptr_t ledger[2]={0};
    uintptr_t va=AROS_RESTRICTED_DYNAMIC_BASE;
    CHECK(AROSRestrictedRegionReserve(&space,&region,va,2*PAGE,metadata,ledger,2),"publication-reserve");
    CHECK(AROSRestrictedRegionCommit(&region,0,(uintptr_t)page,3),"publication-commit");
    uint64_t generation=metadata[0].generation;
    publishing=&region;struct AROSRestrictedPublicationOps ops={NULL,native_publication};
    publication_install=space.mapops.install;space.mapops.install=publication_fault;
    native_calls=0;region.mapping.in_flight=1;
    CHECK(!AROSRestrictedRegionPublish(&region,0,5,&ops) && !native_calls,"publication-pinned-refused");
    region.mapping.in_flight=0;
    for (unsigned access=0;access<16;++access) if (access!=0 && access!=1 && access!=3 && access!=5)
        CHECK(!AROSRestrictedRegionPublish(&region,0,access,&ops) && !native_calls,"publication-permissions");
    region.shared=(void *)1;
    CHECK(!AROSRestrictedRegionPublish(&region,0,5,&ops) && !native_calls,"publication-shared-refused");region.shared=NULL;
    fail_install=1;
    CHECK(!AROSRestrictedRegionPublish(&region,0,5,&ops) && !native_calls && !region.publication_pending && metadata[0].protection==3,"publication-revoke-refusal");
    CHECK(metadata[0].generation==generation,"publication-revoke-keeps-generation");
    fail_install=0;metadata[0].generation=UINT64_MAX;
    CHECK(!AROSRestrictedRegionPublish(&region,0,5,&ops) && !native_calls,"publication-generation-exhausted");
    metadata[0].generation=generation;
    fail_install=0;fail_native=1;
    CHECK(!AROSRestrictedRegionPublish(&region,0,5,&ops) && region.publication_pending && !metadata[0].protection,"publication-native-refusal");
    CHECK(metadata[0].generation==generation+1,"publication-generation-advanced");
    CHECK(!AROSRestrictedRegionProtect(&region,0,3),"publication-pending-write-refused");
    uintptr_t released=0;
    CHECK(!AROSRestrictedRegionDecommit(&region,0,&released) && !released && ledger[0],"publication-pending-decommit-refused");
    CHECK(!AROSRestrictedRegionPublish(&region,1,3,&ops),"publication-other-page-refused");
    fail_native=0;fail_install=2;
    CHECK(!AROSRestrictedRegionPublish(&region,0,5,&ops) && region.publication_pending && !metadata[0].protection && native_policy==1,"publication-grant-refusal");
    fail_install=0;
    CHECK(AROSRestrictedRegionPublish(&region,0,5,&ops) && !region.publication_pending && metadata[0].protection==5 && native_policy==1,"publication-rx");
    CHECK(metadata[0].generation==generation+1,"publication-retry-same-generation");
    uintptr_t code_pa=0;uint64_t code_generation=0,code_reservation=0;
    CHECK(AROSRestrictedSpacePageIdentity(&space,va+4,4,&code_pa,&code_generation,&code_reservation) &&
        code_pa==(uintptr_t)page && code_generation==generation+1 && code_reservation==region.identity,"publication-execute-identity");
    uint64_t leaf=publication_leaf(&space,va);
    CHECK((leaf&128) && !(leaf&(UINT64_C(1)<<54)) && f->synchronizations,"publication-rx-pte-cache");
    CHECK(!AROSRestrictedRegionProtect(&region,0,3),"publication-exec-direct-write-refused");
    CHECK(AROSRestrictedRegionPublish(&region,0,3,&ops) && metadata[0].protection==3 && native_policy==3,"publication-rw");
    CHECK(metadata[0].generation==generation+2,"publication-write-new-generation");
    CHECK(!AROSRestrictedSpacePageIdentity(&space,va,4,&code_pa,&code_generation,&code_reservation),"publication-writable-not-executable");
    leaf=publication_leaf(&space,va);CHECK(!(leaf&128) && (leaf&(UINT64_C(1)<<54)),"publication-rw-nx");
    fail_native=1;
    CHECK(!AROSRestrictedRegionPublish(&region,0,5,&ops) && region.publication_pending,"publication-final-pending");
    CHECK(AROSRestrictedRegionDetach(&region) && !region.publication_pending && ledger[0]==(uintptr_t)page,"publication-detach-retains-backing");
    CHECK(AROSRestrictedSpaceClose(&space),"publication-space-close");
    fail_native=0;free(page);free(data);free(f);publishing=NULL;
}
static void region_tests(void)
{
    struct Fixture *f[2];unsigned char *data[2],*pages[5];
    struct AROSRestrictedSpace spaces[2]={{0}};
    uint64_t tcr=25|(UINT64_C(5)<<32),va=AROS_RESTRICTED_DYNAMIC_BASE;
    for (unsigned i=0;i<2;++i) {
        f[i]=aligned_alloc(PAGE,sizeof(*f[i]));data[i]=aligned_alloc(PAGE,PAGE);
        CHECK(f[i] && data[i],"region-allocation");memset(f[i],0,sizeof(*f[i]));memset(data[i],0,PAGE);f[i]->used=1;
        struct AArch64ProtectionOps ops={f[i],resolve,allocate,clean,invalidate,sync_code,NULL};
        if (i) CHECK(!AROSRestrictedSpaceInit(&spaces[i],22,(uintptr_t)f[i]->tables[0],tcr,(uintptr_t)data[0],&ops) && !spaces[i].root,"duplicate-space-backing");
        CHECK(AROSRestrictedSpaceInit(&spaces[i],i+21,(uintptr_t)f[i]->tables[0],tcr,(uintptr_t)data[i],&ops),"region-space");
    }
    for (unsigned i=0;i<5;++i) { pages[i]=aligned_alloc(PAGE,PAGE);CHECK(pages[i],"region-page");memset(pages[i],0xa5,PAGE);zero_page(pages[i]); }
    struct AROSRestrictedRegion a={0},b={0},collision={0};
    struct KrnMappingPage ap[3],bp[1],cp[1];uintptr_t ab[3]={0},bb[1]={0},cb[1]={0};
    CHECK(AROSRestrictedRegionReserve(&spaces[0],&a,va,3*PAGE,ap,ab,3),"region-reserve");
    CHECK(AROSRestrictedRegionReserve(&spaces[1],&b,va,PAGE,bp,bb,1),"independent-region-va");
    CHECK(!AROSRestrictedRegionReserve(&spaces[0],&collision,va,PAGE,cp,cb,1),"region-collision");
    CHECK(!AROSRestrictedRegionReserve(&spaces[0],&collision,AROS_RESTRICTED_STACK,PAGE,cp,cb,1),"thread-range-excluded");
    CHECK(AROSRestrictedRegionCommit(&a,0,(uintptr_t)pages[0],3),"region-commit");
    CHECK(!AROSRestrictedRegionCommit(&b,0,(uintptr_t)pages[0],3) && !bb[0],"private-alias-refused");
    CHECK(AROSRestrictedRegionCommit(&b,0,(uintptr_t)pages[3],3),"independent-backing");
    uint64_t actual;
    CHECK(AArch64UserPagePhysical(&spaces[0].hardware,spaces[0].root,tcr,va,2,1,&actual) && actual==(uintptr_t)pages[0],"eager-write-permission");
    CHECK(AArch64UserPagePhysical(&spaces[1].hardware,spaces[1].root,tcr,va,2,1,&actual) && actual==(uintptr_t)pages[3],"other-root-backing");
    uintptr_t identity;uint64_t identity_generation,identity_reservation;
    CHECK(AROSRestrictedSpacePageIdentity(&spaces[0],va+16,2,&identity,&identity_generation,&identity_reservation) &&
        identity==(uintptr_t)pages[0] && identity_generation==ap[0].generation,"region-page-identity");
    CHECK(!AROSRestrictedSpacePageIdentity(&spaces[0],va+2*PAGE,0,&identity,&identity_generation,&identity_reservation),"reserved-no-identity");
    CHECK(AArch64UnmapPage(&spaces[0].hardware,spaces[0].root,tcr,va,(uintptr_t)pages[0]) &&
        AArch64MapPage(&spaces[0].hardware,spaces[0].root,tcr,va,(uintptr_t)pages[4],AARCH64_PROTECT_USER|3,1),"identity-foreign-pte");
    CHECK(!AROSRestrictedSpacePageIdentity(&spaces[0],va,2,&identity,&identity_generation,&identity_reservation),"identity-pte-mismatch");
    CHECK(AArch64UnmapPage(&spaces[0].hardware,spaces[0].root,tcr,va,(uintptr_t)pages[4]) &&
        AArch64MapPage(&spaces[0].hardware,spaces[0].root,tcr,va,(uintptr_t)pages[0],AARCH64_PROTECT_USER|3,1),"identity-restore-pte");
    CHECK(AROSRestrictedRegionCommit(&a,1,(uintptr_t)pages[1],3),"second-page-commit");
    CHECK(!spaces[0].mapops.install(spaces[0].mapops.context,va+2*PAGE,(uintptr_t)pages[2],3),"unowned-region-install");
    CHECK(!AROSRestrictedRegionCommit(&a,2,(uintptr_t)pages[2],5) && !ab[2],"region-exec-refusal");
    uint64_t reserved_generation=ap[2].generation;
    ap[2].generation=UINT64_MAX;
    CHECK(!AROSRestrictedRegionCommit(&a,2,(uintptr_t)pages[2],3) && !ab[2] &&
        ap[2].state==KMAP_RESERVED && ap[2].generation==UINT64_MAX &&
        !AArch64MappingPhysical(&spaces[0].hardware,spaces[0].root,tcr,va+2*PAGE,&actual),
        "exhausted-commit-unchanged");
    ap[2].generation=reserved_generation;
    CHECK(AArch64MapPage(&spaces[0].hardware,spaces[0].root,tcr,va+2*PAGE,(uintptr_t)pages[4],AARCH64_PROTECT_USER|3,1),"foreign-pte");
    CHECK(!AROSRestrictedRegionCommit(&a,2,(uintptr_t)pages[2],3) && !ab[2] && ap[2].state==KMAP_RESERVED,"commit-pte-conflict");
    CHECK(AArch64MappingPhysical(&spaces[0].hardware,spaces[0].root,tcr,va+2*PAGE,&actual) && actual==(uintptr_t)pages[4],"conflicting-pte-preserved");
    CHECK(AArch64UnmapPage(&spaces[0].hardware,spaces[0].root,tcr,va+2*PAGE,(uintptr_t)pages[4]),"remove-foreign-pte");
    CHECK(AROSRestrictedRegionCommit(&a,2,(uintptr_t)pages[2],0),"committed-guard");
    CHECK(!AArch64UserPagePhysical(&spaces[0].hardware,spaces[0].root,tcr,va+2*PAGE,1,1,&actual),"guard-refuses-access");
    CHECK(AROSRestrictedSpacePageIdentity(&spaces[0],va+2*PAGE,0,&identity,&identity_generation,&identity_reservation) &&
        !AROSRestrictedSpacePageIdentity(&spaces[0],va+2*PAGE,1,&identity,&identity_generation,&identity_reservation),"guard-identity-not-access");
    CHECK(AROSRestrictedRegionProtect(&a,2,3) && AArch64UserPagePhysical(&spaces[0].hardware,spaces[0].root,tcr,va+2*PAGE,2,1,&actual),"guard-to-write");
    struct AROSRestrictedService channel={0};struct Task worker={9};int context;
    CHECK(AROSRestrictedServiceInit(&channel,&spaces[0],99),"region-channel");
    for (unsigned i=0;i<4;++i) { pages[0][PAGE-4+i]=i+1;pages[1][i]=i+5; }
    CHECK(AROSRestrictedServiceSubmit(&channel,va+PAGE-4,va+128,8),"cross-page-service");
    uintptr_t released=123;
    CHECK(!AROSRestrictedRegionDecommit(&a,0,&released) && released==123 && ab[0]==(uintptr_t)pages[0],"region-decommit-pinned");
    struct AROSRestrictedWork work;
    CHECK(AROSRestrictedServiceTake(&worker,&context,&work),"region-work");
    for (unsigned i=0;i<8;++i) CHECK(work.bytes[i]==i+1,"cross-page-copy");
    CHECK(AROSRestrictedServiceComplete(&worker,&work) && AROSRestrictedServiceCollect(&channel,work.token),"region-reply");
    for (unsigned i=0;i<8;++i) CHECK(pages[0][128+i]==i+1,"region-output");
    CHECK(AROSRestrictedRegionProtect(&a,0,0) && !AROSRestrictedServiceSubmit(&channel,va,va+128,8),"region-guard-buffer");
    CHECK(AROSRestrictedRegionProtect(&a,0,3),"region-restore");
    int (*remove)(void *,uintptr_t,uintptr_t)=spaces[0].mapops.remove;spaces[0].mapops.remove=deny_remove;
    CHECK(!AROSRestrictedRegionDecommit(&a,0,&released) && released==123 && ab[0]==(uintptr_t)pages[0],"region-remove-ack");
    spaces[0].mapops.remove=remove;
    uint64_t generation=ap[0].generation;
    CHECK(AROSRestrictedRegionDecommit(&a,0,&released) && released==(uintptr_t)pages[0] && !ab[0] && ap[0].generation>generation,"region-decommit");
    CHECK(!AArch64MappingPhysical(&spaces[0].hardware,spaces[0].root,tcr,va,&actual),"decommit-unmapped");
    CHECK(!AROSRestrictedRegionReserve(&spaces[0],&collision,va,PAGE,cp,cb,1),"decommit-keeps-reservation");
    memset(pages[0],0xa5,PAGE);zero_page(pages[0]);
    CHECK(AROSRestrictedRegionCommit(&a,0,(uintptr_t)pages[0],3),"region-recommit");
    for (unsigned i=0;i<PAGE;++i) CHECK(pages[0][i]==0,"region-recommit-zero");
    CHECK(AROSRestrictedServiceSubmit(&channel,va,va+128,8) && AROSRestrictedServiceTake(&worker,&context,&work),"retained-region-work");
    CHECK(!AROSRestrictedRegionDetach(&a) && a.space==&spaces[0],"detach-pinned-region");
    memset(work.bytes,0x77,8);
    CHECK(AROSRestrictedServiceComplete(&worker,&work) && !AROSRestrictedServiceCollect(&channel,work.token) && !pages[0][128],"detached-region-no-late-write");
    CHECK(AROSRestrictedServiceClose(&channel),"region-channel-close");
    CHECK(!AROSRestrictedSpaceClose(&spaces[0]) && AArch64MappingPhysical(&spaces[0].hardware,spaces[0].root,tcr,va,&actual),"space-retains-regions");
    CHECK(AArch64MappingPhysical(&spaces[0].hardware,spaces[0].root,tcr,AROS_RESTRICTED_DATA,&actual),"space-keeps-data-with-regions");
    real_remove=spaces[0].mapops.remove;denied_address=va+PAGE;
    spaces[0].mapops.remove=selective_remove;
    CHECK(!AROSRestrictedRegionDetach(&a) && a.space==&spaces[0] && !ap[0].physical && ap[1].physical && ab[0]==(uintptr_t)pages[0],"partial-detach-retains-ownership");
    spaces[0].mapops.remove=real_remove;
    CHECK(AROSRestrictedRegionDetach(&a) && !a.space && ab[0]==(uintptr_t)pages[0],"region-detach-ownership");
    CHECK(AROSRestrictedSpacePageIdentity(&spaces[1],va,1,&identity,&identity_generation,&identity_reservation),"old-reservation-identity");
    uint64_t old_reservation=identity_reservation,old_generation=identity_generation;
    CHECK(AROSRestrictedRegionDetach(&b),"other-region-detach");
    memset(&b,0,sizeof(b));bb[0]=0;
    CHECK(AROSRestrictedRegionReserve(&spaces[1],&b,va,PAGE,bp,bb,1) &&
        AROSRestrictedRegionCommit(&b,0,(uintptr_t)pages[3],3),"reused-reservation");
    CHECK(AROSRestrictedSpacePageIdentity(&spaces[1],va,1,&identity,&identity_generation,&identity_reservation) &&
        identity==(uintptr_t)pages[3] && identity_generation==old_generation &&
        identity_reservation!=old_reservation,"reused-reservation-distinct-identity");
    CHECK(AROSRestrictedRegionDetach(&b),"reused-reservation-detach");
    for (unsigned i=0;i<3;++i)
        CHECK(!AArch64MappingPhysical(&spaces[0].hardware,spaces[0].root,tcr,va+i*PAGE,&actual),"detached-pte-absent");
    CHECK(!AArch64MappingPhysical(&spaces[1].hardware,spaces[1].root,tcr,va,&actual),"other-detached-pte-absent");
    CHECK(AROSRestrictedSpaceClose(&spaces[0]) && AROSRestrictedSpaceClose(&spaces[1]),"region-spaces-close");
    for (unsigned i=0;i<5;++i) free(pages[i]);
    for (unsigned i=0;i<2;++i) { free(data[i]);free(f[i]); }
}
static void shared_tests(void)
{
    struct Fixture *f[2];unsigned char *data[2],*page=aligned_alloc(PAGE,PAGE);
    CHECK(page,"shared-allocation");zero_page(page);uintptr_t backing[1]={(uintptr_t)page};
    struct AROSRestrictedSharedBacking object={0},duplicate={0};
    CHECK(AROSRestrictedSharedInitialize(&object,backing,1),"shared-register");
    CHECK(!AROSRestrictedSharedInitialize(&duplicate,backing,1),"shared-duplicate-pa");
    struct AROSRestrictedSpace spaces[2]={{0}};struct AROSRestrictedRegion regions[2]={{0}};
    struct KrnMappingPage metadata[2][1];uintptr_t ledger[2][1]={{0}};
    uint64_t tcr=25|(UINT64_C(5)<<32),va=AROS_RESTRICTED_DYNAMIC_BASE;
    for (unsigned i=0;i<2;++i) {
        f[i]=aligned_alloc(PAGE,sizeof(*f[i]));data[i]=aligned_alloc(PAGE,PAGE);
        CHECK(f[i] && data[i],"shared-fixture");memset(f[i],0,sizeof(*f[i]));memset(data[i],0,PAGE);f[i]->used=1;
        struct AArch64ProtectionOps ops={f[i],resolve,allocate,clean,invalidate,sync_code,NULL};
        CHECK(AROSRestrictedSpaceInit(&spaces[i],51+i,(uintptr_t)f[i]->tables[0],tcr,(uintptr_t)data[i],&ops),"shared-space");
        CHECK(AROSRestrictedRegionReserve(&spaces[i],&regions[i],va,PAGE,metadata[i],ledger[i],1),"shared-reserve");
        CHECK(!AROSRestrictedRegionCommit(&regions[i],0,(uintptr_t)page,3),"shared-cannot-private-commit");
        CHECK(AROSRestrictedRegionAttachShared(&regions[i],&object),"shared-attach");
        CHECK(!AROSRestrictedRegionAttachShared(&regions[i],&object),"shared-duplicate-view");
        CHECK(!AROSRestrictedRegionCommitShared(&regions[i],0,5),"shared-no-exec");
        CHECK(AROSRestrictedRegionCommitShared(&regions[i],0,i ? 1 : 3),"shared-commit");
    }
    CHECK(object.views==2,"shared-view-count");
    uint64_t pa0,pa1;
    CHECK(AArch64UserPagePhysical(&spaces[0].hardware,spaces[0].root,tcr,va,2,1,&pa0) &&
        AArch64UserPagePhysical(&spaces[1].hardware,spaces[1].root,tcr,va,1,1,&pa1) &&
        pa0==pa1 && pa0==(uintptr_t)page,"shared-same-pa");
    CHECK(!AArch64UserPagePhysical(&spaces[1].hardware,spaces[1].root,tcr,va,2,1,&pa1),"shared-ro-view");
    struct AROSRestrictedService channel={0};struct Task worker={1};int context;
    CHECK(AROSRestrictedServiceInit(&channel,&spaces[0],71),"shared-channel");
    memset(page,0x31,8);CHECK(AROSRestrictedServiceSubmit(&channel,va,va+32,8),"shared-service");
    struct AROSRestrictedWork work;CHECK(AROSRestrictedServiceTake(&worker,&context,&work),"shared-work");
    CHECK(!AROSRestrictedRegionDetach(&regions[0]) && object.views==2,"shared-held-view");
    CHECK(AROSRestrictedServiceComplete(&worker,&work) && !AROSRestrictedServiceCollect(&channel,work.token) && !page[32],"shared-no-late-write");
    CHECK(AROSRestrictedServiceClose(&channel),"shared-channel-close");
    CHECK(AROSRestrictedRegionDetach(&regions[0]) && object.views==1 && !ledger[0][0],"shared-detach-no-ownership");
    CHECK(AROSRestrictedSpaceClose(&spaces[0]),"shared-first-space-close");
    uintptr_t released=123;
    CHECK(AROSRestrictedRegionDecommit(&regions[1],0,&released) && !released && object.views==1 && page[0]==0x31,"shared-decommit-retains-object");
    CHECK(AROSRestrictedRegionCommitShared(&regions[1],0,3) && page[0]==0x31,"shared-recommit-preserves-data");
    CHECK(!AROSRestrictedSharedClose(&object) && object.views==1 && object.pages==backing,"shared-close-retained");
    struct AROSRestrictedRegion late={0};struct KrnMappingPage latepage;uintptr_t latebacking=0;
    CHECK(AROSRestrictedRegionReserve(&spaces[1],&late,va+PAGE,PAGE,&latepage,&latebacking,1),"shared-late-reserve");
    CHECK(!AROSRestrictedRegionAttachShared(&late,&object),"shared-close-stops-attach");
    CHECK(AROSRestrictedRegionDetach(&late),"shared-late-detach");
    int (*remove)(void *,uintptr_t,uintptr_t)=spaces[1].mapops.remove;spaces[1].mapops.remove=deny_remove;
    CHECK(!AROSRestrictedRegionDetach(&regions[1]) && object.views==1 && ledger[1][0],"shared-unmap-ack");
    spaces[1].mapops.remove=remove;
    CHECK(AROSRestrictedRegionDetach(&regions[1]) && !object.views && !ledger[1][0],"shared-final-detach");
    CHECK(AROSRestrictedSpaceClose(&spaces[1]),"shared-second-space-close");
    CHECK(AROSRestrictedSharedClose(&object) && !object.pages && !AROSRestrictedSharedClose(&object),"shared-final-close");
    free(page);for (unsigned i=0;i<2;++i) { free(data[i]);free(f[i]); }
}
static void channel_growth(void)
{
    enum { COUNT=32 };
    struct Fixture *f=aligned_alloc(PAGE,sizeof(*f));unsigned char *data=aligned_alloc(PAGE,PAGE);
    struct AROSRestrictedService *channels=calloc(COUNT,sizeof(*channels));
    CHECK(f && data && channels,"channel-growth-allocation");
    memset(f,0,sizeof(*f));memset(data,0x31,PAGE);f->used=1;
    struct AArch64ProtectionOps ops={f,resolve,allocate,clean,invalidate,sync_code,NULL};
    struct AROSRestrictedSpace space={0};
    CHECK(AROSRestrictedSpaceInit(&space,91,(uintptr_t)f->tables[0],25|(UINT64_C(5)<<32),(uintptr_t)data,&ops),"channel-growth-space");
    for (unsigned i=0;i<COUNT;++i) {
        CHECK(AROSRestrictedServiceInit(&channels[i],&space,i+101),"channel-growth-init");
        CHECK(AROSRestrictedServiceSubmit(&channels[i],AROS_RESTRICTED_DATA,AROS_RESTRICTED_DATA+32,8),"channel-growth-submit");
    }
    CHECK(space.channels==COUNT,"channel-growth-count");
    for (unsigned i=0;i<COUNT;i+=2) CHECK(AROSRestrictedServiceClose(&channels[i]),"channel-growth-cancel");
    struct Task worker={41};int context;struct AROSRestrictedWork work;
    for (unsigned i=1;i<COUNT;i+=2) {
        CHECK(AROSRestrictedServiceTake(&worker,&context,&work) && work.channel==channels[i].channel,"channel-growth-route");
        CHECK(AROSRestrictedServiceComplete(&worker,&work) && AROSRestrictedServiceCollect(&channels[i],work.token),"channel-growth-reply");
        CHECK(AROSRestrictedServiceClose(&channels[i]),"channel-growth-close");
    }
    CHECK(!space.channels && !AROSRestrictedServiceTake(&worker,&context,&work) &&
        AROSRestrictedSpaceClose(&space),"channel-growth-drain");
    free(channels);free(data);free(f);
}
int main(void)
{
    struct Fixture *f=aligned_alloc(PAGE,sizeof(*f));
    unsigned char *data=aligned_alloc(PAGE,PAGE);
    CHECK(f && data,"allocation");memset(f,0,sizeof(*f));memset(data,0x31,PAGE);
    f->used=1;
    struct AArch64ProtectionOps ops={f,resolve,allocate,clean,invalidate,sync_code,NULL};
    struct AROSRestrictedSpace space={0};
    uint64_t root=(uintptr_t)f->tables[0], tcr=25|(UINT64_C(5)<<32);
    CHECK(AROSRestrictedSpaceInit(&space,11,root,tcr,(uintptr_t)data,&ops),"space-init");
    uint64_t first_handle=space.handle;
    struct AROSRestrictedService a={0},b={0};
    CHECK(AROSRestrictedServiceInit(&a,&space,101) && AROSRestrictedServiceInit(&b,&space,102),"channels-init");
    CHECK(space.channels==2 && a.channel!=b.channel,"channels-distinct");
    CHECK(AROSRestrictedServiceSubmit(&a,AROS_RESTRICTED_DATA,AROS_RESTRICTED_DATA+32,8) &&
        AROSRestrictedServiceSubmit(&b,AROS_RESTRICTED_DATA,AROS_RESTRICTED_DATA+64,8),"channels-submit");
    struct Task wa={1},wb={2};int ca,cb;
    struct AROSRestrictedWork aw,bw;
    CHECK(AROSRestrictedServiceTake(&wa,&ca,&aw) && AROSRestrictedServiceTake(&wb,&cb,&bw),"channels-take");
    CHECK(aw.channel==a.channel && bw.channel==b.channel,"channels-route");
    CHECK(!AROSRestrictedServiceClose(&a) && space.channels==2,"close-held-channel");
    CHECK(!space.domain.closing && !space.mapping.closing,"channel-close-keeps-domain");
    memset(bw.bytes,0x88,bw.length);
    CHECK(AROSRestrictedServiceComplete(&wb,&bw) && AROSRestrictedServiceCollect(&b,bw.token) && data[64]==0x88,"other-channel-completes");
    memset(aw.bytes,0x77,aw.length);
    CHECK(AROSRestrictedServiceComplete(&wa,&aw) && data[32]==0x31,"closed-channel-no-write");
    CHECK(AROSRestrictedServiceClose(&a) && space.channels==1 && !space.domain.closing,"channel-detaches");
    struct AROSRestrictedWork stale=aw;
    memset(&a,0,sizeof(a));
    CHECK(AROSRestrictedServiceInit(&a,&space,103) && a.channel!=stale.channel,"channel-generation");
    CHECK(AROSRestrictedServiceSubmit(&a,AROS_RESTRICTED_DATA,AROS_RESTRICTED_DATA+96,8) &&
        AROSRestrictedServiceTake(&wa,&ca,&aw),"reused-channel-take");
    CHECK(aw.token==stale.token,"reused-request-token");
    CHECK(!AROSRestrictedServiceState(&wa,&stale) && !AROSRestrictedServiceComplete(&wa,&stale),"stale-channel-refused");
    CHECK(AROSRestrictedServiceComplete(&wa,&aw) && AROSRestrictedServiceCollect(&a,aw.token),"reused-channel-complete");
    CHECK(!AROSRestrictedSpaceClose(&space),"space-waits-for-channels");
    uint64_t physical=0;
    CHECK(AArch64MappingPhysical(&ops,root,tcr,AROS_RESTRICTED_DATA,&physical) && physical==(uintptr_t)data,"space-keeps-live-backing");
    CHECK(!AROSRestrictedServiceSubmit(&b,AROS_RESTRICTED_DATA,AROS_RESTRICTED_DATA+64,8),"closed-space-admission");
    CHECK(AROSRestrictedServiceClose(&a) && AROSRestrictedServiceClose(&b) && !space.channels,"channels-drain");
    CHECK(AROSRestrictedSpaceClose(&space) && KrnMappingDomainCanReclaim(&space.domain),"space-reclaim");
    CHECK(!AArch64MappingPhysical(&ops,root,tcr,AROS_RESTRICTED_DATA,&physical) && f->invalidations,"space-unmapped");
    memset(&space,0,sizeof(space));
    CHECK(AROSRestrictedSpaceInit(&space,11,root,tcr,(uintptr_t)data,&ops),"space-reuse");
    CHECK(space.handle!=first_handle,"space-generation");
    CHECK(AROSRestrictedSpaceClose(&space),"reused-space-close");
    free(data);free(f);region_tests();shared_tests();channel_growth();publication_tests();puts("PASS restricted space and channel ownership");return 0;
}
