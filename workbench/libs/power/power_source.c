/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    The power state: read from the platform provider on demand, and
    watched by one library task that announces a change of source or of
    whole percent to the subscribers. Without a provider no task is started
    and no source message is ever sent.
*/
#include <string.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <aros/debug.h>
#include <proto/exec.h>

#include "power_private.h"

BOOL power_ReadState(struct PowerBase *PowerBase, struct PowerState *state)
{
    memset(state, 0, sizeof(*state));
    if (PowerBase->pb_Provider == NULL || !power_ProviderRead(PowerBase, state))
        return FALSE;
    state->pst_Flags |= POWERSTF_KNOWN;
    if ((state->pst_Flags & POWERSTF_CURRENT) && (state->pst_Flags & POWERSTF_CAPACITY))
    {
        LONG ma = state->pst_CurrentMa;
        if (ma < 0 && state->pst_RemainingMah != 0)
            state->pst_MinutesLeft = (ULONG)(((UQUAD)state->pst_RemainingMah * 60) / (ULONG)(-ma));
        else if (ma > 0 && state->pst_FullMah > state->pst_RemainingMah)
            state->pst_MinutesLeft = (ULONG)(((UQUAD)(state->pst_FullMah - state->pst_RemainingMah) * 60) / (ULONG)ma);
    }
    return TRUE;
}

/* One source message to every subscription that wants it and has none out. */
ULONG power_SendSource(struct PowerBase *PowerBase, const struct PowerState *state)
{
    struct PowerSubscription *subscription;
    ULONG sent = 0;

    POWER_LOCK(PowerBase);
    ForeachNode(&PowerBase->pb_Subscriptions, subscription)
    {
        struct PowerMessage *message;

        if (subscription->ps_Removed || subscription->ps_Port == NULL ||
            subscription->ps_SourceMsg != NULL || !(subscription->ps_Events & (1UL << POWER_EVENT_SOURCE)))
            continue;
        message = power_NewMessage(PowerBase, POWER_EVENT_SOURCE);
        if (message == NULL)
            continue;
        message->pm_PowerFlags = state->pst_Flags;
        message->pm_Percent = state->pst_Percent;
        message->pm_Subscription = subscription;
        subscription->ps_SourceMsg = message;
        PutMsg(subscription->ps_Port, &message->pm_Message);
        sent++;
    }
    POWER_UNLOCK(PowerBase);
    return sent;
}

/* Staleness is not a change of source: the provider's own sampler is
 * frozen during a sleep, and every long sleep would otherwise announce it. */
#define SOURCE_MASK (POWERSTF_KNOWN | POWERSTF_BATTERY | POWERSTF_MAINS | POWERSTF_CHARGING)

static void watcher(struct PowerBase *PowerBase)
{
    struct MsgPort *port = CreateMsgPort();
    struct timerequest *request = port ? (struct timerequest *)CreateIORequest(port, sizeof(*request)) : NULL;

    if (request == NULL || OpenDevice("timer.device", UNIT_VBLANK, &request->tr_node, 0) != 0)
    {
        if (request != NULL)
            DeleteIORequest(&request->tr_node);
        if (port != NULL)
            DeleteMsgPort(port);
        PowerBase->pb_WatcherState = 2;
        return;
    }
    power_ReadState(PowerBase, &PowerBase->pb_LastState);
    for (;;)
    {
        struct PowerState now;

        request->tr_node.io_Command = TR_ADDREQUEST;
        request->tr_time.tv_secs = 1;
        request->tr_time.tv_micro = 0;
        DoIO(&request->tr_node);

        power_CollectReplies(PowerBase);
        power_ReadState(PowerBase, &now);
        if ((now.pst_Flags & SOURCE_MASK) != (PowerBase->pb_LastState.pst_Flags & SOURCE_MASK) ||
            now.pst_Percent != PowerBase->pb_LastState.pst_Percent)
        {
            ULONG sent = power_SendSource(PowerBase, &now);
            D(bug("[POWER] source flags=0x%lx percent=%lu sent=%lu\n", now.pst_Flags, now.pst_Percent, sent));
            (void)sent;
            PowerBase->pb_LastState = now;
        }
    }
}

/* NewCreateTask() passes TASKTAG_ARG1 as the first C argument. */
static void power_WatcherEntry(struct PowerBase *PowerBase)
{
    watcher(PowerBase);
}

/* Started at the first subscription that wants source events, once, and
 * only when a provider exists. */
void power_StartWatcher(struct PowerBase *PowerBase)
{
    struct Task *task;

    if (PowerBase->pb_Provider == NULL)
        return;
    Forbid();
    /* Claimed atomically: on SMP, Forbid() holds only this CPU. */
    if (!__sync_bool_compare_and_swap(&PowerBase->pb_WatcherState, 0, 1))
    {
        Permit();
        return;
    }
    /* The state, set to 1 first, is what keeps a second start out. The
     * watcher may run before NewCreateTask returns (task.resource's notify
     * semaphore can break the Forbid, and on SMP it can start on another
     * CPU); a watcher that fails sets 2, which is only overwritten here when
     * the task was never created. */
    task = NewCreateTask(TASKTAG_PC, power_WatcherEntry,
        TASKTAG_NAME, "power.library source",
        TASKTAG_PRI, 1,
        TASKTAG_ARG1, PowerBase,
        TAG_DONE);
    PowerBase->pb_Watcher = task;
    if (task == NULL)
        PowerBase->pb_WatcherState = 0;
    Permit();
}
