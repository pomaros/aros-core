/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define R_PAGE 4096U
#include "protection.h"
#include <aros/apple/restricted.h>
struct Space { int hardware; uint64_t handle,root,tcr; };
struct RestrictedDomain {
    unsigned closing,managed;
    void *code,*data;size_t code_bytes;
    uint64_t root;
    struct Space space;
    size_t table_shortage,table_pages,table_used;
};
struct Restricted {
    struct RestrictedDomain *domain;
    uint64_t stack_address,fault_address,tls_address;
    void *stack,*fault_stack,*tls,*owner;
    size_t stack_bytes,fault_bytes;
    int service;
};
static struct { uint64_t va,pa; } maps[32];
static unsigned mapped,attempts,fail_after=~0U,unmap_fail,service_calls;
static uint64_t *r_resolve(void *c,uint64_t pa) { (void)c;(void)pa;return NULL; }
static uint64_t r_allocate(void *c,unsigned b) { (void)c;(void)b;return 0; }
static void r_clean(void *c,const void *p,size_t n) { (void)c;(void)p;(void)n; }
static void r_invalidate(void *c) { (void)c; }
static void r_sync(void *c,uint64_t va,size_t n) { (void)c;(void)va;(void)n; }
static int AArch64CountPrivilegedRoot(const struct AArch64ProtectionOps *o,uint64_t root,uint64_t tcr,size_t budget,size_t *count)
{ (void)o;(void)root;(void)tcr;assert(budget==SIZE_MAX);*count=4;return 1; }
static int AArch64ClonePrivilegedRoot(const struct AArch64ProtectionOps *o,uint64_t root,uint64_t tcr,size_t budget,uint64_t *result)
{ (void)o;(void)root;(void)tcr;assert(budget==4);*result=1;return 1; }
static int AROSRestrictedSpaceInit(struct Space *s,uintptr_t owner,uint64_t root,uint64_t tcr,uintptr_t data,const struct AArch64ProtectionOps *o)
{ (void)owner;(void)data;(void)o;s->root=root;s->handle=1;s->tcr=tcr;return 1; }
static int AArch64MapPage(void *ops,uint64_t root,uint64_t tcr,uint64_t va,uint64_t pa,unsigned protection,int fresh)
{
    (void)ops;(void)tcr;assert(root==1 && (protection==11 || protection==13) && fresh);
    if (attempts++==fail_after) return 0;
    for (unsigned i=0;i<mapped;++i) assert(maps[i].va!=va);
    assert(mapped<32);maps[mapped].va=va;maps[mapped++].pa=pa;return 1;
}
static int AArch64MappingPhysical(int *ops,uint64_t root,uint64_t tcr,uint64_t va,uint64_t *pa)
{
    (void)ops;(void)root;(void)tcr;
    for (unsigned i=0;i<mapped;++i) if (maps[i].va==va) { *pa=maps[i].pa;return 1; }
    return 0;
}
static int AArch64UnmapPage(int *ops,uint64_t root,uint64_t tcr,uint64_t va,uint64_t pa)
{
    (void)ops;(void)root;(void)tcr;
    if (unmap_fail) return 0;
    for (unsigned i=0;i<mapped;++i) if (maps[i].va==va) {
        assert(maps[i].pa==pa);maps[i]=maps[--mapped];return 1;
    }
    assert(0);return 0;
}
static int AROSRestrictedServiceInit(int *service,struct Space *space,uintptr_t owner)
{ (void)service;(void)space;(void)owner;++service_calls;return 1; }
#include "actual.inc"
int main(void)
{
    struct RestrictedDomain d={.managed=1,.root=1,.space={.root=1,.handle=1},.table_pages=64};
    struct Restricted r={.domain=&d,.stack_address=0x1ff000,.fault_address=0x211000,.tls_address=0x215000,
        .stack=(void *)0x800000,.fault_stack=(void *)0x900000,.tls=(void *)0xa00000,
        .stack_bytes=17*R_PAGE,.fault_bytes=3*R_PAGE};
    for (unsigned failure=0;failure<=21;++failure) {
        mapped=attempts=service_calls=0;fail_after=failure;
        assert(r_prepare(&r,0)==(failure==21));
        assert(mapped==failure && service_calls==(failure==21));
        for (unsigned i=0;i<mapped;++i) {
            uint64_t va=maps[i].va;
            assert((va>=r.stack_address && va<r.stack_address+r.stack_bytes) ||
                (va>=r.fault_address && va<r.fault_address+r.fault_bytes) || va==r.tls_address);
        }
        if (mapped) {
            unmap_fail=1;assert(!r_unmap_thread(&r) && mapped==failure);unmap_fail=0;
            uint64_t pa=maps[0].pa;maps[0].pa+=R_PAGE;
            assert(!r_unmap_thread(&r) && mapped==failure);maps[0].pa=pa;
        }
        assert(r_unmap_thread(&r) && !mapped);
    }
    attempts=0;d.table_pages=0;
    assert(!r_prepare(&r,0) && !attempts && d.table_shortage==10);
    for (unsigned failure=0;failure<=3;++failure) {
        d=(struct RestrictedDomain){.managed=1,.code=(void *)0x800000,.data=(void *)0x900000,.code_bytes=2*R_PAGE+4,.table_pages=10};
        mapped=attempts=0;fail_after=failure;
        assert(r_prepare_domain(&d,123)==(failure==3));
        assert(d.root==1 && mapped==failure);
        for (unsigned i=0;i<mapped;++i)
            assert(maps[i].va==AROS_RESTRICTED_CODE+i*R_PAGE && maps[i].pa==0x800000+i*R_PAGE);
        /* Published clone and partial maps remain domain-owned on failure. */
        assert(d.space.handle==(failure==3));
    }
    d=(struct RestrictedDomain){.managed=1,.code=(void *)0x800000,.data=(void *)0x900000,.code_bytes=2*R_PAGE+4};
    attempts=0;assert(!r_prepare_domain(&d,123) && !attempts && !d.root && d.table_shortage==10);
    puts("PASS variable stack maps, guards, shortage and partial rollback");
}
