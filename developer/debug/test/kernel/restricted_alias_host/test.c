/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "protection.h"
enum {R_ALLOCATING,R_LIVE,R_REAPING};
struct RestrictedDomain { struct RestrictedDomain *next; unsigned state; uint64_t root; };
static struct RestrictedDomain *restricted_domains;
static unsigned map_lock,rendezvous,coordinator,calls,failed_root,native_fail,native_permissions;
static unsigned roots[4],policies[4];
static int AROSApplePageArenaContains(uintptr_t address) { return address>=0x8000 && address<0x10000; }
static void r_coordinate(void) { assert(map_lock && rendezvous && !coordinator);coordinator=1; }
static void r_uncoordinate(void) { assert(coordinator);coordinator=0; }
static uint64_t *r_resolve(void *c,uint64_t p) { (void)c;(void)p;return NULL; }
static void r_clean(void *c,const void *p,size_t n) { (void)c;(void)p;(void)n; }
static void r_invalidate(void *c) { (void)c; }
static void r_alias_sync(void *c,uint64_t a,size_t n) { (void)c;(void)a;(void)n; }
static int AArch64RemapPrivilegedPage(const struct AArch64ProtectionOps *ops,uint64_t root,uint64_t tcr,uint64_t va,uint64_t pa,unsigned policy)
{
    assert(coordinator && map_lock && rendezvous && va==pa && tcr==16 && calls<4);
    assert(ops->allocate && !ops->allocate(NULL,12));
    assert(ops->synchronize==r_alias_sync);
    roots[calls]=root;policies[calls++]=policy;return root!=failed_root;
}
static struct { uintptr_t address;unsigned permissions; } protection_operation;
static uint64_t *resolve_table(void *c,uint64_t p) { return r_resolve(c,p); }
static uint64_t take_table(void *c,unsigned b) { (void)c;(void)b;return 0; }
static void clean_descriptors(void *c,const void *p,size_t n) { r_clean(c,p,n); }
static void invalidate_translation(void *c) { r_invalidate(c); }
static void synchronize_code_page(void *c,uint64_t a,size_t n) { r_alias_sync(c,a,n); }
static int can_split(void *c,uint64_t d) { (void)c;(void)d;return 0; }
int AArch64ProtectPage(const struct AArch64ProtectionOps *ops,uint64_t root,uint64_t tcr,uint64_t address,unsigned permissions)
{
    (void)ops;assert(map_lock && rendezvous && !coordinator && root==123 && tcr==16 && address==protection_operation.address);
    if(native_fail)return 0;native_permissions=permissions;return 1;
}
#include "actual.inc"
int main(void)
{
    struct RestrictedDomain a={.state=R_LIVE,.root=1},b={.state=R_ALLOCATING,.root=2},
        retired={.state=R_REAPING,.root=999},empty={.state=R_ALLOCATING};
    a.next=&b;b.next=&retired;retired.next=&empty;restricted_domains=&a;
    map_lock=rendezvous=1;protection_operation.address=0x8000;
    const unsigned flags[]={29,11,0,47},expected[]={5,3,0,3};
    for(unsigned i=0;i<4;++i){
        calls=0;protection_operation.permissions=flags[i];
        assert(protect_one_page() && native_permissions==flags[i] && calls==2);
        assert(roots[0]==1 && roots[1]==2 && policies[0]==expected[i] && policies[1]==expected[i]);
        assert(!coordinator);
    }
    calls=0;native_fail=1;assert(!protect_one_page() && !calls && !coordinator);native_fail=0;
    calls=0;failed_root=2;protection_operation.permissions=29;
    assert(!protect_one_page() && calls==2 && native_permissions==29 && !coordinator);
    calls=0;failed_root=0;assert(protect_one_page() && calls==2);
    calls=0;protection_operation.address=0x20000;
    assert(protect_one_page() && !calls && !coordinator);
    puts("PASS arena alias propagation, partial refusal, retry and retired-root exclusion");
}
