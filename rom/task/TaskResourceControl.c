/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#include <stddef.h>
#include <string.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#define __DOS_NOLIBBASE__
#include <proto/dos.h>
#include <resources/task_rt.h>
#define __KERNEL_NOLIBBASE__
#include <proto/kernel.h>
#include "task_intern.h"

#define RTS_LIVE 0
#define RTS_EXPORTED 1
#define RTS_QUARANTINED 2
#define RTS_CLEANING 3

struct RTContext {
    struct MinNode global;
    struct MinList records;
    struct Task *task; /* identity only; never dereferenced after exit */
    ULONG closing, cleaning, retired;
};
struct RTRecord {
    struct MinNode global, owned;
    struct RTContext *owner;
    struct DosLibrary *dos;
    IPTR handle, object, auxiliary;
    ULONG type, state, pins, dirty, close_error;
};

struct RTPin {
    struct MinNode node;
    IPTR handle;
    struct RTRecord *record;
};
static IPTR rt_handle(struct TaskResBase *TaskResBase)
{
    IPTR handle = TaskResBase->trb_RTNext;
    if (handle)
        TaskResBase->trb_RTNext = handle == ((~(IPTR)0) >> 1) ? 0 : handle + 1;
    return handle;
}

static BOOL rt_context_ok(struct TaskResBase *TaskResBase)
{
    return IDNESTCOUNT_GET < 0 && TDNESTCOUNT_GET < 0 &&
           KernelBase && !KrnIsSuper();
}
/* No allocation, blocking operation or resource destructor under this lock.
 * Disable protects a UP/interrupt caller; SMP also serializes other CPUs. */
static void rt_lock(struct TaskResBase *TaskResBase)
{
    Disable();
#if defined(__AROSEXEC_SMP__)
    EXEC_SPINLOCK_LOCK(&TaskResBase->trb_RTLock, NULL, SPINLOCK_MODE_WRITE);
#endif
}
static void rt_unlock(struct TaskResBase *TaskResBase)
{
#if defined(__AROSEXEC_SMP__)
    EXEC_SPINLOCK_UNLOCK(&TaskResBase->trb_RTLock);
#endif
    Enable();
}
static BOOL rt_closed(struct RTContext *ctx)
{
    return ctx && __atomic_load_n(&ctx->closing, __ATOMIC_ACQUIRE);
}
static struct RTContext *rt_current(void)
{
    struct Task *task = FindTask(NULL);
    return GetETask(task) ? GetIntETask(task)->iet_RT : NULL;
}
static struct RTRecord *rt_find(IPTR handle, struct TaskResBase *TaskResBase)
{
    struct RTRecord *record;
    ForeachNode(&TaskResBase->trb_RTRecords, record)
        if (record->handle == handle) return record;
    return NULL;
}
static BOOL rt_transferable(struct RTRecord *r)
{
    return r->type == RT_MEM || r->type == RT_VEC ||
           r->type == RT_FILE || r->type == RT_LOCK;
}
static void rt_detach(struct RTRecord *r)
{
    if (r->owner) Remove((struct Node *)&r->owned);
    r->owner = NULL;
}
static void rt_dispose_record(struct RTRecord *r)
{
    if (r->dos) CloseLibrary((struct Library *)r->dos);
    FreeMem(r, sizeof(*r));
}
static BOOL rt_destroy(struct RTRecord *r)
{
    struct Task *self = FindTask(NULL);
    struct DosLibrary *DOSBase = r->dos;
    switch (r->type) {
    case RT_MEM: FreeMem((APTR)r->object, r->auxiliary); return TRUE;
    case RT_VEC: FreeVec((APTR)r->object); return TRUE;
    case RT_SEMAPHORE: {
        struct SignalSemaphore *sem = (APTR)r->object;
        if (sem->ss_Owner != self || sem->ss_NestCount != 1) return FALSE;
        ReleaseSemaphore(sem); return TRUE;
    }
    case RT_FILE:
        if (self->tc_Node.ln_Type != NT_PROCESS) return FALSE;
        /* Close consumes the FileHandle even if its handler reports failure. */
        r->close_error = !Close((BPTR)r->object);
        return TRUE;
    case RT_LOCK:
        if (self->tc_Node.ln_Type != NT_PROCESS) return FALSE;
        UnLock((BPTR)r->object); return TRUE;
    case RT_PORT: {
        struct MsgPort *port = (APTR)r->object;
        /* Explicit private ownership is also required: this check cannot
         * establish that an unregistered publisher has dropped its pointer. */
        if (port->mp_SigTask != self || port->mp_Flags != PA_SIGNAL ||
            !IsListEmpty(&port->mp_MsgList)) return FALSE;
        DeleteMsgPort(port); return TRUE;
    }
    case RT_SIGNAL: FreeSignal((LONG)r->object); return TRUE;
    }
    return FALSE;
}

/* Called before destructive Exec teardown, and from DOS before its own
 * process cleanup. Never invoke DOS or other destructors under this lock. */
static BOOL rt_cleanup(struct Task *task, BOOL dos_only, struct TaskResBase *TaskResBase)
{
    struct RTContext *ctx;
    struct RTRecord *r;
    struct MinNode *node;
    BOOL self = task == FindTask(NULL), safe, complete = TRUE;
    safe = self && rt_context_ok(TaskResBase);
    rt_lock(TaskResBase);
    if (!GetETask(task)) { rt_unlock(TaskResBase); return TRUE; }
    ctx = GetIntETask(task)->iet_RT;
    if (!ctx) { rt_unlock(TaskResBase); return TRUE; }
    __atomic_store_n(&ctx->closing, 1, __ATOMIC_RELEASE);
    if (!safe) {
        ctx->retired = 1;
        /* Remote victims can still be executing here; leave their context
         * attached and resources retained. All APIs reject a closed owner.
         * Avoid sleeping if removal was requested under Forbid/Disable. */
        rt_unlock(TaskResBase);
        return FALSE;
    }
    if (ctx->cleaning || ctx->retired) {
        rt_unlock(TaskResBase); return FALSE;
    }
    ctx->cleaning = 1;
    for (;;) {
        if (ctx->retired) { complete = FALSE; break; }
        for (node = ctx->records.mlh_Head; node->mln_Succ; node = node->mln_Succ) {
            r = (struct RTRecord *)((UBYTE *)node - offsetof(struct RTRecord, owned));
            if (!dos_only || r->type == RT_FILE || r->type == RT_LOCK) break;
        }
        if (!node->mln_Succ) break;
        r = (struct RTRecord *)((UBYTE *)node - offsetof(struct RTRecord, owned));
        if (dos_only && r->type != RT_FILE && r->type != RT_LOCK) continue;
        if (r->pins || r->dirty) {
            r->state = RTS_QUARANTINED;
            rt_detach(r); complete = FALSE;
            continue;
        }
        r->state = RTS_CLEANING;
        rt_unlock(TaskResBase);
        BOOL freed = rt_destroy(r);
        rt_lock(TaskResBase);
        rt_detach(r);
        if (freed) {
            Remove((struct Node *)&r->global);
            TaskResBase->trb_RTReclaimed[r->type]++;
            if (r->close_error) {
                TaskResBase->trb_RTCloseErrors++; complete = FALSE;
            }
            rt_unlock(TaskResBase);
            rt_dispose_record(r);
            rt_lock(TaskResBase);
        } else { r->state = RTS_QUARANTINED; complete = FALSE; }
    }
    ctx->cleaning = 0;
    BOOL dispose_context = !dos_only && !ctx->retired;
    if (dispose_context) {
        GetIntETask(task)->iet_RT = NULL;
        Remove((struct Node *)&ctx->global);
    }
    rt_unlock(TaskResBase);
    if (dispose_context) FreeMem(ctx, sizeof(*ctx));
    return complete;
}
void taskres_ResourceExit(struct Task *task, struct TaskResBase *TaskResBase)
{
    rt_cleanup(task, FALSE, TaskResBase);
}

AROS_LH1(IPTR, TaskResourceControl,
    AROS_LHA(struct RTRequest *, request, A0),
    struct TaskResBase *, TaskResBase, 22, Task)
{
    AROS_LIBFUNC_INIT
    struct Task *self = FindTask(NULL);
    struct RTContext *ctx, *fresh = NULL;
    struct RTRecord *r, *candidate = NULL, *dispose = NULL;
    struct RTPin *pin = NULL, *unpin = NULL, *p;
    IPTR result = 0;
    if (!request || request->size != sizeof(*request) ||
        !GetETask(self) || !rt_context_ok(TaskResBase)) return 0;
    if (request->action == RT_CLEANUP || request->action == RT_PREPARE_EXIT)
        return rt_cleanup(self, request->action == RT_PREPARE_EXIT, TaskResBase);
    if (request->action == RT_PIN) {
        pin = AllocMem(sizeof(*pin), MEMF_CLEAR);
        if (!pin) return 0;
    }

    if (request->action == RT_ADOPT || request->action == RT_ACCEPT ||
        request->action == RT_RECOVER) {
        fresh = AllocMem(sizeof(*fresh), MEMF_CLEAR);
        if (!fresh) return 0;
        NEWLIST(&fresh->records); fresh->task = self;
    }
    if (request->action == RT_ADOPT) {
        if (!(request->flags & RTF_EXCLUSIVE) || request->type < RT_MEM ||
            request->type >= RT_TYPE_COUNT ||
            (request->type != RT_SIGNAL && !request->object) ||
            (request->type == RT_MEM && (SIPTR)request->auxiliary <= 0) ||
            (request->type == RT_SIGNAL && request->object >= 32)) goto done;
        if (request->type == RT_SIGNAL &&
            !(self->tc_SigAlloc & (1UL << request->object))) goto done;
        if (request->type == RT_SEMAPHORE) {
            struct SignalSemaphore *sem = (APTR)request->object;
            if (sem->ss_Owner != self || sem->ss_NestCount != 1) goto done;
        }
        if (request->type == RT_PORT) {
            struct MsgPort *port = (APTR)request->object;
            if (port->mp_SigTask != self || port->mp_Flags != PA_SIGNAL) goto done;
        }
        candidate = AllocMem(sizeof(*candidate), MEMF_CLEAR);
        if (!candidate) goto done;
        candidate->type = request->type;
        candidate->object = request->object;
        candidate->auxiliary = request->auxiliary;
        if (request->type == RT_FILE || request->type == RT_LOCK) {
            if (self->tc_Node.ln_Type != NT_PROCESS) goto done;
            candidate->dos = (struct DosLibrary *)OpenLibrary("dos.library", 0);
            if (!candidate->dos) goto done;
        }
    }
    rt_lock(TaskResBase);
    ctx = rt_current();
    if (rt_closed(ctx)) goto unlock;
    if (request->action == RT_STATS) {
        if (!request->stats) goto unlock;
        struct RTStats *stats = request->stats;
        struct RTContext *context;
        struct RTPin *lease;
        memset(stats, 0, sizeof(*stats));
        CopyMem(TaskResBase->trb_RTReclaimed, stats->reclaimed, sizeof(stats->reclaimed));
        stats->close_errors = TaskResBase->trb_RTCloseErrors;
        ForeachNode(&TaskResBase->trb_RTContexts, context) {
            stats->contexts++;
            if (rt_closed(context)) stats->closed_contexts++;
        }
        ForeachNode(&TaskResBase->trb_RTPins, lease) stats->leases++;
        ForeachNode(&TaskResBase->trb_RTRecords, r) {
            stats->records++; stats->live[r->type]++;
            if (r->state == RTS_QUARANTINED || rt_closed(r->owner)) stats->quarantined++;
            if (r->state == RTS_EXPORTED) stats->exported++;
            if (r->pins) stats->pinned++;
            if (r->dirty) stats->dirty++;
        }
        result = 1; goto unlock;
    }
    if (request->action == RT_ADOPT) {
        if (!TaskResBase->trb_RTNext) goto unlock;
        ForeachNode(&TaskResBase->trb_RTRecords, r) {
            if (r->object == candidate->object &&
                ((r->type == RT_SIGNAL) == (candidate->type == RT_SIGNAL)) &&
                (r->type != RT_SIGNAL || (ctx && r->owner == ctx))) goto unlock;
            /* A CreateMsgPort owns its signal; reject double registration. */
            if (r->owner == ctx && r->state == RTS_LIVE &&
                ((r->type == RT_PORT && candidate->type == RT_SIGNAL &&
                  ((struct MsgPort *)r->object)->mp_SigBit == candidate->object) ||
                 (r->type == RT_SIGNAL && candidate->type == RT_PORT &&
                  ((struct MsgPort *)candidate->object)->mp_SigBit == r->object))) goto unlock;
        }
        if (!ctx) {
            ctx = fresh; fresh = NULL; GetIntETask(self)->iet_RT = ctx;
            AddTail((struct List *)&TaskResBase->trb_RTContexts, (struct Node *)&ctx->global);
        }
        candidate->handle = rt_handle(TaskResBase);
        candidate->owner = ctx;
        AddTail((struct List *)&TaskResBase->trb_RTRecords, (struct Node *)&candidate->global);
        AddHead((struct List *)&ctx->records, (struct Node *)&candidate->owned);
        result = candidate->handle; candidate = NULL; goto unlock;
    }
    if (request->action == RT_UNPIN) {
        ForeachNode(&TaskResBase->trb_RTPins, p) {
            if (p->handle == request->handle) {
                p->record->pins--;
                Remove((struct Node *)&p->node); unpin = p; result = 1; break;
            }
        }
        goto unlock;
    }
    r = rt_find(request->handle, TaskResBase);
    if (!r || r->state == RTS_CLEANING) goto unlock;
    if (request->action == RT_PIN) {
        if (r->state == RTS_LIVE && !rt_closed(r->owner) &&
            r->pins != (ULONG)~0U && TaskResBase->trb_RTNext) {
            pin->handle = rt_handle(TaskResBase); pin->record = r;
            AddTail((struct List *)&TaskResBase->trb_RTPins, (struct Node *)&pin->node);
            r->pins++; result = pin->handle; pin = NULL;
        }
        goto unlock;
    }
    if (request->action == RT_ACCEPT || request->action == RT_RECOVER) {
        BOOL recovery = request->action == RT_RECOVER &&
            (r->state == RTS_QUARANTINED && r->owner == NULL) &&
            (request->flags & (RTF_QUIESCENT | RTF_ROLLBACK_DONE)) ==
                (RTF_QUIESCENT | RTF_ROLLBACK_DONE);
        if ((!recovery && r->state != RTS_EXPORTED) || r->pins ||
            (!recovery && r->dirty) || !rt_transferable(r)) goto unlock;
        if ((r->type == RT_FILE || r->type == RT_LOCK) &&
            self->tc_Node.ln_Type != NT_PROCESS) goto unlock;
        /* A supervisor's explicit recovery assertion never authorizes owner-
         * bound signals, ports or semaphore acquisitions in a different task. */
        rt_detach(r);
        if (!ctx) {
            ctx = fresh; fresh = NULL; GetIntETask(self)->iet_RT = ctx;
            AddTail((struct List *)&TaskResBase->trb_RTContexts, (struct Node *)&ctx->global);
        }
        r->owner = ctx; r->state = RTS_LIVE; r->dirty = 0;
        AddHead((struct List *)&ctx->records, (struct Node *)&r->owned);
        result = 1; goto unlock;
    }
    if (!ctx || r->owner != ctx || r->state != RTS_LIVE) goto unlock;
    switch (request->action) {
    case RT_DISOWN:
        if (!r->pins && !r->dirty && rt_transferable(r)) {
            rt_detach(r); r->state = RTS_EXPORTED; result = 1;
        }
        break;
    case RT_FORGET:
        if (!r->pins && !r->dirty) {
            rt_detach(r); Remove((struct Node *)&r->global); dispose = r; result = 1;
        }
        break;
    case RT_BEGIN_MUTATION:
        if (!r->dirty) { r->dirty = 1; result = 1; }
        break;
    case RT_END_MUTATION:
        if (r->dirty) { r->dirty = 0; result = 1; }
        break;
    }
unlock:
    rt_unlock(TaskResBase);
done:
    if (dispose) rt_dispose_record(dispose);
    if (candidate) rt_dispose_record(candidate);
    if (fresh) FreeMem(fresh, sizeof(*fresh));
    if (pin) FreeMem(pin, sizeof(*pin));
    if (unpin) FreeMem(unpin, sizeof(*unpin));
    return result;
    AROS_LIBFUNC_EXIT
}
