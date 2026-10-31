/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <aros/apple/restricted.h>
#define __AROSEXEC_SMP__ 1
#define TF_ETASK 1
#define TS_RUN 1
#define TS_READY 2
#define TS_WAIT 3
#define TS_FROZEN 4
#define TS_REMOVED 5
#define SPINLOCK_MODE_WRITE 1
#define IPI_SCHEDULE 1
#define FALSE 0
#define AROS_TASK_PARK_WAIT 1
#define AROS_TASK_PARKED 2
#define AROS_TASK_PARK_SUSPEND 4
#define AROS_TASK_PARK_REASONS (AROS_TASK_PARK_WAIT|AROS_TASK_PARK_SUSPEND)
struct Node { unsigned list; };
struct ExceptionContext { uint64_t saved_x18; };
struct ETask { struct ExceptionContext *et_RegFrame; };
struct Task { unsigned tc_Flags,tc_State,tc_SigWait,tc_SpinLock;struct { struct ETask *tc_ETask; } tc_UnionETask;struct Node tc_Node; };
struct AROSAppleTaskExecution { uint64_t root,park_flags; };
static struct { unsigned TaskWaitSpinLock,TaskReadySpinLock;unsigned TaskWait,TaskReady; } base;
#define SysBase (&base)
#define PrivExecBase(s) (s)
static struct Task task;
static struct ExceptionContext context,foreign;
static struct ETask etask;
static struct AROSAppleTaskExecution execution,foreign_execution;
static unsigned ipis;
static struct { unsigned iet_FrozenState; } iet;
#define GetIntETask(t) (&iet)
static struct Task *current_task=&task;
#define GET_THIS_TASK current_task
#define CORE_SCHEDULER_PARK(t) restricted_park(t)
static void KrnSpinLock(unsigned *lock,void *unused,int mode) { (void)unused;(void)mode;assert(!*lock);*lock=1; }
static void KrnSpinUnLock(unsigned *lock) { assert(*lock);*lock=0; }
static struct AROSAppleTaskExecution *AROSAppleTaskExecution(struct ExceptionContext *c) { assert(c==&context || c==&foreign);return c==&context?&execution:&foreign_execution; }
static void Remove(struct Node *n) { assert(task.tc_SpinLock && n->list);n->list=0; }
static void Enqueue(unsigned *l,struct Node *n) { assert(task.tc_SpinLock && !n->list);n->list=*l; }
static void exec_TaskEnqueueReady(struct Task *t) { Enqueue(&base.TaskReady,&t->tc_Node); }
static void exec_TaskEnqueueWait(struct Task *t) { Enqueue(&base.TaskWait,&t->tc_Node); }
static unsigned AROSAppleSMPOnlineMask(void) { return 3; }
static void AROSAppleSMPSendIPI(unsigned kind,unsigned unused,unsigned mask) { assert(!task.tc_SpinLock && kind==IPI_SCHEDULE && !unused && mask==3);ipis++; }
#define R_LIVE 1
struct RestrictedDomain { unsigned closing; };
struct Restricted { struct Restricted *next;struct RestrictedDomain *domain;unsigned state;struct { uint64_t channel; } service;struct Task *owner;struct ExceptionContext *owner_context;uint64_t wake_generation;uint32_t suspend_count;uint64_t wait_deadline;unsigned wait_pending,wait_result; };
static struct Restricted *restricted_slots;
struct AROSAppleRuntimeFrame { uint64_t x[4]; };
static uint64_t timed_waiters,fake_now=100;
static unsigned coordinator;
static uint64_t r_wait_clock(void) { return fake_now; }
static void r_coordinate(void) { assert(!coordinator);coordinator=1; }
static void r_uncoordinate(void) { assert(coordinator);coordinator=0; }
#include "actual.inc"
static void reset(void)
{
 memset(&task,0,sizeof(task));context.saved_x18=0x1818;execution=(struct AROSAppleTaskExecution){0x1000,0};
 etask.et_RegFrame=&context;task.tc_UnionETask.tc_ETask=&etask;task.tc_Flags=TF_ETASK;task.tc_State=TS_RUN;
 task.tc_Node.list=3;base.TaskWait=1;base.TaskReady=2;base.TaskWaitSpinLock=base.TaskReadySpinLock=0;ipis=0;
 foreign_execution=(struct AROSAppleTaskExecution){0x2000,0};
 iet.iet_FrozenState=TS_WAIT;timed_waiters=0;fake_now=100;coordinator=0;
}
static void save_and_switch(void)
{
 /* Complete frame save precedes the production RUN->READY hook. */
 context.saved_x18=0x1919;task.tc_SpinLock=1;Remove(&task.tc_Node);task.tc_State=TS_READY;
 outgoing(&task);
 if(task.tc_State==TS_READY)exec_TaskEnqueueReady(&task);
 task.tc_SpinLock=0;
}
static void interleavings(void)
{
 reset();assert(AROSAppleRestrictedSetWait(&task,&context,1));
 assert(AROSAppleRestrictedWaitState(&task,&context)==3);
 assert(task.tc_State==TS_RUN && task.tc_Node.list==3 && context.saved_x18==0x1818);
 assert(AROSAppleRestrictedSetWait(&task,&context,0));save_and_switch();
 assert(task.tc_State==TS_READY && task.tc_Node.list==2 && !execution.park_flags);
 reset();assert(AROSAppleRestrictedSetWait(&task,&context,1));save_and_switch();
 assert(task.tc_State==TS_WAIT && task.tc_Node.list==1 && context.saved_x18==0x1919);
 assert(AROSAppleRestrictedWaitState(&task,&context)==7);
 assert(AROSAppleRestrictedSetWait(&task,&context,0));
 assert(task.tc_State==TS_READY && task.tc_Node.list==2 && !execution.park_flags && ipis==1);
 assert(AROSAppleRestrictedWaitState(&task,&context)==1);
 assert(AROSAppleRestrictedSetWait(&task,&context,0) && ipis==1);
}
static void native_signal(void)
{
 reset();AROSAppleRestrictedSetWait(&task,&context,1);save_and_switch();
 /* An ordinary signal moves the task onto Ready; dispatch has removed it
  * under the task lock before this actual hook runs. */
 task.tc_State=TS_READY;task.tc_Node.list=2;
 assert(AROSAppleRestrictedWaitState(&task,&context)==3);
 task.tc_Node.list=0;task.tc_SpinLock=1;
 assert(!incoming(&task));assert(task.tc_State==TS_WAIT && task.tc_Node.list==1 && !task.tc_SpinLock);
 assert(AROSAppleRestrictedSetWait(&task,&context,0));
 task.tc_Node.list=0;task.tc_SpinLock=1;assert(incoming(&task));task.tc_SpinLock=0;
}
static void ownership(void)
{
 reset();assert(!AROSAppleRestrictedSetWait(&task,&foreign,1) && !execution.park_flags);
 assert(!AROSAppleRestrictedWaitState(&task,&foreign));
 execution.root=0;assert(!AROSAppleRestrictedSetWait(&task,&context,1));
 reset();task.tc_State=TS_REMOVED;assert(!AROSAppleRestrictedSetWait(&task,&context,0));
 assert(!AROSAppleRestrictedWaitState(&task,&context));
 reset();AROSAppleRestrictedSetWait(&task,&context,1);save_and_switch();task.tc_State=TS_FROZEN;
 assert(AROSAppleRestrictedSetWait(&task,&context,0));assert(task.tc_State==TS_FROZEN && task.tc_Node.list==1 && !ipis);
 assert(iet.iet_FrozenState==TS_READY);
 reset();task.tc_Flags=0;task.tc_SpinLock=1;assert(!restricted_park(&task));task.tc_SpinLock=0;
}
static void generations(void)
{
 reset();struct RestrictedDomain domain={0},other={0};
 struct Restricted slot={NULL,&domain,R_LIVE,{17},&task,&context,0};restricted_slots=&slot;
 struct AROSAppleRuntimeFrame frame={0};assert(payload(&slot,&frame,&task,&context,AROS_RESTRICTED_THREAD_INFO)==1);
 assert(frame.x[0]==17 && frame.x[1]==0);
 assert(!r_wake_channel(&other,17) && !r_wake_channel(&domain,18));
 assert(r_wake_channel(&domain,17) && slot.wake_generation==1);
 frame.x[0]=0;assert(payload(&slot,&frame,&task,&context,AROS_RESTRICTED_WAIT)==1 && !frame.x[0] && !execution.park_flags);
 frame.x[0]=1;assert(payload(&slot,&frame,&task,&context,AROS_RESTRICTED_WAIT)==2 && frame.x[0]==1);
 save_and_switch();assert(r_wake_channel(&domain,17));assert(slot.wake_generation==2 && task.tc_State==TS_READY);
 slot.wake_generation=UINT64_MAX;assert(!r_wake_channel(&domain,17) && slot.wake_generation==UINT64_MAX);
 frame.x[0]=UINT64_MAX;assert(payload(&slot,&frame,&task,&context,AROS_RESTRICTED_WAIT)==1 && !frame.x[0]);
 reset();slot.wake_generation=0;AROSAppleRestrictedSetWait(&task,&context,1);save_and_switch();domain.closing=1;
 assert(!r_wake_channel(&domain,17));r_cancel_waiters(&domain);
 assert(task.tc_State==TS_READY && !execution.park_flags && ipis==1);
 restricted_slots=NULL;
}
static void suspension(void)
{
 reset();struct RestrictedDomain domain={0},other={0};
 struct Restricted slot={NULL,&domain,R_LIVE,{17},&task,&context,0,0};restricted_slots=&slot;
 assert(r_suspend_channel(&other,17,0)==-1 && r_suspend_channel(&domain,18,0)==-1);
 assert(r_suspend_channel(&domain,17,0)==0 && slot.suspend_count==1);
 assert(ipis==1 && task.tc_State==TS_RUN && AROSAppleRestrictedWaitState(&task,&context)==9);
 save_and_switch();assert(task.tc_State==TS_WAIT && AROSAppleRestrictedWaitState(&task,&context)==13);
 assert(r_suspend_channel(&domain,17,1)==1 && task.tc_State==TS_READY && !execution.park_flags);
 reset();slot.suspend_count=0;
 assert(r_suspend_channel(&domain,17,0)==0 && r_suspend_channel(&domain,17,1)==1);
 save_and_switch();assert(task.tc_State==TS_READY && !execution.park_flags);
 reset();slot.suspend_count=0;task.tc_State=TS_READY;task.tc_Node.list=2;
 assert(r_suspend_channel(&domain,17,0)==0 && task.tc_State==TS_WAIT && !ipis);
 assert(r_suspend_channel(&domain,17,0)==1 && slot.suspend_count==2);
 assert(r_suspend_channel(&domain,17,1)==2);
 assert(slot.suspend_count==1 && task.tc_State==TS_WAIT && !ipis);
 assert(r_suspend_channel(&domain,17,1)==1 && task.tc_State==TS_READY && ipis==1);
 assert(r_suspend_channel(&domain,17,1)==0 && ipis==1);
 reset();slot.suspend_count=0;AROSAppleRestrictedSetWait(&task,&context,1);save_and_switch();
 assert(r_suspend_channel(&domain,17,0)==0 && AROSAppleRestrictedWaitState(&task,&context)==15);
 assert(r_wake_channel(&domain,17));
 assert(task.tc_State==TS_WAIT && AROSAppleRestrictedWaitState(&task,&context)==13);
 assert(r_suspend_channel(&domain,17,1)==1 && task.tc_State==TS_READY);
 reset();slot.suspend_count=0;AROSAppleRestrictedSetWait(&task,&context,1);save_and_switch();
 r_suspend_channel(&domain,17,0);assert(r_suspend_channel(&domain,17,1)==1);
 assert(task.tc_State==TS_WAIT && AROSAppleRestrictedWaitState(&task,&context)==7);
 assert(r_wake_channel(&domain,17) && task.tc_State==TS_READY);
 reset();slot.suspend_count=0;AROSAppleRestrictedSetWait(&task,&context,1);save_and_switch();
 r_suspend_channel(&domain,17,0);r_suspend_channel(&domain,17,0);domain.closing=1;r_cancel_waiters(&domain);
 assert(slot.suspend_count==0 && task.tc_State==TS_READY && !execution.park_flags);
 assert(r_suspend_channel(&domain,17,1)==-1);domain.closing=0;
 reset();slot.suspend_count=0;AROSAppleRestrictedSetWait(&task,&context,1);save_and_switch();
 r_suspend_channel(&domain,17,0);task.tc_State=TS_FROZEN;
 assert(r_wake_channel(&domain,17) && task.tc_State==TS_FROZEN && iet.iet_FrozenState==TS_WAIT);
 assert(r_suspend_channel(&domain,17,1)==1 && task.tc_State==TS_FROZEN && iet.iet_FrozenState==TS_READY && !ipis);
 reset();slot.suspend_count=UINT32_MAX;
 assert(r_suspend_channel(&domain,17,0)==-1 && slot.suspend_count==UINT32_MAX && !execution.park_flags);
 execution.root=0;slot.suspend_count=0;assert(r_suspend_channel(&domain,17,0)==-1);
 restricted_slots=NULL;
}
static void timed_reset(struct Restricted *slot,struct RestrictedDomain *domain,struct AROSAppleRuntimeFrame *frame)
{
 reset();*domain=(struct RestrictedDomain){0};
 *slot=(struct Restricted){.domain=domain,.state=R_LIVE,.service={17},.owner=&task,.owner_context=&context};
 restricted_slots=slot;memset(frame,0,sizeof(*frame));
}
static void timed_arm(struct Restricted *slot,struct AROSAppleRuntimeFrame *frame,uint64_t duration)
{
 frame->x[0]=slot->wake_generation;frame->x[1]=duration;
 assert(payload(slot,frame,&task,&context,AROS_RESTRICTED_WAIT_FOR)==2 && frame->x[0]==1);
 assert(slot->wait_pending && !slot->wait_result && slot->wait_deadline==fake_now+duration && timed_waiters==1);
}
static void timeouts(void)
{
 struct Restricted slot;struct RestrictedDomain domain;struct AROSAppleRuntimeFrame frame;
 assert(r_wait_microseconds(1500000,3000000)==500000);
 assert(r_wait_microseconds(1,1000000)==1);
 assert(r_wait_microseconds(123,0)==UINT64_MAX && r_wait_microseconds(123,UINT64_MAX)==UINT64_MAX);
 assert(r_wait_microseconds(UINT64_MAX,1)==UINT64_MAX);
 timed_reset(&slot,&domain,&frame);frame.x[1]=0;
 assert(payload(&slot,&frame,&task,&context,AROS_RESTRICTED_WAIT_FOR)==1 && frame.x[0]==2 && !timed_waiters && !slot.wait_pending);
 frame.x[0]=0;frame.x[1]=UINT64_MAX;
 assert(payload(&slot,&frame,&task,&context,AROS_RESTRICTED_WAIT_FOR)==1 && !frame.x[0] && !timed_waiters);
 timed_arm(&slot,&frame,50);frame.x[0]=0;frame.x[1]=10;
 assert(payload(&slot,&frame,&task,&context,AROS_RESTRICTED_WAIT_FOR)==1 && !frame.x[0] && slot.wait_deadline==150 && timed_waiters==1);
 save_and_switch();fake_now=149;AROSAppleRestrictedExpireWaits();
 assert(task.tc_State==TS_WAIT && !slot.wait_result && timed_waiters==1);
 fake_now=150;AROSAppleRestrictedExpireWaits();
 assert(task.tc_State==TS_READY && slot.wait_result==2 && slot.wait_pending && !timed_waiters);
 r_deliver_wait(&slot,&frame);assert(frame.x[0]==2 && !slot.wait_pending && !slot.wait_result);
 frame.x[0]=77;r_deliver_wait(&slot,&frame);assert(frame.x[0]==77);
 timed_reset(&slot,&domain,&frame);timed_arm(&slot,&frame,1);fake_now=101;
 AROSAppleRestrictedExpireWaits();r_deliver_wait(&slot,&frame);save_and_switch();
 assert(frame.x[0]==2 && task.tc_State==TS_READY && !timed_waiters);
 timed_reset(&slot,&domain,&frame);timed_arm(&slot,&frame,50);save_and_switch();fake_now=149;
 assert(r_wake_channel(&domain,17));fake_now=200;AROSAppleRestrictedExpireWaits();r_deliver_wait(&slot,&frame);
 assert(frame.x[0]==1 && slot.wake_generation==1 && !timed_waiters);
 timed_reset(&slot,&domain,&frame);timed_arm(&slot,&frame,50);save_and_switch();fake_now=150;
 assert(r_wake_channel(&domain,17));r_deliver_wait(&slot,&frame);
 assert(frame.x[0]==2 && slot.wake_generation==1 && !timed_waiters);
 timed_reset(&slot,&domain,&frame);timed_arm(&slot,&frame,50);save_and_switch();
 assert(r_suspend_channel(&domain,17,0)==0);fake_now=150;AROSAppleRestrictedExpireWaits();
 assert(task.tc_State==TS_WAIT && AROSAppleRestrictedWaitState(&task,&context)==13 && slot.wait_result==2 && !timed_waiters);
 assert(r_wake_channel(&domain,17));assert(slot.wait_result==2);
 assert(r_suspend_channel(&domain,17,1)==1);r_deliver_wait(&slot,&frame);assert(frame.x[0]==2);
 timed_reset(&slot,&domain,&frame);timed_arm(&slot,&frame,50);save_and_switch();
 domain.closing=1;r_cancel_waiters(&domain);fake_now=200;AROSAppleRestrictedExpireWaits();
 frame.x[0]=77;r_deliver_wait(&slot,&frame);assert(frame.x[0]==77 && !slot.wait_pending && !timed_waiters && !slot.wait_deadline);
 timed_reset(&slot,&domain,&frame);timed_arm(&slot,&frame,50);slot.owner_context=NULL;r_clear_wait(&slot);
 fake_now=200;AROSAppleRestrictedExpireWaits();assert(!timed_waiters && !slot.wait_pending && !slot.wait_deadline);
 restricted_slots=NULL;
}
int main(void) { interleavings();native_signal();ownership();generations();suspension();timeouts();puts("PASS wait scheduler interleavings"); }
