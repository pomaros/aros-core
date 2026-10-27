/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef void *APTR;
typedef void *BPTR;
typedef intptr_t SIPTR;
typedef uintptr_t IPTR;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define BNULL NULL
#define BADDR(p) (p)
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define ERROR_INVALID_LOCK 211
#define ERROR_NO_FREE_STORE 103
#define ACTION_COPY_DIR 19
#define ACTION_FREE_LOCK 15
#define LOCK_SAME 0
#define NT_PROCESS 13
struct MsgPort { int unused; } filesystem;
struct FileLock { struct MsgPort *fl_Task; unsigned identity; };
struct SignalSemaphore { int held; };
struct Node { int ln_Type; };
struct Task { struct Node tc_Node; };
struct Process { struct Task task; BPTR pr_CurrentDir; } process;
struct Task plain;
struct DosPathResolver { int unused; };
struct PosixCIntBase;
struct DosLibrary;
static struct Task *self;
static struct __cwd_binding *current;
static int allocations, locks, fail_copy;
static struct FileLock root={&filesystem,1}, db={&filesystem,2}, data={&filesystem,3}, other={&filesystem,4};
static void *AllocVec(size_t n, unsigned flags) { (void)flags; void *p=calloc(1,n); assert(p);allocations++;return p; }
static void FreeVec(void *p) { if(p){allocations--;free(p);} }
static void InitSemaphore(struct SignalSemaphore *s) { s->held=0; }
static void ObtainSemaphore(struct SignalSemaphore *s) { assert(!s->held);s->held=1; }
static void ReleaseSemaphore(struct SignalSemaphore *s) { assert(s->held);s->held=0; }
static struct Task *FindTask(APTR unused) { (void)unused;return self; }
static int SameLock(BPTR a,BPTR b) { return ((struct FileLock *)a)->identity==((struct FileLock *)b)->identity ? LOCK_SAME : 1; }
static SIPTR DoPktResult(struct MsgPort *port,LONG action,const SIPTR *args,SIPTR *error) {
 assert(port==&filesystem);*error=0;
 if(action==ACTION_COPY_DIR){if(fail_copy){fail_copy=0;*error=ERROR_NO_FREE_STORE;return 0;}
  struct FileLock *p=malloc(sizeof(*p));assert(p);*p=*(struct FileLock *)args[0];locks++;return (SIPTR)p;}
 assert(action==ACTION_FREE_LOCK);locks--;free((APTR)args[0]);return 1;
}
static void UnLock(BPTR lock) { SIPTR args[7]={(SIPTR)lock},error;DoPktResult(&filesystem,ACTION_FREE_LOCK,args,&error); }
/* CONTEXT_HEADER */
/* RUNTIME_HEADER */
/* CONTEXT_ENGINE */
/* BINDING_TYPE */
static struct __cwd_binding *binding(void) { return current; }
static BPTR native_initial_directory(struct Task *task,BPTR *program) {
 SIPTR args[7]={(SIPTR)&root},error;assert(task==&process.task);*program=BNULL;
 return (BPTR)DoPktResult(&filesystem,ACTION_COPY_DIR,args,&error);
}
/* FUNCTIONS */
static unsigned selected(void) {
 struct __cwd_snapshot snapshot;assert(__cwd_runtime_snapshot(&snapshot));
 unsigned n=((struct FileLock *)__cwd_directory_lock(snapshot.cwd))->identity;
 __cwd_snapshot_release(&snapshot);return n;
}
static void logical(struct FileLock *lock) {
 LONG error;struct __cwd_directory *d=__cwd_directory_capture(lock,&error);assert(d);
 assert(__cwd_context_set(current->context,d));__cwd_directory_release(d);
}
int main(void) {
 LONG error;
 process.task.tc_Node.ln_Type=NT_PROCESS;process.pr_CurrentDir=&root;self=&process.task;
 struct __cwd_binding *b=AllocVec(sizeof(*b),MEMF_PUBLIC|MEMF_CLEAR);
 b->references=1;b->context=__cwd_context_create(&root,&data,FALSE,&error);
 b->native_cwd=__cwd_directory_capture(&root,&error);current=b;
 assert(selected()==1);
 logical(&other);
 assert(selected()==4 && "unchanged native directory must preserve shared logical chdir");
 process.pr_CurrentDir=&db;
 assert(selected()==2 && "native CurrentDir must select the new directory");
 assert(process.pr_CurrentDir==&db && "runtime must not steal or replace the native lock");
 assert(__cwd_runtime_scope_enter());process.pr_CurrentDir=&data;
 assert(selected()==2 && "a scoped operation must retain its admitted snapshot");
 __cwd_runtime_scope_leave();assert(selected()==3);
 process.pr_CurrentDir=&other;fail_copy=1;
 struct __cwd_snapshot snapshot;
 assert(!__cwd_runtime_snapshot(&snapshot) && "failed native lock copy must refuse snapshot");
 assert(((struct FileLock *)__cwd_directory_lock(b->native_cwd))->identity==3);
 assert(selected()==4 && "copy failure must leave a retryable owned observation");
 self=&plain;assert(selected()==4 && "plain Task must not read a Process extension");
 self=&process.task;process.pr_CurrentDir=BNULL;assert(selected()==1);
 logical(&other);assert(selected()==4 && "unchanged root must preserve shared logical chdir");
 binding_release(b);current=NULL;
 assert(!allocations && !locks && "all native observations and snapshots must be released");
 puts("PASS native CurrentDir changes, logical namespace, scope pin, plain Task, root and copy refusal");
}
