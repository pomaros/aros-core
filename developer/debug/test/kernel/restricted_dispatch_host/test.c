/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
typedef void *APTR;
#define R_COOKIE 123
#define TASK_SPSR_ALLOWED UINT64_C(0xf00003c0)
struct AROSAppleRuntimeFrame {
 uint64_t x[31],sp,elr,spsr,simd[66],esr,far,entry_ttbr0,return_ttbr0,task_tls;
};
struct Task { void *tc_SPLower,*tc_SPUpper,*tc_SPReg;int tc_IDNestCnt; };
struct AROSAppleTaskExecution { uint64_t root,user_tls;void *domain;uint64_t reserved; };
struct ExceptionContext { uint64_t x[29],fp,lr,sp,pc,cpsr;void *fpuContext; };
struct Domain { unsigned closing;void *data; };
struct Restricted {
 unsigned cookie,callback_active,fault_active,wait_pending,wait_result,suspend_count;uint64_t wait_deadline;struct Task *owner;struct Domain *domain;
 void *native_lower,*native_upper;struct AROSAppleRuntimeFrame native;
 struct { int64_t status;uint64_t value,esr,far,pc; } result;
};
static struct ExceptionContext saved;
static struct AROSAppleTaskExecution execution;
static struct { uint64_t trusted_ttbr0,kernel_tls; } local;
static uint64_t AROSAppleKernelLastDispatchSPSR,task_pointer;
static unsigned coordinated,context_present=1;
static uint64_t timed_waiters;
static struct ExceptionContext *r_context(struct Task *task) { (void)task;return context_present?&saved:NULL; }
static struct AROSAppleTaskExecution *AROSAppleTaskExecution(struct ExceptionContext *c) { assert(c==&saved);return &execution; }
static void r_coordinate(void) { assert(!coordinated);coordinated=1; }
static void r_uncoordinate(void) { assert(coordinated);coordinated=0; }
static int r_stack_sp(struct Restricted *r,uint64_t sp) { assert(coordinated);return sp>(uintptr_t)r->owner->tc_SPLower && sp<=(uintptr_t)r->owner->tc_SPUpper; }
static void cpu_ApplyTaskMasks(uint64_t *s,int nesting) { (void)nesting;*s&=~UINT64_C(0xc0); }
static void AROSAppleJitRestore(struct Task *task) { (void)task;assert(!coordinated); }
static void AROSAppleTaskTLSRestore(struct ExceptionContext *c) { assert(c==&saved);task_pointer=0x9000; }
static uint64_t AROSAArch64ReadTaskPointer(void) { return task_pointer; }
#define AROSAppleRuntimeLocal() (&local)
#include "actual.inc"
static struct Task task;
static struct Domain domain;
static struct Restricted slot;
static uint64_t data,simd[66];
static void reset(void)
{
 memset(&saved,0,sizeof(saved));memset(&execution,0,sizeof(execution));memset(&slot,0,sizeof(slot));
 task=(struct Task){(void *)0x1000,(void *)0x2000,NULL,-1};domain=(struct Domain){0,&data};
 slot.cookie=R_COOKIE;slot.owner=&task;slot.domain=&domain;slot.native_lower=(void *)0x7000;slot.native_upper=(void *)0x8000;
 slot.native.sp=0x7800;slot.native.elr=0xab00;slot.native.spsr=0xc0;slot.native.task_tls=0x9000;slot.native.return_ttbr0=0xa000;
 for(unsigned i=0;i<31;i++)slot.native.x[i]=i+100;
 for(unsigned i=0;i<66;i++){slot.native.simd[i]=i+200;simd[i]=i+300;}
 slot.callback_active=slot.fault_active=1;
 execution=(struct AROSAppleTaskExecution){0xb000,0xc000,&slot,0};
 saved.sp=0x1800;saved.pc=0x4000;saved.fpuContext=simd;saved.x[18]=0x1234;
 local.trusted_ttbr0=0xa000;data=17;context_present=1;
}
static void cancelled(void)
{
 reset();slot.wait_pending=1;slot.wait_deadline=200;slot.suspend_count=2;timed_waiters=1;domain.closing=1;struct AROSAppleRuntimeFrame f={0};restore(&f,&task,&saved);
 assert(slot.result.status==-5 && execution.root==0);
 assert(!timed_waiters && !slot.wait_pending && !slot.wait_result && !slot.wait_deadline && !slot.suspend_count);
 assert(f.elr==slot.native.elr && f.sp==slot.native.sp && task.tc_SPReg==(void *)(uintptr_t)slot.native.sp);
 assert(f.return_ttbr0==0xa000 && f.task_tls==0x9000 && local.kernel_tls==0x9000);
 assert(!(f.spsr&0xc0) && AROSAppleKernelLastDispatchSPSR==f.spsr);
 assert(!slot.callback_active && !slot.fault_active && slot.result.value==17);
 assert(f.x[0]==1 && f.x[18]==slot.native.x[18] && !memcmp(f.simd,slot.native.simd,sizeof(f.simd)));
 assert(task.tc_SPLower==slot.native_lower && task.tc_SPUpper==slot.native_upper && !coordinated);
 reset();slot.wait_pending=1;slot.wait_result=2;domain.closing=1;restore(&f,&task,&saved);
 assert(slot.result.status==-5 && f.x[0]==1 && !slot.wait_pending && !slot.wait_result);
}
static void active(void)
{
 reset();struct AROSAppleRuntimeFrame f={0};restore(&f,&task,&saved);
 assert(execution.root==0xb000 && f.return_ttbr0==0xb000 && f.task_tls==0xc000);
 assert(f.sp==saved.sp && f.elr==saved.pc && f.x[18]==saved.x[18] && !memcmp(f.simd,simd,sizeof(simd)));
 reset();saved.sp=0x2800;restore(&f,&task,&saved);assert(slot.result.status==-3 && !execution.root);
 reset();execution.root=0;domain.closing=1;restore(&f,&task,&saved);
 assert(f.elr==saved.pc && f.sp==saved.sp && f.return_ttbr0==0xa000 && f.task_tls==0x9000);
}
int main(void) { cancelled();active();puts("PASS dispatch close/native return, active context and invalid stack"); }
