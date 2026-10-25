/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Counter-backed timer.device initialization for Apple AArch64.
 */

#include <aros/kernel.h>
#include <aros/apple/platformtimer.h>
#include <aros/symbolsets.h>
#include <exec/execbase.h>
#include <exec/errors.h>
#include <exec/io.h>
#include <exec/interrupts.h>
#include <hardware/intbits.h>
#include <aros/asmcall.h>

#include <proto/exec.h>
#include <proto/kernel.h>

#include "timer_intern.h"
#include "timer_macros.h"

#define KernelBase LIBBASE->tb_KernelBase

static void clear_control(struct AROSAppleTimerControl *control)
{
    volatile UQUAD *word = (volatile UQUAD *)control;
    ULONG index;

    for (index = 0; index < sizeof(*control) / sizeof(*word); ++index)
        word[index] = 0;
}

static int list_head(
    struct MinList *list, struct timerequest **request)
{
    struct timerequest *candidate =
        (struct timerequest *)list->mlh_Head;

    if (candidate == NULL ||
        candidate->tr_node.io_Message.mn_Node.ln_Succ == NULL)
        return 0;
    *request = candidate;
    return 1;
}

static UQUAD deadline_from_time(
    struct TimerBase *TimerBase, UQUAD now_counter,
    struct timeval *now, struct timeval *wanted)
{
    struct timeval difference;
    UQUAD ticks;
    UQUAD micro_ticks;

    if (CMPTIME(now, wanted) <= 0)
        return now_counter == UINT64_MAX ? UINT64_MAX : now_counter + 1U;
    difference = *wanted;
    SUBTIME(&difference, now);
    micro_ticks = ((UQUAD)difference.tv_micro *
        TimerBase->tb_eclock_rate + 999999ULL) / 1000000ULL;
    if ((UQUAD)difference.tv_secs >
        (UINT64_MAX - micro_ticks) / TimerBase->tb_eclock_rate)
        ticks = UINT64_MAX;
    else
        ticks = (UQUAD)difference.tv_secs *
            TimerBase->tb_eclock_rate + micro_ticks;
    if (ticks == 0)
        ticks = 1;
    if (ticks > UINT64_MAX - now_counter)
        return UINT64_MAX;
    return now_counter + ticks;
}

static UQUAD next_deadline(
    struct TimerBase *TimerBase, UQUAD now_counter)
{
    struct timerequest *request;
    UQUAD selected = 0;
    UQUAD candidate;

    /* While the up time is paused the relative lists cannot complete, so
     * no deadline is armed for them: the wall-clock list alone wakes us. */
    if (!TimerBase->tb_ElapsedPaused &&
        list_head(&TimerBase->tb_Lists[TL_MICROHZ], &request)) {
        candidate = deadline_from_time(TimerBase, now_counter,
            &TimerBase->tb_Elapsed, &request->tr_time);
        selected = candidate;
    }
    if (!TimerBase->tb_ElapsedPaused &&
        list_head(&TimerBase->tb_Lists[TL_VBLANK], &request)) {
        candidate = deadline_from_time(TimerBase, now_counter,
            &TimerBase->tb_Elapsed, &request->tr_time);
        if (selected == 0 || candidate < selected)
            selected = candidate;
    }
    if (list_head(&TimerBase->tb_Lists[TL_WAITVBL], &request)) {
        candidate = deadline_from_time(TimerBase, now_counter,
            &TimerBase->tb_CurrentTime, &request->tr_time);
        if (selected == 0 || candidate < selected)
            selected = candidate;
    }
    if (TimerBase->tb_Platform.tb_VBlankTicks != 0 &&
        (selected == 0 || TimerBase->tb_Platform.tb_NextVBlank < selected))
        selected = TimerBase->tb_Platform.tb_NextVBlank;
    return selected;
}

static LONG timer_control(
    struct TimerBase *TimerBase, ULONG operation, UQUAD deadline)
{
    struct AROSAppleTimerControl control;

    clear_control(&control);
    control.version = AROS_APPLE_TIMER_CONTROL_VERSION;
    control.size = sizeof(control);
    control.operation = operation;
    control.owner = (UQUAD)(IPTR)TimerBase;
    control.deadline = deadline;
    return KrnPlatformTimerControl(&control);
}

BOOL AROSAppleTimerProgram(struct TimerBase *TimerBase)
{
    LONG result;
    if (!TimerBase || !TimerBase->tb_Platform.tb_DeadlineOwned)
        return FALSE;
    Disable();
#if defined(__AROSEXEC_SMP__)
    result = timer_control(TimerBase, AROS_APPLE_TIMER_OP_NOTIFY, 0);
#else
    AROSAppleTimerLock(TimerBase);
    UQUAD now = KrnPlatformTimerRead();
    AROSAppleEClockUpdateAt(TimerBase, now);
    UQUAD deadline = next_deadline(TimerBase, now);
    result = timer_control(TimerBase, deadline ? AROS_APPLE_TIMER_OP_ARM :
        AROS_APPLE_TIMER_OP_DISARM, deadline);
    AROSAppleTimerUnlock(TimerBase);
#endif
    Enable();
    return result == AROS_APPLE_TIMER_OK;
}

static void reply_completed(struct MinList *completed, struct ExecBase *SysBase)
{
    struct timerequest *request;
    while ((request = (struct timerequest *)RemHead((struct List *)completed)))
        ReplyMsg(&request->tr_node.io_Message);
}

void AROSAppleTimerFailQueues(struct TimerBase *TimerBase)
{
    struct MinList completed;
    struct timerequest *request;
    NEWLIST(&completed);
    AROSAppleTimerLock(TimerBase);
    TimerBase->tb_Platform.tb_DeadlineFault = 1;
    for (unsigned i = 0; i < NUM_LISTS; ++i) {
        while ((request = (struct timerequest *)RemHead((struct List *)&TimerBase->tb_Lists[i]))) {
            request->tr_node.io_Error = IOERR_ABORTED;
            request->tr_time.tv_secs = request->tr_time.tv_micro = 0;
            ADDTAIL(&completed, request);
        }
    }
    AROSAppleTimerUnlock(TimerBase);
    reply_completed(&completed, SysBase);
}

static UQUAD timer_deadline_callback(void *context, UQUAD now)
{
    struct TimerBase *TimerBase = context;
    struct MinList completed;
    struct timerequest *request;
    UQUAD deadline;
    BOOL vblank = FALSE;
    if (!TimerBase)
        return 0;
    NEWLIST(&completed);
    AROSAppleTimerLock(TimerBase);
    /* Another CPU may have advanced the clock since the interrupt sampled it. */
    now = KrnPlatformTimerRead();
    AROSAppleEClockUpdateAt(TimerBase, now);
    for (unsigned i = 0; i < NUM_LISTS; ++i) {
        struct timeval *time = i == TL_WAITVBL ?
            &TimerBase->tb_CurrentTime : &TimerBase->tb_Elapsed;
        while (list_head(&TimerBase->tb_Lists[i], &request) &&
               CMPTIME(time, &request->tr_time) <= 0) {
            REMOVE(request);
            request->tr_node.io_Error = 0;
            request->tr_time.tv_secs = request->tr_time.tv_micro = 0;
            ADDTAIL(&completed, request);
        }
    }
    if (TimerBase->tb_Platform.tb_VBlankTicks != 0 &&
        now >= TimerBase->tb_Platform.tb_NextVBlank) {
        UQUAD ticks = TimerBase->tb_Platform.tb_VBlankTicks;

        vblank = TRUE;
        TimerBase->tb_Platform.tb_NextVBlank += ticks;
        /* Missed ones are not made up: the next comes one period on. */
        if (TimerBase->tb_Platform.tb_NextVBlank <= now)
            TimerBase->tb_Platform.tb_NextVBlank = now + ticks;
    }
    deadline = TimerBase->tb_Platform.tb_DeadlineFault ? 0 : next_deadline(TimerBase, now);
    AROSAppleTimerUnlock(TimerBase);
    reply_completed(&completed, SysBase);
    if (vblank)
        vblank_Cause(SysBase);
    return deadline;
}

static int counter_Init(struct TimerBase *LIBBASE)
{
    struct KrnPlatformTimer *timer;

    if (KernelBase == NULL)
        return FALSE;
    timer = (struct KrnPlatformTimer *)KrnGetSystemAttr(KATTR_PlatformTimer);
    if (timer == NULL || timer == (APTR)-1 ||
        (timer->kpt_Flags & KPTF_COUNTER_READY) == 0 ||
        timer->kpt_Frequency == 0)
        return FALSE;

    LIBBASE->tb_Platform.tb_KPT = timer;
    LIBBASE->tb_Platform.tb_LastTime = KrnPlatformTimerRead();
    LIBBASE->tb_eclock_rate = timer->kpt_Frequency;
    SysBase->ex_EClockFrequency = timer->kpt_Frequency;
    /* Nominal software VBlank heartbeat; this is not a scanout timestamp. */
    if (SysBase->VBlankFrequency == 0)
        SysBase->VBlankFrequency = 60;
    LIBBASE->tb_Platform.tb_VBlankTicks = timer->kpt_Frequency / SysBase->VBlankFrequency;
    LIBBASE->tb_Platform.tb_NextVBlank = LIBBASE->tb_Platform.tb_LastTime +
        LIBBASE->tb_Platform.tb_VBlankTicks;
    if ((timer->kpt_Flags & KPTF_DEADLINE_READY) != 0) {
        struct AROSAppleTimerControl control;

        clear_control(&control);
        control.version = AROS_APPLE_TIMER_CONTROL_VERSION;
        control.size = sizeof(control);
        control.operation = AROS_APPLE_TIMER_OP_CLAIM;
        control.owner = (UQUAD)(IPTR)LIBBASE;
        control.callback = (UQUAD)(IPTR)timer_deadline_callback;
        if (KrnPlatformTimerControl(&control) != AROS_APPLE_TIMER_OK ||
            (control.flags & AROS_APPLE_TIMER_CONTROL_CLAIMED) == 0)
            return FALSE;
        LIBBASE->tb_Platform.tb_DeadlineOwned = 1;
        AROSAppleTimerProgram(LIBBASE);
    }
    return TRUE;
}

ADD2INITLIB(counter_Init, 0);

static int counter_Expunge(struct TimerBase *LIBBASE)
{
    if (LIBBASE->tb_Platform.tb_DeadlineOwned != 0) {
        if (timer_control(LIBBASE, AROS_APPLE_TIMER_OP_RELEASE, 0) !=
                AROS_APPLE_TIMER_OK)
            return FALSE;
        LIBBASE->tb_Platform.tb_DeadlineOwned = 0;
    }
    return TRUE;
}

ADD2EXPUNGELIB(counter_Expunge, 0);
