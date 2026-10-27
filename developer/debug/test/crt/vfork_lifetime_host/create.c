/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
/* Production parent_createchild, native Exec child queue primitives and
 * waitpid are injected. Only Task execution/signalling and DOS are modeled. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <errno.h>
typedef long LONG;typedef uint32_t ULONG;typedef signed char BYTE;typedef void *APTR;typedef void *BPTR;typedef uintptr_t IPTR;typedef unsigned char UBYTE;typedef int BOOL;typedef void fdesc;
#define D(x) do {} while(0)
#define ASSERTCHILDSTATE(x) do {} while(0)
#define PRINTSTATE do {} while(0)
#define SETPARENTSTATE(x) udata->parent_state=(x)
#define _VFORK_COPYENV(a,b) memcpy((a),(b),sizeof(jmp_buf))
#define VFORK_PARENT 2
#define PRETEND_CHILD 4
#define TRUE 1
#define FALSE 0
#define PARENT_STATE_EXIT_CALLED 1
#define PARENT_STATE_STOPPED_PRETENDING 8
#define CHILD_STATE_SETUP_FAILED 1
#define CHILD_STATE_SETUP_FINISHED 2
#define CHILD_STATE_UDATA_NOT_USED 8
#define NP_Entry 1
#define NP_StackSize 2
#define NP_CloseInput 3
#define NP_CloseOutput 4
#define NP_CloseError 5
#define NP_Input 6
#define NP_Output 7
#define NP_Error 8
#define NP_Cli 9
#define NP_CurrentDir 10
#define NP_HomeDir 11
#define NP_Name 12
#define NP_UserData 13
#define NP_NotifyOnDeath 14
#define TAG_DONE 0
#define TF_ETASK 1
#define SIGF_CHILD (1U<<3)
#define CHILD_NOTNEW ((ULONG)-2)
#define CHILD_NOTFOUND ((ULONG)-1)
#define CHILD_ACTIVE 1
#define CHILD_EXITED 2
#ifndef WNOHANG
#define WNOHANG 1
#endif
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT
struct Node {struct Node *ln_Succ,*ln_Pred;};
struct List {struct Node *lh_Head,*lh_Tail,*lh_TailPred;};
#define ForeachNode(list,node) for(node=(void *)((list)->lh_Head);((struct Node *)node)->ln_Succ;node=(void *)((struct Node *)node)->ln_Succ)
#define ListLength(list,count) do { struct Node *n; (count)=0; ForeachNode((list),n) (count)++; } while (0)
static void newlist(struct List *l) {l->lh_Head=(void *)&l->lh_Tail;l->lh_Tail=NULL;l->lh_TailPred=(void *)&l->lh_Head;}
static void AddTail(struct List *l,struct Node *n) {n->ln_Succ=(void *)&l->lh_Tail;n->ln_Pred=l->lh_TailPred;l->lh_TailPred->ln_Succ=n;l->lh_TailPred=n;}
static void Remove(struct Node *n) {n->ln_Pred->ln_Succ=n->ln_Succ;n->ln_Succ->ln_Pred=n->ln_Pred;}
struct ETask {struct Node node;ULONG et_UniqueID;LONG et_Result1;struct List et_Children;struct {struct List mp_MsgList;} et_TaskMsgPort;BOOL dead;};
struct Task {APTR tc_SPLower,tc_SPUpper;ULONG tc_Flags;union {struct ETask *tc_ETask;} tc_UnionETask;};
struct TagItem {IPTR ti_Tag,ti_Data;};
struct Process {struct Task task;BPTR pr_CES;};
struct __cwd_context;struct __fd_namespace;struct StdCBase;struct Library;struct __env_item;
struct __vfork_binding { int unused; };
struct __cwd_snapshot {void *cwd,*progdir;BOOL unix_paths;};
struct PosixCIntBase {int flags;};
/* VFORK_DATA */
static struct PosixCIntBase base;
static struct Process parent;
static struct ETask parent_record;
static struct Process *running, *pending_exit;
static int failure, signals, messages, allocations, records, forbidden, final_return;
static int locks,refs,snapshots,copies;
static ULONG next_uid=100;
static struct vfork_data *published;
#define GET_THIS_TASK (&parent.task)
static void Forbid(void) {assert(!forbidden);forbidden=1;}
static void Permit(void) {assert(forbidden);forbidden=0;}
struct ETask *GetETask(struct Task *task) {return task->tc_UnionETask.tc_ETask;}
int GetETaskID(struct Task *task) {return GetETask(task)->et_UniqueID;}
static struct ETask *record_new(void) {struct ETask *e=calloc(1,sizeof(*e));assert(e);e->et_UniqueID=++next_uid;newlist(&e->et_Children);newlist(&e->et_TaskMsgPort.mp_MsgList);records++;return e;}
static void retire(struct Process *p) {
 struct ETask *e=GetETask(&p->task);assert(!e->dead);e->dead=TRUE;
 Remove(&e->node);AddTail(&parent_record.et_TaskMsgPort.mp_MsgList,&e->node);
 if(running==p)running=NULL;if(pending_exit==p)pending_exit=NULL;
 free(p); /* Task storage is gone; the parent retains only ETask records. */
}
static void ExpungeETask(struct ETask *e) {assert(e->dead);records--;free(e);}
ULONG SetSignal(ULONG new,ULONG mask) {(void)new;assert(mask==SIGF_CHILD);return 0;}
struct PosixCIntBase *__cwd_runtime_base(void) {return &base;}
struct Task *FindTask(APTR unused) {(void)unused;return &parent.task;}
LONG launcher(void) {return 0;}
void FreeMem(APTR p,size_t size) {(void)size;free(p);allocations--;}
void vfork_longjmp(jmp_buf env,int val) {longjmp(env,val);}
BOOL __cwd_runtime_snapshot(struct __cwd_snapshot *s) {
 if(failure==5)return FALSE;
 s->cwd=(APTR)(uintptr_t)21;s->progdir=(APTR)(uintptr_t)22;s->unix_paths=TRUE;snapshots++;return TRUE;
}
BPTR __cwd_directory_lock(APTR dir) {return dir;}
BPTR DupLock(BPTR lock) {
 assert(snapshots==1);copies++;assert(lock==(APTR)(uintptr_t)(copies==1?21:22));
 if(failure==copies+5)return NULL;
 locks++;return malloc(1);
}
void UnLock(BPTR p) {if(p){locks--;free(p);}}
void __cwd_snapshot_release(struct __cwd_snapshot *s) {(void)s;assert(snapshots==1);snapshots--;}
void __fd_namespace_drop(struct __fd_namespace *s) {if(s){assert(s==(APTR)(uintptr_t)41);refs--;}}
void __cwd_context_release(struct __cwd_context *s) {if(s){assert(s==(APTR)(uintptr_t)42);refs--;}}
BOOL __vfork_binding_enter(struct PosixCIntBase *b,struct __fd_namespace *n,struct __cwd_context *c,struct __vfork_binding *scope) {
 assert(b==&base&&n==(APTR)(uintptr_t)41&&c==(APTR)(uintptr_t)42&&scope);return failure!=8;
}
BYTE AllocSignal(int bit) {(void)bit;if(failure==1)return -1;signals++;return 4;}
void FreeSignal(int bit) {assert(bit==4 && signals==1);signals--;}
BPTR Input(void) {return (APTR)(uintptr_t)31;}
BPTR Output(void) {return (APTR)(uintptr_t)32;}
struct Process *CreateNewProc(struct TagItem *tags) {
 assert((base.flags & VFORK_PARENT)&&!snapshots);BOOL notify=FALSE;BPTR cwd=NULL,home=NULL;
 for(struct TagItem *t=tags;t->ti_Tag;t++) {
  if(t->ti_Tag==NP_CurrentDir)cwd=(APTR)t->ti_Data;
  if(t->ti_Tag==NP_HomeDir)home=(APTR)t->ti_Data;
  if(t->ti_Tag==NP_UserData)published=(APTR)t->ti_Data;
  if(t->ti_Tag==NP_NotifyOnDeath)notify=t->ti_Data;
 }
 assert(published && notify && cwd && home && cwd!=home && locks==2 && published->child_unix_paths);
 if(failure==2)return NULL;
 struct Process *p=calloc(1,sizeof(*p));assert(p);struct ETask *e=record_new();
 p->task.tc_Flags=TF_ETASK;p->task.tc_UnionETask.tc_ETask=e;
 AddTail(&parent_record.et_Children,&e->node);running=p;
 UnLock(cwd);UnLock(home);
 published->child_pid=GetETaskID(&p->task); /* launcher publishes while live */
 published->child_signal=4;
 if(failure==3 || failure==4) {
  published->child_errno=ENOMEM;published->child_state=CHILD_STATE_SETUP_FAILED;
  /* Fast failure destroys the Task before CreateNewProc returns its address. */
  if(failure==3)retire(p);else pending_exit=p;
 } else {published->child_namespace=(APTR)(uintptr_t)41;published->child_context=(APTR)(uintptr_t)42;published->child_state=CHILD_STATE_SETUP_FINISHED;refs=2;}
 return p;
}
ULONG Wait(ULONG mask) {
 if(mask==SIGF_CHILD) {assert(forbidden && pending_exit);retire(pending_exit);return mask;}
 assert(mask==(1U<<4));messages++;return mask;
}
void Signal(struct Task *task,ULONG mask) {
 assert(running && task==&running->task&&mask==(1U<<4));
 if(final_return)retire(running);
 else if(published->parent_state==PARENT_STATE_STOPPED_PRETENDING)pending_exit=running;
 else published->child_state=CHILD_STATE_UDATA_NOT_USED;
}
/* CHILD_WAIT */
/* CHILD_FREE */
/* CHILD_STATUS */
/* WAITPID */
static void parent_leavepretendchild(struct vfork_data *u) {(void)u;}
static int *__stdc_set_errorptr(int *p) {return p;}
static void __stdc_set_exitjmp(jmp_buf a,jmp_buf b) {(void)a;(void)b;}
/* EXIT_CONTROLLED */
/* CREATE_CHILD */
int main(void) {
 parent.task.tc_Flags=TF_ETASK;parent.task.tc_UnionETask.tc_ETask=&parent_record;
 newlist(&parent_record.et_Children);newlist(&parent_record.et_TaskMsgPort.mp_MsgList);
 for(int repeat=0;repeat<64;repeat++) for(failure=0;failure<=8;failure++) for(int nested=0;nested<2;nested++) {
  copies=0;published=NULL;messages=0;base.flags=64|(nested?VFORK_PARENT|PRETEND_CHILD:0);int old_flags=base.flags;
  struct ETask *other=record_new();other->dead=TRUE;other->et_Result1=73;
  ULONG unrelated=other->et_UniqueID;AddTail(&parent_record.et_TaskMsgPort.mp_MsgList,&other->node);
  struct vfork_data *u=calloc(1,sizeof(*u));assert(u);allocations++;
  jmp_buf env;int result=setjmp(env);
  if(!result) {_VFORK_COPYENV(u->vfork_jmp,env);parent_createchild(u);assert(failure==0);}
  else assert(result==-1 && failure && errno==ENOMEM);
  if(!failure) {
   assert(signals==1&&allocations==1);retire(running);
   assert(ChildWait(u->child_pid)!=(IPTR)CHILD_NOTNEW);ChildFree(u->child_pid);
   FreeSignal(u->parent_signal);__fd_namespace_drop(u->child_namespace);__cwd_context_release(u->child_context);FreeMem(u,sizeof(*u));
  }
  assert(base.flags==old_flags);assert(!signals&&!locks&&!allocations&&!refs&&!snapshots&&!running&&!pending_exit);
  assert(records==1);int status=0;
  assert(host_waitpid(-1,&status,WNOHANG)==(int)unrelated && status==73);
  assert(host_waitpid(unrelated,&status,WNOHANG)==-1 && errno==ECHILD);
  assert(!records&&!forbidden);
 }
 /* The launcher can finish before the parent returns from vfork. The real
    controlled-stack tail must return the captured ID after acknowledgement
    releases the child and shared data is freed, without either dead pointer. */
 struct vfork_data *u=calloc(1,sizeof(*u));assert(u);allocations++;
 struct Process *p=calloc(1,sizeof(*p));assert(p);
 struct ETask *e=record_new();p->task.tc_UnionETask.tc_ETask=e;
 AddTail(&parent_record.et_Children,&e->node);running=p;
 u->child=&p->task;u->child_pid=GetETaskID(&p->task);u->parent_signal=AllocSignal(-1);u->child_signal=4;
 int expected=u->child_pid;published=u;final_return=1;
 jmp_buf env;int result=setjmp(env);
 if(!result) {_VFORK_COPYENV(u->vfork_jmp,env);__vfork_exit_controlled_stack(u);abort();}
 assert(result==expected&&!allocations&&!signals&&!running);ChildFree(expected);assert(!records);
 puts("PASS: controlled-stack return after child and shared-data destruction");
 puts("PASS: 1152 create attempts; actual Exec child queues/wait/free/status + actual waitpid; no ghosts, unrelated result73 preserved");return 0;
}
