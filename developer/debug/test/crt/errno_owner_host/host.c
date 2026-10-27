/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License 1.1. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <setjmp.h>
#define D(x) do {} while (0)
typedef int BOOL;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef uintptr_t IPTR;
typedef void *APTR;
typedef const unsigned char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define LIB_VECTSIZE 6
#define CRT_THREAD_CONTEXT_VERSION 3
struct Library { unsigned lib_NegSize, lib_Version; };
struct StdCBase { struct Library lib; LONG _errno_slot; APTR _errno_task_resource; };
struct StdCIntBase { struct StdCBase StdCBase; APTR errno_state; int *startup_errorptr; jmp_buf exit_jmpbuf; };
struct CRTThreadContextHooks {
    ULONG tc_Version;const char *tc_LibName;ULONG tc_MinVersion;
    APTR (*tc_Select)(void); BOOL (*tc_Capture)(APTR,APTR *);
    BOOL (*tc_Enter)(APTR);struct Library *(*tc_Leave)(APTR);void (*tc_Cancel)(APTR);
};
static _Thread_local ULONG task_id=1;
static _Thread_local IPTR task_slot;
static _Thread_local struct StdCBase *access_base;
static struct StdCBase *creator_base;
/* Native resource startup leaves library allocation sizes unset. */
static struct Library resource={.lib_NegSize=0,.lib_Version=2};
static unsigned allocations,frees,registered,slots_allocated,slots_freed;
static int fail_alloc,fail_set,fail_register,fail_slot,fail_resource,fail_library;
static void *FindTask(void *unused) { (void)unused;return (void *)(uintptr_t)task_id; }
#define GetETaskID(t) ((ULONG)(uintptr_t)(t))
static void *AllocVec(size_t n,unsigned flags) { assert(flags==(MEMF_PUBLIC|MEMF_CLEAR));if(fail_alloc)return NULL;void *p=calloc(1,n);assert(p);allocations++;return p; }
static void FreeVec(void *p) { if(p){assert((IPTR)p!=task_slot && "clear binding before final disposal");frees++;free(p);} }
static struct Library *OpenResource(CONST_STRPTR name) { assert(!strcmp((const char *)name,"task.resource"));return fail_resource?NULL:&resource; }
static LONG AllocTaskStorageSlot(void) { if(fail_slot)return 0;slots_allocated++;return 1; }
static void FreeTaskStorageSlot(LONG slot) { assert(slot==1&&!task_slot);slots_freed++; }
static IPTR mock_get_slot(LONG slot) { assert(slot==1);return task_slot; }
#define GetTaskStorageSlot(s) ((void)TaskResBase, mock_get_slot(s))
static BOOL SetTaskStorageSlot(LONG slot,IPTR value) { assert(slot==1);if(value&&fail_set)return FALSE;task_slot=value;return TRUE; }
static BOOL CRTThreadContextRegister(struct CRTThreadContextHooks *h) { assert(h->tc_Version==3&&!strcmp(h->tc_LibName,"stdc.library"));if(fail_register)return FALSE;assert(!registered);registered=1;return TRUE; }
static void CRTThreadContextUnregister(struct CRTThreadContextHooks *h) { (void)h;assert(registered);registered=0; }
static struct Library *OpenLibrary(CONST_STRPTR name,ULONG version);
static int error_can_close(struct StdCIntBase *base);
/* OWNER */
static struct StdCBase *__aros_getbase_StdCBase(void) { assert(access_base);return access_base; }
/* CLIENT_ACCESSOR */
/* MODULE_ACCESSOR */
#undef errno
#define errno (*client_errno())
/* PROGRAM_STARTUP */
static struct Library *OpenLibrary(CONST_STRPTR name,ULONG version) {
    assert(!strcmp((const char *)name,"stdc.library")&&!version);
    if(fail_library)return NULL;
    struct StdCIntBase *b=calloc(1,sizeof(*b));assert(b);
    if(!error_open(b)){free(b);return NULL;}return (struct Library *)b;
}
static void close_worker(struct Library *worker) {
    struct StdCIntBase *b=(void *)worker;assert(error_can_close(b));error_close(b);free(b);
}
static void *worker(void *data) {
    task_id=2;task_slot=0;
    assert(error_hooks.tc_Enter(data));
    access_base=creator_base; /* linked client may still name its creator base */
    assert(*client_errno()==0 && client_errno()==module_errno());
    *module_errno()=ENOSPC;assert(*client_errno()==ENOSPC);
    struct Library *library=error_hooks.tc_Leave(data);close_worker(library);
    assert(!task_slot);return NULL;
}
int main(void) {
    struct StdCIntBase root={0},a={0},b={0},c={0};
    assert(error_init(&root)&&registered&&slots_allocated==1);
    assert(error_open(&a));access_base=&a.StdCBase;
    assert(*client_errno()==0);*module_errno()=EINVAL;
    assert(*client_errno()==EINVAL && client_errno()==module_errno());
    jmp_buf entry;int exit_status=0;assert(!setjmp(entry));
    __stdc_program_startup(entry,&exit_status);
    assert(*client_errno()==0 && a.startup_errorptr==&exit_status);
    *client_errno()=EINVAL;
    assert(error_open(&b)&&error_open(&c));access_base=&b.StdCBase;
    assert(*client_errno()==EINVAL && a.errno_state==b.errno_state && a.errno_state==c.errno_state);
    /* A borrowed base still resolves the current task's admitted cell. */
    *client_errno()=77;creator_base=&a.StdCBase;APTR context=NULL;
    assert(error_hooks.tc_Capture(error_hooks.tc_Select(),&context));
    pthread_t thread;assert(!pthread_create(&thread,NULL,worker,context));assert(!pthread_join(thread,NULL));
    assert(*client_errno()==77);
    task_id=99;assert(!error_can_close(&a)&&!error_can_close(&b));task_id=1;
    IPTR saved_slot=task_slot;task_slot=0;assert(!error_can_close(&a));task_slot=saved_slot;
    assert(error_can_close(&a));error_close(&a);assert(task_slot&&*client_errno()==77);
    error_close(&c);assert(task_slot&&*module_errno()==77);
    struct StdCErrnoState *state=(void *)task_slot;state->references=UINT32_MAX;
    struct StdCIntBase refused={0};assert(!error_open(&refused)&&!refused.errno_state);state->references=1;
    error_close(&b);assert(!task_slot);error_close(&b); /* abort cleanup is idempotent */
    fail_alloc=1;assert(!error_open(&a)&&!task_slot);fail_alloc=0;
    fail_set=1;assert(!error_open(&a)&&!task_slot);fail_set=0;
    task_id=0;assert(!error_open(&a));task_id=1;
    assert(error_open(&a));access_base=&a.StdCBase;assert(*client_errno()==0);
    fail_alloc=1;context=(void *)1;assert(!error_hooks.tc_Capture(error_select(),&context)&&!context);fail_alloc=0;
    assert(error_capture(error_select(),&context));fail_library=1;
    assert(!error_enter(context));error_cancel(context);fail_library=0;
    assert(error_capture(error_select(),&context));error_cancel(context);
    error_close(&a);assert(!task_slot&&allocations==frees);
    assert(error_expunge(&root)&&!registered&&slots_freed==1);
    fail_register=1;assert(!error_init(&root)&&!registered&&slots_allocated==2&&slots_freed==2);fail_register=0;
    fail_slot=1;assert(!error_init(&root)&&!error_slot);fail_slot=0;
    fail_resource=1;assert(!error_init(&root));fail_resource=0;
    resource.lib_Version=1;resource.lib_NegSize=200;
    assert(!error_init(&root));resource.lib_Version=2;resource.lib_NegSize=0;
    assert(allocations==frees);
    puts("PASS: actual task errno ownership, two accessors, arbitrary same-task close order, worker admission/isolation and refusal rollback");
}
