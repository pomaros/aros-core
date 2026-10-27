/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef intptr_t SIPTR; typedef uintptr_t BPTR; typedef void *APTR;
typedef int32_t LONG; typedef uint32_t ULONG; typedef int BOOL;
typedef const char *CONST_STRPTR; typedef char *STRPTR;
#define BADDR(x) ((void *)(uintptr_t)(x))
#define BNULL 0
#define MKBADDR(x) ((BPTR)(uintptr_t)(x))
#define CMPBSTR(a,b) strcmp((a),(b))
#define TRUE 1
#define FALSE 0
#define DOSFALSE 0
#define DOSTRUE -1
#define NT_TASK 1
#define NT_PROCESS 13
#define NT_MSGPORT 4
#define DLT_DEVICE 0
#define DLT_DIRECTORY 1
#define DLT_VOLUME 2
#define DLT_LATE 3
#define DLT_NONBINDING 4
#define LDF_ALL 7
#define LDF_ASSIGNS 4
#define LDF_READ 8
#define LDF_WRITE 16
#define LDF_ENTRY 32
#define SHARED_LOCK -2
#define LOCK_SAME 0
#define ERROR_NO_MORE_ENTRIES 232
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_OBJECT_IN_USE 202
#define ACTION_SAME_LOCK 40
#define ACTION_NAME 41
#define ACTION_LOCATE_OBJECT 8
#define ERROR_OBJECT_WRONG_TYPE 212
#define ERROR_NO_FREE_STORE 103
#define ERROR_BAD_NUMBER 115
#define ERROR_DEVICE_NOT_MOUNTED 218
#define ERROR_INVALID_LOCK 211
#define ERROR_OBJECT_EXISTS 203
#define ACTION_COPY_DIR 19
#define ACTION_FREE_LOCK 15
#define AN_AsyncPkt 1
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define AROS_LIBFUNC_INIT
#define D(x)
#define bug(...) ((void)0)
struct Node {int ln_Type; char *ln_Name;}; struct Task {struct Node tc_Node;};
struct MsgPort {struct Node mp_Node;};
struct Process {struct Task pr_Task; struct MsgPort pr_MsgPort; SIPTR pr_Result2;};
struct FileLock {struct MsgPort *fl_Task; unsigned object; BOOL active;};
struct AssignList {BPTR al_Lock;struct AssignList *al_Next;};
struct DosList {BPTR dol_Next;char *dol_Name;LONG dol_Type; BPTR dol_Lock; struct MsgPort *dol_Task; union {struct {struct AssignList *dol_List;STRPTR dol_AssignName;} dol_assign;} dol_misc;};
struct DosInfo {BPTR di_DevInfo;};
struct RootNode {BPTR rn_Info;};
struct DosLibrary {ULONG assignGenerationLow,assignGenerationHigh;struct RootNode *dl_Root;};
struct assign_generation {ULONG low,high;};
#define IDosBase(x) (x)
static struct DosInfo info;static struct RootNode root={(BPTR)&info};
static struct DosLibrary base={.dl_Root=&root}; static struct DosLibrary *DOSBase=&base;
struct DosPacket {LONG dp_Type;SIPTR dp_Arg1,dp_Arg2,dp_Arg3,dp_Arg4,dp_Arg5,dp_Arg6,dp_Arg7,dp_Res1,dp_Res2;struct DosPacket *next;pthread_mutex_t mutex;pthread_cond_t cond;int done;};
static _Thread_local struct Task *self; static _Thread_local struct DosPacket *pending;
static _Thread_local unsigned read_held,write_held,entry_held;
static pthread_rwlock_t entry_lock=PTHREAD_RWLOCK_INITIALIZER;
static pthread_rwlock_t dos_lock=PTHREAD_RWLOCK_INITIALIZER;
static pthread_mutex_t fifo_lock=PTHREAD_MUTEX_INITIALIZER;static pthread_cond_t fifo_cond=PTHREAD_COND_INITIALIZER;
static struct DosPacket *head,*tail;
#define fixture_entry ((struct DosList*)BADDR(info.di_DevInfo))
static struct MsgPort target={{NT_MSGPORT,NULL}},wrong={{NT_MSGPORT,NULL}};
static atomic_int enqueued,published,writer_done; static int race_mode,mutator,stopping,copy_fail,copy_error,fail_packet,fail_port,fail_vec,vec_calls,fail_locate;
static atomic_uint reads,unreads,packets,freed,ports,deleted,copies,frees,allocs;
#define __is_process(t) (((struct Task *)(t))->tc_Node.ln_Type==NT_PROCESS)
#define ASSERT_VALID_TASK(t) assert((t)&&((t)->tc_Node.ln_Type==NT_TASK||(t)->tc_Node.ln_Type==NT_PROCESS))
static struct Task *FindTask(void *p){(void)p;return self;}
static int TypeOfMem(void *p){return p==&target||p==&wrong;}
static void SetIoErr(LONG x){if(__is_process(self))((struct Process*)self)->pr_Result2=x;}
static struct DosList *LockDosList(ULONG flags){
 if(flags&LDF_ENTRY){assert(!entry_held&&(read_held||write_held));assert(!(flags&LDF_WRITE)?!pthread_rwlock_rdlock(&entry_lock):!pthread_rwlock_wrlock(&entry_lock));entry_held=1;return (void*)&info.di_DevInfo;}
 if(flags&LDF_WRITE){if(!write_held){assert(!read_held);assert(!pthread_rwlock_wrlock(&dos_lock));}write_held++;if(race_mode&&atomic_load(&enqueued))atomic_store(&published,1);}
 else{if(!write_held&&!read_held)assert(!pthread_rwlock_rdlock(&dos_lock));read_held++;reads++;}return (void*)&info.di_DevInfo;
}
static void unlock_dos(ULONG flags){if(flags&LDF_ENTRY){assert(entry_held);entry_held=0;assert(!pthread_rwlock_unlock(&entry_lock));return;}assert(!entry_held);if(flags&LDF_WRITE){assert(write_held&&!read_held);if(!--write_held)assert(!pthread_rwlock_unlock(&dos_lock));}else{assert(read_held);unreads++;if(!--read_held&&!write_held)assert(!pthread_rwlock_unlock(&dos_lock));}}
#define UnLockDosList(flags) ((void)DOSBase, unlock_dos(flags))
void assign_generation_changed(struct DosLibrary *DOSBase);
LONG AddDosEntry(struct DosList *dlist);
LONG RemDosEntry(struct DosList *dlist);
static struct DosList *FindDosEntry(struct DosList *list,const char *name,ULONG flags){(void)list;(void)flags;assert(read_held||write_held);return !strcmp(name,"TEST")?fixture_entry:NULL;}
static struct DosList *MakeDosEntry(const char *name,LONG type){assert(!strcmp(name,"TEST"));struct DosList *v=calloc(1,sizeof(*v));assert(v);allocs++;v->dol_Type=type;v->dol_Name="TEST";return v;}
static void FreeDosEntry(struct DosList *v){if(v){allocs--;free(v);}}
static void *AllocVec(size_t n,unsigned flags){(void)flags;if(++vec_calls==fail_vec)return NULL;void *v=calloc(1,n);assert(v);return v;}
static void FreeVec(void *p){free(p);}
static void CopyMem(const void *s,void *d,size_t n){memcpy(d,s,n);}
static struct DosPacket *allocdospacket(void){if(fail_packet)return NULL;struct DosPacket *p=calloc(1,sizeof(*p));assert(p);pthread_mutex_init(&p->mutex,NULL);pthread_cond_init(&p->cond,NULL);packets++;return p;}
static void freedospacket(struct DosPacket *p){pthread_mutex_destroy(&p->mutex);pthread_cond_destroy(&p->cond);freed++;free(p);}
static struct MsgPort *CreateMsgPort(void){if(fail_port)return NULL;struct MsgPort *p=calloc(1,sizeof(*p));assert(p);ports++;return p;}
static void DeleteMsgPort(struct MsgPort *p){deleted++;free(p);}
static SIPTR handleNIL(LONG a,SIPTR b,SIPTR c,SIPTR d){(void)a;(void)b;(void)c;(void)d;abort();}
static void pause_short(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
static void internal_SendPkt(struct DosPacket *dp,struct MsgPort *to,struct MsgPort *back){(void)back;assert(to==&target);if(race_mode&&(dp->dp_Type==ACTION_COPY_DIR&&race_mode==1)&&!read_held){atomic_store(&enqueued,1);while(!atomic_load(&writer_done))pause_short();}pending=dp;pthread_mutex_lock(&fifo_lock);if(tail)tail->next=dp;else head=dp;tail=dp;if((dp->dp_Type==ACTION_COPY_DIR&&race_mode==1)||(dp->dp_Type==ACTION_SAME_LOCK&&race_mode==2)||(dp->dp_Type==ACTION_LOCATE_OBJECT&&race_mode==3))atomic_store(&enqueued,1);pthread_cond_signal(&fifo_cond);pthread_mutex_unlock(&fifo_lock);}
static struct DosPacket *internal_WaitPkt(struct MsgPort *p){(void)p;struct DosPacket *dp=pending;pthread_mutex_lock(&dp->mutex);while(!dp->done)pthread_cond_wait(&dp->cond,&dp->mutex);pthread_mutex_unlock(&dp->mutex);return dp;}
static void Alert(int x){(void)x;abort();}
static SIPTR dopacket(SIPTR*,struct MsgPort*,LONG,SIPTR,SIPTR,SIPTR,SIPTR,SIPTR,SIPTR,SIPTR);
static BPTR newlock(unsigned object){struct FileLock *f=calloc(1,sizeof(*f));assert(f);f->fl_Task=&target;f->object=object;f->active=1;return (BPTR)f;}
/* Tombstones remain until teardown so FIFO violations fail an assertion,
 * rather than depending on allocator reuse or producing a harness UAF. */
static BPTR retired[512];static unsigned retired_count;
static void UnLock(BPTR lock){if(lock){SIPTR e;assert(dopacket(&e,&target,ACTION_FREE_LOCK,(SIPTR)lock,0,0,0,0,0,0));}}
static LONG SameLock(BPTR a,BPTR b){SIPTR e;return dopacket(&e,&target,ACTION_SAME_LOCK,a,b,0,0,0,0,0);}
static BOOL NameFromLock(BPTR a,STRPTR name,LONG length){SIPTR e;return dopacket(&e,&target,ACTION_NAME,a,(SIPTR)name,length,0,0,0,0);}
static BPTR Lock(CONST_STRPTR path,LONG mode){SIPTR e;return dopacket(&e,&target,ACTION_LOCATE_OBJECT,(SIPTR)path,mode,0,0,0,0,0);}
static LONG IoErr(void){return __is_process(self)?((struct Process*)self)->pr_Result2:0;}
#include "actual.c"
static void *handler(void *unused){(void)unused;for(;;){pthread_mutex_lock(&fifo_lock);while(!head&&!stopping)pthread_cond_wait(&fifo_cond,&fifo_lock);if(!head){pthread_mutex_unlock(&fifo_lock);break;}struct DosPacket *p=head;head=p->next;if(!head)tail=NULL;pthread_mutex_unlock(&fifo_lock);
 if((race_mode==1&&p->dp_Type==ACTION_COPY_DIR)||(race_mode==2&&p->dp_Type==ACTION_SAME_LOCK)||(race_mode==3&&p->dp_Type==ACTION_LOCATE_OBJECT))while(!atomic_load(&published))pause_short();
 assert(!pthread_rwlock_wrlock(&dos_lock));struct FileLock *f=(void*)p->dp_Arg1;if(p->dp_Type!=ACTION_LOCATE_OBJECT)assert(f&&f->active&&f->fl_Task==&target);
 if(p->dp_Type==ACTION_COPY_DIR){copies++;if(copy_fail){p->dp_Res1=0;p->dp_Res2=copy_error;}else p->dp_Res1=(SIPTR)newlock(f->object);}
 else if(p->dp_Type==ACTION_SAME_LOCK){struct FileLock *other=(void*)p->dp_Arg2;assert(other&&other->active);p->dp_Res1=f->object==other->object?LOCK_SAME:1;}
 else if(p->dp_Type==ACTION_NAME){assert(p->dp_Arg3>=9);memcpy((void*)p->dp_Arg2,"SYS:path",9);p->dp_Res1=1;}
 else if(p->dp_Type==ACTION_LOCATE_OBJECT){assert(!strcmp((char*)p->dp_Arg1,"SYS:path"));p->dp_Res1=fail_locate?0:(SIPTR)newlock(7);p->dp_Res2=fail_locate?ERROR_OBJECT_NOT_FOUND:0;}
 else{assert(p->dp_Type==ACTION_FREE_LOCK);frees++;f->active=0;assert(retired_count<512);retired[retired_count++]=(BPTR)f;p->dp_Res1=1;}
 pthread_rwlock_unlock(&dos_lock);pthread_mutex_lock(&p->mutex);p->done=1;pthread_cond_signal(&p->cond);pthread_mutex_unlock(&p->mutex);
 }return NULL;}
static void *writer(void *unused){(void)unused;struct Task task={.tc_Node.ln_Type=NT_TASK};self=&task;while(!atomic_load(&enqueued))pause_short();switch(mutator){case 0:assert(AssignLock("TEST",newlock(2)));break;case 1:assert(AssignLock("TEST",BNULL));break;case 2:assert(AssignPath("TEST","SYS:path"));break;case 3:assert(AssignLate("TEST","SYS:path"));break;default:abort();}atomic_store(&writer_done,1);return NULL;}
static void reset(void){assert(!fixture_entry&&!read_held&&!write_held&&!head);race_mode=copy_fail=copy_error=fail_packet=fail_port=fail_vec=vec_calls=fail_locate=0;reads=unreads=packets=freed=ports=deleted=copies=frees=0;atomic_store(&enqueued,0);atomic_store(&published,0);atomic_store(&writer_done,0);}
static void cleanup(void){race_mode=0;if(fixture_entry){assert(AssignLock("TEST",BNULL));}assert(packets==freed&&ports==deleted&&reads==unreads&&!allocs);for(unsigned i=0;i<retired_count;i++)free((void*)retired[i]);retired_count=0;}
int main(void){pthread_t service;assert(!pthread_create(&service,NULL,handler,NULL));struct Task *task=calloc(1,sizeof(*task));assert(task);task->tc_Node.ln_Type=NT_TASK;struct Process proc={.pr_Task.tc_Node.ln_Type=NT_PROCESS};unsigned cases=0;
 for(int kind=0;kind<2;kind++){self=kind?&proc.pr_Task:task;
  for(mutator=0;mutator<4;mutator++){reset();assert(AssignLock("TEST",newlock(1)));fixture_entry->dol_Task=&wrong;struct AssignList *al=calloc(1,sizeof(*al));assert(al);al->al_Lock=newlock(3);fixture_entry->dol_misc.dol_assign.dol_List=al;race_mode=1;pthread_t thread;assert(!pthread_create(&thread,NULL,writer,NULL));SIPTR error=-1;LONG type=-1;struct MsgPort *port=NULL;BPTR copy=DupAssignLock("TEST",&port,&type,&error);assert(copy&&type==DLT_DIRECTORY&&!error&&port==&target&&((struct FileLock*)copy)->object==1);pthread_join(thread,NULL);race_mode=0;UnLock(copy);assert(copies==1&&frees>=3);cleanup();cases++;}
  for(int fail=1;fail<=4;fail++){reset();assert(AssignLock("TEST",newlock(1)));fail_packet=fail==1;fail_port=fail==2&&!kind;copy_fail=fail>=3;copy_error=fail==3?ERROR_INVALID_LOCK:0;SIPTR error=-1;LONG type=-1;BPTR copy=DupAssignLock("TEST",NULL,&type,&error);if(fail==2&&kind){assert(copy);UnLock(copy);}else{assert(!copy);assert(error==(fail<=2?ERROR_NO_FREE_STORE:fail==3?ERROR_INVALID_LOCK:ERROR_BAD_NUMBER));}fail_packet=fail_port=copy_fail=0;cleanup();cases++;}
  for(LONG type=DLT_DEVICE;type<=5;type++){if(type==DLT_DIRECTORY)continue;reset();assert(AddDosEntry(MakeDosEntry("TEST",type)));SIPTR error=-1;LONG actual=-1;assert(!DupAssignLock("TEST",NULL,&actual,&error)&&actual==type&&error==(type==5?ERROR_BAD_NUMBER:0)&&!packets);struct DosList *old=fixture_entry;assert(RemDosEntry(old));FreeDosEntry(old);cleanup();cases++;}
  reset();SIPTR error;LONG actual;assert(!DupAssignLock(NULL,NULL,&actual,&error)&&error==ERROR_BAD_NUMBER);assert(!DupAssignLock("",NULL,NULL,&error)&&error==ERROR_BAD_NUMBER);assert(!DupAssignLock("missing",NULL,NULL,&error)&&error==ERROR_DEVICE_NOT_MOUNTED);assert(AddDosEntry(MakeDosEntry("TEST",DLT_DIRECTORY)));assert(!DupAssignLock("TEST",NULL,NULL,&error)&&error==ERROR_INVALID_LOCK);cleanup();cases+=4;
 }
 self=task;
 for(int scenario=0;scenario<5;scenario++){
  reset();assert(AssignLock("TEST",newlock(1)));assert(AssignAdd("TEST",newlock(2)));assert(AssignAdd("TEST",newlock(3)));
  BPTR sought=newlock(scenario==0?1:scenario==1?2:scenario==2?3:9);
  if(scenario==4){race_mode=2;mutator=0;pthread_t w;assert(!pthread_create(&w,NULL,writer,NULL));assert(!RemAssignList("TEST",sought));pthread_join(w,NULL);race_mode=0;assert(fixture_entry&&((struct FileLock*)fixture_entry->dol_Lock)->object==2);}
  else{BOOL ok=RemAssignList("TEST",sought);assert(!!ok==(scenario<3));if(scenario==0)assert(((struct FileLock*)fixture_entry->dol_Lock)->object==2);else assert(((struct FileLock*)fixture_entry->dol_Lock)->object==1);}
  UnLock(sought);cleanup();cases++;
 }
 for(int scenario=0;scenario<7;scenario++){
  reset();if(scenario==2||scenario==3||scenario==4||scenario==5||scenario==6){assert((scenario==3?AssignPath("TEST","SYS:path"):AssignLate("TEST","SYS:path")));}else assert(AssignLock("TEST",newlock(1)));
  BPTR incoming=newlock(9);
  if(scenario==6){struct AssignList *al=calloc(1,sizeof(*al));assert(al);al->al_Lock=newlock(8);fixture_entry->dol_misc.dol_assign.dol_List=al;assert(AssignAddToList("TEST",incoming,0));assert(fixture_entry->dol_Type==DLT_DIRECTORY&&((struct FileLock*)fixture_entry->dol_Lock)->object==7&&((struct FileLock*)al->al_Next->al_Lock)->object==9);}
  else if(scenario==4){race_mode=3;mutator=0;pthread_t w;assert(!pthread_create(&w,NULL,writer,NULL));assert(AssignAddToList("TEST",incoming,(ULONG)-1));pthread_join(w,NULL);race_mode=0;assert(fixture_entry->dol_misc.dol_assign.dol_List&&((struct FileLock*)fixture_entry->dol_misc.dol_assign.dol_List->al_Lock)->object==2);}
  else if(scenario==5){fail_locate=1;assert(!AssignAddToList("TEST",incoming,(ULONG)-1));fail_locate=0;assert(!fixture_entry->dol_Lock);UnLock(incoming);}
  else {assert(AssignAddToList("TEST",incoming,scenario==1?0:(ULONG)-1));assert(fixture_entry->dol_Type==DLT_DIRECTORY&&((struct FileLock*)fixture_entry->dol_Lock)->object==9);assert(fixture_entry->dol_misc.dol_assign.dol_List);}
  cleanup();cases++;
 }
 for(int point=1;point<=3;point++){reset();assert(AssignLate("TEST","SYS:path"));BPTR incoming=newlock(9);vec_calls=0;fail_vec=point;assert(!AssignAddToList("TEST",incoming,(ULONG)-1));fail_vec=0;assert(!fixture_entry->dol_Lock);UnLock(incoming);cleanup();cases++;}
 reset();assert(AssignLock("TEST",newlock(1)));base.assignGenerationLow=UINT32_MAX;ULONG high=base.assignGenerationHigh;assert(AssignAdd("TEST",newlock(2)));assert(!base.assignGenerationLow&&base.assignGenerationHigh==high+1);cleanup();cases++;
 self=task;pthread_mutex_lock(&fifo_lock);stopping=1;pthread_cond_signal(&fifo_cond);pthread_mutex_unlock(&fifo_lock);pthread_join(service,NULL);free(task);printf("PASS %u actual assign/transport cases, COPY before FREE with handler DOS WRITE\n",cases);return 0;}
