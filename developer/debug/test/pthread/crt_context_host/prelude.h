/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
/* Host Exec, task.resource and library mocks for crt_context_host. Each
   AROS Task is a host thread. */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char UBYTE; typedef signed char BYTE; typedef uint32_t ULONG; typedef int32_t LONG;
typedef short BOOL; typedef void *APTR; typedef intptr_t IPTR; typedef const unsigned char *CONST_STRPTR;
typedef unsigned char *STRPTR;
#define TRUE 1
#define FALSE 0
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 0x10000
#define MEMF_ANY 0
struct MinNode { struct MinNode *mln_Succ, *mln_Pred; };
struct MinList { struct MinNode *mlh_Head, *mlh_Tail, *mlh_TailPred; };
struct Node { struct Node *ln_Succ, *ln_Pred; UBYTE ln_Type; signed char ln_Pri; char *ln_Name; };
struct List { struct Node *lh_Head, *lh_Tail, *lh_TailPred; };
struct SignalSemaphore { struct Node ss_Link; pthread_rwlock_t lock; };
struct Library { int id; };
struct Task { pthread_mutex_t m; pthread_cond_t c; ULONG sigs; IPTR storage[16]; APTR tc_UserData; int id; int crt_live; };
#define NEWLIST(l) do { struct List *_l = (struct List *)(l); _l->lh_Head = (struct Node *)&_l->lh_Tail; _l->lh_Tail = NULL; _l->lh_TailPred = (struct Node *)&_l->lh_Head; } while (0)
static inline void AddTail(struct List *l, struct Node *n) { n->ln_Succ = (struct Node *)&l->lh_Tail; n->ln_Pred = l->lh_TailPred; l->lh_TailPred->ln_Succ = n; l->lh_TailPred = n; }
static inline void Remove(struct Node *n) { n->ln_Pred->ln_Succ = n->ln_Succ; n->ln_Succ->ln_Pred = n->ln_Pred; }

/* semaphores and the public list */
static pthread_mutex_t semlist_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { struct List SemaphoreList; } execbase, *SysBase=&execbase;
static inline void InitSemaphore(struct SignalSemaphore *s) { assert(!pthread_rwlock_init(&s->lock,NULL)); }
static inline struct Node *FindName(struct List *list, const char *name) {
    for(struct Node *n=list->lh_Head;n->ln_Succ;n=n->ln_Succ)
        if(!strcmp(n->ln_Name,name)) return n;
    return NULL;
}
static inline void Enqueue(struct List *list, struct Node *node) { AddTail(list,node); }
#define SPINLOCK_MODE_READ 0
#define SPINLOCK_MODE_WRITE 1
#define LOCKF_DISABLE 1
static inline BOOL ObtainSystemLock(void *list,ULONG mode,ULONG flags) {
    (void)mode;(void)flags;assert(list==&SysBase->SemaphoreList);
    assert(!pthread_mutex_lock(&semlist_lock));return TRUE;
}
static inline void ReleaseSystemLock(void *list,ULONG flags) {
    (void)flags;assert(list==&SysBase->SemaphoreList);assert(!pthread_mutex_unlock(&semlist_lock));
}
extern __thread int sem_depth;
static inline void ObtainSemaphore(struct SignalSemaphore *s) { pthread_rwlock_wrlock(&s->lock); sem_depth++; }
static inline void ObtainSemaphoreShared(struct SignalSemaphore *s) { pthread_rwlock_rdlock(&s->lock); sem_depth++; }
static inline void ReleaseSemaphore(struct SignalSemaphore *s) { sem_depth--; pthread_rwlock_unlock(&s->lock); }

/* memory */
extern atomic_int live_mem, fail_alloc_at, alloc_count;
static inline APTR AllocMem(ULONG n, ULONG f) { if (atomic_fetch_add(&alloc_count, 1) + 1 == atomic_load(&fail_alloc_at)) return NULL; void *p = (f & MEMF_CLEAR) ? calloc(1, n) : malloc(n); atomic_fetch_add(&live_mem, 1); return p; }
static inline void CopyMem(const void *s, void *d, ULONG n) { memcpy(d, s, n); }
static inline void FreeMem(APTR p, ULONG n) { (void)n; atomic_fetch_sub(&live_mem, 1); free(p); }
static inline APTR AllocVec(ULONG n, ULONG f) { return AllocMem(n, f | MEMF_CLEAR); }
static inline void FreeVec(APTR p) { if (p) FreeMem(p, 0); }

/* tasks, signals, storage */
extern __thread struct Task *this_task;
static inline struct Task *FindTask(void *n) { assert(!n); return this_task; }
static inline APTR OpenResource(CONST_STRPTR n) { (void)n; return (APTR)1; }
static inline BYTE AllocSignal(LONG b) { (void)b; return 20; }
static inline void FreeSignal(LONG b) { (void)b; }
static inline ULONG SetSignal(ULONG n, ULONG m) { pthread_mutex_lock(&this_task->m); ULONG o = this_task->sigs; this_task->sigs = (o & ~m) | (n & m); pthread_mutex_unlock(&this_task->m); return o; }
static inline void Signal(struct Task *t, ULONG m) { pthread_mutex_lock(&t->m); t->sigs |= m; pthread_cond_broadcast(&t->c); pthread_mutex_unlock(&t->m); }
static inline ULONG Wait(ULONG m) { pthread_mutex_lock(&this_task->m); while (!(this_task->sigs & m)) pthread_cond_wait(&this_task->c, &this_task->m); ULONG r = this_task->sigs & m; this_task->sigs &= ~m; pthread_mutex_unlock(&this_task->m); return r; }

/* libraries: per-name open counts; a library with no opener and an expunge
   request is unloaded, which also unregisters its provider */
extern struct Library *OpenLibrary(CONST_STRPTR name, ULONG v);
extern void CloseLibrary(struct Library *l);

static inline void Enable(void) { assert(!"unexpected UP namespace fallback"); }
