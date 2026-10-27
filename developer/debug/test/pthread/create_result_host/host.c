/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define pthread_t aros_pthread_t
#define pthread_attr_t aros_pthread_attr_t
#define pthread_create aros_pthread_create
#define pthread_attr_init aros_pthread_attr_init
typedef unsigned pthread_t;
typedef struct { int detachstate; void *stackaddr; size_t stacksize; } pthread_attr_t;
typedef unsigned long IPTR;
#define NAMELEN 64
#define PTHREAD_THREADS_MAX 4
#define PTHREAD_CANCEL_ENABLE 0
#define PTHREAD_CANCEL_DEFERRED 0
#define PTHREAD_CREATE_DETACHED 1
#define MEMF_ANY 0
#define NP_Entry 1
#define NP_StackSize 2
#define NP_UserData 3
#define NP_Name 4
#define TAG_IGNORE 5
#define TAG_DONE 0
#define D(...) ((void)0)
#define NEWLIST(x) ((void)(x))
struct List { int unused; };
struct Task { int value; };
static struct Task creator, child, replacement;
#define GET_THIS_TASK (&creator)
typedef struct { void *(*start)(void *); void *arg; struct Task *parent, *task; pthread_attr_t attr; struct List cleanup; int cancelstate,canceltype,detached; } ThreadInfo;
static ThreadInfo slot, *threads[PTHREAD_THREADS_MAX];
static unsigned threads_used;
static int thread_sem, locked, allocation_fails, creation_fails, exhausted, release_action;
static void ObtainSemaphore(int *p) { (void)p;assert(!locked);locked=1; }
static void ReleaseSemaphore(int *p) {
 (void)p;assert(locked);locked=0;
 if(release_action==1) { assert(slot.task==&child);memset(&slot,0,sizeof(slot)); }
 if(release_action==2) { memset(&slot,0,sizeof(slot));slot.parent=&replacement; }
}
static pthread_t GetThreadId(void *p) { (void)p;return exhausted ? PTHREAD_THREADS_MAX : 0; }
static ThreadInfo *GetThreadInfo(pthread_t t) { assert(t==0);return &slot; }
static void *AllocVec(size_t n,int flags) { (void)n;(void)flags;return allocation_fails ? NULL : &slot; }
static void pthread_attr_init(pthread_attr_t *p) { memset(p,0,sizeof(*p)); }
static void StarterFunc(void) {}
static struct Task *CreateNewProcTags(int tag,...) { (void)tag;assert(locked);return creation_fails ? NULL : &child; }
#include "actual.c"
static void *entry(void *p) { return p; }
static void reset(void) { memset(&slot,0,sizeof(slot));threads_used=1;locked=allocation_fails=creation_fails=exhausted=release_action=0; }
int main(void) {
 pthread_t id=99;pthread_attr_t detached={.detachstate=PTHREAD_CREATE_DETACHED};
 reset();release_action=1;
 assert(pthread_create(&id,&detached,entry,NULL)==0 && id==0);
 assert(!locked && !slot.task);
 reset();creation_fails=1;release_action=2;id=99;
 assert(pthread_create(&id,NULL,entry,NULL)==EAGAIN && id==99);
 assert(slot.parent==&replacement && !locked);
 reset();exhausted=1;id=99;
 assert(pthread_create(&id,NULL,entry,NULL)==EAGAIN && id==99 && !locked);
 reset();threads_used=0;allocation_fails=1;
 assert(pthread_create(&id,NULL,entry,NULL)==EAGAIN && id==99 && !locked);
 reset();assert(pthread_create(NULL,NULL,entry,NULL)==EINVAL && !locked);
 puts("PASS actual pthread_create: immediate detached completion, failed-slot reuse, exhaustion and allocation refusal");
}
