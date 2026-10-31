/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <aros/apple/restricted.h>
#define R_PAGE 4096
#define KMAP_EXEC 4
#define KMAP_READ 1
#define KMAP_WRITE 2
struct AROSRestrictedSpace { uint64_t handle; };
struct RestrictedDomain { unsigned closing;void *code;size_t code_bytes;struct AROSRestrictedSpace space; };
struct RestrictedCodeIdentity { uint64_t pc,generation,reservation;uintptr_t physical; };
struct Frame { uint64_t x[31],elr,sp,esr,far,other[16]; };
struct Service { struct { void *queue;uint64_t token; } request; };
struct Restricted {
 struct RestrictedDomain *domain;unsigned callback_active,fault_active;
 uint64_t callback_pc,fault_handler,fault_generation,seed,stack_address;size_t stack_bytes;
 uintptr_t fault_physical;uint64_t fault_page_generation,fault_reservation;
 struct RestrictedCodeIdentity callback_entry,callback_return,fault_code,handler_code;
 struct Frame callback,fault;struct Service service;
 struct { unsigned rejected,replies,denied,callbacks,resumed;int status; } result;
};
static uintptr_t dynamic=UINT64_C(0x200000000),physical=0x8000;
static uint64_t generation=7,reservation=9;
static unsigned executable=1,submit_ok=1,collect_ok=1;
static int AROSRestrictedSpacePageIdentity(struct AROSRestrictedSpace *s,uintptr_t va,unsigned access,
 uintptr_t *pa,uint64_t *gen,uint64_t *res)
{
 if (!s->handle || va<dynamic || va>=dynamic+4096 || (access==4 && !executable)) return 0;
 *pa=physical;*gen=generation;*res=reservation;return 1;
}
static int AROSRestrictedServiceSubmit(struct Service *s,uintptr_t a,uintptr_t b,uint64_t n)
{ (void)a;(void)b;(void)n;if (!submit_ok || s->request.queue) return 0;s->request.queue=s;s->request.token=17;return 1; }
static int AROSRestrictedServiceCollect(struct Service *s,uint64_t token)
{ if (!collect_ok || !s->request.queue || token!=s->request.token) return 0;s->request.queue=NULL;return 1; }
static void r_finish(struct Restricted *r,void *execution,struct Frame *frame,int status)
{ (void)execution;r->result.status=status;frame->elr=0; }
static void r_stack_bounds(struct Restricted *r,unsigned fault) { (void)r;(void)fault; }
#include "actual.inc"
static struct RestrictedDomain domain;
static struct Restricted slot;
static struct Frame frame;
static void reset(void)
{
 memset(&domain,0,sizeof(domain));memset(&slot,0,sizeof(slot));memset(&frame,0,sizeof(frame));
 domain.code=(void *)0x100000;domain.code_bytes=8192;domain.space.handle=1;
 slot.domain=&domain;slot.stack_address=0x4000;slot.stack_bytes=4096;
 physical=0x8000;generation=7;reservation=9;executable=submit_ok=collect_ok=1;
 frame.elr=AROS_RESTRICTED_CODE+4;frame.sp=0x5000;
}
static void identity(void)
{
 reset();struct RestrictedCodeIdentity id;
 assert(r_capture_code(&slot,dynamic+4,&id) && r_match_code(&slot,&id));
 ++generation;assert(!r_match_code(&slot,&id));--generation;
 ++reservation;assert(!r_match_code(&slot,&id));--reservation;
 physical+=4096;assert(!r_match_code(&slot,&id));physical-=4096;
 executable=0;assert(!r_match_code(&slot,&id));executable=1;
 assert(!r_capture_code(&slot,dynamic+1,&id));
 assert(r_capture_code(&slot,AROS_RESTRICTED_CODE+8192-4,&id));
 assert(id.physical==0x101000 && r_match_code(&slot,&id));
 assert(!r_capture_code(&slot,AROS_RESTRICTED_CODE+8192,&id));
 domain.closing=1;assert(!r_match_code(&slot,&id));
}
static void submit(void) { frame.x[3]=dynamic;assert(dispatch(&slot,&frame,AROS_RESTRICTED_SUBMIT)==1 && frame.x[0]==17); }
static void collect(void) { frame.x[0]=17;assert(dispatch(&slot,&frame,AROS_RESTRICTED_COLLECT)==1); }
static void callback(void)
{
 reset();submit();struct RestrictedCodeIdentity id=slot.callback_entry;
 frame.x[3]=AROS_RESTRICTED_CODE;dispatch(&slot,&frame,AROS_RESTRICTED_SUBMIT);
 assert(!frame.x[0] && !memcmp(&id,&slot.callback_entry,sizeof(id)));
 ++generation;collect();assert(slot.result.status==-4 && !slot.callback_active);
 reset();submit();frame.elr=dynamic+4;frame.other[11]=1234;collect();
 assert(slot.callback_active && frame.elr==dynamic);
 struct Frame saved=slot.callback;frame.x[0]=0;
 dispatch(&slot,&frame,AROS_RESTRICTED_CALLBACK_RETURN);
 assert(!memcmp(&frame,&saved,sizeof(frame)) && slot.result.callbacks==1 && !slot.callback_active);
 reset();submit();frame.elr=dynamic+4;collect();++generation;frame.x[0]=0;
 dispatch(&slot,&frame,AROS_RESTRICTED_CALLBACK_RETURN);assert(slot.result.status==-4);
 reset();submit();executable=0;collect();assert(slot.result.status==-4);
}
static void fault(void)
{
 reset();frame.x[0]=dynamic;dispatch(&slot,&frame,AROS_RESTRICTED_FAULT_HANDLER);
 assert(frame.x[0]==1 && r_match_code(&slot,&slot.handler_code));
 slot.fault_active=1;frame.x[0]=0;dispatch(&slot,&frame,AROS_RESTRICTED_FAULT_HANDLER);
 assert(!frame.x[0] && slot.fault_handler==dynamic);
 ++generation;assert(!r_match_code(&slot,&slot.handler_code));--generation;
 slot.fault=frame;slot.fault.elr=dynamic+4;slot.fault.far=dynamic+128;slot.fault.sp=0x5000;
 slot.fault_generation=3;slot.fault_physical=physical;slot.fault_page_generation=generation;slot.fault_reservation=reservation;
 assert(r_capture_code(&slot,slot.fault.elr,&slot.fault_code));
 ++generation;slot.fault_page_generation=generation;
 frame.x[0]=slot.fault.elr;frame.x[1]=3;dispatch(&slot,&frame,AROS_RESTRICTED_FAULT_RESUME);
 assert(frame.x[0]==UINT64_MAX && slot.fault_active && !slot.result.resumed);
 --generation;slot.fault_page_generation=generation;frame.x[0]=slot.fault.elr;frame.x[1]=3;
 struct Frame saved=slot.fault;dispatch(&slot,&frame,AROS_RESTRICTED_FAULT_RESUME);
 assert(!slot.fault_active && slot.result.resumed==1 && !memcmp(&frame,&saved,sizeof(frame)));
}
int main(void) { identity();callback();fault();puts("PASS continuation identity transitions"); }
