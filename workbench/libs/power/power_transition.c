/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    The transition: tell the subscribers, wait for their replies or the
    deadline, freeze, hold the up time, sleep, run the scheduled wakes
    that fall in the sleep one requester at a time with their budget,
    release, thaw, tell them again. Message memory follows one rule: a
    message is freed only after its receiver replied it.
*/
#include <string.h>
#include <exec/memory.h>
#include <aros/debug.h>
#include <proto/exec.h>

#include "power_private.h"

#define BURST_MAX 16

struct PowerMessage *power_NewMessage(struct PowerBase *PowerBase, ULONG event)
{
    struct PowerMessageInternal *internal = AllocVec(sizeof(*internal), MEMF_PUBLIC | MEMF_CLEAR);

    if (internal == NULL)
        return NULL;
    internal->pmi_Generation = PowerBase->pb_Generation;
    internal->pmi_Public.pm_Message.mn_Node.ln_Type = NT_MESSAGE;
    internal->pmi_Public.pm_Message.mn_ReplyPort = &PowerBase->pb_ReplyPort;
    internal->pmi_Public.pm_Message.mn_Length = sizeof(struct PowerMessage);
    internal->pmi_Public.pm_Event = event;
    return &internal->pmi_Public;
}

/* Free every message that came back to the library's reply port and clear
 * the slot it occupied. A subscription or a wake removed while its message
 * was out is freed here once the reply arrives; a wake only outside a
 * transition, which holds pointers to the wakes it is running. */
void power_CollectReplies(struct PowerBase *PowerBase)
{
    struct PowerMessage *message;

    while ((message = (struct PowerMessage *)GetMsg(&PowerBase->pb_ReplyPort)) != NULL)
    {
        POWER_LOCK(PowerBase);
        if (message->pm_Wake != NULL)
        {
            struct PowerWake *wake = message->pm_Wake;

            if (wake->pw_Message == message)
                wake->pw_Message = NULL;
            if (wake->pw_Removed && wake->pw_Message == NULL && !PowerBase->pb_InTransition)
            {
                Remove((struct Node *)wake);
                FreeVec(wake);
            }
        }
        else if (message->pm_Subscription != NULL)
        {
            struct PowerSubscription *subscription = message->pm_Subscription;

            if (subscription->ps_Sleep == message)
                subscription->ps_Sleep = NULL;
            else if (subscription->ps_Wake == message)
                subscription->ps_Wake = NULL;
            else if (subscription->ps_SourceMsg == message)
                subscription->ps_SourceMsg = NULL;
            if (subscription->ps_Removed && subscription->ps_Sleep == NULL &&
                subscription->ps_Wake == NULL && subscription->ps_SourceMsg == NULL)
            {
                Remove((struct Node *)subscription);
                FreeVec(subscription);
            }
        }
        POWER_UNLOCK(PowerBase);
        FreeVec(message);
    }
}

/* The task is going away, its port with it. What it holds no message of is
 * freed now; the rest stays, marked, until its messages come back, which
 * they never will if the port is freed with them inside: bounded by the
 * slots, three per subscription. Its wakes go the same way. */
void power_TaskRemoved(struct PowerBase *PowerBase, struct Task *task)
{
    struct PowerSubscription *subscription, *next;
    struct PowerWake *wake, *nextwake;

    POWER_LOCK(PowerBase);
    ForeachNodeSafe(&PowerBase->pb_Subscriptions, subscription, next)
    {
        if (subscription->ps_Task != task)
            continue;
        if (subscription->ps_Sleep == NULL && subscription->ps_Wake == NULL && subscription->ps_SourceMsg == NULL)
        {
            Remove((struct Node *)subscription);
            FreeVec(subscription);
        }
        else
        {
            subscription->ps_Removed = TRUE;
            subscription->ps_Port = NULL;
        }
    }
    ForeachNodeSafe(&PowerBase->pb_Wakes, wake, nextwake)
    {
        ULONG i;

        /* A task named with POWER_WakeWith is forgotten, never touched again. */
        for (i = 0; i < wake->pw_WithCount; i++)
            if (wake->pw_With[i] == task)
                wake->pw_With[i] = NULL;
        if (wake->pw_Task != task)
            continue;
        wake->pw_Task = NULL;
        if (wake->pw_Message == NULL && !PowerBase->pb_InTransition)
        {
            Remove((struct Node *)wake);
            FreeVec(wake);
        }
        else
        {
            wake->pw_Removed = TRUE;
            wake->pw_Port = NULL;
        }
    }
    POWER_UNLOCK(PowerBase);
}

static ULONG ms_between(const struct timeval *a, const struct timeval *b)
{
    LONG ms = ((LONG)b->tv_secs - (LONG)a->tv_secs) * 1000 +
              ((LONG)b->tv_micro - (LONG)a->tv_micro) / 1000;
    return ms < 0 ? 0 : (ULONG)ms;
}

static int time_before(const struct timeval *a, const struct timeval *b)
{
    return a->tv_secs < b->tv_secs || (a->tv_secs == b->tv_secs && a->tv_micro < b->tv_micro);
}

static void add_ms(struct timeval *t, ULONG ms)
{
    t->tv_secs += ms / 1000;
    t->tv_micro += (ms % 1000) * 1000;
    if (t->tv_micro >= 1000000)
    {
        t->tv_secs++;
        t->tv_micro -= 1000000;
    }
}

/* Send one event to every live subscription that wants it, is not the
 * caller's own, and has its slot for that event free. A sleep message is
 * stamped with the transition's generation; a wake message goes to the
 * subscriptions that were sent this transition's sleep message. Returns the
 * number sent. Under the lock, AllocVec and PutMsg only. */
static ULONG send_event(struct PowerBase *PowerBase, ULONG event, ULONG depth, ULONG source,
                        ULONG slept, ULONG deadline)
{
    struct PowerSubscription *subscription;
    struct Task *caller = FindTask(NULL);
    ULONG generation = PowerBase->pb_Generation;
    ULONG sent = 0;

    POWER_LOCK(PowerBase);
    ForeachNode(&PowerBase->pb_Subscriptions, subscription)
    {
        struct PowerMessage *message;
        struct PowerMessage **slot = event == POWER_EVENT_SLEEP ? &subscription->ps_Sleep : &subscription->ps_Wake;

        if (subscription->ps_Removed || subscription->ps_Port == NULL ||
            subscription->ps_Task == caller || *slot != NULL ||
            !(subscription->ps_Events & (1UL << event)))
            continue;
        if (event == POWER_EVENT_WAKE && subscription->ps_SentGeneration != generation)
            continue;
        message = power_NewMessage(PowerBase, event);
        if (message == NULL)
            continue;
        message->pm_Depth = depth;
        message->pm_Source = source;
        message->pm_Slept = slept;
        message->pm_Deadline = deadline;
        message->pm_Subscription = subscription;
        *slot = message;
        if (event == POWER_EVENT_SLEEP)
            subscription->ps_SentGeneration = generation;
        PutMsg(subscription->ps_Port, &message->pm_Message);
        sent++;
    }
    POWER_UNLOCK(PowerBase);
    return sent;
}

/* How many of this transition's sleep messages are still out. Older ones,
 * held by a subscriber that never answered, do not hold this one up. */
static ULONG outstanding(struct PowerBase *PowerBase)
{
    struct PowerSubscription *subscription;
    ULONG generation = PowerBase->pb_Generation;
    ULONG count = 0;

    POWER_LOCK(PowerBase);
    ForeachNode(&PowerBase->pb_Subscriptions, subscription)
        if (subscription->ps_Sleep != NULL &&
            ((struct PowerMessageInternal *)subscription->ps_Sleep)->pmi_Generation == generation)
            count++;
    POWER_UNLOCK(PowerBase);
    return count;
}

/* The date a wake fires at: its own, or the next line of the grid. */
static void effective_date(const struct PowerWake *wake, ULONG grid, struct timeval *out)
{
    *out = wake->pw_Date;
    if (grid != 0 && (out->tv_secs % grid != 0 || out->tv_micro != 0))
    {
        out->tv_secs = (out->tv_secs / grid + 1) * grid;
        out->tv_micro = 0;
    }
}

/* The earliest pending wake's effective date. */
static BOOL next_wake(struct PowerBase *PowerBase, ULONG grid, struct timeval *next)
{
    struct PowerWake *wake;
    BOOL found = FALSE;

    POWER_LOCK(PowerBase);
    ForeachNode(&PowerBase->pb_Wakes, wake)
    {
        struct timeval date;

        if (wake->pw_Status != POWER_WAKE_PENDING || wake->pw_Removed || wake->pw_Port == NULL ||
            wake->pw_Task == NULL || wake->pw_Message != NULL)
            continue;
        effective_date(wake, grid, &date);
        if (!found || time_before(&date, next))
        {
            *next = date;
            found = TRUE;
        }
    }
    POWER_UNLOCK(PowerBase);
    return found;
}

/* Slot 0 is the wake's own task, slot n the (n-1)th task named with it. */
static struct Task *wake_task(struct PowerWake *wake, ULONG slot)
{
    return slot == 0 ? wake->pw_Task : wake->pw_With[slot - 1];
}

/* A wake of the burst has ended: freeze again only what this burst thawed
 * for it, and only tasks still alive (power_TaskRemoved clears the
 * pointers of a task that exited). A task that another unfinished wake of
 * the burst also names stays thawed, and that wake takes over freezing it,
 * so one wake ending does not stop the other's budget. */
static void finish_wake(struct PowerBase *PowerBase, struct PowerWake **due, const BOOL *done,
                        BOOL thawed[][POWER_WAKE_WITH_MAX + 1], ULONG count, ULONG i)
{
    ULONG slot;

    POWER_LOCK(PowerBase);
    for (slot = 0; slot <= due[i]->pw_WithCount; slot++)
    {
        struct Task *task = wake_task(due[i], slot);
        BOOL handed = FALSE;
        ULONG k;

        if (!thawed[i][slot] || task == NULL)
            continue;
        thawed[i][slot] = FALSE;
        for (k = 0; k < count && !handed; k++)
        {
            ULONG other;

            if (k == i || done[k])
                continue;
            for (other = 0; other <= due[k]->pw_WithCount; other++)
                if (wake_task(due[k], other) == task)
                {
                    thawed[k][other] = TRUE;
                    handed = TRUE;
                    break;
                }
        }
        if (!handed)
            FreezeTask(task);
    }
    POWER_UNLOCK(PowerBase);
}

/* One wake of the machine: every pending wake due by now runs, its task
 * and the tasks named with it thawed alone, the up time running, until it
 * replies or its budget is spent; then it is frozen again. Returns the
 * awake milliseconds spent. */
static ULONG run_burst(struct PowerBase *PowerBase, struct timerequest *request, struct MsgPort *port,
                       BYTE signal, ULONG grid, struct PowerSleepResult *result)
{
    struct Device *TimerBase = request->tr_node.io_Device;
    struct PowerWake *due[BURST_MAX];
    struct timeval ends[BURST_MAX];
    BOOL done[BURST_MAX];
    BOOL thawed[BURST_MAX][POWER_WAKE_WITH_MAX + 1];
    struct PowerWake *wake;
    struct timeval now, up_start, up_end;
    ULONG count = 0, i, left;

    GetSysTime(&now);
    POWER_LOCK(PowerBase);
    ForeachNode(&PowerBase->pb_Wakes, wake)
    {
        struct timeval date;
        struct PowerMessage *message;

        if (count == BURST_MAX)
            break;
        if (wake->pw_Status != POWER_WAKE_PENDING || wake->pw_Removed || wake->pw_Port == NULL ||
            wake->pw_Task == NULL || wake->pw_Message != NULL)
            continue;
        effective_date(wake, grid, &date);
        if (time_before(&now, &date))
            continue;
        message = power_NewMessage(PowerBase, POWER_EVENT_SCHEDULED);
        if (message == NULL)
        {
            /* No memory for the message: the wake cannot run; end it so the
             * sleep loop does not come back to it at once. */
            wake->pw_Status = POWER_WAKE_EXPIRED;
            result->psr_WakesExpired++;
            continue;
        }
        message->pm_Deadline = wake->pw_BudgetMs;
        message->pm_Wake = wake;
        wake->pw_Message = message;
        PutMsg(wake->pw_Port, &message->pm_Message);
        ends[count] = now;
        add_ms(&ends[count], wake->pw_BudgetMs);
        done[count] = FALSE;
        due[count++] = wake;
    }
    POWER_UNLOCK(PowerBase);
    if (count == 0)
        return 0;

    GetUpTime(&up_start);
    PauseUpTime(FALSE);
    kprintf("[POWER] scheduled wake burst wakes=%lu\n", (unsigned long)count);
    for (i = 0; i < count; i++)
    {
        ULONG j;

        POWER_LOCK(PowerBase);
        thawed[i][0] = due[i]->pw_Task != NULL && ThawTask(due[i]->pw_Task);
        for (j = 0; j < due[i]->pw_WithCount; j++)
            thawed[i][j + 1] = due[i]->pw_With[j] != NULL && ThawTask(due[i]->pw_With[j]);
        POWER_UNLOCK(PowerBase);
    }

    for (left = count; left != 0;)
    {
        struct timeval first;
        BOOL have_first = FALSE;
        ULONG mask = 1UL << port->mp_SigBit;

        power_CollectReplies(PowerBase);
        GetSysTime(&now);
        for (i = 0; i < count; i++)
        {
            if (done[i])
                continue;
            if (due[i]->pw_Message == NULL)
            {
                due[i]->pw_Status = POWER_WAKE_RAN;
                done[i] = TRUE;
                finish_wake(PowerBase, due, done, thawed, count, i);
                left--;
            }
            else if (!time_before(&now, &ends[i]))
            {
                due[i]->pw_Status = POWER_WAKE_EXPIRED;
                done[i] = TRUE;
                finish_wake(PowerBase, due, done, thawed, count, i);
                left--;
                result->psr_WakesExpired++;
            }
            else if (!have_first || time_before(&ends[i], &first))
            {
                first = ends[i];
                have_first = TRUE;
            }
        }
        if (left == 0 || !have_first)
            break;
        request->tr_node.io_Command = TR_ADDREQUEST;
        request->tr_time = first;
        SendIO(&request->tr_node);
        if (signal >= 0)
            mask |= 1UL << signal;
        Wait(mask);
        if (!CheckIO(&request->tr_node))
            AbortIO(&request->tr_node);
        WaitIO(&request->tr_node);
    }

    GetUpTime(&up_end);
    PauseUpTime(TRUE);
    result->psr_Wakes += count;
    result->psr_WakeBursts++;
    return ms_between(&up_start, &up_end);
}

LONG power_Transition(struct PowerBase *PowerBase, ULONG depth, ULONG seconds, ULONG source,
                      ULONG coalesce, ULONG grid_seconds, struct PowerSleepResult *result)
{
    struct MsgPort *port;
    struct timerequest *request;
    struct Device *TimerBase;
    struct timeval up_before, up_after, wall_before, wall_after, sent_at, proceeded_at, end;
    ULONG deadline = depth == POWER_DEPTH_LIGHT ? POWER_DEADLINE_LIGHT_MS : POWER_DEADLINE_DEEP_MS;
    ULONG frozen, thawed, grid = 0, awake_ms = 0;
    BYTE signal;
    LONG error = POWER_OK;

    memset(result, 0, sizeof(*result));
    result->psr_Depth = depth;
    result->psr_DeadlineMs = deadline;

    if (__sync_lock_test_and_set(&PowerBase->pb_InTransition, 1) != 0)
        return POWER_ERROR_BUSY;

    port = CreateMsgPort();
    request = port ? (struct timerequest *)CreateIORequest(port, sizeof(struct timerequest)) : NULL;
    if (request == NULL || OpenDevice("timer.device", UNIT_WAITUNTIL, &request->tr_node, 0) != 0)
    {
        if (request != NULL)
            DeleteIORequest(&request->tr_node);
        if (port != NULL)
            DeleteMsgPort(port);
        PowerBase->pb_InTransition = 0;
        return POWER_ERROR_DEVICE;
    }
    TimerBase = request->tr_node.io_Device;

    /* Coalescing: on battery, or when the machine does not say, wakes share
     * the grid; on mains each fires at its own date. */
    if (coalesce == POWER_COALESCE_ALWAYS)
        grid = grid_seconds;
    else if (coalesce == POWER_COALESCE_BATTERY)
    {
        struct PowerState state;
        if (!power_ReadState(PowerBase, &state) || !(state.pst_Flags & POWERSTF_MAINS))
            grid = grid_seconds;
    }

    /* The reply port is PA_IGNORE and shared; for the transition we borrow
     * a signal of this task and point the port at it. */
    power_CollectReplies(PowerBase);
    PowerBase->pb_Generation++;
    signal = AllocSignal(-1);
    if (signal >= 0)
    {
        Disable();
        PowerBase->pb_ReplyPort.mp_SigTask = FindTask(NULL);
        PowerBase->pb_ReplyPort.mp_SigBit = signal;
        PowerBase->pb_ReplyPort.mp_Flags = PA_SIGNAL;
        Enable();
    }

    /* 1. Tell them, and wait for the replies or the deadline. */
    GetSysTime(&sent_at);
    result->psr_Subscribers = send_event(PowerBase, POWER_EVENT_SLEEP, depth, source, 0, deadline);
    kprintf("[POWER] sleep depth=%lu subscribers=%lu deadline-ms=%lu grid-s=%lu\n",
        (unsigned long)depth, (unsigned long)result->psr_Subscribers, (unsigned long)deadline,
        (unsigned long)grid);
    if (result->psr_Subscribers != 0)
    {
        request->tr_node.io_Command = TR_ADDREQUEST;
        request->tr_time = sent_at;
        add_ms(&request->tr_time, deadline);
        SendIO(&request->tr_node);
        for (;;)
        {
            ULONG mask = 1UL << port->mp_SigBit;

            power_CollectReplies(PowerBase);
            if (outstanding(PowerBase) == 0)
            {
                AbortIO(&request->tr_node);
                WaitIO(&request->tr_node);
                break;
            }
            if (CheckIO(&request->tr_node))
            {
                WaitIO(&request->tr_node);
                break;
            }
            if (signal >= 0)
                mask |= 1UL << signal;
            Wait(mask);
        }
    }
    power_CollectReplies(PowerBase);
    GetSysTime(&proceeded_at);
    result->psr_ProceededAtMs = ms_between(&sent_at, &proceeded_at);
    {
        ULONG left = outstanding(PowerBase);
        result->psr_Replied = left <= result->psr_Subscribers ? result->psr_Subscribers - left : 0;
    }

    /* 2. The freeze and the held clock. Nothing of a frozen task is touched
     * from here to the thaw: no DOS, no shared semaphore, no port of theirs,
     * except the port of a scheduled wake whose task is thawed to read it. */
    GetSysTime(&wall_before);
    GetUpTime(&up_before);
    kprintf("[POWER] freezing replied=%lu/%lu proceeded-at-ms=%lu\n",
        (unsigned long)result->psr_Replied, (unsigned long)result->psr_Subscribers,
        (unsigned long)result->psr_ProceededAtMs);
    frozen = FreezeTasks(0);
    if (frozen == FREEZE_UNSUPPORTED)
    {
        kprintf("[POWER] freeze unsupported on this build\n");
        error = POWER_ERROR_UNSUPPORTED;
        goto wake;
    }
    PauseUpTime(TRUE);
    result->psr_Frozen = frozen;

    /* 3. The sleep itself: a commanded hold on the wall clock, broken by the
     * scheduled wakes that fall in it. The hardware depths slot in around
     * each wait, between the hold and the release. */
    end = wall_before;
    end.tv_secs += seconds;
    for (;;)
    {
        struct timeval next;

        request->tr_node.io_Command = TR_ADDREQUEST;
        if (next_wake(PowerBase, grid, &next) && time_before(&next, &end))
        {
            request->tr_time = next;
            DoIO(&request->tr_node);
            awake_ms += run_burst(PowerBase, request, port, signal, grid, result);
            continue;
        }
        request->tr_time = end;
        DoIO(&request->tr_node);
        break;
    }

    /* 4. Release and thaw. */
    GetUpTime(&up_after);
    PauseUpTime(FALSE);
    thawed = ThawTasks(0);
    result->psr_Thawed = thawed;
    {
        ULONG moved = ms_between(&up_before, &up_after);
        result->psr_UptimeHeldMs = moved > awake_ms ? moved - awake_ms : 0;
    }
    GetSysTime(&wall_after);
    result->psr_WallSeconds = wall_after.tv_secs - wall_before.tv_secs;
    kprintf("[POWER] thawed=%lu frozen=%lu uptime-moved-ms=%lu wall-s=%lu wakes=%lu expired=%lu bursts=%lu\n",
        (unsigned long)thawed, (unsigned long)frozen, (unsigned long)result->psr_UptimeHeldMs,
        (unsigned long)result->psr_WallSeconds, (unsigned long)result->psr_Wakes,
        (unsigned long)result->psr_WakesExpired, (unsigned long)result->psr_WakeBursts);

wake:
    /* 5. Tell them it is over: every subscription sent this transition's
     * sleep message, replied or not, so a late replier finds the wake
     * behind the sleep message in its port. */
    power_CollectReplies(PowerBase);
    result->psr_Woken = send_event(PowerBase, POWER_EVENT_WAKE, depth, source,
        result->psr_WallSeconds, 0);
    kprintf("[POWER] wake source=%lu slept-s=%lu woken=%lu\n",
        (unsigned long)source, (unsigned long)result->psr_WallSeconds, (unsigned long)result->psr_Woken);

    if (signal >= 0)
    {
        Disable();
        PowerBase->pb_ReplyPort.mp_Flags = PA_IGNORE;
        PowerBase->pb_ReplyPort.mp_SigTask = NULL;
        Enable();
        /* A reply after the last Wait() left the bit set; clear it before
         * the bit goes back to the caller. */
        SetSignal(0, 1UL << signal);
        FreeSignal(signal);
    }
    CloseDevice(&request->tr_node);
    DeleteIORequest(&request->tr_node);
    DeleteMsgPort(port);
    PowerBase->pb_InTransition = 0;

    /* Wakes removed while the transition held them. */
    {
        struct PowerWake *wake_node, *next_node;
        POWER_LOCK(PowerBase);
        ForeachNodeSafe(&PowerBase->pb_Wakes, wake_node, next_node)
            if (wake_node->pw_Removed && wake_node->pw_Message == NULL)
            {
                Remove((struct Node *)wake_node);
                FreeVec(wake_node);
            }
        POWER_UNLOCK(PowerBase);
    }
    return error;
}
