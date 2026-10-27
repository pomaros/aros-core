/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define TS_INVALID 0
#define TS_REMOVED 6
struct Node { struct Node *ln_Succ, *ln_Pred; };
struct List { struct Node *lh_Head, *lh_Tail, *lh_TailPred; };
struct Task { int tc_State; };
struct MsgPort { struct Task *mp_SigTask; };
static struct MsgPort service_port;
static struct { struct { unsigned lib_Version, lib_Revision; } LibNode; struct MsgPort *ServicePort; } execbase={ {51,12},&service_port };
#define SysBase (&execbase)
#define PrivExecBase(base) (base)
#ifndef TEST_NO_HANDOFF
#define EXEC_SELF_EXIT_HANDOFF(task) ((void)(task))
#endif
struct Hook { struct Node h_MinNode; void (*entry)(struct Hook *,struct Task *,void *); atomic_uint calls; };
struct TaskResBase { struct List trb_StopHooks; pthread_mutex_t trb_StopLock; };
static struct TaskResBase base;
static struct Task stopped={TS_INVALID}, self={TS_INVALID};
static _Thread_local struct Task *caller=&self;
static _Thread_local int irq=-1, forbid=-1;
#define IDNESTCOUNT_GET irq
#define TDNESTCOUNT_GET forbid
#if !defined(__AROSEXEC_SMP__)
static pthread_mutex_t irq_lock=PTHREAD_MUTEX_INITIALIZER;
#endif
static void Disable(void) {
#if !defined(__AROSEXEC_SMP__)
    if(irq==-1) pthread_mutex_lock(&irq_lock);
#endif
    irq++;
}
static void Enable(void) {
    irq--;
#if !defined(__AROSEXEC_SMP__)
    if(irq==-1) pthread_mutex_unlock(&irq_lock);
#endif
}
#define SPINLOCK_MODE_WRITE 1
#define EXEC_SPINLOCK_LOCK(lock,hook,mode) pthread_mutex_lock(lock)
#define EXEC_SPINLOCK_UNLOCK(lock) pthread_mutex_unlock(lock)
#define ForeachNode(list,node) for(node=(void *)(list)->lh_Head;node->h_MinNode.ln_Succ;node=(void *)node->h_MinNode.ln_Succ)
#define CALLHOOKPKT(h,t,m) ((h)->entry((h),(t),(m)))
static void AddTail(struct List *list,struct Node *node) {
    node->ln_Succ=(void *)&list->lh_Tail;node->ln_Pred=list->lh_TailPred;
    list->lh_TailPred->ln_Succ=node;list->lh_TailPred=node;
}
static void Remove(struct Node *node) {
    node->ln_Pred->ln_Succ=node->ln_Succ;node->ln_Succ->ln_Pred=node->ln_Pred;
}
static struct Task *FindTask(void *ignored) { (void)ignored;return caller; }
#include "actual.c"
static atomic_uint entered, release_callback, removal_started, removal_done, running;
static void ordinary(struct Hook *hook,struct Task *task,void *message) {
    assert(irq>=0 && task==&stopped && !message);
    atomic_fetch_add(&hook->calls,1);
}
/* Deliberately stalled host callback tests removal's join. Production
 * callbacks must only publish bounded atomic state and cannot stall. */
static void held(struct Hook *hook,struct Task *task,void *message) {
    ordinary(hook,task,message);atomic_store(&entered,1);
    while(!atomic_load(&release_callback)) sched_yield();
}
static void *dispatch_once(void *unused) { (void)unused;NotifyTaskStopped(&stopped,&base);return NULL; }
static void *remove_held(void *hook) {
    atomic_store(&removal_started,1);
    assert(TaskStopHookControl(hook,FALSE,&base));
    atomic_store(&removal_done,1);return NULL;
}
static void *dispatch_loop(void *unused) {
    (void)unused;
    while(atomic_load(&running)) NotifyTaskStopped(&stopped,&base);
    return NULL;
}
int main(void) {
    struct List *list=&base.trb_StopHooks;
    list->lh_Head=(void *)&list->lh_Tail;list->lh_Tail=NULL;list->lh_TailPred=(void *)&list->lh_Head;
    pthread_mutex_init(&base.trb_StopLock,NULL);
    struct Hook hook={.entry=ordinary};
    service_port.mp_SigTask=&self;
#ifdef TEST_NO_HANDOFF
    assert(!TaskStopHookControl(&hook,TRUE,&base));
    puts("PASS unsupported platform refuses registration");return 0;
#endif
    assert(!TaskStopHookControl(NULL,TRUE,&base));
    execbase.LibNode.lib_Revision=11;assert(!TaskStopHookControl(&hook,TRUE,&base));
    execbase.LibNode.lib_Version=50;assert(!TaskStopHookControl(&hook,TRUE,&base));
    execbase.LibNode.lib_Version=51;execbase.LibNode.lib_Revision=12;
    assert(!TaskStopHookControl(&hook,FALSE,&base));
    assert(TaskStopHookControl(&hook,TRUE,&base));
    assert(!TaskStopHookControl(&hook,TRUE,&base));
    NotifyTaskStopped(NULL,&base);NotifyTaskStopped(&self,&base);
    struct Task foreign={TS_INVALID};caller=&foreign;
    NotifyTaskStopped(&stopped,&base);caller=&self;
    execbase.ServicePort=NULL;NotifyTaskStopped(&stopped,&base);execbase.ServicePort=&service_port;
    struct Task removed={TS_REMOVED};NotifyTaskStopped(&removed,&base);
    assert(!atomic_load(&hook.calls));
    irq=0;assert(!TaskStopHookControl(&hook,FALSE,&base));NotifyTaskStopped(&stopped,&base);irq=-1;
    forbid=0;assert(!TaskStopHookControl(&hook,FALSE,&base));NotifyTaskStopped(&stopped,&base);forbid=-1;
    assert(!atomic_load(&hook.calls));
    NotifyTaskStopped(&stopped,&base);assert(atomic_load(&hook.calls)==1);
    assert(TaskStopHookControl(&hook,FALSE,&base));
    NotifyTaskStopped(&stopped,&base);assert(atomic_load(&hook.calls)==1);
    hook.entry=held;assert(TaskStopHookControl(&hook,TRUE,&base));
    pthread_t dispatch,remover;pthread_create(&dispatch,NULL,dispatch_once,NULL);
    while(!atomic_load(&entered)) sched_yield();
    pthread_create(&remover,NULL,remove_held,&hook);
    while(!atomic_load(&removal_started)) sched_yield();
    for(unsigned i=0;i<10000;i++) { assert(!atomic_load(&removal_done));sched_yield(); }
    atomic_store(&release_callback,1);pthread_join(dispatch,NULL);pthread_join(remover,NULL);
    assert(atomic_load(&removal_done));
    atomic_store(&running,1);pthread_create(&dispatch,NULL,dispatch_loop,NULL);
    for(unsigned i=0;i<2000;i++) {
        struct Hook *dynamic=calloc(1,sizeof(*dynamic));assert(dynamic);dynamic->entry=ordinary;
        assert(TaskStopHookControl(dynamic,TRUE,&base));
        sched_yield();assert(TaskStopHookControl(dynamic,FALSE,&base));free(dynamic);
    }
    atomic_store(&running,0);pthread_join(dispatch,NULL);
    assert(list->lh_Head==(void *)&list->lh_Tail);
    pthread_mutex_destroy(&base.trb_StopLock);
    puts("PASS real stop-hook bodies: guards, duplicate control, dispatch, removal join, 2000 concurrent retire/free cycles");
}
