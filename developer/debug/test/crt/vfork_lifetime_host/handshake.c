/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
/* The complete production launcher and final parent-return function are
 * injected by handshake.py. Exec signals and module bodies are the boundary. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sched.h>
typedef long LONG;typedef signed char BYTE;typedef unsigned char UBYTE;
typedef uintptr_t IPTR;typedef void *APTR;typedef void *BPTR;typedef int BOOL;typedef void fdesc;
#define D(x) do {} while(0)
#define ASSERTPARENTSTATE(x) do {} while(0)
#define ASSERTCHILDSTATE(x) do {} while(0)
#define PRINTSTATE do {} while(0)
#define SETCHILDSTATE(x) udata->child_state=(x)
#define SETPARENTSTATE(x) udata->parent_state=(x)
#define CHILD_STATE_SETUP_FAILED 1
#define CHILD_STATE_SETUP_FINISHED 2
#define CHILD_STATE_EXEC_PREPARE_FINISHED 4
#define CHILD_STATE_UDATA_NOT_USED 8
#define CHILD_STATE_EXEC_FAILED_READY 16
#define PARENT_STATE_EXEC_CALLED 2
#define PARENT_STATE_EXEC_DO_FINISHED 4
#define PARENT_STATE_STOPPED_PRETENDING 8
#define _VFORK_COPYENV(a,b) memcpy((a),(b),sizeof(jmp_buf))
struct Library {int kind;};struct StdCBase;struct StdCIOBase;struct __cwd_context;struct __fd_namespace;struct __env_item;
struct Task { APTR tc_UserData; pthread_mutex_t guard; pthread_cond_t changed; unsigned signals; };
struct PosixCIntBase {struct vfork_data *vfork_data;struct {struct StdCBase *StdCBase;struct StdCIOBase *StdCIOBase;} PosixCBase;int doupath;};
/* VFORK_DATA */
static _Thread_local struct Task *self;
static _Thread_local jmp_buf *exit_jump;
static _Thread_local int *exit_code;
static struct Task parent,child;
static atomic_int freed,closed,restored,borrowed;
static int terminal_wait, premature_control, fail_first, setup_failure;
struct ETask {LONG et_Result1;};static struct ETask etask;
struct ETask *GetETask(struct Task *task) {assert(task==&child);return &etask;}
struct Task *FindTask(APTR n) {(void)n;return self;}
BYTE AllocSignal(int n) {(void)n;return setup_failure==1?-1:4;}
void FreeSignal(int n) {assert(n==4);}
void Signal(struct Task *task,unsigned bits) {
 if(task==&parent && self==&child && ((struct vfork_data *)child.tc_UserData)->child_state==CHILD_STATE_SETUP_FINISHED)atomic_store(&borrowed,1);
 assert(!pthread_mutex_lock(&task->guard));task->signals|=bits;
 assert(!pthread_cond_signal(&task->changed));assert(!pthread_mutex_unlock(&task->guard));
}
unsigned Wait(unsigned bits) {
 assert(!pthread_mutex_lock(&self->guard));
 while(!(self->signals&bits))assert(!pthread_cond_wait(&self->changed,&self->guard));
 unsigned v=self->signals&bits;self->signals&=~bits;assert(!pthread_mutex_unlock(&self->guard));
 if(self==&child && terminal_wait)while(!atomic_load(&freed))sched_yield();
 return v;
}
struct Library *OpenLibrary(char *name,int version) {
 (void)version;
 if((setup_failure==2&&!strcmp(name,"posixc.library")) || (setup_failure==3&&!strcmp(name,"stdc.library")) || (setup_failure==4&&!strcmp(name,"stdcio.library")))return NULL;
 if(!strcmp(name,"posixc.library")) {struct PosixCIntBase *b=calloc(1,sizeof(*b));assert(b);return (APTR)b;}
 struct Library *lib=calloc(1,sizeof(*lib));assert(lib);lib->kind=1;return lib;
}
void CloseLibrary(struct Library *lib) {
 assert(!atomic_load(&borrowed)||atomic_load(&restored)); /* no proxy before setup succeeds */
 atomic_store(&closed,1);free(lib);
}
static void child_takeover(struct vfork_data *u) {(void)u;}
void *__exec_prepare(const char *name,int flags,char *const *argv,char *const *env) {(void)name;(void)flags;(void)argv;(void)env;if(!strcmp(name,"missing")) {errno=ENOENT;return NULL;}return(APTR)(uintptr_t)3;}
void __stdc_program_startup(jmp_buf env,int *code) {exit_jump=(APTR)env;exit_code=code;}
void __exec_do(APTR id) {assert(id==(APTR)(uintptr_t)3);*exit_code=37;longjmp(*exit_jump,1);}
int *__stdc_set_errorptr(int *p) {return p;}
void __stdc_set_exitjmp(jmp_buf a,jmp_buf b) {(void)a;(void)b;}
int GetETaskID(struct Task *t) {assert(t==&child && !atomic_load(&closed));return 91;}
void FreeMem(APTR p,size_t bytes) {(void)bytes;free(p);atomic_store(&freed,1);}
static void parent_leavepretendchild(struct vfork_data *u) {(void)u;assert(!atomic_load(&closed));atomic_store(&restored,1);}
void vfork_longjmp(jmp_buf env,int val) {longjmp(env,val);}
/* LAUNCHER */
/* PARENT_RETURN */
/* PREPARE_PROXY */
static void *run_child(void *unused) {(void)unused;self=&child;return(APTR)(intptr_t)launcher();}
static void init_task(struct Task *t) {memset(t,0,sizeof(*t));assert(!pthread_mutex_init(&t->guard,NULL));assert(!pthread_cond_init(&t->changed,NULL));}
int main(void) {
 for(int mode=0;mode<4;mode++) {
  int exec=mode==1 || mode==3;
  init_task(&parent);init_task(&child);self=&parent;
  atomic_store(&freed,0);atomic_store(&closed,0);atomic_store(&restored,0);atomic_store(&borrowed,0);terminal_wait=0;
  etask.et_Result1=-999;fail_first=mode>=2;
  struct vfork_data *u=calloc(1,sizeof(*u));assert(u);child.tc_UserData=u;u->parent=&parent;u->child=&child;u->parent_signal=4;
  struct PosixCIntBase parent_base={.vfork_data=u};u->parent_posixcbase=&parent_base;
  pthread_t thread;assert(!pthread_create(&thread,NULL,run_child,NULL));Wait(1U<<4);assert(u->child_state==CHILD_STATE_SETUP_FINISHED);
  if(fail_first) for(int attempt=0;attempt<128;attempt++) {
   assert(!__exec_prepare_pretend_child("missing",NULL,NULL,&parent_base));
   assert(errno==ENOENT && !u->child_executed && u->child_state==CHILD_STATE_EXEC_FAILED_READY);
  }
  if(exec) {
   assert(__exec_prepare_pretend_child("good",NULL,NULL,&parent_base)==&parent_base);
   assert(u->exec_id);Signal(&child,1U<<4);
  } else {u->child_error=mode==2?43:23;Signal(&child,1U<<4);}
  Wait(1U<<4);assert(u->child_state==CHILD_STATE_UDATA_NOT_USED);
  if(premature_control && !exec)while(!atomic_load(&closed))sched_yield();
  jmp_buf env;int result=setjmp(env);
  if(!result) {_VFORK_COPYENV(u->vfork_jmp,env);__vfork_exit_controlled_stack(u);assert(0);}
  assert(result==91);APTR ret;assert(!pthread_join(thread,&ret));assert((intptr_t)ret==(exec?37:mode==2?43:23));assert(etask.et_Result1==(LONG)(intptr_t)ret);
  assert(atomic_load(&freed)&&atomic_load(&closed));
  assert(!pthread_mutex_destroy(&parent.guard));assert(!pthread_cond_destroy(&parent.changed));
  assert(!pthread_mutex_destroy(&child.guard));assert(!pthread_cond_destroy(&child.changed));
 }
 for(setup_failure=1;setup_failure<=4;setup_failure++) {
  init_task(&parent);init_task(&child);self=&parent;
  atomic_store(&freed,0);atomic_store(&closed,0);atomic_store(&restored,0);atomic_store(&borrowed,0);terminal_wait=0;
  struct vfork_data *u=calloc(1,sizeof(*u));assert(u);child.tc_UserData=u;u->parent=&parent;u->child=&child;u->parent_signal=4;
  pthread_t thread;assert(!pthread_create(&thread,NULL,run_child,NULL));Wait(1U<<4);
  assert(u->child_state==CHILD_STATE_SETUP_FAILED && u->child_pid==91 && u->child_errno==ENOMEM);
  APTR ret;assert(!pthread_join(thread,&ret));assert((intptr_t)ret==-1);
  assert(!atomic_load(&borrowed));FreeMem(u,sizeof(*u));
  assert(!pthread_mutex_destroy(&parent.guard));assert(!pthread_cond_destroy(&parent.changed));
  assert(!pthread_mutex_destroy(&child.guard));assert(!pthread_cond_destroy(&child.changed));
 }
 puts("PASS: launcher/proxy/return exit23/exec37/failure then43/retry37; four real launcher setup failures publish UID before teardown");return 0;
}
