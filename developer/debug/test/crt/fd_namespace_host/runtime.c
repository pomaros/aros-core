/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
/* Actual POSIXC attachment/teardown and owner functions; modeled DOS/Exec. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "test_boundary.h"
typedef long LONG;
typedef intptr_t BPTR;
typedef void *APTR;
#define FALSE 0
#define TRUE 1
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define NT_TASK 1
#define NT_PROCESS 13
#define BNULL 0
#define PRETEND_CHILD 4
#define VFORK_PARENT 2
#define EXEC_PARENT 1
#define STDIN_FILENO 0
#define STDERR_FILENO 2
#define FD_OWNER_POSIXC 1
#define _FCB_DONTCLOSE_FH 2
#define _FCB_ISDIR 1
#define _FCB_FLUSHONREAD 4
#define D(x) do {} while(0)
struct Node { struct Node *ln_Succ,*ln_Pred; int ln_Type; };
struct MinNode { struct MinNode *mln_Succ,*mln_Pred; };
struct List { struct Node *lh_Head,*lh_Tail,*lh_TailPred; };
struct MinList { struct MinNode *mlh_Head,*mlh_Tail,*mlh_TailPred; };
#define NEWLIST(l) new_list((struct List *)(l))
static void new_list(struct List *l) {
    l->lh_Head=(void *)&l->lh_Tail;l->lh_Tail=NULL;l->lh_TailPred=(void *)&l->lh_Head;
}
static void AddTail(struct List *l,struct Node *n) {
    n->ln_Succ=(void *)&l->lh_Tail;n->ln_Pred=l->lh_TailPred;
    l->lh_TailPred->ln_Succ=n;l->lh_TailPred=n;
}
static void Remove(struct Node *n) { n->ln_Pred->ln_Succ=n->ln_Succ;n->ln_Succ->ln_Pred=n->ln_Pred; }
#define ForeachNodeSafe(l,n,next) \
    for(n=(void *)(l)->mlh_Head;n->node.mln_Succ && ((next=(void *)n->node.mln_Succ),1);n=next)
struct Task { struct Node tc_Node; ULONG id; };
struct Process { struct Task task; BPTR pr_CLI; };
struct Library { int alive; };
struct __fd_namespace;
struct PosixCIntBase {
    int flags;
    struct PosixCIntBase *fd_owner;
    struct __fd_namespace *fd_namespace;
    struct Library *PosixCUserGroupBase,*PosixCDOS64Base;
    void *internalpool;
};
static _Thread_local struct PosixCIntBase *current_base,*parent_base;
static _Thread_local struct Task *current_task;
static _Thread_local struct __fd_namespace *admitted_space;
static struct PosixCIntBase *__cwd_runtime_base(void) { return current_base; }
static void *__GM_GetBaseParent(struct PosixCIntBase *base) { assert(base==current_base);return parent_base; }
static struct Task *FindTask(void *unused) { (void)unused;return current_task; }
static ULONG GetETaskID(struct Task *task) { assert(task==current_task);return task->id; }
static void ObtainSemaphore(struct SignalSemaphore *s) { assert(!pthread_mutex_lock(&s->mutex)); }
static void ReleaseSemaphore(struct SignalSemaphore *s) { assert(!pthread_mutex_unlock(&s->mutex)); }
static struct SignalSemaphore *guards[64];
static int guard_count;
static atomic_int live_allocations,active_records,closed_blocks,closed_registry,pools_freed,ready,go,worker_ids;
static int fail_alloc;
void *AllocVec(size_t size,unsigned flags) {
    (void)flags;if(fail_alloc){fail_alloc=0;return NULL;}
    size_t *header=calloc(1,sizeof(*header)+size);assert(header);*header=size;
    atomic_fetch_add(&live_allocations,1);return header+1;
}
void InitSemaphore(struct SignalSemaphore *s) {
    pthread_mutexattr_t attr;assert(!pthread_mutexattr_init(&attr));
    assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE));
    assert(!pthread_mutex_init(&s->mutex,&attr));assert(!pthread_mutexattr_destroy(&attr));
    assert(guard_count<64);guards[guard_count++]=s;
}
void FreeVec(void *p) {
    if(!p)return;
    size_t *header=(size_t *)p-1;
    for(int n=0;n<guard_count;n++)if((uintptr_t)guards[n]>=(uintptr_t)p &&
        (uintptr_t)guards[n]<(uintptr_t)p+*header) {
        assert(!pthread_mutex_destroy(&guards[n]->mutex));guards[n]=NULL;
    }
    free(header);atomic_fetch_sub(&live_allocations,1);
}
static struct Library *OpenLibrary(const char *name,int version) {
    assert(!strcmp(name,"fd.library")&&version==51);
    struct Library *lib=AllocVec(sizeof(*lib),0);assert(lib);lib->alive=1;return lib;
}
void CloseLibrary(struct Library *lib) {
    assert(lib->alive);assert(atomic_load(&active_records)==0 && atomic_load(&closed_blocks)==3);
    lib->alive=0;atomic_fetch_add(&closed_registry,1);FreeVec(lib);
}
#include "NAMESPACE_SOURCE"
/* FCB_TYPES */
/* FDESC_TYPES */
/* RECLOCK_TYPES */
static struct SignalSemaphore reclock_sem;
static struct MinList reclock_files,reclock_list;
/* ROOT_INIT */
/* SPACE */
/* CURRENT_SPACE */
/* REGISTRY */
#define FD_SLOTS(b) (__fd_space(b)->slots)
#define FD_ARRAY(b) (__fd_space(b)->array)
#define FD_LOCK(b) ObtainSemaphore(&__fd_space(b)->guard)
#define FD_UNLOCK(b) ReleaseSemaphore(&__fd_space(b)->guard)
static fdesc __fd_reserved;
#define FD_RESERVED (&__fd_reserved)
/* TABLE_LOCK */
/* TABLE_UNLOCK */
/* OWNER */
/* OWNER_PID */
static void free_lock(struct reclock *r,BOOL native) {
    assert(native);Remove((struct Node *)r);FreeVec(r);atomic_fetch_sub(&active_records,1);
}
/* RELEASE_OWNER */
/* ALLOC_DESC */
/* FREE_DESC */
static LONG FD_GetOwner(int fd) { (void)fd;return 0; }
static void *FD_GetData(int fd) { (void)fd;return NULL; }
static void FD_Free(int fd,int owner) { (void)fd;(void)owner;abort(); }
static void __releasefcb(fcb *file) {
    assert(!__atomic_sub_fetch(&file->opencount,1,__ATOMIC_ACQ_REL));
    atomic_fetch_add(&closed_blocks,1);FreeVec(file);
}
/* DISPOSE */
/* CAPTURE_SPACE */
/* DROP_SPACE */
/* Model the admission token boundary with an owned namespace, never a base. */
static struct __fd_namespace *__cwd_admission_namespace(BOOL *pending) {
    *pending=admitted_space!=NULL;
    return admitted_space && __fd_namespace_retain(admitted_space) ? admitted_space : NULL;
}
static void __fcb_lock(fcb *file) { ObtainSemaphore(&file->io_lock); }
static void __fcb_unlock(fcb *file) { ReleaseSemaphore(&file->io_lock); }
static int __copy_fdarray(fdesc **array,int slots) { (void)array;(void)slots;abort(); }
/* DOS stdio setup is the boundary. Use actual descriptor allocation, with
   independently owned table storage; no private runtime pool participates. */
static int __init_stdfiles(struct PosixCIntBase *base) {
    struct __fd_namespace *space=base->fd_namespace;
    space->slots=3;space->array=AllocVec(3*sizeof(*space->array),MEMF_PUBLIC|MEMF_CLEAR);
    for(int n=0;n<3;n++) {
        fdesc *d=__alloc_fdesc();assert(d);d->fcb=AllocVec(sizeof(*d->fcb),MEMF_PUBLIC|MEMF_CLEAR);
        InitSemaphore(&d->fcb->io_lock);d->fcb->opencount=1;d->fdflags=n;space->array[n]=d;
    }
    return TRUE;
}
/* INIT_FD */
/* EXIT_FD */
/* OPTIONAL_CLOSE */
static void __upath_free_others(struct PosixCIntBase *base) { assert(base->internalpool); }
static void DeletePool(void *pool) { free(pool);atomic_fetch_add(&pools_freed,1); }
/* POOL_CLOSE */
static void *worker(void *data) {
    struct PosixCIntBase base={.internalpool=malloc(8)};
    struct Task task={.tc_Node.ln_Type=NT_TASK,.id=101+(ULONG)atomic_fetch_add(&worker_ids,1)};
    current_base=&base;parent_base=NULL;current_task=&task;admitted_space=data;
    atomic_fetch_add(&ready,1);while(!atomic_load(&go))sched_yield();
    assert(__init_fd(&base));
    __fd_namespace_drop(admitted_space);admitted_space=NULL;
    struct __fd_namespace *space=__fd_namespace_current();
    assert(space && space->process_id==42);
    if(task.id==101)assert(space && space->process_id==42 && owner_pid(owner_namespace())==42);
    __fdesc_lock();assert(space->array[2]->fdflags==2);__fdesc_unlock();
    assert(space->registry->alive);
    assert(__optionallibs_close(&base));__exit_fd(&base);__posixc_close(&base);
    __exit_fd(&base);__posixc_close(&base); /* repeated rollback is harmless */
    return NULL;
}
int main(void) {
    struct PosixCIntBase *creator=calloc(1,sizeof(*creator));assert(creator);
    struct Process *process=calloc(1,sizeof(*process));assert(process);
    process->task.tc_Node.ln_Type=NT_PROCESS;process->task.id=42;process->pr_CLI=1;
    current_base=creator;current_task=&process->task;assert(reclock_init(creator));
    creator->internalpool=malloc(8);
    fail_alloc=1;assert(!__init_fd(creator));__exit_fd(creator);__posixc_close(creator);
    __exit_fd(creator);__posixc_close(creator);
    assert(atomic_load(&pools_freed)==1 && !atomic_load(&live_allocations));
    atomic_store(&pools_freed,0);
    creator->internalpool=malloc(8);assert(__init_fd(creator));assert(__fdlib_available(creator));
    struct reclock *record=AllocVec(sizeof(*record),MEMF_PUBLIC|MEMF_CLEAR);
    record->owner=creator->fd_namespace;AddTail((struct List *)&reclock_list,(struct Node *)record);
    atomic_store(&active_records,1);
    pthread_t threads[8];
    for(int n=0;n<8;n++) {
        struct __fd_namespace *captured=__fd_namespace_capture();assert(captured);
        assert(!pthread_create(&threads[n],NULL,worker,captured));
    }
    while(atomic_load(&ready)!=8)sched_yield();
    assert(__optionallibs_close(creator));__exit_fd(creator);__posixc_close(creator);
    free(creator);free(process);current_base=NULL;current_task=NULL;
    assert(atomic_load(&closed_registry)==0 && atomic_load(&active_records)==1 && atomic_load(&pools_freed)==1);
    atomic_store(&go,1);for(int n=0;n<8;n++)assert(!pthread_join(threads[n],NULL));
    assert(atomic_load(&closed_registry)==1 && !atomic_load(&active_records));
    assert(atomic_load(&closed_blocks)==3 && atomic_load(&pools_freed)==9);
    assert(!atomic_load(&live_allocations));assert(!pthread_mutex_destroy(&reclock_sem.mutex));
    puts("PASS: actual POSIXC table attach/exit, eight workers admitted after creator base/Task/pool free, stable lock PID and final owner/registry disposal");
    return 0;
}
