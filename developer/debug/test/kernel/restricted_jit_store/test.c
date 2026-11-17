/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <stdint.h>
#include <stddef.h>
#include <aros/apple/runtime.h>
#include "release_store.h"
#ifdef FREESTANDING
void *memcpy(void *,const void *,size_t);
void *memset(void *,int,size_t);
int memcmp(const void *,const void *,size_t);
void fail(const char *);
void passed(void);
#else
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
static void fail(const char *s) { fprintf(stderr,"FAIL %s\n",s);exit(1); }
static void passed(void) { puts("PASS release stores and restricted admission"); }
#endif
#define CHECK(v,s) do { if (!(v)) fail(s); } while (0)
#define R_PAGE 4096U
#define KMAP_WRITE 2
#define KMAP_EXEC 4
#define ECF_JITWRITE 1
#define AROS_APPLE_JIT_SPRR 1
struct ExceptionContext { unsigned Flags; };
struct AROSRestrictedSpace { unsigned handle; };
struct RestrictedDomain { struct AROSRestrictedSpace space; };
struct Restricted { struct RestrictedDomain *domain; };
static struct RestrictedDomain domain={{1}};
static struct Restricted slot={&domain};
static struct ExceptionContext context;
static unsigned mode=1,code_access=6,data_access=6;
static unsigned AROSAppleJitMode(void) { return mode; }
static unsigned char code[R_PAGE] __attribute__((aligned(R_PAGE)));
static unsigned char data[R_PAGE] __attribute__((aligned(R_PAGE)));
static const uint64_t code_va=0x100000,data_va=0x200000;
static int AROSRestrictedSpacePageIdentity(struct AROSRestrictedSpace *space,uintptr_t va,
    unsigned access,uintptr_t *pa,uint64_t *generation,uint64_t *reservation)
{
    if (space!=&domain.space || !space->handle) return 0;
    if (va>=code_va && va<code_va+R_PAGE && (code_access&access)==access) *pa=(uintptr_t)code;
    else if (va>=data_va && va<data_va+R_PAGE && (data_access&access)==access) *pa=(uintptr_t)data;
    else return 0;
    *generation=7;*reservation=9;return 1;
}
#include "actual.inc"
struct StoreCase {
    void (*execute)(void *,uint64_t);
    const uint32_t *instruction;
    unsigned bytes,rn,rt;
    int offset;
};
#include "corpus.inc"
static unsigned char expected[R_PAGE] __attribute__((aligned(R_PAGE)));
static unsigned char observed[R_PAGE] __attribute__((aligned(R_PAGE)));
static unsigned char emulated[R_PAGE] __attribute__((aligned(R_PAGE)));
/* Keep one production call visible for checking the compiler's release
 * instructions. Sequential byte comparisons cannot establish ordering. */
__attribute__((noinline)) void AROSReleaseStoreTest(const struct AArch64ReleaseStore *store,void *alias)
{ AArch64PerformReleaseStore(store,alias); }
static void differential(void)
{
    for (unsigned c=0;c<sizeof(cases)/sizeof(cases[0]);++c) {
        const struct StoreCase *t=&cases[c];
        for (unsigned trial=0;trial<16;++trial) {
            uint64_t value=UINT64_C(0x81020304ffeeffee)^(UINT64_C(0x0101010101010101)*trial);
            uint64_t x[31]={0};struct AArch64ReleaseStore store;
            memset(expected,0xa5,R_PAGE);memset(observed,0xa5,R_PAGE);memset(emulated,0xa5,R_PAGE);
            uint64_t base=(uintptr_t)(emulated+2048);
            if (t->rn!=31) x[t->rn]=base;
            if (t->rt!=31) x[t->rt]=value;
            CHECK(AArch64DecodeReleaseStore(*t->instruction,x,base,&store),"decode-assembled");
            CHECK(store.bytes==t->bytes && store.address==base+t->offset &&
                store.value==(t->rt==31 ? 0 : value),"decoded-operands");
            for (unsigned b=0;b<t->bytes;++b)
                expected[2048+t->offset+b]=(unsigned char)((t->rt==31 ? 0 : value)>>(8*b));
            t->execute(observed+2048,value);
            AROSReleaseStoreTest(&store,(void *)(uintptr_t)store.address);
            CHECK(!memcmp(expected,observed,R_PAGE),"real-instruction");
            CHECK(!memcmp(observed,emulated,R_PAGE),"emulated-instruction");
        }
    }
}
static void refusals(void)
{
    uint64_t x[31]={0};struct AArch64ReleaseStore out,sentinel;
    memset(&sentinel,0xa7,sizeof(sentinel));out=sentinel;
    x[4]=0x200000;x[20]=0xffeeffee;
    /* Same address, but load, ordinary store, pair and atomic opcodes must not
     * be mistaken for a release store. Reserved low encoding bits too. */
    const uint32_t invalid[]={0x99408094,0xb9000894,0xa9005094,0x889ffc94,0x99008494,0xffffffff,0};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        CHECK(!AArch64DecodeReleaseStore(invalid[i],x,0,&out),"wrong-opcode");
        CHECK(!memcmp(&out,&sentinel,sizeof(out)),"decode-refusal-state");
    }
    x[4]++;
    CHECK(!AArch64DecodeReleaseStore(0x99008094,x,0,&out),"unaligned-store");
    x[4]=UINT64_MAX-3;
    CHECK(!AArch64DecodeReleaseStore(0x99008094,x,0,&out),"address-overflow");
    /* The assembler supplies the SP and negative-offset encodings. */
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        const struct StoreCase *t=&cases[i];
        if (t->rn==31) CHECK(!AArch64DecodeReleaseStore(*t->instruction,x,3,&out),"sp-alignment");
        if (t->offset<0 && t->rn!=31) {
            x[t->rn]=0;
            CHECK(!AArch64DecodeReleaseStore(*t->instruction,x,0,&out),"address-underflow");
        }
    }
}
static struct AROSAppleRuntimeFrame frame(void)
{
    struct AROSAppleRuntimeFrame f;
    memset(&f,0x3c,sizeof(f));f.elr=code_va+128;f.far=data_va+8;
    f.sp=data_va+2048;f.esr=0x9200004f;f.x[4]=data_va;f.x[20]=0xffeeffee;
    uint32_t instruction=0x99008094;
    memcpy(code+128,&instruction,4);memset(data,0xa5,R_PAGE);
    return f;
}
static void integration(void)
{
    struct AROSAppleRuntimeFrame f=frame(),before=f;
    CHECK(r_jit_store_fault(&slot,&context,&f,0x24,15),"classify-jit-store");
    CHECK(r_emulate_jit_store(&slot,&f),"emulate-jit-store");
    before.elr+=4;
    CHECK(!memcmp(&f,&before,sizeof(f)),"only-pc-changes");
    memset(expected,0xa5,R_PAGE);
    expected[8]=0xee;expected[9]=0xff;expected[10]=0xee;expected[11]=0xff;
    CHECK(!memcmp(data,expected,R_PAGE),"owned-store-bytes");
    for (unsigned test=0;test<14;++test) {
        f=frame();mode=1;context.Flags=0;code_access=6;data_access=6;domain.space.handle=1;
        switch (test) {
        case 0: mode=0;break;
        case 1: context.Flags=ECF_JITWRITE;break;
        case 2: code_access=KMAP_EXEC;break;
        case 3: data_access=KMAP_WRITE;break;
        case 4: f.esr&=~UINT64_C(64);break;
        case 5: f.esr|=UINT64_C(1)<<7;break;
        case 6: f.esr|=UINT64_C(1)<<8;break;
        case 7: f.esr|=UINT64_C(1)<<9;break;
        case 8: f.esr|=UINT64_C(1)<<10;break;
        case 9: domain.space.handle=0;break;
        case 10: f.elr=code_va+R_PAGE;break;
        case 11: f.far=data_va+R_PAGE;break;
        case 12: f.esr=0x92000045;break;
        case 13: f.esr=0x8200004f;break;
        }
        before=f;memcpy(expected,data,R_PAGE);
        CHECK(!r_jit_store_fault(&slot,&context,&f,(unsigned)(f.esr>>26)&63,(unsigned)f.esr&63),"classification-refusal");
        CHECK(!memcmp(&f,&before,sizeof(f)) && !memcmp(data,expected,R_PAGE),"classification-no-write");
    }
    mode=1;context.Flags=0;code_access=6;data_access=6;domain.space.handle=1;
    for (unsigned test=0;test<7;++test) {
        f=frame();code_access=6;data_access=6;
        switch (test) {
        case 0: f.elr++;break;
        case 1: f.far+=4;break;
        case 2: f.x[4]++;f.far++;break;
        case 3: code_access=KMAP_WRITE;break;
        case 4: data_access=KMAP_EXEC;break;
        case 5: memset(code+128,0,4);break;
        case 6: f.elr=UINT64_MAX-3;break;
        }
        before=f;memcpy(expected,data,R_PAGE);
        CHECK(!r_emulate_jit_store(&slot,&f),"emulation-refusal");
        CHECK(!memcmp(&f,&before,sizeof(f)) && !memcmp(data,expected,R_PAGE),"refusal-no-write");
    }
}
int main(void) { differential();refusals();integration();passed();return 0; }
