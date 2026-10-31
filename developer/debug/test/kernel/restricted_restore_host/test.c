/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <aros/apple/restricted.h>
#define R_PAGE 4096U
#define KMAP_READ 1
#define KMAP_EXEC 4
struct AROSRestrictedSpace { unsigned hardware;uint64_t root,tcr,handle; };
struct RestrictedDomain { unsigned closing;void *code;size_t code_bytes;struct AROSRestrictedSpace space; };
struct RestrictedCodeIdentity { uint64_t pc,generation,reservation;uintptr_t physical; };
struct AROSAppleRuntimeFrame { uint64_t x[31],sp,elr,spsr,esr,far,vector,service,status,task_tls,simd[66],return_ttbr0,entry_ttbr0; };
struct Restricted {
 struct RestrictedDomain *domain;void *stack;uint64_t stack_address,fault_address;size_t stack_bytes,fault_bytes;
 unsigned fault_active,callback_active,wait_pending;uint64_t callback_pc;
 struct { struct { void *queue; } request; } service;
};
static unsigned char stack[8192],data[8192];
static unsigned deny_second,wrong_stack_pa,executable;
static int AArch64UserPagePhysical(void *ops,uint64_t root,uint64_t tcr,uintptr_t va,unsigned access,int user,uint64_t *pa)
{
 (void)ops;(void)root;(void)tcr;(void)access;(void)user;
 if (va<0x10000 || va>=0x12000 || (deny_second && va==0x11000)) return 0;
 *pa=(uintptr_t)stack+(va-0x10000)+wrong_stack_pa;return 1;
}
static int AROSRestrictedSpacePageIdentity(struct AROSRestrictedSpace *space,uintptr_t va,unsigned access,uintptr_t *pa,uint64_t *gen,uint64_t *res)
{
 (void)space;*gen=7;*res=9;
 if (access==KMAP_EXEC) { if (!executable || va<0x30000 || va>=0x31000) return 0;*pa=0x90000;return 1; }
 if (va<0x20000 || va>=0x22000 || (deny_second && va==0x21000)) return 0;
 *pa=(uintptr_t)data+(va-0x20000);return 1;
}
#include "actual.inc"
static struct RestrictedDomain domain;
static struct Restricted slot;
static struct AROSAppleRuntimeFrame frame;
static struct AROSRestrictedContext input;
static void reset(void)
{
 memset(&domain,0,sizeof(domain));memset(&slot,0,sizeof(slot));memset(&frame,0xa5,sizeof(frame));
 domain.code=(void *)0x50000;domain.code_bytes=4096;domain.space.handle=1;
 slot.domain=&domain;slot.stack=stack;slot.stack_address=0x10000;slot.stack_bytes=8192;
 deny_second=wrong_stack_pa=0;executable=1;
 for (unsigned i=0;i<31;++i) input.x[i]=0x1010101010101010ULL*i;
 for (unsigned i=0;i<64;++i) input.simd[i]=0x0102030405060708ULL*i;
 input.sp=0x12000;input.pc=0x30000;input.nzcv=0xb0000000;input.fpcr=0x400000;input.fpsr=0x8000010;
 frame.x[0]=0x10e80;frame.x[1]=sizeof(input);frame.spsr=0x60000040;
}
static void publish(void) { memcpy(stack+0xe80,&input,sizeof(input)); }
static void refused(void)
{
 publish();struct AROSAppleRuntimeFrame old=frame;
 assert(!r_restore_context(&slot,&frame));assert(!memcmp(&old,&frame,sizeof(frame)));
}
int main(void)
{
 _Static_assert(sizeof(struct AROSRestrictedContext)==800,"context ABI");
 reset();publish();struct AROSAppleRuntimeFrame old=frame;
 assert(r_restore_context(&slot,&frame));
 assert(!memcmp(frame.x,input.x,sizeof(input.x)) && !memcmp(frame.simd,input.simd,sizeof(input.simd)));
 assert(frame.sp==input.sp && frame.elr==input.pc && frame.spsr==0xb0000040);
 assert(frame.simd[64]==input.fpcr && frame.simd[65]==input.fpsr);
 old.sp=frame.sp;old.elr=frame.elr;old.spsr=frame.spsr;
 memcpy(old.x,frame.x,sizeof(frame.x));memcpy(old.simd,frame.simd,sizeof(frame.simd));
 assert(!memcmp(&old,&frame,sizeof(frame))); /* TLS, roots and exception state unchanged */
 reset();memcpy(data+0xe80,&input,sizeof(input));frame.x[0]=0x20e80;assert(r_restore_context(&slot,&frame));
 reset();input.pc=AROS_RESTRICTED_CODE;publish();assert(r_restore_context(&slot,&frame));
 reset();deny_second=1;refused();
 reset();wrong_stack_pa=4096;refused();
 reset();input.pc++;refused();
 reset();executable=0;refused();
 reset();input.sp=0x10000;refused();
 reset();input.sp=0x12010;refused();
 reset();input.sp--;refused();
 reset();input.nzcv|=1;refused();
 reset();input.nzcv|=0x80;refused();
 reset();input.fpcr=UINT64_MAX;refused();
 reset();input.fpsr=UINT64_MAX;refused();
 reset();frame.x[0]=UINT64_MAX-7;refused();
 reset();frame.x[0]++;refused();
 reset();frame.x[0]=0x12000;refused();
 reset();frame.x[1]--;refused();
 reset();domain.closing=1;refused();
 reset();slot.callback_active=1;refused();
 reset();slot.callback_pc=0x30000;refused();
 reset();slot.fault_active=1;refused();
 reset();slot.wait_pending=1;refused();
 reset();slot.service.request.queue=&slot;refused();
 /* Failure on the second source page must not partially copy the record. */
 reset();deny_second=1;struct AROSRestrictedContext output,sentinel;
 memset(&output,0xcc,sizeof(output));sentinel=output;
 assert(!r_read_context(&slot,0x10e80,&output));assert(!memcmp(&output,&sentinel,sizeof(output)));
 assert(!r_read_context(&slot,0x20e80,&output));assert(!memcmp(&output,&sentinel,sizeof(output)));
 puts("PASS restore context (HOST modeled MMU, no eret)");return 0;
}
