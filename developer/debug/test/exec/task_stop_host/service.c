/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
typedef unsigned char UBYTE;
typedef void *APTR;
typedef unsigned long IPTR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define TS_INVALID 0
#define TS_REMOVED 6
#define TS_TOMBSTONED 7
#define TS_READY 2
#define NT_TASK 1
#define NT_PROCESS 13
#define EXECF_CPUAffinity 1
#define DINIT(...) ((void)0)
#define DREMTASK(...) ((void)0)
struct Node { struct Node *ln_Succ; int ln_Type; const char *ln_Name; };
struct MemList { struct Node ml_Node; };
struct List { struct Node *lh_Head, *lh_TailPred; };
struct Task { struct Node tc_Node; UBYTE tc_State; IPTR tc_UserData; struct List tc_MemEntry; IPTR iet_CpuNumber; };
struct Message { int unused; };
struct Library { unsigned lib_Version; };
struct ExecBase { struct Library *lb_TaskResBase; void *ServicePort; unsigned IntFlags; struct List TaskReady; };
#define PrivExecBase(x) (x)
#define GetIntETask(x) (x)
static struct Task target, service;
static struct MemList allocation;
static jmp_buf done;
static unsigned queued, requeued, freed, notified, expected;
static struct Task *FindTask(void *p) { (void)p; return &service; }
static void *GetMsg(void *p) { (void)p; if (queued++) return NULL; return &target; }
static void InternalPutMsg(void *p,struct Message *m,struct ExecBase *b) { (void)p;(void)m;(void)b; ++requeued; }
static void FreeEntry(struct MemList *m) { assert(m==&allocation); assert(notified==expected); ++freed; }
static void WaitPort(void *p) { (void)p; longjmp(done,1); }
static void Enqueue(struct List *l,struct Node *n) { (void)l;(void)n; assert(0); }
static void observe(struct Task *t) { assert(t==&target && !freed && t->tc_State==TS_INVALID); ++notified; }
#define NotifyTaskStopped(t) ((void)TaskResBase, observe(t))
#ifndef TEST_NO_HANDOFF
#define EXEC_SELF_EXIT_HANDOFF 1
#endif
#include "service_actual.c"
static void check(unsigned state, unsigned version, int resource, unsigned want_free, unsigned want_notify) {
 struct Library library={version};
 struct ExecBase base={.lb_TaskResBase=resource ? &library : NULL};
 target.tc_State=state;
 target.tc_MemEntry.lh_Head=target.tc_MemEntry.lh_TailPred=&allocation.ml_Node;
 allocation.ml_Node.ln_Succ=(void *)1;
 queued=requeued=freed=notified=0; expected=want_notify;
 if(!setjmp(done)) ServiceTask(&base);
 assert(freed==want_free && notified==want_notify);
 assert(requeued==(state==TS_REMOVED));
}
int main(void) {
#ifdef TEST_NO_HANDOFF
 check(TS_INVALID,2,1,1,0);
#else
 check(TS_INVALID,2,1,1,1);
#endif
 check(TS_INVALID,1,1,1,0);
 check(TS_INVALID,2,0,1,0);
 check(TS_REMOVED,2,1,0,0);
 check(TS_TOMBSTONED,2,1,1,0);
 puts("PASS actual ServiceTask notification before free, capability/version gates and non-detached states");
}
