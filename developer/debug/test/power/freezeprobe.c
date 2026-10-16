/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    PWRPROBE: [PWR7] in the guest, the freeze, the thaw and the two clocks.
    Started by the cold-start proof when PWRTEST.REQ is on SYS:. It starts
    two child processes (a counter that ticks every 100 ms, and a sleeper on
    a dated request that completes during the sleep), posts a 15 s relative
    request and a dated request 3 s ahead on its own ports, then runs
    C:Sleep FOR=6 in this process. Afterwards: the counter did not advance,
    the dated requests completed, the relative request completes 15 s of
    awake time after it was posted, and both children run again.
    Returns 76 on success, a distinct code for each failing step.
*/
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <exec/execbase.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <libraries/power.h>
#include <proto/power.h>
#include <exec/resident.h>
#include <workbench/startup.h>
#include <aros/debug.h>

int __nowbsupport = 1;
int __nostdiowin = 1;
struct WBStartup *WBenchMsg;

#define SLEEP_SECONDS     6
#define RELATIVE_SECONDS  15
#define DATED_SECONDS     3

struct Device *TimerBase;
struct Library *PowerBase;

static volatile ULONG counter_ticks;
static volatile ULONG counter_running;
static volatile ULONG sleeper_done;
static volatile ULONG sleeper_up_secs;
static volatile ULONG sleeper_exited;

static LONG open_timer(struct MsgPort **port, struct timerequest **request, ULONG unit)
{
    *port = CreateMsgPort();
    if (*port == NULL)
        return 1;
    *request = (struct timerequest *)CreateIORequest(*port, sizeof(struct timerequest));
    if (*request == NULL)
        return 2;
    if (OpenDevice("timer.device", unit, &(*request)->tr_node, 0) != 0) {
        (*request)->tr_node.io_Device = NULL;
        return 3;
    }
    return 0;
}

static void close_timer(struct MsgPort *port, struct timerequest *request)
{
    if (request != NULL) {
        if (request->tr_node.io_Device != NULL)
            CloseDevice(&request->tr_node);
        DeleteIORequest(&request->tr_node);
    }
    if (port != NULL)
        DeleteMsgPort(port);
}

/* Child 1: counts awake time in 100 ms steps through DOS Delay(), which is
 * a relative UNIT_VBLANK request. */
static void counter_entry(void)
{
    counter_running = 1;
    while (counter_running) {
        Delay(5);
        counter_ticks++;
    }
}

/* Child 2: waits on a dated request that falls inside the sleep, so it is
 * frozen in Wait() with its reply arriving meanwhile, and records the up
 * time at which it ran again. */
static void sleeper_entry(void)
{
    struct MsgPort *port = NULL;
    struct timerequest *request = NULL;
    struct timeval now;

    if (open_timer(&port, &request, UNIT_WAITUNTIL) == 0) {
        struct Device *tb = request->tr_node.io_Device;
        TimerBase = tb;
        GetSysTime(&now);
        request->tr_node.io_Command = TR_ADDREQUEST;
        request->tr_time = now;
        request->tr_time.tv_secs += DATED_SECONDS;
        DoIO(&request->tr_node);
        GetUpTime(&now);
        sleeper_up_secs = now.tv_secs;
    }
    sleeper_done = 1;
    /* Hold the device open until the probe is finished: no CloseDevice,
     * no new request, nothing that could re-arm the timer behind the
     * probe's back while it waits on the last relative request. */
    Wait(SIGBREAKF_CTRL_C);
    close_timer(port, request);
    sleeper_exited = 1;
}

static LONG up_ms(struct timeval *a, struct timeval *b)
{
    /* b - a in milliseconds */
    LONG secs = (LONG)b->tv_secs - (LONG)a->tv_secs;
    LONG micro = (LONG)b->tv_micro - (LONG)a->tv_micro;
    return secs * 1000 + micro / 1000;
}

/* ---- [PWR8]: the messages ------------------------------------------------ */

/* The bundle may carry no library loader: initialise the resident from
 * LIBS: when OpenLibrary() cannot find it, as SETPROBE does. */
static struct Library *open_power(void)
{
    struct Library *base = OpenLibrary("power.library", 1);
    BPTR seglist, seg;
    const int resident_size = offsetof(struct Resident, rt_Init) + sizeof(APTR);

    if (base != NULL)
        return base;
    seglist = LoadSeg("LIBS:power.library");
    if (seglist == BNULL)
        return NULL;
    for (seg = seglist; seg != BNULL; seg = *(BPTR *)BADDR(seg)) {
        STRPTR addr = (STRPTR)((IPTR)BADDR(seg) - sizeof(ULONG));
        ULONG size = *(ULONG *)addr;
        for (addr += sizeof(BPTR) + sizeof(ULONG), size -= sizeof(BPTR) + sizeof(ULONG);
             size >= (ULONG)resident_size; size -= 2, addr += 2) {
            struct Resident *res = (struct Resident *)addr;
            if (res->rt_MatchWord == RTC_MATCHWORD && res->rt_MatchTag == res) {
                Forbid();
                InitResident(res, seglist);
                Permit();
                return OpenLibrary("power.library", 1);
            }
        }
    }
    return NULL;
}

/* One record per subscriber child; written by the child, read by the probe. */
struct Subscriber
{
    volatile ULONG ready, done;
    volatile ULONG sleeps, wakes, slept, source, depth, deadline;
};
static struct Subscriber sub_now, sub_late, sub_never, sub_gone;

/* mode 0: reply at once; 1: reply after one second; 2: keep the sleep
 * message until CTRL_C; 3: subscribe and exit holding the port. */
static void subscriber(struct Subscriber *me, int mode)
{
    struct MsgPort *port = CreateMsgPort();
    APTR handle = port ? AddPowerNotifyA(port, NULL) : NULL;
    struct PowerMessage *held = NULL;

    if (handle == NULL) {
        me->done = 1;
        return;
    }
    me->ready = 1;
    if (mode == 3) {
        /* Exit with the subscription and the port: the task hook ends it. */
        me->done = 1;
        return;
    }
    for (;;) {
        struct PowerMessage *message;
        ULONG got = Wait((1UL << port->mp_SigBit) | SIGBREAKF_CTRL_C);

        while ((message = (struct PowerMessage *)GetMsg(port)) != NULL) {
            if (message->pm_Event == POWER_EVENT_SLEEP) {
                me->sleeps++;
                me->depth = message->pm_Depth;
                me->deadline = message->pm_Deadline;
                if (mode == 1)
                    Delay(50);
                if (mode == 2) {
                    held = message;
                    continue;
                }
                ReplyMsg(&message->pm_Message);
            } else {
                if (message->pm_Event == POWER_EVENT_WAKE) {
                    me->wakes++;
                    me->slept = message->pm_Slept;
                    me->source = message->pm_Source;
                }
                ReplyMsg(&message->pm_Message);
            }
        }
        if ((got & SIGBREAKF_CTRL_C) || (mode != 2 && me->wakes > 0))
            break;
    }
    if (held != NULL)
        ReplyMsg(&held->pm_Message);
    RemPowerNotify(handle);
    DeleteMsgPort(port);
    me->done = 1;
}

static void sub_now_entry(void) { subscriber(&sub_now, 0); }
static void sub_late_entry(void) { subscriber(&sub_late, 1); }
static void sub_never_entry(void) { subscriber(&sub_never, 2); }
static void sub_gone_entry(void) { subscriber(&sub_gone, 3); }

static int wait_flag(volatile ULONG *flag)
{
    int i;
    for (i = 0; i < 40 && !*flag; i++)
        Delay(5);
    return *flag != 0;
}

/* Returns 0 on success, a code from 40 on for each failing step. */
static int pwr8(void)
{
    struct Process *now, *late, *never, *gone;
    struct PowerSleepResult result, control;
    struct TagItem tags[3];
    LONG status;
    int i;

    PowerBase = open_power();
    if (PowerBase == NULL)
        return 40;

    gone = CreateNewProcTags(NP_Entry, (IPTR)sub_gone_entry, NP_Name, (IPTR)"PWRPROBE gone", TAG_DONE);
    if (gone == NULL || !wait_flag(&sub_gone.done))
        return 41;
    /* Let the exited subscriber's task be removed, and its hook run. */
    for (i = 0; i < 40 && FindTask("PWRPROBE gone") != NULL; i++)
        Delay(5);
    if (FindTask("PWRPROBE gone") != NULL)
        return 42;

    now = CreateNewProcTags(NP_Entry, (IPTR)sub_now_entry, NP_Name, (IPTR)"PWRPROBE now", TAG_DONE);
    late = CreateNewProcTags(NP_Entry, (IPTR)sub_late_entry, NP_Name, (IPTR)"PWRPROBE late", TAG_DONE);
    never = CreateNewProcTags(NP_Entry, (IPTR)sub_never_entry, NP_Name, (IPTR)"PWRPROBE never", TAG_DONE);
    if (now == NULL || late == NULL || never == NULL ||
        !wait_flag(&sub_now.ready) || !wait_flag(&sub_late.ready) || !wait_flag(&sub_never.ready))
        return 43;

    tags[0].ti_Tag = POWER_Seconds; tags[0].ti_Data = 3;
    tags[1].ti_Tag = POWER_Result;  tags[1].ti_Data = (IPTR)&result;
    tags[2].ti_Tag = TAG_DONE;
    status = PowerSleepA(POWER_DEPTH_LIGHT, tags);
    kprintf("[PWRPROBE] pwr8 status=%ld subscribers=%lu replied=%lu proceeded-at-ms=%lu frozen=%lu thawed=%lu woken=%lu wall-s=%lu\n",
        (long)status, (unsigned long)result.psr_Subscribers, (unsigned long)result.psr_Replied,
        (unsigned long)result.psr_ProceededAtMs, (unsigned long)result.psr_Frozen,
        (unsigned long)result.psr_Thawed, (unsigned long)result.psr_Woken, (unsigned long)result.psr_WallSeconds);
    if (status != POWER_OK)
        return 44;
    /* The exited subscriber received nothing and left no subscription. */
    if (result.psr_Subscribers != 3 || sub_gone.sleeps != 0)
        return 45;
    /* Two replied; the transition waited for the deadline, and no longer. */
    if (result.psr_Replied != 2 || result.psr_ProceededAtMs + 100 < POWER_DEADLINE_LIGHT_MS ||
        result.psr_ProceededAtMs > POWER_DEADLINE_LIGHT_MS + 100)
        return 46;
    if (result.psr_Frozen == 0 || result.psr_Frozen != result.psr_Thawed || result.psr_UptimeHeldMs >= 500)
        return 47;
    /* All three are woken, the holder too: it finds the wake message behind
     * the sleep message it still holds, so a late replier learns the sleep
     * ended. */
    if (!wait_flag(&sub_now.done) || !wait_flag(&sub_late.done) || !wait_flag(&sub_never.wakes))
        return 48;
    if (sub_now.sleeps != 1 || sub_late.sleeps != 1 || sub_never.sleeps != 1 ||
        sub_now.wakes != 1 || sub_late.wakes != 1 || sub_never.wakes != 1 || result.psr_Woken != 3)
        return 49;
    if (sub_now.depth != POWER_DEPTH_LIGHT || sub_now.deadline != POWER_DEADLINE_LIGHT_MS ||
        sub_now.source != POWER_SOURCE_COMMAND || sub_late.source != POWER_SOURCE_COMMAND)
        return 50;
    if (sub_now.slept + 1 < 3 || sub_now.slept > 4 || sub_late.slept + 1 < 3 || sub_late.slept > 4)
        return 51;
    kprintf("[PWR8] PASS messages model=vm rev=- boot=command depth=light subscribers=%lu sleep-delivered=3 "
        "wake-delivered=%lu replied=%lu deadline-ms=%lu proceeded-at-ms=%lu slept-s=%lu exited-subscriber=removed held=woken\n",
        (unsigned long)result.psr_Subscribers, (unsigned long)result.psr_Woken, (unsigned long)result.psr_Replied,
        (unsigned long)POWER_DEADLINE_LIGHT_MS, (unsigned long)result.psr_ProceededAtMs, (unsigned long)sub_now.slept);

    /* Stale holder: a second transition while the holder still keeps the
     * first one's sleep message. It is sent nothing new, and its old message
     * must not hold this transition up or skew its count. */
    {
        struct PowerSleepResult stale;
        tags[0].ti_Data = 1;
        tags[1].ti_Data = (IPTR)&stale;
        status = PowerSleepA(POWER_DEPTH_LIGHT, tags);
        kprintf("[PWRPROBE] stale status=%ld subscribers=%lu replied=%lu proceeded-at-ms=%lu woken=%lu\n",
            (long)status, (unsigned long)stale.psr_Subscribers, (unsigned long)stale.psr_Replied,
            (unsigned long)stale.psr_ProceededAtMs, (unsigned long)stale.psr_Woken);
        if (status != POWER_OK || stale.psr_Subscribers != 0 || stale.psr_Replied != 0 ||
            stale.psr_ProceededAtMs >= 100 || stale.psr_Woken != 0 || sub_never.wakes != 1)
            return 54;
        kprintf("[PWR8] (control) stale-holder subscribers=0 proceeded-at-ms=%lu replied=0\n",
            (unsigned long)stale.psr_ProceededAtMs);
    }

    /* Release the holder: it replies its sleep message and unsubscribes. */
    Signal(&never->pr_Task, SIGBREAKF_CTRL_C);
    if (!wait_flag(&sub_never.done))
        return 52;
    Delay(10);

    /* Control: nobody subscribed, the transition proceeds at once. */
    tags[0].ti_Data = 1;
    tags[1].ti_Data = (IPTR)&control;
    status = PowerSleepA(POWER_DEPTH_LIGHT, tags);
    if (status != POWER_OK || control.psr_Subscribers != 0 || control.psr_ProceededAtMs >= 100)
        return 53;
    kprintf("[PWR8] (control) subscribers=0 proceeded-at-ms=%lu\n", (unsigned long)control.psr_ProceededAtMs);
    return 0;
}

/* ---- [PWR9]: what runs during a scheduled wake --------------------------- */

struct WakeChild
{
    struct timeval date;
    ULONG budget, mode;                 /* mode 0: reply after half a second; 1: keep the message */
    volatile ULONG ready, got, done, status;
    volatile ULONG got_secs, got_micro;
};
static struct WakeChild wk_ok, wk_long, wk_c1, wk_c2;
static volatile ULONG bys_ticks, bys_run;

static void wake_child(struct WakeChild *me)
{
    struct MsgPort *port = CreateMsgPort();
    struct TagItem tags[2];
    struct PowerMessage *held = NULL;
    APTR handle;

    tags[0].ti_Tag = POWER_Budget; tags[0].ti_Data = me->budget;
    tags[1].ti_Tag = TAG_DONE;
    handle = port ? AddPowerWakeA(&me->date, port, tags) : NULL;
    if (handle == NULL) {
        me->done = 1;
        return;
    }
    me->ready = 1;
    for (;;) {
        struct PowerMessage *message;
        ULONG got = Wait((1UL << port->mp_SigBit) | SIGBREAKF_CTRL_C);

        while ((message = (struct PowerMessage *)GetMsg(port)) != NULL) {
            if (message->pm_Event == POWER_EVENT_SCHEDULED) {
                struct timeval now;
                GetSysTime(&now);
                me->got_secs = now.tv_secs;
                me->got_micro = now.tv_micro;
                me->got++;
                if (me->mode == 1) {
                    held = message;
                    continue;
                }
                Delay(25);
            }
            ReplyMsg(&message->pm_Message);
        }
        if (got & SIGBREAKF_CTRL_C)
            break;
    }
    if (held != NULL)
        ReplyMsg(&held->pm_Message);
    me->status = RemPowerWake(handle);
    DeleteMsgPort(port);
    me->done = 1;
}
static void wk_ok_entry(void) { wake_child(&wk_ok); }
static void wk_long_entry(void) { wake_child(&wk_long); }
static void wk_c1_entry(void) { wake_child(&wk_c1); }
static void wk_c2_entry(void) { wake_child(&wk_c2); }
static void bystander_entry(void)
{
    bys_run = 1;
    while (bys_run) {
        Delay(5);
        bys_ticks++;
    }
}

static struct Process *start_wake(struct WakeChild *c, void (*entry)(void), CONST_STRPTR name,
                                  ULONG at_secs, ULONG budget, ULONG mode)
{
    struct Process *p;
    c->date.tv_secs = at_secs;
    c->date.tv_micro = 0;
    c->budget = budget;
    c->mode = mode;
    p = CreateNewProcTags(NP_Entry, (IPTR)entry, NP_Name, (IPTR)name, TAG_DONE);
    if (p != NULL)
        wait_flag(&c->ready);
    return p;
}

static void stop_wake(struct Process *p, struct WakeChild *c)
{
    if (p != NULL && !c->done) {
        Signal(&p->pr_Task, SIGBREAKF_CTRL_C);
        wait_flag(&c->done);
    }
}

/* Returns 0 on success, a code from 60 on for each failing step. */
static int pwr9(void)
{
    struct PowerSleepResult r;
    struct TagItem tags[5];
    struct timeval now;
    struct Process *bys, *ok, *lng, *c1, *c2;
    ULONG before, after, base;
    LONG status;

    bys = CreateNewProcTags(NP_Entry, (IPTR)bystander_entry, NP_Name, (IPTR)"PWRPROBE bystander", TAG_DONE);
    if (bys == NULL || !wait_flag(&bys_run))
        return 60;

    /* A: exact dates. One requester replies within its budget, one runs past it. */
    GetSysTime(&now);
    ok = start_wake(&wk_ok, wk_ok_entry, "PWRPROBE wake ok", now.tv_secs + 2, 5000, 0);
    lng = start_wake(&wk_long, wk_long_entry, "PWRPROBE wake long", now.tv_secs + 4, 1500, 1);
    if (ok == NULL || lng == NULL || !wk_ok.ready || !wk_long.ready)
        return 61;
    Delay(10);
    tags[0].ti_Tag = POWER_Seconds;  tags[0].ti_Data = 9;
    tags[1].ti_Tag = POWER_Result;   tags[1].ti_Data = (IPTR)&r;
    tags[2].ti_Tag = POWER_Coalesce; tags[2].ti_Data = POWER_COALESCE_NEVER;
    tags[3].ti_Tag = TAG_DONE;
    before = bys_ticks;
    status = PowerSleepA(POWER_DEPTH_LIGHT, tags);
    after = bys_ticks;
    kprintf("[PWRPROBE] pwr9a status=%ld wakes=%lu expired=%lu bursts=%lu held-ms=%lu bystander %lu->%lu ok-got=%lu long-got=%lu\n",
        (long)status, (unsigned long)r.psr_Wakes, (unsigned long)r.psr_WakesExpired, (unsigned long)r.psr_WakeBursts,
        (unsigned long)r.psr_UptimeHeldMs, (unsigned long)before, (unsigned long)after,
        (unsigned long)wk_ok.got, (unsigned long)wk_long.got);
    if (status != POWER_OK || r.psr_Wakes != 2 || r.psr_WakesExpired != 1 || r.psr_WakeBursts != 2)
        return 62;
    if (wk_ok.got != 1 || wk_long.got != 1)
        return 63;
    /* Each ran at its date, not before, and within a second of it. */
    if (wk_ok.got_secs < wk_ok.date.tv_secs || wk_ok.got_secs > wk_ok.date.tv_secs + 1 ||
        wk_long.got_secs < wk_long.date.tv_secs || wk_long.got_secs > wk_long.date.tv_secs + 1)
        return 64;
    /* The bystander never ran during the sleep, wakes included. */
    if (after > before + 1)
        return 65;
    if (r.psr_UptimeHeldMs >= 500)
        return 66;
    stop_wake(ok, &wk_ok);
    stop_wake(lng, &wk_long);
    if (wk_ok.status != POWER_WAKE_RAN || wk_long.status != POWER_WAKE_EXPIRED)
        return 67;
    kprintf("[PWR9] PASS budget model=vm rev=- boot=command wakes=2 requester-ran=1 others-ran=0 budget-ms=1500 "
        "expired=1 bursts=2 held-ms=%lu\n", (unsigned long)r.psr_UptimeHeldMs);

    /* B: two wakes four seconds apart on a 10 s grid fire together. */
    GetSysTime(&now);
    base = (now.tv_secs / 10 + 2) * 10;
    c1 = start_wake(&wk_c1, wk_c1_entry, "PWRPROBE wake c1", base - 8, 3000, 0);
    c2 = start_wake(&wk_c2, wk_c2_entry, "PWRPROBE wake c2", base - 4, 3000, 0);
    if (c1 == NULL || c2 == NULL)
        return 68;
    tags[0].ti_Data = base - now.tv_secs + 2;
    tags[2].ti_Data = POWER_COALESCE_ALWAYS;
    tags[3].ti_Tag = POWER_GridSeconds; tags[3].ti_Data = 10;
    tags[4].ti_Tag = TAG_DONE;
    status = PowerSleepA(POWER_DEPTH_LIGHT, tags);
    kprintf("[PWRPROBE] pwr9b status=%ld wakes=%lu bursts=%lu base=%lu c1=%lu c2=%lu\n",
        (long)status, (unsigned long)r.psr_Wakes, (unsigned long)r.psr_WakeBursts, (unsigned long)base,
        (unsigned long)wk_c1.got_secs, (unsigned long)wk_c2.got_secs);
    if (status != POWER_OK || r.psr_Wakes != 2 || r.psr_WakeBursts != 1 ||
        wk_c1.got != 1 || wk_c2.got != 1 || wk_c1.got_secs < base || wk_c2.got_secs < base ||
        wk_c1.got_secs > base + 1 || wk_c2.got_secs > base + 1)
        return 69;
    stop_wake(c1, &wk_c1);
    stop_wake(c2, &wk_c2);
    kprintf("[PWR9] PASS coalesce model=vm grid-s=10 wakes=2 apart-s=4 bursts=1 fired-at=grid\n");

    /* Control: the same two wakes with no grid fire at their own dates. */
    memset(&wk_c1, 0, sizeof(wk_c1));
    memset(&wk_c2, 0, sizeof(wk_c2));
    GetSysTime(&now);
    base = now.tv_secs + 3;
    c1 = start_wake(&wk_c1, wk_c1_entry, "PWRPROBE wake c1", base, 3000, 0);
    c2 = start_wake(&wk_c2, wk_c2_entry, "PWRPROBE wake c2", base + 4, 3000, 0);
    if (c1 == NULL || c2 == NULL)
        return 70;
    tags[0].ti_Data = 10;
    tags[2].ti_Data = POWER_COALESCE_NEVER;
    status = PowerSleepA(POWER_DEPTH_LIGHT, tags);
    if (status != POWER_OK || r.psr_Wakes != 2 || r.psr_WakeBursts != 2 ||
        wk_c2.got_secs < wk_c1.got_secs + 3 || wk_c2.got_secs > wk_c1.got_secs + 5)
        return 71;
    stop_wake(c1, &wk_c1);
    stop_wake(c2, &wk_c2);
    kprintf("[PWR9] (control) never coalesced=0 bursts=2 apart-s=%lu\n",
        (unsigned long)(wk_c2.got_secs - wk_c1.got_secs));

    bys_run = 0;
    Delay(10);
    return 0;
}

/* ---- [PWR6] on a machine that reports no power ---------------------------- */
static int pwr6_none(void)
{
    struct PowerState state;
    struct MsgPort *port = CreateMsgPort();
    APTR handle;
    ULONG messages = 0;
    LONG status;
    struct Message *m;

    status = GetPowerStateA(&state, NULL);
    if (status == POWER_OK) {
        /* A machine that reports its power: this check is for one that
         * does not; the battery path is qualified on the laptop. */
        kprintf("[PWR6] (native) provider=present flags=0x%lx percent=%lu age-ms=%lu\n",
            (unsigned long)state.pst_Flags, (unsigned long)state.pst_Percent, (unsigned long)state.pst_AgeMs);
        if (port != NULL)
            DeleteMsgPort(port);
        return 0;
    }
    if (port == NULL)
        return 80;
    handle = AddPowerNotifyA(port, NULL);
    if (handle == NULL)
        return 81;
    Delay(150);
    while ((m = GetMsg(port)) != NULL) {
        messages++;
        ReplyMsg(m);
    }
    RemPowerNotify(handle);
    DeleteMsgPort(port);
    kprintf("[PWR6] (vm) provider=%s state=%s flags=0x%lx source-messages=%lu\n",
        status == POWER_OK ? "present" : "none", status == POWER_OK ? "known" : "unavailable",
        (unsigned long)state.pst_Flags, (unsigned long)messages);
    if (status != POWER_ERROR_UNAVAILABLE || state.pst_Flags != 0 || messages != 0)
        return 82;
    return 0;
}

int main(void)
{
    struct MsgPort *rel_port = NULL, *abs_port = NULL;
    struct timerequest *rel = NULL, *abs = NULL;
    struct Process *counter = NULL, *sleeper = NULL;
    struct timeval up_post, up_after, up_done, wall_post, wall_after;
    BPTR seglist;
    ULONG ticks_before, ticks_after, ticks_later, tasks;
    struct Task *t;
    LONG rc, skew_ms, i;
    static char sleep_args[] = "FOR=6 QUIET\n";

    if (open_timer(&rel_port, &rel, UNIT_MICROHZ) != 0)
        return 10;
    if (open_timer(&abs_port, &abs, UNIT_WAITUNTIL) != 0)
        return 11;
    TimerBase = rel->tr_node.io_Device;

    seglist = LoadSeg("C:Sleep");
    if (seglist == BNULL) {
        kprintf("[PWRPROBE] C:Sleep missing ioerr=%ld\n", (long)IoErr());
        return 12;
    }

    counter = CreateNewProcTags(NP_Entry, (IPTR)counter_entry,
        NP_Name, (IPTR)"PWRPROBE counter", NP_Priority, 0, TAG_DONE);
    sleeper = CreateNewProcTags(NP_Entry, (IPTR)sleeper_entry,
        NP_Name, (IPTR)"PWRPROBE sleeper", NP_Priority, 0, TAG_DONE);
    if (counter == NULL || sleeper == NULL)
        return 13;
    /* Let both children reach their waits. */
    for (i = 0; i < 20 && !counter_running; i++)
        Delay(5);
    Delay(10);

    /* The two requests of the probe itself. */
    GetSysTime(&wall_post);
    GetUpTime(&up_post);
    rel->tr_node.io_Command = TR_ADDREQUEST;
    rel->tr_time.tv_secs = RELATIVE_SECONDS;
    rel->tr_time.tv_micro = 0;
    SendIO(&rel->tr_node);
    abs->tr_node.io_Command = TR_ADDREQUEST;
    abs->tr_time = wall_post;
    abs->tr_time.tv_secs += DATED_SECONDS;
    SendIO(&abs->tr_node);

    /* Count the tasks the freeze will see (everything but this one). */
    tasks = 0;
    Disable();
    for (t = (struct Task *)SysBase->TaskReady.lh_Head; t->tc_Node.ln_Succ; t = (struct Task *)t->tc_Node.ln_Succ)
        tasks++;
    for (t = (struct Task *)SysBase->TaskWait.lh_Head; t->tc_Node.ln_Succ; t = (struct Task *)t->tc_Node.ln_Succ)
        tasks++;
    Enable();

    ticks_before = counter_ticks;
    {
        /* A deterministic input for C:Sleep's ReadArgs: RunCommand injects
         * the argument string into Input(), which the proof leaves unset. */
        BPTR nil_in = Open("NIL:", MODE_OLDFILE);
        BPTR old_in = SelectInput(nil_in);
        kprintf("[PWRPROBE] running C:Sleep tasks=%lu ticks=%lu\n", (unsigned long)tasks, (unsigned long)ticks_before);
        rc = RunCommand(seglist, AROS_STACKSIZE, sleep_args, sizeof(sleep_args) - 1);
        SelectInput(old_in);
        if (nil_in != BNULL)
            Close(nil_in);
    }
    ticks_after = counter_ticks;
    GetUpTime(&up_after);
    GetSysTime(&wall_after);
    UnLoadSeg(seglist);

    kprintf("[PWRPROBE] sleep rc=%ld ticks %lu->%lu wall-s=%lu up-ms=%ld\n",
        (long)rc, (unsigned long)ticks_before, (unsigned long)ticks_after,
        (unsigned long)(wall_after.tv_secs - wall_post.tv_secs),
        (long)up_ms(&up_post, &up_after));

    if (rc != RETURN_OK)
        return 20;
    /* One tick may have been in flight when the freeze began. */
    if (ticks_after > ticks_before + 1)
        return 21;
    if (wall_after.tv_secs - wall_post.tv_secs < SLEEP_SECONDS)
        return 22;
    /* The up time must have held for the sleep: well under the 6 s slept. */
    if (up_ms(&up_post, &up_after) > 2000)
        return 23;
    /* The dated request of the probe completed during the sleep. */
    if (!CheckIO(&abs->tr_node))
        return 24;
    WaitIO(&abs->tr_node);
    /* The relative request did not: 15 s of awake time have not passed. */
    if (CheckIO(&rel->tr_node))
        return 25;

    /*
     * The isolated case first: the relative request is the only timer
     * activity between the thaw and its completion. The counter child is
     * still inside its pending Delay(), the sleeper holds its device open
     * without a request, and this task posts nothing. If the resume did
     * not re-arm the deadline, this wait never ends and the run times out.
     */
    WaitIO(&rel->tr_node);
    GetUpTime(&up_done);
    skew_ms = up_ms(&up_post, &up_done) - RELATIVE_SECONDS * 1000;
    if (skew_ms < 0)
        skew_ms = -skew_ms;
    kprintf("[PWRPROBE] relative done up-ms=%ld skew-ms=%ld\n",
        (long)up_ms(&up_post, &up_done), (long)skew_ms);
    if (skew_ms >= 500)
        return 28;

    /* Both children run again: the counter ticks, the sleeper finished. */
    ticks_later = counter_ticks;
    if (ticks_later <= ticks_after)
        return 26;
    for (i = 0; i < 20 && !sleeper_done; i++)
        Delay(5);
    if (!sleeper_done)
        return 27;

    counter_running = 0;
    Signal(&sleeper->pr_Task, SIGBREAKF_CTRL_C);
    for (i = 0; i < 20 && !sleeper_exited; i++)
        Delay(5);
    Delay(10);

    kprintf("[PWR7] PASS freeze model=vm rev=- boot=command depth=light tasks=%lu "
        "relative-skew-ms=%ld absolute-fired=1 socket=n/a sleeper-resumed=1 counter-held=1 relative-isolated=1\n",
        (unsigned long)tasks, (long)skew_ms);
    rc = pwr8();
    if (rc != 0) {
        kprintf("[PWRPROBE] pwr8 failed code=%ld\n", (long)rc);
        return rc;
    }
    rc = pwr9();
    if (rc != 0) {
        kprintf("[PWRPROBE] pwr9 failed code=%ld\n", (long)rc);
        return rc;
    }
    rc = pwr6_none();
    if (rc != 0) {
        kprintf("[PWRPROBE] pwr6 failed code=%ld\n", (long)rc);
        return rc;
    }
    kprintf("[B2W] power lifecycle freeze PASS\n");

    close_timer(rel_port, rel);
    close_timer(abs_port, abs);
    return 76;
}
