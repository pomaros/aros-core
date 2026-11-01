/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <aros/apple/restricted.h>
#define R_PAGE 4096U
#define KMAP_EXEC 4
struct AROSRestrictedSpace { uint64_t handle; };
struct RestrictedDomain { unsigned closing;void *code;size_t code_bytes;struct AROSRestrictedSpace space; };
struct RestrictedCodeIdentity { uint64_t pc,generation,reservation;uintptr_t physical; };
struct AROSAppleRuntimeFrame { uint64_t x[31],sp,elr,spsr,esr,far,vector,service,status,task_tls,simd[66],return_ttbr0,entry_ttbr0; };
struct Service { struct { void *queue; } request; };
struct Restricted {
 struct RestrictedDomain *domain;void *fault_stack;uint64_t stack_address,fault_address;size_t stack_bytes,fault_bytes;
 unsigned fault_active,callback_active,wait_pending,exception_active;
 uint64_t fault_handler,fault_generation,exception_handler,exception_generation;
 struct RestrictedCodeIdentity handler_code,exception_code;
 struct Service service;
 struct { unsigned rejected,denied,exceptions;int status; } result;
 unsigned bounds_fault,bounds_calls,restore_ok;
};
static uintptr_t dynamic=UINT64_C(0x200000000);
static uint64_t generation=7,reservation=9;
static unsigned executable=1;
static int AROSRestrictedSpacePageIdentity(struct AROSRestrictedSpace *s,uintptr_t va,unsigned access,
 uintptr_t *pa,uint64_t *gen,uint64_t *res)
{
 if (!s->handle || va<dynamic || va>=dynamic+4096 || (access==KMAP_EXEC && !executable)) return 0;
 *pa=0x8000;*gen=generation;*res=reservation;return 1;
}
static void r_stack_bounds(struct Restricted *r,int fault) { r->bounds_fault=fault;++r->bounds_calls; }
static int r_restore_context(struct Restricted *r,struct AROSAppleRuntimeFrame *frame) { (void)frame;return r->restore_ok; }
static void r_finish(struct Restricted *r,void *execution,struct AROSAppleRuntimeFrame *frame,int status)
{ (void)execution;r->result.status=status;frame->elr=0; }
#include "actual.inc"
static unsigned char fault_stack[8192];
static struct RestrictedDomain domain;
static struct Restricted slot;
static struct AROSAppleRuntimeFrame frame;
static void reset(void)
{
 memset(&domain,0,sizeof(domain));memset(&slot,0,sizeof(slot));memset(fault_stack,0x5a,sizeof(fault_stack));
 domain.code=(void *)0x100000;domain.code_bytes=8192;domain.space.handle=1;
 slot.domain=&domain;slot.stack_address=0x4000;slot.stack_bytes=4096;
 slot.fault_stack=fault_stack;slot.fault_address=0x6000;slot.fault_bytes=sizeof(fault_stack);
 generation=7;reservation=9;executable=1;
 for (unsigned i=0;i<31;++i) frame.x[i]=0x0101010101010101ULL*(i+1);
 for (unsigned i=0;i<66;++i) frame.simd[i]=0x1111111111111111ULL*(i+1);
 frame.sp=0x4f00;frame.elr=AROS_RESTRICTED_CODE+0x40;frame.spsr=0x6000004d;
 frame.esr=UINT64_C(0x92000045);frame.far=0x1234;frame.vector=8;frame.task_tls=0x77;
}
static void frame_svc(unsigned svc) { frame.esr=(UINT64_C(0x15)<<26)|svc; }
static void registered(void)
{
 frame.x[0]=dynamic+8;frame_svc(AROS_RESTRICTED_EXCEPTION_HANDLER);
 assert(dispatch(&slot,&frame,AROS_RESTRICTED_EXCEPTION_HANDLER)==1 && frame.x[0]==1);
 assert(slot.exception_handler==dynamic+8 && !slot.result.rejected);
 frame.esr=UINT64_C(0x92000045);
}
static void delivered(void)
{
 reset();registered();struct AROSAppleRuntimeFrame fault=frame;
 assert(r_deliver_exception(&slot,&frame));
 const struct AROSRestrictedException *record=(const void *)(fault_stack+sizeof(fault_stack)-sizeof(*record));
 assert(!memcmp(record->context.x,fault.x,sizeof(fault.x)) && record->context.sp==fault.sp && record->context.pc==fault.elr);
 assert(record->context.nzcv==0x60000000 && !memcmp(record->context.simd,fault.simd,sizeof(record->context.simd)));
 assert(record->context.fpcr==fault.simd[64] && record->context.fpsr==fault.simd[65]);
 assert(record->esr==fault.esr && record->far==fault.far && record->generation==1 && !record->reserved);
 assert(frame.elr==dynamic+8 && frame.x[0]==0x6000+sizeof(fault_stack)-sizeof(*record) && frame.sp==frame.x[0]);
 assert(frame.x[1]==fault.esr && frame.x[2]==fault.far && frame.x[3]==1 && !frame.x[30]);
 assert(frame.spsr==fault.spsr && frame.task_tls==fault.task_tls && frame.vector==fault.vector);
 assert(slot.exception_active && slot.result.exceptions==1 && slot.bounds_fault==1 && slot.bounds_calls==1);
 /* Once active, a second exception ends the domain rather than nesting. */
 assert(!r_deliver_exception(&slot,&frame));
}
static void refusals(void)
{
 reset();assert(!r_deliver_exception(&slot,&frame) && !slot.result.exceptions && !slot.exception_active);
 reset();registered();slot.exception_active=1;assert(!r_deliver_exception(&slot,&frame));
 reset();registered();slot.fault_active=1;assert(!r_deliver_exception(&slot,&frame));
 reset();registered();slot.callback_active=1;assert(!r_deliver_exception(&slot,&frame));
 reset();registered();slot.wait_pending=1;assert(!r_deliver_exception(&slot,&frame));
 reset();registered();slot.service.request.queue=&slot;assert(!r_deliver_exception(&slot,&frame));
 reset();registered();slot.fault_bytes=sizeof(struct AROSRestrictedException)+1008;assert(!r_deliver_exception(&slot,&frame));
 reset();registered();slot.fault_bytes=sizeof(struct AROSRestrictedException)+1024;assert(r_deliver_exception(&slot,&frame));
 reset();registered();slot.fault_stack=NULL;assert(!r_deliver_exception(&slot,&frame));
 reset();registered();++generation;assert(!r_deliver_exception(&slot,&frame));
 reset();registered();executable=0;assert(!r_deliver_exception(&slot,&frame));
 reset();registered();slot.exception_generation=UINT64_MAX;assert(!r_deliver_exception(&slot,&frame));
 /* Trapped system register access (EC 0x18) and asynchronous entries are not guest exceptions. */
 reset();registered();frame.esr=UINT64_C(0x18)<<26;assert(!r_deliver_exception(&slot,&frame));
 reset();registered();frame.esr=UINT64_C(0x2f)<<26;assert(!r_deliver_exception(&slot,&frame));
 static const unsigned classes[]={0x00,0x20,0x21,0x22,0x24,0x25,0x26,0x2c,0x3c};
 for (unsigned i=0;i<sizeof(classes)/sizeof(classes[0]);++i) {
  reset();registered();frame.esr=(uint64_t)classes[i]<<26;assert(r_deliver_exception(&slot,&frame) && slot.result.exceptions==1);
 }
}
static void registration(void)
{
 reset();frame.x[0]=dynamic+1;frame_svc(AROS_RESTRICTED_EXCEPTION_HANDLER);
 dispatch(&slot,&frame,AROS_RESTRICTED_EXCEPTION_HANDLER);assert(!frame.x[0] && slot.result.rejected==1 && !slot.exception_handler);
 reset();frame.x[0]=dynamic+4096;dispatch(&slot,&frame,AROS_RESTRICTED_EXCEPTION_HANDLER);assert(!frame.x[0] && !slot.exception_handler);
 reset();frame.x[0]=AROS_RESTRICTED_CODE+8;dispatch(&slot,&frame,AROS_RESTRICTED_EXCEPTION_HANDLER);assert(frame.x[0]==1 && slot.exception_handler==AROS_RESTRICTED_CODE+8);
 frame.x[0]=0;dispatch(&slot,&frame,AROS_RESTRICTED_EXCEPTION_HANDLER);assert(frame.x[0]==1 && !slot.exception_handler);
 reset();registered();slot.exception_active=1;frame.x[0]=dynamic+16;dispatch(&slot,&frame,AROS_RESTRICTED_EXCEPTION_HANDLER);
 assert(frame.x[0]==UINT64_MAX && slot.result.denied==1 && slot.exception_handler==dynamic+8);
 reset();registered();slot.fault_active=1;frame.x[0]=dynamic+16;dispatch(&slot,&frame,AROS_RESTRICTED_EXCEPTION_HANDLER);
 assert(!frame.x[0] && slot.exception_handler==dynamic+8);
}
static void continuation(void)
{
 /* While the handler runs, only EXIT and RESTORE_CONTEXT are admitted. */
 reset();registered();assert(r_deliver_exception(&slot,&frame));
 frame.x[0]=5;assert(dispatch(&slot,&frame,AROS_RESTRICTED_FAULT_HANDLER)==1 && frame.x[0]==UINT64_MAX && slot.result.denied==1);
 assert(dispatch(&slot,&frame,AROS_RESTRICTED_EXCEPTION_HANDLER)==1 && frame.x[0]==UINT64_MAX && slot.result.denied==2);
 assert(dispatch(&slot,&frame,AROS_RESTRICTED_EXIT)==7 && slot.result.denied==2);
 slot.restore_ok=0;assert(dispatch(&slot,&frame,AROS_RESTRICTED_RESTORE_CONTEXT)==1 && !frame.x[0] && slot.result.rejected==1);
 assert(slot.exception_active && slot.bounds_calls==1);
 slot.restore_ok=1;frame.x[0]=0x4e00;assert(dispatch(&slot,&frame,AROS_RESTRICTED_RESTORE_CONTEXT)==1 && frame.x[0]==0x4e00);
 assert(!slot.exception_active && slot.bounds_fault==0 && slot.bounds_calls==2 && slot.result.rejected==1);
 /* A later exception is delivered again with the next generation. */
 frame.esr=UINT64_C(0x92000045);assert(r_deliver_exception(&slot,&frame) && frame.x[3]==2 && slot.result.exceptions==2);
 /* Without an active exception a restore leaves the stack selection alone. */
 reset();slot.restore_ok=1;assert(dispatch(&slot,&frame,AROS_RESTRICTED_RESTORE_CONTEXT)==1 && !slot.bounds_calls);
}
int main(void)
{
 _Static_assert(sizeof(struct AROSRestrictedException)==832,"exception record ABI");
 delivered();refusals();registration();continuation();
 puts("PASS exception delivery (HOST modeled fault stack, no eret)");return 0;
}
