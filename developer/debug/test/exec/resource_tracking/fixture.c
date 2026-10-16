/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
/* Host service mocks; implementation and lifecycle blocks are extracted afresh. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
typedef uintptr_t IPTR, BPTR;
typedef intptr_t SIPTR;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef unsigned char UBYTE;
typedef void *APTR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MEMF_CLEAR 1
#define LIB_VECTSIZE 8
#define NT_PROCESS 13
#define PA_SIGNAL 0
#define TNA_REMOVED 3
#define ETASK_RSVD_SLOTID 1
#define __TS_FIRSTSLOT 0
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT
#define D(...)
#define AROS_LCA(t,v,r) v
#define AROS_CALL1(t,f,v,bt,b) original_rem(v,b)
struct MinNode { struct MinNode *mln_Succ,*mln_Pred; };
struct Node { struct Node *ln_Succ,*ln_Pred; int ln_Type; char *ln_Name; };
struct MinList { struct MinNode *mlh_Head,*mlh_Tail,*mlh_TailPred; };
struct List { struct Node *lh_Head,*lh_Tail,*lh_TailPred; };
static void newlist(void *p) { struct MinList *l=p; l->mlh_Head=(void*)&l->mlh_Tail; l->mlh_Tail=NULL; l->mlh_TailPred=(void*)&l->mlh_Head; }
#define NEWLIST(p) newlist(p)
#define ForeachNode(l,n) for(n=(void*)((struct MinList*)(l))->mlh_Head; ((struct MinNode*)(n))->mln_Succ; n=(void*)((struct MinNode*)(n))->mln_Succ)
#define ForeachNodeSafe(l,n,t) for(n=(void*)((struct MinList*)(l))->mlh_Head,t=(void*)((struct MinNode*)n)->mln_Succ; t; n=t,t=(void*)((struct MinNode*)t)->mln_Succ)
static void AddTail(struct List *p,struct Node *q) { struct MinList *l=(void*)p; struct MinNode *n=(void*)q; n->mln_Succ=(void*)&l->mlh_Tail; n->mln_Pred=l->mlh_TailPred; l->mlh_TailPred->mln_Succ=n; l->mlh_TailPred=n; }
static void AddHead(struct List *p,struct Node *q) { struct MinList *l=(void*)p; struct MinNode *n=(void*)q; n->mln_Pred=(void*)&l->mlh_Head; n->mln_Succ=l->mlh_Head; l->mlh_Head->mln_Pred=n; l->mlh_Head=n; }
static void Remove(struct Node *p) { struct MinNode *n=(void*)p; n->mln_Pred->mln_Succ=n->mln_Succ; n->mln_Succ->mln_Pred=n->mln_Pred; }
#define IsListEmpty(l) (((struct MinList*)(l))->mlh_Head->mln_Succ==NULL)
#define IsMinListEmpty(l) IsListEmpty(l)
struct RTContext;
struct ETask { IPTR *et_Reserved[2]; struct RTContext *iet_RT; };
struct Task { struct Node tc_Node; ULONG tc_SigAlloc; union { struct ETask *tc_ETask; } tc_UnionETask; int alive,notified,dos_live; };
struct SignalSemaphore { struct Task *ss_Owner; int ss_NestCount; };
struct MsgPort { struct Task *mp_SigTask; ULONG mp_Flags,mp_SigBit; struct List mp_MsgList; };
struct Library { unsigned lib_NegSize; }; struct DosLibrary { int dummy; }; struct KernelBase { int dummy; };
struct TaskListEntry { struct Node tle_Node; struct Task *tle_Task; };
struct TaskResBase { struct Library trb_LibNode; struct KernelBase *trb_KernelBase; struct MinList trb_RTContexts,trb_RTRecords,trb_RTPins,trb_NotifyHooks,trb_NewTasks,trb_TaskList,trb_LockedLists; IPTR trb_RTNext; ULONG trb_RTReclaimed[8],trb_RTCloseErrors; int trb_RemTask; };
struct ExecBase { struct TaskResBase *lb_TaskResBase; };
static struct TaskResBase base;
static struct ExecBase execbase={&base};
static struct Task *current;
static int idnest=-1,tdnest=-1,supervisor;
#define IDNESTCOUNT_GET idnest
#define TDNESTCOUNT_GET tdnest
#define GetETask(t) ((t)->tc_UnionETask.tc_ETask)
#define GetIntETask(t) GetETask(t)
#define CopyMem(a,b,n) memcpy(b,a,n)
#define KrnIsSuper() ((void)KernelBase, supervisor)
static struct Task *FindTask(void *unused) { return current; }
static void Disable(void) { idnest++; }
static void Enable(void) { assert(idnest>=0); idnest--; }
static void Forbid(void) { tdnest++; }
static void Permit(void) { assert(tdnest>=0); tdnest--; }
static void safe_service(void) { assert(idnest==-1 && tdnest==-1 && !supervisor && current->alive); }
/* Independent allocation ledger, including RT metadata: not RT_STATS. */
struct Allocation { void *ptr; size_t size; };
static struct Allocation allocations[4096];
static unsigned allocation_count,free_count,library_refs,files,locks,ports,sem_releases;
static void *AllocMem(size_t size,int flags) { safe_service(); void *p=flags?calloc(1,size):malloc(size); assert(p); for(unsigned i=0;i<4096;i++) if(!allocations[i].ptr) { allocations[i]=(struct Allocation){p,size}; allocation_count++; return p; } abort(); }
static void FreeMem(void *p,size_t size) { safe_service(); for(unsigned i=0;i<4096;i++) if(allocations[i].ptr==p) { assert(allocations[i].size==size); allocations[i].ptr=NULL; free_count++; free(p); return; } assert(!"unknown or double free"); }
static unsigned outstanding(void) { return allocation_count-free_count; }
static void FreeVec(void *p) { for(unsigned i=0;i<4096;i++) if(allocations[i].ptr==p) { FreeMem(p,allocations[i].size); return; } abort(); }
static struct Library *OpenLibrary(const char *n,int v) { safe_service(); library_refs++; return (void*)1; }
static void CloseLibrary(struct Library *l) { safe_service(); assert(library_refs); library_refs--; }
static void ReleaseSemaphore(struct SignalSemaphore *s) { safe_service(); assert(s->ss_Owner==current && s->ss_NestCount==1); s->ss_Owner=NULL; s->ss_NestCount=0; sem_releases++; }
struct Handle { int kind,fail; };
static void (*during_close)(void);
static BOOL Close(BPTR h) { safe_service(); assert(current->dos_live); struct Handle *p=(void*)h; assert(p->kind==1); int ok=!p->fail; if(during_close) { void(*fn)(void)=during_close; during_close=NULL; fn(); } files--; FreeMem(p,sizeof(*p)); return ok; }
static void UnLock(BPTR h) { safe_service(); assert(current->dos_live); struct Handle *p=(void*)h; assert(p->kind==2); locks--; FreeMem(p,sizeof(*p)); }
static void FreeSignal(LONG bit) { safe_service(); assert(bit>=0 && bit<32 && (current->tc_SigAlloc&(1u<<bit))); current->tc_SigAlloc&=~(1u<<bit); }
static void DeleteMsgPort(struct MsgPort *p) { safe_service(); assert(current->notified); assert(IsListEmpty(&p->mp_MsgList)); ports--; FreeSignal(p->mp_SigBit); FreeMem(p,sizeof(*p)); }
static void taskres_NotifyTasks(struct TaskResBase *b,int action,struct Task *t,void*a,void*c) { assert(action==TNA_REMOVED); t->notified=1; }
static void original_rem(struct Task *t,struct ExecBase *b) { assert(idnest==-1 && tdnest==-1); assert(t->notified); t->alive=0; }
static void *OpenResource(const char *n) { return &base; }
#define Close(h) ((void)DOSBase, Close(h))
#define UnLock(h) ((void)DOSBase, UnLock(h))
/* PUBLIC_HEADER */
#define KernelBase TaskResBase->trb_KernelBase
/* PRODUCTION_API */
#undef KernelBase
#define TaskResourceControl(r) rt_api(r,TaskResBase)
/* PRODUCTION_REMTASK */
static void process_exit(void) {
    /* PRODUCTION_DOS_EXIT */
    current->dos_live=0; /* subsequent DOS teardown, after extracted block */
    production_RemTask(NULL,&execbase);
}
static IPTR action(ULONG op,IPTR h,ULONG flags) { struct RTRequest r={.size=sizeof(r),.action=op,.handle=h,.flags=flags}; return rt_api(&r,&base); }
static IPTR adopt(ULONG type,void *object,IPTR aux) { struct RTRequest r={.size=sizeof(r),.action=RT_ADOPT,.type=type,.object=(IPTR)object,.auxiliary=aux,.flags=RTF_EXCLUSIVE}; return rt_api(&r,&base); }
static struct RTStats stats(void) { struct RTStats s; struct RTRequest r={.size=sizeof(r),.action=RT_STATS,.stats=&s}; assert(rt_api(&r,&base)); return s; }
static struct Task tasks[32]; static struct ETask etasks[32]; static unsigned nexttask;
static struct Task *task(void) { assert(nexttask<32); struct Task *t=&tasks[nexttask]; t->tc_UnionETask.tc_ETask=&etasks[nexttask++]; t->tc_Node.ln_Type=NT_PROCESS; t->alive=t->dos_live=1; return t; }
static void initialize(void) { memset(&base,0,sizeof(base)); base.trb_LibNode.lib_NegSize=22*LIB_VECTSIZE; base.trb_KernelBase=(void*)1; base.trb_RTNext=1; base.trb_RemTask=1; NEWLIST(&base.trb_RTContexts); NEWLIST(&base.trb_RTRecords); NEWLIST(&base.trb_RTPins); NEWLIST(&base.trb_NotifyHooks); NEWLIST(&base.trb_NewTasks); NEWLIST(&base.trb_TaskList); NEWLIST(&base.trb_LockedLists); static struct MinNode notify; AddHead((void*)&base.trb_NotifyHooks,(void*)&notify); }
static struct Handle *handle(int kind,int fail) { struct Handle *h=AllocMem(sizeof(*h),1); h->kind=kind; h->fail=fail; if(kind==1)files++;else locks++; return h; }
static struct MsgPort *port(void) { struct MsgPort *p=AllocMem(sizeof(*p),1); p->mp_SigTask=current; p->mp_SigBit=7; current->tc_SigAlloc|=1u<<7; NEWLIST(&p->mp_MsgList); ports++; return p; }
static void assert_clean(void) { struct RTStats s=stats(); assert(!s.records&&!s.contexts&&!s.leases); assert(!outstanding()&&!files&&!locks&&!ports&&!library_refs); }
static void reenter(void) { assert(!action(RT_CLEANUP,0,0)); }
static struct Task *interrupt_monitor;
static IPTR cleaning_handle, later_handle;
static void remote_during_cleanup(void) {
    struct Task *victim=current; current=interrupt_monitor;
    assert(!action(RT_PIN,cleaning_handle,0));
    assert(!action(RT_PIN,later_handle,0));
    taskres_ResourceExit(victim,&base); /* removal requested before callback returns */
    current=victim;
}
int main(void) {
    current=task(); initialize(); struct Task *monitor=task();
    /* Real extracted DOS and RemTask paths, all seven resource kinds. */
    struct SignalSemaphore sem={current,1};
    assert(adopt(RT_MEM,AllocMem(64,1),64)); assert(adopt(RT_VEC,AllocMem(32,1),0));
    assert(adopt(RT_SEMAPHORE,&sem,0)); assert(adopt(RT_FILE,handle(1,0),0));
    assert(adopt(RT_LOCK,handle(2,0),0)); assert(adopt(RT_PORT,port(),0));
    current->tc_SigAlloc|=1u<<8; assert(adopt(RT_SIGNAL,(void*)8,0));
    during_close=reenter; process_exit(); assert(current->tc_SigAlloc==0); current=monitor; assert_clean(); assert(sem_releases==1);
    /* Close failure consumed the handle: no stale quarantine/recovery. */
    current=task(); IPTR failed=adopt(RT_FILE,handle(1,1),0); assert(failed); process_exit(); current=monitor;
    assert(stats().close_errors==1); assert(!action(RT_RECOVER,failed,6)); assert_clean();
    /* Transfer preserves peer resource when former owner exits. */
    struct Task *a=task(),*b=task(); current=a; void *mem=AllocMem(23,1); IPTR h=adopt(RT_MEM,mem,23); assert(h&&action(RT_DISOWN,h,0));
    current=b; assert(action(RT_ACCEPT,h,0)); current=a; process_exit(); current=b; assert(outstanding()==3); process_exit(); current=monitor; assert_clean();
    /* A raw task cannot accept a DOS file; refusal preserves exported owner. */
    current=task(); h=adopt(RT_FILE,handle(1,0),0); assert(h&&action(RT_DISOWN,h,0)); process_exit();
    current=task(); current->tc_Node.ln_Type=1; assert(!action(RT_ACCEPT,h,0)); assert(stats().exported==1 && files==1); production_RemTask(NULL,&execbase);
    current=task(); assert(action(RT_ACCEPT,h,0)); process_exit(); current=monitor; assert_clean();
    /* Per-owner signal identity and duplicate prevention. */
    a=task(); b=task(); current=a; a->tc_SigAlloc|=1u<<4; h=adopt(RT_SIGNAL,(void*)4,0); assert(h); assert(!adopt(RT_SIGNAL,(void*)4,0));
    current=b; b->tc_SigAlloc|=1u<<4; assert(adopt(RT_SIGNAL,(void*)4,0)); process_exit(); current=a; process_exit(); current=monitor; assert_clean();
    /* Wrong contexts reject before allocation or destruction. */
    unsigned before=allocation_count; idnest=0; assert(!adopt(RT_MEM,(void*)1,1)); idnest=-1;
    tdnest=0; assert(!adopt(RT_MEM,(void*)1,1)); tdnest=-1; supervisor=1; assert(!adopt(RT_MEM,(void*)1,1)); supervisor=0; assert(before==allocation_count);
    /* Backing allocation dependency, reverse registration order. */
    current=task(); struct Backing { int prefix; struct SignalSemaphore sem; };
    struct Backing *backing=AllocMem(sizeof(*backing),1); backing->sem=(struct SignalSemaphore){current,1};
    assert(adopt(RT_SEMAPHORE,&backing->sem,0)); h=adopt(RT_MEM,backing,sizeof(*backing)); assert(h);
    IPTR lease=action(RT_PIN,h,0); assert(lease); process_exit(); current=monitor;
    assert(stats().quarantined==1 && stats().leases==1 && !backing->sem.ss_Owner);
    assert(!action(RT_RECOVER,h,6)); assert(action(RT_UNPIN,lease,0)); assert(!action(RT_UNPIN,lease,0));
    assert(!action(RT_RECOVER,h,RTF_QUIESCENT)); assert(action(RT_RECOVER,h,6)); assert(action(RT_CLEANUP,0,0)); assert_clean();
    /* Cycles do not trigger blind reclamation; explicit lease release recovers. */
    current=task(); IPTR x=adopt(RT_MEM,AllocMem(24,1),24), y=adopt(RT_VEC,AllocMem(24,1),0);
    IPTR px=action(RT_PIN,x,0),py=action(RT_PIN,y,0); assert(px&&py); process_exit(); current=monitor;
    assert(stats().quarantined==2 && stats().leases==2 && stats().contexts==0);
    assert(!action(RT_RECOVER,x,6)); assert(action(RT_UNPIN,px,0)); assert(action(RT_UNPIN,py,0));
    assert(action(RT_RECOVER,x,6)&&action(RT_RECOVER,y,6)); assert(action(RT_CLEANUP,0,0)); assert_clean();
    /* Omitted registration: independent ledger must report the leaked sentinel. */
    current=task(); void *sentinel=AllocMem(29,1); process_exit(); current=monitor;
    assert(stats().records==0 && outstanding()==1); unsigned omitted_leaks=outstanding(); FreeMem(sentinel,29); assert_clean();
    /* Explicit FORGET leaves resource to caller; stale token cannot pin/free. */
    current=task(); mem=AllocMem(30,1); h=adopt(RT_MEM,mem,30); assert(action(RT_FORGET,h,0)); assert(!action(RT_PIN,h,0)); FreeMem(mem,30); process_exit(); current=monitor; assert_clean();
    /* Remote request while unlocked in Close: stop subsequent destructors. */
    a=task(); current=a; later_handle=adopt(RT_MEM,AllocMem(42,1),42);
    cleaning_handle=adopt(RT_FILE,handle(1,0),0); assert(later_handle&&cleaning_handle);
    interrupt_monitor=monitor; during_close=remote_during_cleanup; process_exit(); current=monitor;
    assert(stats().closed_contexts==1 && stats().records==1 && outstanding()==3);
    /* Abort during mutation: EVERY transaction member, including lock, dirty. */
    a=task(); current=a; struct SignalSemaphore dirty_sem={a,1};
    IPTR ds=adopt(RT_SEMAPHORE,&dirty_sem,0); h=adopt(RT_MEM,AllocMem(40,1),40);
    assert(ds&&h&&action(RT_BEGIN_MUTATION,ds,0)&&action(RT_BEGIN_MUTATION,h,0));
    unsigned released=sem_releases; process_exit(); current=monitor;
    assert(stats().quarantined==3 && stats().dirty==2 && sem_releases==released && dirty_sem.ss_Owner==a);
    assert(!action(RT_RECOVER,ds,6)); assert(!action(RT_RECOVER,h,RTF_QUIESCENT));
    assert(action(RT_RECOVER,h,6)); assert(action(RT_CLEANUP,0,0));
    /* Pending messages: port and its signal remain, with no stale free. */
    current=task(); struct MsgPort *pending=port(); struct Node message={0}; AddTail(&pending->mp_MsgList,&message);
    IPTR pending_h=adopt(RT_PORT,pending,0); assert(pending_h); process_exit(); current=monitor;
    assert(stats().quarantined==3 && ports==1 && !action(RT_RECOVER,pending_h,6));
    assert(pending->mp_MsgList.lh_Head==&message);
    /* Closed owner signal quarantine must not collide with fresh owner's bit. */
    current=task(); current->tc_SigAlloc|=1u<<4; h=adopt(RT_SIGNAL,(void*)4,0); assert(h&&action(RT_BEGIN_MUTATION,h,0)); process_exit();
    current=task(); current->tc_SigAlloc|=1u<<4; assert(adopt(RT_SIGNAL,(void*)4,0)); process_exit(); current=monitor;
    /* Remote removal retains resources AND context; never claims reclamation. */
    a=task(); current=a; assert(adopt(RT_MEM,AllocMem(41,1),41)); current=monitor;
    production_RemTask(a,&execbase); struct RTStats retained=stats();
    assert(retained.closed_contexts==2 && retained.contexts==2 && retained.quarantined==5 && retained.records==5);
    assert(outstanding()==10); /* five records, two contexts, three actual objects */
    unsigned retained_allocations=outstanding();
    assert(files==0 && locks==0 && ports==1 && library_refs==0);
    /* Test teardown only: quarantines above are intentionally NOT RT reclaimed. */
    for(unsigned i=0;i<4096;i++) if(allocations[i].ptr) { free(allocations[i].ptr); allocations[i].ptr=NULL; free_count++; }
    assert(outstanding()==0);
    printf("{\"status\":\"PASS\",\"clean_exit\":true,\"close_failure_consumed\":true,\"transfer\":true,\"raw_task_dos_transfer_rejected\":true,\"signal_identity\":true,\"wrong_context_rejected\":true,\"backing_dependency\":true,\"cycles_quarantined\":true,\"mutation_abort_quarantined\":true,\"pending_port_quarantined\":true,\"remote_metadata_retained\":true,\"remote_during_cleanup_retained\":true,\"omitted_registration_leaks\":%u,\"intentional_retained_allocations_before_test_teardown\":%u}\n",omitted_leaks,retained_allocations);
    return 0;
}
