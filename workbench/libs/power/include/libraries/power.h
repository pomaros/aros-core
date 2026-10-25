#ifndef LIBRARIES_POWER_H
#define LIBRARIES_POWER_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    power.library: what a program sees of a sleep transition. A program
    that wants to know subscribes a port once and receives a PowerMessage
    before the freeze (with a deadline to reply) and one after the thaw;
    a program that never subscribes is frozen and thawed unchanged.
*/

#include <exec/types.h>
#include <exec/ports.h>
#include <utility/tagitem.h>

/* Depths of sleep. */
#define POWER_DEPTH_LIGHT           1
#define POWER_DEPTH_DEEP            2
#define POWER_DEPTH_HIBERNATE       3

/* Events. */
#define POWER_EVENT_SLEEP           1   /* before the freeze; reply within pm_Deadline  */
#define POWER_EVENT_WAKE            2   /* after the thaw; reply when convenient         */
#define POWER_EVENT_SOURCE          3   /* mains or battery changed, or the charge moved */
#define POWER_EVENT_SCHEDULED       4   /* AddPowerWakeA() date: run, then reply          */

/* Wake sources. */
#define POWER_SOURCE_UNKNOWN        0
#define POWER_SOURCE_TIMER          1
#define POWER_SOURCE_LID            2
#define POWER_SOURCE_KEY            3
#define POWER_SOURCE_CHARGER        4
#define POWER_SOURCE_NETWORK        5
#define POWER_SOURCE_COMMAND        6   /* a commanded transition (C:Sleep FOR=)         */

/* The message. The library owns it: reply it, never free it. */
struct PowerMessage
{
    struct Message  pm_Message;
    ULONG           pm_Event;           /* POWER_EVENT_*                                 */
    ULONG           pm_Depth;           /* POWER_DEPTH_*                                 */
    ULONG           pm_Source;          /* POWER_SOURCE_*, wake only                     */
    ULONG           pm_Slept;           /* seconds of wall clock slept, wake only        */
    ULONG           pm_Deadline;        /* milliseconds to reply: sleep, and the budget of a scheduled wake */
    APTR            pm_Subscription;    /* the handle AddPowerNotifyA() returned         */
    ULONG           pm_PowerFlags;      /* POWERSTF_*, source events                     */
    ULONG           pm_Percent;         /* charge, source events                         */
    APTR            pm_Wake;            /* the handle AddPowerWakeA() returned, scheduled */
};

/* What the machine runs on. */
struct PowerState
{
    ULONG   pst_Flags;          /* POWERSTF_*                                          */
    ULONG   pst_Percent;        /* charge 0..100, with POWERSTF_BATTERY                */
    ULONG   pst_MinutesLeft;    /* to empty while discharging, to full while charging; 0 unknown */
    ULONG   pst_AgeMs;          /* age of the provider's sample                        */
    LONG    pst_CurrentMa;      /* negative while discharging, with POWERSTF_CURRENT   */
    ULONG   pst_RemainingMah;
    ULONG   pst_FullMah;
};

#define POWERSTF_KNOWN              0x01    /* a provider answered; nothing else is valid without it */
#define POWERSTF_BATTERY            0x02    /* a battery is present                    */
#define POWERSTF_MAINS              0x04    /* the adapter is connected                */
#define POWERSTF_CHARGING           0x08
#define POWERSTF_CURRENT            0x10    /* pst_CurrentMa is valid                  */
#define POWERSTF_CAPACITY           0x20    /* the mAh fields are valid                */
#define POWERSTF_STALE              0x40    /* the sample is older than ten seconds    */

/* Scheduled wakes. */
#define POWER_WAKE_PENDING          0
#define POWER_WAKE_RAN              1       /* the requester replied within its budget */
#define POWER_WAKE_EXPIRED          2       /* the budget ran out; the requester was put back to sleep */

#define POWER_COALESCE_BATTERY      0       /* on the grid when on battery or unknown, exact on mains */
#define POWER_COALESCE_ALWAYS       1
#define POWER_COALESCE_NEVER        2

#define POWER_DEFAULT_BUDGET_MS     30000
#define POWER_DEFAULT_GRID_S        900

/* Deadlines: the transition proceeds at the reply or at the deadline. */
#define POWER_DEADLINE_LIGHT_MS     2000
#define POWER_DEADLINE_DEEP_MS      5000

/* Results. */
#define POWER_OK                    0
#define POWER_ERROR_MEMORY          1
#define POWER_ERROR_BUSY            2   /* a transition is in progress                   */
#define POWER_ERROR_UNSUPPORTED     3   /* this build cannot freeze tasks (SMP)          */
#define POWER_ERROR_DEVICE          4   /* timer.device unavailable                      */
#define POWER_ERROR_ARGUMENT        5
#define POWER_ERROR_UNAVAILABLE     6   /* no provider: the machine does not report its power */

/* Tags. */
#define POWER_TAGBASE               (TAG_USER | 0x00500000)
#define POWER_Events                (POWER_TAGBASE + 1)  /* AddPowerNotifyA: mask of 1<<POWER_EVENT_*; default all */
#define POWER_Seconds               (POWER_TAGBASE + 2)  /* PowerSleepA: hold the commanded sleep this long */
#define POWER_Result                (POWER_TAGBASE + 3)  /* PowerSleepA: struct PowerSleepResult * to fill  */
#define POWER_Source                (POWER_TAGBASE + 4)  /* PowerSleepA: the wake source to report          */
#define POWER_Budget                (POWER_TAGBASE + 5)  /* AddPowerWakeA: ms the requester may run, default 30000 */
#define POWER_WakeWith              (POWER_TAGBASE + 6)  /* AddPowerWakeA: struct Task ** NULL-terminated, thawed with it */
#define POWER_Coalesce              (POWER_TAGBASE + 7)  /* PowerSleepA: POWER_COALESCE_*            */
#define POWER_GridSeconds           (POWER_TAGBASE + 8)  /* PowerSleepA: the coalescing grid, default 900 */
#define POWER_WakeHook              (POWER_TAGBASE + 9)  /* PowerSleepA: struct Hook * asked during the hold;
                                                            it answers a POWER_SOURCE_* to wake, 0 to sleep on */
#define POWER_CheckMs               (POWER_TAGBASE + 10) /* PowerSleepA: how often the hook is asked, default 1000 */

/* What a transition did, for the caller's one line. */
struct PowerSleepResult
{
    ULONG   psr_Depth;
    ULONG   psr_Subscribers;        /* subscriptions that were sent POWER_EVENT_SLEEP   */
    ULONG   psr_Replied;            /* of those, replied before the deadline            */
    ULONG   psr_DeadlineMs;         /* the deadline used                                */
    ULONG   psr_ProceededAtMs;      /* milliseconds after the first send the freeze began */
    ULONG   psr_Frozen;
    ULONG   psr_Thawed;
    ULONG   psr_UptimeHeldMs;       /* up time that passed during the sleep (0 is held) */
    ULONG   psr_WallSeconds;        /* wall clock slept                                 */
    ULONG   psr_Woken;              /* subscriptions sent POWER_EVENT_WAKE              */
    ULONG   psr_Wakes;              /* scheduled wakes run during the sleep             */
    ULONG   psr_WakesExpired;       /* of those, put back to sleep at their budget      */
    ULONG   psr_WakeBursts;         /* times the machine woke for them (coalesced wakes share one) */
    ULONG   psr_WokenBy;            /* the source the wake hook answered, 0 when the hold ran out */
};

#endif /* LIBRARIES_POWER_H */
