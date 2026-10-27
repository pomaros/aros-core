/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
typedef uint32_t ULONG;
typedef intptr_t IPTR;
#define CHILD_NOTNEW ((ULONG)-1)
#define CHILD_NOTFOUND 0
#define CHILD_ACTIVE 1
#define CHILD_EXITED 2
#define TF_ETASK 1
#define __AROSEXEC_SMP__ 1
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT
#define SPINLOCK_MODE_READ 0
#define IntETask(e) (e)
#define D(x) do {} while (0)
struct ETask;
struct List { struct ETask *head; };
struct ETask {
    struct ETask *next;
    ULONG et_UniqueID;
    int et_Result1, iet_TaskLock;
    struct List et_Children;
    struct { int mp_SpinLock; struct List mp_MsgList; } et_TaskMsgPort;
};
struct Task { int tc_Flags; struct { struct ETask *tc_ETask; } tc_UnionETask; };
static struct ETask parent, active, exited, end;
static struct Task task;
static int locks, forbidden, waits, frees, missing_task;
#define GET_THIS_TASK (missing_task ? NULL : &task)
#define ForeachNode(list, item) for ((item)=(list)->head; (item)->next; (item)=(item)->next)
static void Forbid(void) { assert(!forbidden++); }
static void Permit(void) { assert(!locks && forbidden-- == 1); }
static void lock(int *p)
{
    assert(forbidden);
    if (p == &parent.iet_TaskLock) assert(locks++ == 0);
    else { assert(p == &parent.et_TaskMsgPort.mp_SpinLock); assert(locks++ == 1); }
}
static void unlock(int *p)
{
    if (p == &parent.iet_TaskLock) assert(locks-- == 1);
    else { assert(p == &parent.et_TaskMsgPort.mp_SpinLock); assert(locks-- == 2); }
}
#define EXEC_SPINLOCK_LOCK(p,h,m) lock(p)
#define EXEC_SPINLOCK_UNLOCK(p) unlock(p)
static struct Task *FindTask(void *unused) { (void)unused; return GET_THIS_TASK; }
static struct ETask *GetETask(struct Task *t) { return t && (t->tc_Flags & TF_ETASK) ? &parent : NULL; }
#include "childstatus-production.c"
#define ChildStatus fixture_ChildStatus
static IPTR ChildWait(ULONG id)
{
    assert(!locks && !forbidden); waits++;
    struct ETask *e = parent.et_TaskMsgPort.mp_MsgList.head;
    if (!e->next || (id && e->et_UniqueID != id)) e = parent.et_Children.head;
    assert(e->next && (!id || e->et_UniqueID == id));
    return (IPTR)e;
}
static void ChildFree(ULONG id) { assert(id == 41 || id == 42); frees++; }
#include "waitpid-production.c"
static void reset(void)
{
    assert(!locks && !forbidden);
    memset(&parent,0,sizeof(parent)); memset(&active,0,sizeof(active));
    memset(&exited,0,sizeof(exited)); memset(&end,0,sizeof(end));
    parent.et_Children.head = parent.et_TaskMsgPort.mp_MsgList.head = &end;
    active.next = exited.next = &end;
    active.et_UniqueID = 41; exited.et_UniqueID = 42;
    active.et_Result1 = exited.et_Result1 = 123;
    task.tc_Flags = TF_ETASK; task.tc_UnionETask.tc_ETask = &parent;
    waits = frees = missing_task = 0; errno = 0;
}
static void expect(pid_t pid, int options, pid_t result, int error, int called)
{
    int status = 777;
    assert(fixture_waitpid(pid,&status,options) == result);
    assert(!error || errno == error);
    assert(status == (result > 0 ? 123 : 777));
    assert(waits == called && frees == called && !locks && !forbidden);
}
int main(void)
{
    reset(); assert(ChildStatus(0) == CHILD_NOTFOUND); expect(-1,WNOHANG,-1,ECHILD,0);
    reset(); expect(-1,0,-1,ECHILD,0);
    reset(); parent.et_Children.head=&active;
    assert(ChildStatus(0) == CHILD_ACTIVE); expect(-1,WNOHANG,0,0,0);
    reset(); parent.et_Children.head=&active; expect(41,WNOHANG,0,0,0);
    reset(); parent.et_Children.head=&active; expect(99,WNOHANG,-1,ECHILD,0);
    reset(); parent.et_Children.head=&active; expect(41,0,41,0,1);
    reset(); parent.et_Children.head=&active; parent.et_TaskMsgPort.mp_MsgList.head=&exited;
    assert(ChildStatus(0) == CHILD_EXITED); expect(-1,WNOHANG,42,0,1);
    reset(); parent.et_TaskMsgPort.mp_MsgList.head=&exited; expect(42,WNOHANG,42,0,1);
    reset(); parent.et_Children.head=&active; parent.et_TaskMsgPort.mp_MsgList.head=&exited;
    expect(41,WNOHANG,0,0,0);
    reset(); expect(0,0,-1,EINVAL,0);
    reset(); expect(-2,0,-1,EINVAL,0);
    reset(); expect(-1,0x40000000,-1,EINVAL,0);
    reset(); task.tc_Flags=0; assert(ChildStatus(0)==CHILD_NOTNEW); expect(-1,0,-1,ECHILD,0);
    reset(); missing_task=1; expect(-1,0,-1,ECHILD,0);
    puts("PASS actual ChildStatus/waitpid: wildcard precedence, no-child ECHILD, active WNOHANG, guarded scans and reap");
}
