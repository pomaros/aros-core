/*
    Copyright (C) 2015-2026, The AROS Development Team. All rights reserved.
*/

#ifndef TASKRES_H
#define TASKRES_H

#ifndef EXEC_TASKS_H
#   include <exec/tasks.h>
#endif

#ifndef UTILITY_TAGITEM_H
#   include <utility/tagitem.h>
#endif

#define LTF_RUNNING                     (1 << 0)
#define LTF_READY                       (1 << 1)
#define LTF_WAITING                     (1 << 2)                // N.B: includes spinning tasks
#define LTF_ALL                         (LTF_RUNNING|LTF_READY|LTF_WAITING)
#define LTF_WRITE                       (1 << 31)

#define TaskTag_CPUNumber               (TAG_USER + 0x00000001) // CPU Number task is currently running on
#define TaskTag_CPUAffinity             (TAG_USER + 0x00000002) // CPU Affinity mask
#define TaskTag_CPUTime                 (TAG_USER + 0x00000003) // Amount of CPU time spent running
#define TaskTag_StartTime               (TAG_USER + 0x00000004) // Time the task was started
#define TaskTag_CPUUsage                (TAG_USER + 0x00000005) // CPU Usage of this task
#define TaskTag_REG_PC                  (TAG_USER + 0x00000020)
#define TaskTag_REG_SP                  (TAG_USER + 0x00000021)
#define TaskTag_REG_GeneralCount        (TAG_USER + 0x00000030)
#define TaskTag_REG_GeneralSize         (TAG_USER + 0x00000031)
#define TaskTag_REG_General0            (TAG_USER + 0x00000032)
#define TaskTag_REG_FloatCount          (TAG_USER + 0x00000050)
#define TaskTag_REG_FloatSize           (TAG_USER + 0x00000051)
#define TaskTag_REG_Float0              (TAG_USER + 0x00000052)
#define TaskTag_REG_VecCount            (TAG_USER + 0x00000070)
#define TaskTag_REG_VecSize             (TAG_USER + 0x00000071)
#define TaskTag_REG_Vec0                (TAG_USER + 0x00000072)

#define TaskTag_REGID_f                 0x00001000

#define TaskTag_REGID_General0          (TaskTag_REG_General0   | TaskTag_REGID_f)
#define TaskTag_REGID_Float0            (TaskTag_REG_Float0     | TaskTag_REGID_f)
#define TaskTag_REGID_Vec0              (TaskTag_REG_Vec0       | TaskTag_REGID_f)

/* Task Hook Flags */
#define THF_ROA                         1
#define THF_IAR                         2

// The contents of the tasklist are private to task.resource
struct TaskList
{
    void *tl_Private;
};

typedef int (*TaskResHookDispatcher)(struct Hook *);

#define TASKHOOK_TYPE_LATEINIT          (1 << 0)

/*
 * Task lifecycle notifications (AddTaskNotifyHook()/RemTaskNotifyHook()).
 *
 * Registered hooks are called as CallHookPkt(hook, task, &msg) with a
 * struct TaskNotifyMsg. TNA_ADDED is delivered in the context of the creating
 * task BEFORE exec.library's NewAddTask() runs (the ETask does not exist yet);
 * TNA_FAILED follows if NewAddTask() then fails. TNA_REMOVED is delivered in
 * the context of the caller of RemTask() BEFORE exec removes the task. Hooks
 * are not called under Forbid()/Disable() and may allocate memory or obtain
 * semaphores, but must not Wait() for other tasks.
 */
struct TaskNotifyMsg
{
    ULONG               tnm_Action;             /* TNA_#?                                       */
    struct Task         *tnm_Task;              /* The task being added/removed                 */
    struct Task         *tnm_Parent;            /* Creator (TNA_ADDED/TNA_FAILED) or NULL       */
    struct TagItem      *tnm_Tags;              /* NewAddTask() taglist (TNA_ADDED) or NULL     */
};

#define TNA_ADDED                       (1)
#define TNA_REMOVED                     (2)
#define TNA_FAILED                      (3)

/* task.resource 2.0: a separate list, controlled by TaskStopHookControl().
 * Adding requires Exec 51.12 or later, which supplies the service handoff.
 * It also requires platform EXEC_SELF_EXIT_HANDOFF support; unsupported
 * platforms refuse addition and never dispatch this notification. Dispatch
 * accepts only the actual Exec service task, identified by its private port.
 * Hooks receive CallHookPkt(hook, task, NULL) from Exec's service task after
 * its TS_INVALID handoff and before freeing the Task allocation. ETask,
 * context, TaskStorage and borrowed library bases are already unavailable.
 * Only compare the Task pointer with a pre-existing heap record and publish
 * bounded atomic state. Hooks run with interrupts disabled and the list's
 * SMP lock held: no allocation, waiting, Exec calls or hook-list mutation.
 * The hook and its code/data must remain resident until successful removal;
 * install it before publishing owner records; serialize its own registration
 * and destruction. The callback may not take any additional locks.
 * Each Hook node belongs exclusively to this list; never also register it
 * with AddTaskNotifyHook, AddTaskHook or another intrusive list. A refused
 * removal (including calls under Disable/Forbid or from a callback) leaves
 * the hook registered and its code/data must remain resident.
 * Successful removal joins callbacks already in flight. This is one barrier only:
 * records published to another actor also need that actor's completion.
 * Notification covers ServiceTask's INVALID path, including the certified SMP
 * self-exit handoff. It does not certify remotely removed RUN tasks, reclaim their
 * requests, or promise notification on other removal paths. */

#endif /* TASKRES_H */
