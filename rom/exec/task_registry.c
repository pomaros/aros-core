/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Task identity membership, independent of scheduler list migration.
*/

#include <stddef.h>
#include <proto/exec.h>
#include <exec/processmessage.h>
#include "etask.h"
#include "exec_intern.h"
#include "exec_util.h"

#if defined(__AROSEXEC_SMP__)

/* Caller holds TaskRegistrySpinLock. Never acquire a scheduler/task lock
 * from here. Registry nodes belong to ETasks, not tc_Node. */
static struct IntETask *FindRegisteredID(ULONG id, struct ExecBase *SysBase)
{
    struct Node *node;

    ForeachNode(&PrivExecBase(SysBase)->TaskRegistry, node)
    {
        struct IntETask *et = (struct IntETask *)((UBYTE *)node -
            offsetof(struct IntETask, iet_RegistryNode));
        if (et->iet_ETask.et_UniqueID == id)
            return et;
    }
    return NULL;
}

void Exec_RegisterTask(struct Task *task, struct ExecBase *SysBase)
{
    struct IntETask *et = GetIntETask(task);
    ULONG id;

    Disable();
    EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskRegistrySpinLock, NULL,
        SPINLOCK_MODE_WRITE);
    /* Reserve the identity and publish its record in the same critical
     * section. Forbid alone permits two CPUs to issue the same ID. */
    do
    {
        id = ++SysBase->ex_TaskID;
        if (id == 0)
            id = SysBase->ex_TaskID = 1024;
    } while (FindRegisteredID(id, SysBase) != NULL);
    et->iet_ETask.et_UniqueID = id;
    et->iet_RegistryTask = task;
    et->iet_RegistryVisible = FALSE;
    ADDTAIL(&PrivExecBase(SysBase)->TaskRegistry, &et->iet_RegistryNode);
    EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskRegistrySpinLock);
    Enable();
}

void Exec_PublishTask(struct Task *task, struct ExecBase *SysBase)
{
    struct IntETask *et = GetIntETask(task);

    Disable();
    EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskRegistrySpinLock, NULL,
        SPINLOCK_MODE_WRITE);
    et->iet_RegistryVisible = TRUE;
    EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskRegistrySpinLock);
    Enable();
}

void Exec_UnregisterTask(struct Task *task, struct ExecBase *SysBase)
{
    struct IntETask *et = GetIntETask(task);

    /* RemTask also accepts tasks without an ETask. */
    if (et == NULL)
        return;
    Disable();
    EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskRegistrySpinLock, NULL,
        SPINLOCK_MODE_WRITE);
    if (et->iet_RegistryTask != NULL)
    {
        REMOVE(&et->iet_RegistryNode);
        et->iet_RegistryVisible = FALSE;
        et->iet_RegistryTask = NULL;
        et->iet_RegistryNode.ln_Succ = NULL;
        et->iet_RegistryNode.ln_Pred = NULL;
    }
    EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskRegistrySpinLock);
    Enable();
}

void Exec_RetireTask(struct Task *task, struct ExecBase *SysBase)
{
    struct IntETask *et = GetIntETask(task);

    if (et == NULL)
        return;
    Disable();
    EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskRegistrySpinLock, NULL,
        SPINLOCK_MODE_WRITE);
    /* Keep the identity reserved through context cleanup. An endpoint query
     * must distinguish retiring from absent before reclaiming external state. */
    et->iet_RegistryVisible = FALSE;
    EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskRegistrySpinLock);
    Enable();
}

ULONG Exec_LockProcessIdentity(struct Task *task, ULONG id, struct ExecBase *SysBase)
{
    struct IntETask *et;
    ULONG result = TPM_DEAD;

    Disable();
    EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskRegistrySpinLock, NULL,
        SPINLOCK_MODE_READ);
    et = FindRegisteredID(id, SysBase);
    if (et != NULL && et->iet_RegistryTask == task)
    {
        if (!et->iet_RegistryVisible)
            result = TPM_BUSY;
        else if (task->tc_Node.ln_Type != NT_PROCESS)
            result = TPM_INVALID;
        else
            return TPM_OK; /* Caller owns the read lock and Disable nesting. */
    }
    EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskRegistrySpinLock);
    Enable();
    return result;
}

void Exec_UnlockProcessIdentity(struct ExecBase *SysBase)
{
    EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskRegistrySpinLock);
    Enable();
}

struct Task *Exec_FindRegisteredTask(ULONG id, struct ExecBase *SysBase)
{
    struct IntETask *et;
    struct Task *task;

    Disable();
    EXEC_SPINLOCK_LOCK(&PrivExecBase(SysBase)->TaskRegistrySpinLock, NULL,
        SPINLOCK_MODE_READ);
    et = FindRegisteredID(id, SysBase);
    task = et != NULL && et->iet_RegistryVisible ? et->iet_RegistryTask : NULL;
    EXEC_SPINLOCK_UNLOCK(&PrivExecBase(SysBase)->TaskRegistrySpinLock);
    Enable();
    /* As with FindTask, this raw pointer does NOT retain the task. */
    return task;
}

#endif
