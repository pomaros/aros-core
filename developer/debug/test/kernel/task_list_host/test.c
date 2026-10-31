/* Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#define __AROSEXEC_SMP__ 1
#define SPINLOCK_MODE_WRITE 1
struct Node { struct Node *succ,*pred; };
struct List { struct Node *head,*tail,*tailpred; };
struct Task { int id; };
struct TaskListEntry { struct Node tle_Node; struct Task *tle_Task; };
struct TaskResBase { int TaskListSpinLock; struct List trb_TaskList,trb_NewTasks,trb_LockedLists; };
static unsigned disabled,held,freed;
static void Disable(void) { ++disabled; }
static void Enable(void) { assert(disabled && !held);--disabled; }
static void lock(int *p) { assert(disabled && !held && !*p);held=1;*p=1; }
static void unlock(int *p) { assert(disabled && held && *p);held=0;*p=0; }
#define EXEC_SPINLOCK_LOCK(p,h,m) lock(p)
#define EXEC_SPINLOCK_UNLOCK(p) unlock(p)
#define NEWLIST(l) ((l)->head=(struct Node *)&(l)->tail,(l)->tail=NULL,(l)->tailpred=(struct Node *)&(l)->head)
static int IsListEmpty(struct List *l) { return !l->head->succ; }
static void AddTail(struct List *l,struct Node *n) { n->pred=l->tailpred;n->succ=(struct Node *)&l->tail;l->tailpred->succ=n;l->tailpred=n; }
static void Remove(struct Node *n) { n->pred->succ=n->succ;n->succ->pred=n->pred; }
static struct Node *RemHead(struct List *l) { struct Node *n=l->head;if (!n->succ) return NULL;Remove(n);return n; }
#define ForeachNode(l,e) for ((e)=(void *)(l)->head;(e)->tle_Node.succ;(e)=(void *)(e)->tle_Node.succ)
#define ForeachNodeSafe(l,e,n) for ((e)=(void *)(l)->head;(e)->tle_Node.succ && ((n)=(void *)(e)->tle_Node.succ,1);(e)=(n))
static void FreeMem(void *p,size_t bytes) { assert(!held && !disabled && bytes==sizeof(struct TaskListEntry));++freed;free(p); }
#include "actual.inc"
static struct TaskListEntry *add(struct List *list,struct Task *task)
{ struct TaskListEntry *e=calloc(1,sizeof(*e));assert(e);e->tle_Task=task;AddTail(list,&e->tle_Node);return e; }
int main(void)
{
 struct TaskResBase base={0};NEWLIST(&base.trb_TaskList);NEWLIST(&base.trb_NewTasks);NEWLIST(&base.trb_LockedLists);
 struct Task a={1},b={2},c={3};struct Node reader;
 struct TaskListEntry *ea=add(&base.trb_TaskList,&a);add(&base.trb_TaskList,&b);
 assert(GetTaskEntry(&a,&base)==ea && !disabled && !held);
 Disable();assert(GetTaskEntry(&a,&base)==ea && disabled==1 && !held);Enable();
 AddTail(&base.trb_LockedLists,&reader);
 task_RemoveEntry(&a,&base);assert(!freed && !ea->tle_Task && !GetTaskEntry(&a,&base));
 add(&base.trb_NewTasks,&c);task_CleanList(NULL,&base);
 assert(!freed && !IsListEmpty(&base.trb_NewTasks));
 task_RemoveEntry(&c,&base);assert(freed==1 && IsListEmpty(&base.trb_NewTasks));
 add(&base.trb_NewTasks,&c);Remove(&reader);task_CleanList(NULL,&base);
 assert(freed==2 && IsListEmpty(&base.trb_NewTasks) && GetTaskEntry(&c,&base));
 task_RemoveEntry(&b,&base);assert(freed==3 && !GetTaskEntry(&b,&base));
 task_CleanList(&c,&base);assert(freed==4 && IsListEmpty(&base.trb_TaskList));
 task_RemoveEntry(&a,&base);task_CleanList(NULL,&base);
 assert(freed==4 && !held && !disabled);puts("PASS task list exclusion deferred free and iterator retention");return 0;
}
