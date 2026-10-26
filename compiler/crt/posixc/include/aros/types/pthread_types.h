#ifndef _AROS_TYPES_PTHREAD_TYPES_H_
#define _AROS_TYPES_PTHREAD_TYPES_H_

/*
  Copyright (C) 2014 Szilard Biro
  Copyright (C) 2018 Harry Sintonen
  Copyright (C) 2026 John KNIPPER <code@jkn.me>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Altered: the pthread types moved here from <pthread.h>, so that
  <sys/types.h> defines them as POSIX requires. Their exec members are
  replaced by layout twins built from C types only, so that no exec header
  and none of its names (Node, List, Task, BOOL, ULONG...) reach a POSIX
  program through <sys/types.h>. The pthread library is compiled with the
  real exec types (__PTHREAD_EXEC_MEMBERS) and checks at compile time that
  every twin has the size, alignment and member offsets of its original.
*/

#include <aros/system.h>
#include <aros/config.h>
#include <aros/types/size_t.h>
#include <aros/types/clockid_t.h>

/* Twins of exec's Node, MinNode, MinList, SemaphoreRequest, spinlock_t and
   SignalSemaphore (exec/nodes.h, exec/lists.h, exec/semaphores.h,
   aros/types/spinlock_s.h). */
struct __pthread_twin_node
{
    struct __pthread_twin_node *__succ, *__pred;
    unsigned char __type;
    signed char __pri;
    char *__name;
};

struct __pthread_twin_minnode
{
    struct __pthread_twin_minnode *__succ, *__pred;
};

struct __pthread_twin_minlist
{
    struct __pthread_twin_minnode *__head, *__tail;
#ifdef __GNUC__
    union
    {
        struct __pthread_twin_minnode *__tailpred;
        struct __pthread_twin_minlist *__tailpred_;
    };
#else
    struct __pthread_twin_minnode *__tailpred;
#endif
};

#if defined(__AROSPLATFORM_SMP__)
#ifndef AROS_SPINLOCK_ALIGN
#define AROS_SPINLOCK_ALIGN
#endif
typedef struct
{
    union
    {
        volatile unsigned int __lock;
        volatile unsigned char __block[4];
    };
    void *__owner;
#if defined(AROS_SPINLOCK_ISOLATION)
    unsigned char __isolation[AROS_SPINLOCK_ISOLATION - (2 * sizeof(void *))];
#endif
} AROS_SPINLOCK_ALIGN __pthread_twin_spinlock_t;
#endif

struct __pthread_twin_semaphorerequest
{
    struct __pthread_twin_minnode __link;
    void *__waiter;
#if defined(__AROSPLATFORM_SMP__)
    __pthread_twin_spinlock_t __spinlock;
#endif
};

struct __pthread_twin_signalsemaphore
{
    struct __pthread_twin_node __link;
    short __nestcount;
    struct __pthread_twin_minlist __waitqueue;
    struct __pthread_twin_semaphorerequest __multiplelink;
    void *__owner;
    short __queuecount;
};

#if defined(__PTHREAD_EXEC_MEMBERS)
#include <exec/semaphores.h>
typedef struct SignalSemaphore __pthread_sigsem_member_t;
typedef struct MinList __pthread_minlist_member_t;
#else
typedef struct __pthread_twin_signalsemaphore __pthread_sigsem_member_t;
typedef struct __pthread_twin_minlist __pthread_minlist_member_t;
#endif

#ifndef __AROS_SCHED_PARAM_DEFINED
#define __AROS_SCHED_PARAM_DEFINED
struct sched_param
{
    int sched_priority;
};
#endif

typedef unsigned int pthread_t;
typedef unsigned int pthread_key_t;

struct pthread_attr
{
    void *stackaddr;
    size_t stacksize;
#ifdef __MORPHOS__
    size_t stacksize68k;
#endif
    int detachstate;
    struct sched_param param;
    int inheritsched;
    int contentionscope;
};
typedef struct pthread_attr pthread_attr_t;

struct pthread_once
{
    volatile int done;
    int started;
    int lock;
};
typedef struct pthread_once pthread_once_t;

struct pthread_mutexattr
{
    int pshared;
    int kind;
};
typedef struct pthread_mutexattr pthread_mutexattr_t;

struct pthread_mutex
{
    int kind;
    __pthread_sigsem_member_t semaphore;
    int incond;
    /* Zero in static initializers; release/acquire publication by Exec. */
    unsigned int initialized;
};
typedef struct pthread_mutex pthread_mutex_t;

struct pthread_condattr
{
    int pshared;
    /* Clock an absolute pthread_cond_timedwait() deadline is read against
     * (CLOCK_REALTIME default per POSIX; CLOCK_MONOTONIC for Mesa's
     * u_cnd_monotonic_*). Set with pthread_condattr_setclock(); always set
     * by pthread_condattr_init(). A manually-zeroed attr reads 0, which is
     * CLOCK_MONOTONIC on AROS - use pthread_condattr_init(). */
    clockid_t cond_clock;
};
typedef struct pthread_condattr pthread_condattr_t;

struct pthread_cond
{
    /* Copied from the attr by pthread_cond_init(); REALTIME when the cond
     * was created with a NULL attr, a static initializer, or lazy init.
     * Lives in the slot that was the unused `int pad1` so that
     * sizeof(pthread_cond_t) is UNCHANGED: std::condition_variable embeds
     * this struct, and libc++.a (toolchain, prebuilt) plus every object
     * compiled before the field appeared lay out std::__assoc_sub_state
     * (mutex, condition_variable, __state_) from that size. Appending a
     * member instead moved __state_ for newly compiled code only, and
     * std::future::get() (libc++.a's __sub_wait) then waited forever on a
     * ready future - the LLVM MCJIT resolveExternalSymbols() stall. */
    clockid_t cond_clock;
    __pthread_sigsem_member_t semaphore;
    __pthread_minlist_member_t waiters;
};
typedef struct pthread_cond pthread_cond_t;

struct pthread_barrierattr
{
    int pshared;
};
typedef struct pthread_barrierattr pthread_barrierattr_t;

struct pthread_barrier
{
    unsigned int curr_height;
    unsigned int total_height;
    pthread_cond_t breeched;
    pthread_mutex_t lock;
};
typedef struct pthread_barrier pthread_barrier_t;

struct pthread_rwlockattr
{
    int pshared;
};
typedef struct pthread_rwlockattr pthread_rwlockattr_t;

struct pthread_rwlock
{
    __pthread_sigsem_member_t semaphore;
};
typedef struct pthread_rwlock pthread_rwlock_t;

typedef int pthread_spinlock_t;

#endif /* _AROS_TYPES_PTHREAD_TYPES_H_ */
