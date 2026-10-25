/*
    Copyright 2010-2019, The AROS Development Team. All rights reserved.
    $Id: tls.c 56280 2019-04-17 18:42:57Z NicJA $
*/

#include "tls.h"

#include <string.h>

#include <proto/exec.h>
#include <proto/task.h>
#include <exec/memory.h>

struct TaskLocalNode
{
    struct TaskLocalNode    * tl_Succ;
    struct Task             * tl_Task;
    APTR                    tl_Data;
};

struct TaskLocalStorage
{
    struct TaskLocalNode    * tls_Head;
    struct SignalSemaphore  tls_WriteSemaphore;
};

/* Implementation uses locking only when adding objects. Objects are always added
   at head. The list is never reordered, thus reading can be done without locking */

/* This approach is used to achieve acceptable performance. With semaphore-locking
   of read path, the performance was degraded several times. The TLS is used to
   hold current per-task GL context - retrieving this context MUST BE fast */

struct TaskLocalStorage * CreateTLS()
{
    struct TaskLocalStorage * tls =
        AllocVec(sizeof(struct TaskLocalStorage), MEMF_PUBLIC | MEMF_CLEAR);

    InitSemaphore(&tls->tls_WriteSemaphore);
    tls->tls_Head = NULL;

    return tls;
}

VOID InsertIntoTLS(struct TaskLocalStorage * tls, APTR ptr)
{
    struct TaskLocalNode * tl = tl = tls->tls_Head;
    struct Task * me = FindTask(NULL);
    struct TaskLocalNode * selected = NULL;
    
    /* Assumption: one task cannot be reviewing the list and adding the head
       "at the same time" - do not alter this function to recurse */
    /* Assumption: only task A can add entry for task A */

    /* Check if task's storage is already on the list */
    while(tl)
    {
        if (tl->tl_Task == me)
        {
            selected = tl;
            break;
        }
        tl = tl->tl_Succ;
    }
    
    if (!selected)
    {
        /* No, it is not. Create, set task pointer and at to head of list */
        selected = AllocVec(sizeof(struct TaskLocalNode), MEMF_PUBLIC | MEMF_CLEAR);
        selected->tl_Task = me;
        ObtainSemaphore(&tls->tls_WriteSemaphore);
        selected->tl_Succ = tls->tls_Head;
        tls->tls_Head = selected;
        ReleaseSemaphore(&tls->tls_WriteSemaphore);
        
    }

    /* Set the passed value */
    selected->tl_Data = ptr;
}

VOID ClearFromTLS(struct TaskLocalStorage * tls)
{
    /* Clearing is inserting a NULL. Element can't be removed from list - since
       there is no read locking, altering structure of list when other tasks
       are reading it, would cause crashes */
    /* TODO: How real clearing can be achieved:
     * a) acquire write lock
     * b) copy all element (copy not relink!) from _current list to _new list except for the element that
     *    is beeing cleared
     * c) _old = _current, _current = _new
     * d) release write lock
     *
     * How to delete _old? It can't be deleted right away, because some read tasks can be iterating over it.
     * a) put it on garbage collect list (the whole list, not relinking nodes!) and clear it when shutting down
     * b) use memory pool to allocate all TLS objects and clear pool at shutdown
     *
     * Solution b is much easier and convenient
     */
    InsertIntoTLS(tls, NULL);
}

APTR GetFromTLS(struct TaskLocalStorage * tls)
{
    struct TaskLocalNode * tl = tls->tls_Head;
    struct Task * me = FindTask(NULL);
    APTR data = NULL;
    
    while(tl)
    {
        if (tl->tl_Task == me)
        {
            data = tl->tl_Data;
            break;
        }
        tl = tl->tl_Succ;
    }

    return data;
}

VOID DestroyTLS(struct TaskLocalStorage * tls)
{
    /* Destroy needs no lock. If a task is still iterating over list, we are doomed
       anyway */
    struct TaskLocalNode * tl = tls->tls_Head, * temp;

    while (tl)
    {
        temp = tl->tl_Succ;
        FreeVec(tl);
        tl = temp;
    }

    FreeVec(tls);
}

/* EGL's per-thread state (Mesa's _EGLThreadInfo: current context, surfaces,
   API, last error), one per task, in a task.resource storage slot: the slot
   reads 0 in a task that never set it, goes with the task, and DOS
   RunCommand saves and restores it around a command. The block hangs on the
   task's tc_MemEntry, so it is freed with the task. */
APTR TaskResBase;
static LONG egl_thread_slot;
/* Only when even a few bytes cannot be had: every such task shares it. */
static UBYTE egl_thread_fallback[256] __attribute__((aligned(16)));

APTR AROSEGLGetThreadInfo(unsigned long size)
{
    struct Task * me = FindTask(NULL);
    struct MemList * ml;
    struct
    {
        struct Node ml_Node;
        UWORD ml_NumEntries;
        struct MemEntry ml_ME[1];
    } template;
    ULONG failed;
    APTR info;

    if (egl_thread_slot == 0)
    {
        Forbid();
        if (TaskResBase == NULL)
            TaskResBase = OpenResource((CONST_STRPTR)"task.resource");
        if (egl_thread_slot == 0 && TaskResBase != NULL)
            egl_thread_slot = AllocTaskStorageSlot();
        Permit();
    }
    if (egl_thread_slot == 0 || size > sizeof(egl_thread_fallback))
        return egl_thread_fallback;
    if ((info = (APTR)GetTaskStorageSlot(egl_thread_slot)) != NULL)
        return info;

    memset(&template, 0, sizeof(template));
    template.ml_NumEntries = 1;
    template.ml_ME[0].me_Reqs = MEMF_PUBLIC | MEMF_CLEAR;
    template.ml_ME[0].me_Length = size;
    if (!(ml = NewAllocEntry((struct MemList *)&template, &failed)))
        return egl_thread_fallback;
    info = ml->ml_ME[0].me_Addr;
    Forbid();
    AddHead(&me->tc_MemEntry, &ml->ml_Node);
    Permit();
    SetTaskStorageSlot(egl_thread_slot, (IPTR)info);
    return info;
}
