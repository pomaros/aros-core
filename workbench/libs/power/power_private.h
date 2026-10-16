#ifndef POWER_PRIVATE_H
#define POWER_PRIVATE_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <exec/libraries.h>
#include <exec/lists.h>
#include <exec/semaphores.h>
#include <exec/ports.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>
#include <devices/timer.h>
#include <libraries/power.h>

#define __UTILITY_NOLIBBASE__
#define __TIMER_NOLIBBASE__
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/timer.h>

/* One subscription: a port, the task that owns it (the subscription dies
 * with the task), the events wanted, and at most one sleep and one wake
 * message out. The library never frees a message a subscriber has not
 * replied. A subscriber that replies its sleep message late still finds the
 * wake behind it in its port, in order; one that never replies holds one
 * sleep message, and is not sent another until it does. */
struct PowerSubscription
{
    struct MinNode          ps_Node;
    struct MsgPort         *ps_Port;
    struct Task            *ps_Task;
    ULONG                   ps_Events;
    struct PowerMessage    *ps_Sleep;       /* sleep message out, or NULL           */
    struct PowerMessage    *ps_Wake;        /* wake message out, or NULL            */
    struct PowerMessage    *ps_SourceMsg;   /* power-source message out, or NULL    */
    ULONG                   ps_SentGeneration; /* transition that sent ps_Sleep    */
    BOOL                    ps_Removed;     /* task gone or RemPowerNotify(): freed when both slots are empty */
};

/* One AddPowerWakeA() request: run task (and the tasks named with it) for
 * at most budget milliseconds at date while the machine sleeps. */
#define POWER_WAKE_WITH_MAX 8
struct PowerWake
{
    struct MinNode          pw_Node;
    struct Task            *pw_Task;
    struct MsgPort         *pw_Port;
    struct timeval          pw_Date;
    ULONG                   pw_BudgetMs;
    struct Task            *pw_With[POWER_WAKE_WITH_MAX];
    ULONG                   pw_WithCount;
    ULONG                   pw_Status;      /* POWER_WAKE_*                          */
    struct PowerMessage    *pw_Message;     /* scheduled message out, or NULL        */
    BOOL                    pw_Removed;     /* RemPowerWake() while the message is out */
};

/* The library's message: the public part first, then what it needs to
 * account the reply to the right transition. */
struct PowerMessageInternal
{
    struct PowerMessage     pmi_Public;
    ULONG                   pmi_Generation;
};

struct PowerBase
{
    struct Library          pb_Library;
    /* The lists below are guarded by POWER_LOCK. On a uniprocessor build
     * it is a Forbid(): every section is short and never waits (list
     * operations, AllocVec, FreeVec, PutMsg), and a frozen task cannot hold
     * a Forbid, so the transition may take it between the freeze and the
     * thaw, where a semaphore could be held by a task the freeze stopped.
     * On an SMP build Forbid() holds only the calling CPU, so the lock is
     * the semaphore pb_Lock; the freeze is refused there, so nothing can
     * stop its holder. */
#if defined(__AROSEXEC_SMP__)
    struct SignalSemaphore  pb_Lock;
#endif
    struct MinList          pb_Subscriptions;
    struct MsgPort          pb_ReplyPort;   /* PA_IGNORE: replies collected at each use   */
    struct Hook             pb_TaskHook;    /* task.resource notify hook                  */
    APTR                    pb_TaskResBase;
    struct Library         *pb_UtilityBase;
    volatile ULONG          pb_InTransition;
    ULONG                   pb_Generation;  /* incremented by every transition      */
    struct MinList          pb_Wakes;       /* struct PowerWake, under POWER_LOCK       */
    struct Task            *pb_Watcher;     /* the source watcher task, once started */
    volatile ULONG          pb_WatcherState;/* 0 none, 1 starting or running, 2 failed */
    struct PowerState       pb_LastState;   /* what the watcher last announced       */
    APTR                    pb_Provider;    /* the platform provider's handle        */
};

#define UtilityBase (PowerBase->pb_UtilityBase)

#if defined(__AROSEXEC_SMP__)
#define POWER_LOCK(base)    ObtainSemaphore(&(base)->pb_Lock)
#define POWER_UNLOCK(base)  ReleaseSemaphore(&(base)->pb_Lock)
#else
#define POWER_LOCK(base)    Forbid()
#define POWER_UNLOCK(base)  Permit()
#endif

/* power_provider.c, one per platform: fills state from the platform's
 * single source; FALSE with no provider. Never a second hardware client. */
BOOL power_ProviderInit(struct PowerBase *PowerBase);
BOOL power_ProviderRead(struct PowerBase *PowerBase, struct PowerState *state);

/* power_source.c */
void power_StartWatcher(struct PowerBase *PowerBase);
BOOL power_ReadState(struct PowerBase *PowerBase, struct PowerState *state);
ULONG power_SendSource(struct PowerBase *PowerBase, const struct PowerState *state);

/* power_transition.c */
struct PowerMessage *power_NewMessage(struct PowerBase *PowerBase, ULONG event);
void power_CollectReplies(struct PowerBase *PowerBase);
void power_TaskRemoved(struct PowerBase *PowerBase, struct Task *task);
LONG power_Transition(struct PowerBase *PowerBase, ULONG depth, ULONG seconds, ULONG source,
                      ULONG coalesce, ULONG grid_seconds, struct PowerSleepResult *result);

#endif /* POWER_PRIVATE_H */
