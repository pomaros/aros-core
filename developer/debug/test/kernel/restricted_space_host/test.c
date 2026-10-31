/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "restricted_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x,n) do { if (!(x)) { puts("FAIL " n); exit(1); } } while(0)
#define PAGE 4096U
struct Task { unsigned id; };
struct Fixture { unsigned used, invalidations; unsigned char tables[16][PAGE] __attribute__((aligned(PAGE))); };
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
{ (void)opaque;(void)va;(void)bytes; }
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
    free(data);free(f);puts("PASS restricted space and channel ownership");return 0;
}
