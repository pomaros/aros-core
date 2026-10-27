/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdatomic.h>
typedef uintptr_t BPTR, IPTR;
typedef intptr_t SIPTR;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef void *APTR;
#define BNULL 0
#define BADDR(x) ((void *)(x))
#define AROS_CONST_BSTR(x) (x)
#define SHARED_LOCK (-2)
#define TRUE 1
#define FALSE 0
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define ERROR_INVALID_LOCK 211
#define ERROR_BAD_NUMBER 115
#define ERROR_NO_FREE_STORE 103
#define ACTION_COPY_DIR 19
#define ACTION_LOCATE_OBJECT 8
#define TAG_DONE 0
#define AROS_LHA(t,n,r) t n
#define AROS_LH5(t,n,a,b,c,d,e,bt,bn,v,l) t n(a,b,c,d,e,bt bn)
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT
struct MsgPort { unsigned unused; };
struct RootNode { struct MsgPort *rn_BootProc; };
struct DosLibrary { struct RootNode *dl_Root; };
struct Task { APTR tc_UserData; pthread_mutex_t mutex; pthread_cond_t changed; ULONG signals,allocated; };
struct Process { struct Task task; };
struct FileLock { struct MsgPort *fl_Task; unsigned identity; };
struct TagItem { IPTR ti_Tag, ti_Data; };
typedef SIPTR (*DosContextCallback)(APTR,struct DosLibrary *);
enum { NP_Entry=1, NP_Name, NP_UserData, NP_ExitCode, NP_ExitData, NP_CurrentDir,
 NP_HomeDir, NP_Input, NP_Output, NP_Error, NP_CloseInput, NP_CloseOutput,
 NP_CloseError, NP_CopyVars, NP_Cli, NP_Synchronous, NP_WindowPtr, NP_StackSize, NP_ConsoleTask };
static _Thread_local struct Task *current;
static _Thread_local SIPTR ioerr;
static _Atomic int live_locks,live_vectors,callbacks,notifications;
static int fail_vector,fail_signal,fail_create,fail_copy,copy_calls;
static struct MsgPort handler;
static pthread_t worker;
static int started;
static struct Task caller;
static BPTR expected_cwd,expected_home;
static struct Task *FindTask(APTR unused) { (void)unused; return current; }
static SIPTR SetIoErr(SIPTR value) { SIPTR old=ioerr;ioerr=value;return old; }
static SIPTR IoErr(void) { return ioerr; }
static void *AllocVec(size_t n,unsigned f) { (void)f; if(fail_vector)return NULL;void *p=calloc(1,n);if(p)live_vectors++;return p; }
static void FreeVec(void *p) { if(p){live_vectors--;free(p);} }
static LONG AllocSignal(LONG requested) { assert(current==&caller && requested==-1);if(fail_signal)return -1;assert(!caller.allocated);caller.allocated=8;return 3; }
static ULONG SetSignal(ULONG value,ULONG mask) { assert(current==&caller);ULONG old=caller.signals;caller.signals=(old&~mask)|(value&mask);return old; }
static void FreeSignal(LONG bit) { pthread_mutex_lock(&caller.mutex);assert(bit==3 && caller.allocated==8);assert(!started || (notifications==1 && !caller.signals));caller.allocated=0;pthread_mutex_unlock(&caller.mutex); }
static void Signal(struct Task *task,ULONG mask) {
 assert(task==&caller && mask==8 && callbacks==1);
 /* Unlike PutMsg, there is no published completion before this notification.
  * All helper context locks must have been released already. */
 assert(live_locks==2);
 pthread_mutex_lock(&task->mutex);assert(task->allocated==mask);notifications++;
 task->signals|=mask;pthread_cond_signal(&task->changed);pthread_mutex_unlock(&task->mutex);
}
static ULONG Wait(ULONG mask) {
 assert(current==&caller && mask==8);pthread_mutex_lock(&caller.mutex);
 while(!(caller.signals&mask))pthread_cond_wait(&caller.changed,&caller.mutex);
 caller.signals&=~mask;pthread_mutex_unlock(&caller.mutex);return mask;
}
static BPTR lock_new(unsigned identity) {struct FileLock *p=malloc(sizeof(*p));assert(p);*p=(struct FileLock){&handler,identity};live_locks++;return (BPTR)p;}
static void UnLock(BPTR lock) {if(lock){live_locks--;free(BADDR(lock));}}
static SIPTR DoPktResult(struct MsgPort *p,LONG action,const SIPTR *args,SIPTR *error) {
 assert(p==&handler && (action==ACTION_COPY_DIR || action==ACTION_LOCATE_OBJECT));copy_calls++;
 if(fail_copy==copy_calls){*error=777;return 0;}*error=0;
 if(action==ACTION_LOCATE_OBJECT){assert(!args[0] && !strcmp((void *)args[1],"") && args[2]==SHARED_LOCK);return (SIPTR)lock_new(101);}
 return (SIPTR)lock_new(((struct FileLock *)BADDR(args[0]))->identity);
}
struct Launch { struct Process process; SIPTR (*entry)(void);void (*exit)(IPTR,IPTR);IPTR data;BPTR cwd,home; };
static _Thread_local struct Launch *running;
static BPTR CurrentDir(BPTR lock) {assert(current!=&caller && running);BPTR old=running->cwd;running->cwd=lock;return old;}
static BPTR SetProgramDir(BPTR lock) {assert(current!=&caller && running);BPTR old=running->home;running->home=lock;return old;}
static void *run(void *data) {
 struct Launch *p=data;current=&p->process.task;running=p;
 assert(p->cwd!=expected_cwd && p->home!=expected_home);
 assert(((struct FileLock *)BADDR(p->cwd))->identity==101);
 assert(((struct FileLock *)BADDR(p->home))->identity==(expected_home?303:101));
 SIPTR value=p->entry();p->exit(value,p->data);
 assert(!p->cwd && !p->home);UnLock(p->cwd);UnLock(p->home);free(p);return NULL;
}
static struct Process *CreateNewProc(const struct TagItem *tags) {
 if(fail_create)return NULL;struct Launch *p=calloc(1,sizeof(*p));assert(p);
 for(;tags->ti_Tag;tags++)switch(tags->ti_Tag){
 case NP_Entry:p->entry=(void *)tags->ti_Data;break;
 case NP_ExitCode:p->exit=(void *)tags->ti_Data;break;
 case NP_ExitData:p->data=tags->ti_Data;break;
 case NP_UserData:p->process.task.tc_UserData=(void *)tags->ti_Data;break;
 case NP_CurrentDir:p->cwd=tags->ti_Data;break;
 case NP_HomeDir:p->home=tags->ti_Data;break;
 case NP_CopyVars:case NP_Cli:case NP_Synchronous:case NP_Input:case NP_Output:case NP_Error:case NP_ConsoleTask:assert(!tags->ti_Data);break;
 }
 assert(p->entry && p->exit && p->data && p->cwd && p->home);
 started=1;assert(!pthread_create(&worker,NULL,run,p));return &p->process;
}
/* ACTUAL_SOURCE */
static SIPTR callback(APTR data,struct DosLibrary *dos) {
 assert(current!=&caller && data==&caller && dos);assert(callbacks==0);callbacks++;
 SetIoErr(987);return 123;
}
static void invoke(struct DosLibrary *dos,int expected_error) {
 SIPTR error=-1;callbacks=0;notifications=0;copy_calls=0;started=0;
 SIPTR value=RunWithContext(expected_cwd,expected_home,callback,&caller,&error,dos);
 if(started)pthread_join(worker,NULL);
 assert(error==expected_error);assert(live_locks==2 && live_vectors==0 && !caller.allocated);
 assert(value==(expected_error==987?123:0));assert(callbacks==(expected_error==987));
}
int main(void) {
 struct RootNode root={&handler};struct DosLibrary dos={&root};current=&caller;
 pthread_mutex_init(&caller.mutex,NULL);pthread_cond_init(&caller.changed,NULL);
 BPTR a=expected_cwd=lock_new(101), b=expected_home=lock_new(303);
 invoke(&dos,987);
 expected_cwd=0;invoke(&dos,987);expected_cwd=a;
 expected_home=0;invoke(&dos,987);expected_home=b;
 fail_copy=1;invoke(&dos,777);fail_copy=2;invoke(&dos,777);fail_copy=0;
 fail_vector=1;invoke(&dos,103);fail_vector=0;
 fail_signal=1;invoke(&dos,103);fail_signal=0;
 fail_create=1;invoke(&dos,103);fail_create=0;
 SIPTR error;assert(!RunWithContext(a,b,NULL,&caller,&error,&dos) && error==115);
 UnLock(a);UnLock(b);assert(!live_locks && !live_vectors);
 pthread_cond_destroy(&caller.changed);pthread_mutex_destroy(&caller.mutex);return 0;
}
