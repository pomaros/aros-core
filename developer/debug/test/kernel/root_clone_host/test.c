/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include "mapping.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x,n) do {if(!(x)){puts("FAIL " n);exit(1);}}while(0)
static uint64_t pages[16][2048] __attribute__((aligned(16384)));
static unsigned used, bits, cleaned;
static uint64_t *resolve(void *c,uint64_t pa){(void)c;return (uint64_t *)(uintptr_t)pa;}
static uint64_t allocate(void *c,unsigned b){(void)c;CHECK(b==bits,"granule");if(used==16)return 0;return (uintptr_t)pages[used++];}
static void clean(void *c,const void *p,size_t n){(void)c;(void)p;CHECK(n==(1U<<bits),"clean-size");++cleaned;}
static const struct AArch64ProtectionOps ops={0,resolve,allocate,clean,0,0,0};
static uint64_t tcr(unsigned va){return (64-va)|(UINT64_C(5)<<32)|(bits==14 ? UINT64_C(2)<<14 : 0);}
static void reset(unsigned b){bits=b;used=4;cleaned=0;memset(pages,0,sizeof(pages));}
static void valid(unsigned b)
{
 reset(b);unsigned stride=b-3,va=b+3*stride; // starts at L1
 uint64_t mask=UINT64_C(0x0000ffffffffffff)&~((UINT64_C(1)<<b)-1);
 pages[0][0]=(uintptr_t)pages[1]|3;
 pages[1][0]=(uintptr_t)pages[2]|3;
 uint64_t ro=(UINT64_C(1)<<7)|(UINT64_C(1)<<6)|3|4|(UINT64_C(1)<<10);
 uint64_t rw=(UINT64_C(1)<<6)|3|4|(UINT64_C(1)<<10);
 pages[2][0]=UINT64_C(0x80000000)|ro;
 pages[2][1]=UINT64_C(0x80004000)|rw;
 uint64_t original[2048];memcpy(original,pages[2],sizeof(original));
 uint64_t result=99;
 CHECK(AArch64ClonePrivilegedRoot(&ops,(uintptr_t)pages[0],tcr(va),3,&result),"clone");
 CHECK(result!=(uintptr_t)pages[0] && cleaned==3 && used==7,"independent-tables");
 uint64_t *l2=resolve(0,*(uint64_t *)(uintptr_t)result&mask);
 uint64_t *l3=resolve(0,l2[0]&mask);
 CHECK(!(l3[0]&(UINT64_C(1)<<6)) && !(l3[1]&(UINT64_C(1)<<6)),"deny-el0");
 CHECK((l3[0]&(UINT64_C(1)<<54)) && (l3[1]&(UINT64_C(1)<<54)),"force-uxn");
 CHECK(!(l3[0]&(UINT64_C(1)<<53)) && (l3[1]&(UINT64_C(1)<<53)),"preserve-el1-execute");
 CHECK((l3[0]&(UINT64_C(1)<<7)) && !(l3[1]&(UINT64_C(1)<<7)),"preserve-readonly");
 CHECK(!memcmp(original,pages[2],sizeof(original)),"source-unchanged");
 uint64_t pa=0;CHECK(AArch64TranslateResident(&ops,result,tcr(va),17,&pa) && pa==UINT64_C(0x80000011),"preserve-physical");
}
static void refusals(void)
{
 reset(12);uint64_t result=99;
 pages[0][0]=(uintptr_t)pages[1]|3;
 CHECK(!AArch64ClonePrivilegedRoot(&ops,(uintptr_t)pages[0],tcr(39),1,&result) && result==99,"budget-refusal");
 reset(12);pages[0][1]=UINT64_C(0x40000000)|1; // privileged RWX
 CHECK(!AArch64ClonePrivilegedRoot(&ops,(uintptr_t)pages[0],tcr(39),8,&result) && result==99,"privileged-rwx-refusal");
 pages[0][1]|=UINT64_C(1)<<53;
 CHECK(AArch64ClonePrivilegedRoot(&ops,(uintptr_t)pages[0],tcr(39),8,&result),"privileged-rw-nx");
 reset(12);pages[0][0]=(uintptr_t)pages[1]|3|(UINT64_C(1)<<61);result=99;
 CHECK(!AArch64ClonePrivilegedRoot(&ops,(uintptr_t)pages[0],tcr(39),8,&result) && result==99,"table-restriction-refusal");
 reset(12);pages[0][0]=(uintptr_t)pages[1]|3|4;result=99;
 CHECK(!AArch64ClonePrivilegedRoot(&ops,(uintptr_t)pages[0],tcr(39),8,&result) && result==99,"table-reserved-refusal");
 reset(14);pages[0][2]=(uintptr_t)pages[1]|3;
 CHECK(!AArch64ClonePrivilegedRoot(&ops,(uintptr_t)pages[0],tcr(48),8,&result) && result==99,"unused-root-entry");
}
int main(void){valid(12);valid(14);refusals();puts("PASS privileged root clone geometry permissions ownership refusals");return 0;}
