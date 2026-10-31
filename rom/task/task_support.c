/*
    Copyright (C) 2017-2026, The AROS Development Team. All rights reserved.
*/

#define DEBUG 0

#include <aros/debug.h>

#include <proto/exec.h>

#include "task_intern.h"

/* Copyright (C) 2026 John KNIPPER. The list lock protects links and entry
 * publication only. Mask local preemption before taking the SMP spinlock; a
 * contender must not strand a preempted owner on its own CPU. Never allocate,
 * free or invoke callbacks while this lock is held. */
void task_ListLock(struct TaskResBase *TaskResBase)
{
    Disable();
#if defined(__AROSEXEC_SMP__)
    EXEC_SPINLOCK_LOCK(&TaskResBase->TaskListSpinLock, NULL, SPINLOCK_MODE_WRITE);
#endif
}
void task_ListUnlock(struct TaskResBase *TaskResBase)
{
#if defined(__AROSEXEC_SMP__)
    EXEC_SPINLOCK_UNLOCK(&TaskResBase->TaskListSpinLock);
#endif
    Enable();
}
void task_CleanList(struct Task *task, struct TaskResBase *TaskResBase)
{
    struct List garbage;struct TaskListEntry *entry,*next;
    NEWLIST(&garbage);
    task_ListLock(TaskResBase);
    if (IsListEmpty(&TaskResBase->trb_LockedLists))
    {
        ForeachNodeSafe(&TaskResBase->trb_TaskList,entry,next)
            if (!entry->tle_Task || (task && task==entry->tle_Task)) {
                Remove(&entry->tle_Node);AddTail(&garbage,&entry->tle_Node);
            }
        while ((entry=(struct TaskListEntry *)RemHead(&TaskResBase->trb_NewTasks)))
            AddTail(&TaskResBase->trb_TaskList,&entry->tle_Node);
    }
    task_ListUnlock(TaskResBase);
    while ((entry=(struct TaskListEntry *)RemHead(&garbage)))
        FreeMem(entry,sizeof(*entry));
}
void task_RemoveEntry(struct Task *task, struct TaskResBase *TaskResBase)
{
    struct TaskListEntry *entry,*next,*retired=NULL;
    task_ListLock(TaskResBase);
    ForeachNodeSafe(&TaskResBase->trb_NewTasks,entry,next)
        if (entry->tle_Task==task) {
            Remove(&entry->tle_Node);retired=entry;break;
        }
    if (!retired) {
        ForeachNodeSafe(&TaskResBase->trb_TaskList,entry,next)
            if (entry->tle_Task==task) {
                if (IsListEmpty(&TaskResBase->trb_LockedLists)) {
                    Remove(&entry->tle_Node);retired=entry;
                } else entry->tle_Task=NULL;
                break;
            }
    }
    task_ListUnlock(TaskResBase);
    if (retired) FreeMem(retired,sizeof(*retired));
}
struct TaskListEntry *GetTaskEntry(struct Task *task, struct TaskResBase *TaskResBase)
{
    struct TaskListEntry *entry,*result=NULL;
    task_ListLock(TaskResBase);
    ForeachNode(&TaskResBase->trb_TaskList,entry)
        if (entry->tle_Task==task) { result=entry;break; }
    task_ListUnlock(TaskResBase);
    return result;
}

/* task hook support */
struct TaskListHookEntry *GetHookTypeEntry(struct List *htList, ULONG thType, BOOL create)
{
    struct TaskListHookEntry *currEntry;

    D(bug("%s(0x%p)\n", __func__, htList));

    ForeachNode(htList, currEntry)
    {
        if (currEntry->tlhe_Node.ln_Type == thType)
        {
            D(bug("Hook Type Entry Found @ 0x%p\n", currEntry));
            return currEntry;
        }
    }

    D(bug("Hook Type %d Not Found\n", thType));

    if (create)
    {
        currEntry = AllocMem(sizeof(struct TaskListHookEntry), MEMF_ANY|MEMF_CLEAR);
        if (currEntry)
        {
            D(bug("New Hook Type Entry @ 0x%p for type %d\n", currEntry, thType));

            currEntry->tlhe_Node.ln_Type = thType;
            NEWLIST(&currEntry->tlhe_Hooks);
            AddTail(htList, &currEntry->tlhe_Node);

            return currEntry;
        }
    }

    return NULL;
}

VOID TaskHookTypeDispose(struct TaskListHookEntry *task, ULONG type)
{
    D(bug("%s(0x%p)\n", __func__));
}

VOID TaskHooksDispose(struct TaskListHookEntry *task)
{
    D(bug("%s(0x%p)\n", __func__));
}

/*
 * Deliver a task lifecycle notification to every registered hook.
 * Called from the NewAddTask()/RemTask() wrappers, never under Forbid().
 */
void taskres_NotifyTasks(struct TaskResBase *TaskResBase, ULONG action, struct Task *task, struct Task *parent, struct TagItem *tags)
{
    struct TaskNotifyMsg msg;
    struct Hook *hook;

    msg.tnm_Action = action;
    msg.tnm_Task = task;
    msg.tnm_Parent = parent;
    msg.tnm_Tags = tags;

    ObtainSemaphoreShared(&TaskResBase->trb_NotifySem);
    ForeachNode(&TaskResBase->trb_NotifyHooks, hook)
    {
        CALLHOOKPKT(hook, task, &msg);
    }
    ReleaseSemaphore(&TaskResBase->trb_NotifySem);
}
