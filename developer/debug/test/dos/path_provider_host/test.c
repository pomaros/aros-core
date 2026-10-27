/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <sched.h>
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef intptr_t SIPTR;
typedef uintptr_t IPTR, BPTR;
typedef int BOOL;
typedef void *APTR;
typedef const char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define NT_TASK 1
#define NT_PROCESS 13
#define ERROR_TOO_MANY_LEVELS 217
#define AROS_LHA(t,n,r) t n
#define AROS_LH0(t,n,bt,bn,v,l) t aros_##n(bt bn)
#define AROS_LH1(t,n,a,bt,bn,v,l) t aros_##n(a,bt bn)
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT
struct DosLibrary { int unused; };
struct Node { unsigned ln_Type; };
struct Task { struct Node tc_Node; IPTR slots[64]; APTR expected; };
struct Process { struct Task pr_Task; SIPTR pr_Result2; };
#define __is_process(p) ((p)->pr_Task.tc_Node.ln_Type==NT_PROCESS)
#define ASSERT_VALID_PROCESS(p) assert(__is_process(p))
static _Thread_local struct Task *task;
static _Thread_local BOOL fail_set;
static _Atomic LONG allocation_calls, free_slots, alloc_arrivals;
static BOOL fail_allocate;
static int resource;
static APTR stale_parent;
static struct Task *FindTask(APTR ignored) { (void)ignored;return task; }
static APTR OpenResource(CONST_STRPTR ignored) { (void)ignored;return &resource; }
static LONG allocate(APTR base) {
 assert(base==&resource);if(fail_allocate)return -1;
 LONG id=atomic_fetch_add(&allocation_calls,1)+1;
 atomic_fetch_add(&alloc_arrivals,1);while(atomic_load(&alloc_arrivals)<2)sched_yield();return id;
}
static void release_slot(LONG id,APTR base) { assert(base==&resource && id>0);atomic_fetch_add(&free_slots,1); }
static IPTR get_slot(LONG id,APTR base) { assert(base==&resource && id>0 && id<64);return task->slots[id]; }
static BOOL set_slot(LONG id,IPTR value,APTR base) {assert(base==&resource && id>0 && id<64);if(fail_set)return FALSE;task->slots[id]=value;return TRUE;}
#define AllocTaskStorageSlot() allocate(TaskResBase)
#define FreeTaskStorageSlot(id) release_slot(id,TaskResBase)
#define GetTaskStorageSlot(id) get_slot(id,TaskResBase)
#define SetTaskStorageSlot(id,value) set_slot(id,value,TaskResBase)
#define GetParentTaskStorageSlot(id) ((void)(id),(void)TaskResBase,(IPTR)stale_parent)
static SIPTR aros_SetIoErr(SIPTR,struct DosLibrary *);
static SIPTR aros_IoErr(struct DosLibrary *);
#define SetIoErr(v) aros_SetIoErr((v),DOSBase)
#define IoErr() aros_IoErr(DOSBase)
/* PUBLIC_HEADER */
/* PRIVATE_HEADER */
/* REGISTRY_SOURCE */
/* IOERR_SOURCE */
/* SETIOERR_SOURCE */

static BPTR boundary_Open(CONST_STRPTR name,LONG accessMode,struct DosLibrary *DOSBase)
{
/* OPEN_DISPATCH */
 /* The native operation requires Process fields, which are absent for the
  * ordinary Task used below. Reaching it is a deterministic boundary failure. */
 assert(task->tc_Node.ln_Type==NT_PROCESS);
 (void)name;(void)accessMode;return 999;
}
static _Atomic int invocations;
static BOOL accept(APTR data) {
 struct DosLibrary dos;
 assert(!aros_SetTaskPathResolver(NULL,&dos) && "policy callback registration must stay held");
 return *(int *)data != 3;
}
static SIPTR invoke(APTR data,LONG operation,const SIPTR *args,SIPTR *error,struct DosLibrary *DOSBase)
{
 assert(data==task->expected && operation==DOS_PATH_OPEN && args[1]==17);
 assert(!strcmp((CONST_STRPTR)args[0],"relative-name"));
 invocations++;
 assert(!aros_SetTaskPathResolver(NULL,DOSBase)); /* in-flight registration stays held */
 if(*(int *)data==2) {
  BOOL handled=FALSE;SIPTR nested=__dos_path_operation(operation,args,&handled,DOSBase);
  assert(handled && !nested && IoErr()==ERROR_TOO_MANY_LEVELS);
 }
 *error=789;return 456;
}
static void *worker(void *data)
{
 struct Task local={.tc_Node={NT_TASK},.expected=data};task=&local;
 struct DosLibrary dos;
 struct DosPathResolver own={.version=DOS_PATH_RESOLVER_VERSION,.accept=accept,.invoke=invoke,.data=data};
 assert(aros_SetTaskPathResolver(&own,&dos));assert(__dos_task_path_resolver()==&own);
 assert(boundary_Open("relative-name",17,&dos)==456 && aros_IoErr(&dos)==789);
 assert(aros_SetIoErr(123,&dos)==789 && aros_IoErr(&dos)==123);
 struct DosPathResolver invalid=own;invalid.version++;
 assert(!aros_SetTaskPathResolver(&invalid,&dos) && __dos_task_path_resolver()==&own);
 fail_set=TRUE;assert(!aros_SetTaskPathResolver(NULL,&dos));fail_set=FALSE;
 assert(__dos_task_path_resolver()==&own);
 int decline=3;own.data=&decline;
 assert(!boundary_Open("relative-name",17,&dos) && aros_IoErr(&dos)==ERROR_TOO_MANY_LEVELS && "plain Task must never fall through to native Process fields");
 assert(aros_SetTaskPathResolver(NULL,&dos));assert(!__dos_task_path_resolver());
 assert(aros_IoErr(&dos)==0 && aros_SetIoErr(1,&dos)==0);
 return NULL;
}
int main(void)
{
 struct Task caller={.tc_Node={NT_TASK}};task=&caller;struct DosLibrary dos;
 struct DosPathResolver valid={.version=DOS_PATH_RESOLVER_VERSION,.accept=accept,.invoke=invoke};
 fail_allocate=TRUE;assert(!aros_SetTaskPathResolver(&valid,&dos));fail_allocate=FALSE;
 assert(!__dos_task_path_resolver());
 struct DosPathResolver *parent=calloc(1,sizeof(*parent));assert(parent);
 *parent=valid;stale_parent=parent;free(parent); /* wrong parent fallback is detectable */
 int a=1,b=2;pthread_t first,second;
 assert(!pthread_create(&first,NULL,worker,&a));assert(!pthread_create(&second,NULL,worker,&b));
 assert(!pthread_join(first,NULL));assert(!pthread_join(second,NULL));
 assert(allocation_calls==2 && free_slots==1 && invocations==2);
 struct Process process={.pr_Task={.tc_Node={NT_PROCESS}},.pr_Result2=321};task=&process.pr_Task;
 assert(aros_IoErr(&dos)==321 && aros_SetIoErr(654,&dos)==321 && aros_IoErr(&dos)==654);
 assert(boundary_Open("native",0,&dos)==999);
 int decline=3;valid.data=&decline;
 assert(aros_SetTaskPathResolver(&valid,&dos));
 assert(boundary_Open("native",0,&dos)==999 && "declined Process operation must run natively");
 assert(aros_IoErr(&dos)==654 && "declining must preserve native IoErr");
 assert(aros_SetTaskPathResolver(NULL,&dos));return 0;
}
