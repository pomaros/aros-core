/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 * Apple cached battery adapter. No SMC request executes in this process.
 */
#include "battery.h"
#include "consumers.h"
#include "subscriptions.h"
#include <resources/task.h>
#include <proto/task.h>
#include "source.h"
#include <aros/apple/startup.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/kernel.h>
#include <proto/rexxsyslib.h>
#include <proto/timer.h>
#include <stdio.h>
#include <string.h>
APTR KernelBase;
APTR TaskResBase;
struct RxsLib *RexxSysBase;
struct Device *TimerBase;
static ULONG now_ms(void)
{
    struct EClockVal v;
    ULONG f=ReadEClock(&v);
    uint64_t ticks=((uint64_t)v.ev_hi<<32)|v.ev_lo;
    return f ? (ULONG)((ticks/f)*1000+(ticks%f)*1000/f) : 0;
}
#define SUBSCRIBERS IND_SUBSCRIBERS
static void result(APTR owner, BOOL setting, BOOL success, CONST_STRPTR text, APTR context)
{
    struct IndicatorSubscriber *subs=context;
    (void)setting; (void)text;
    for (unsigned i=0;i<SUBSCRIBERS;++i)
        if (owner==&subs[i] && !success && subs[i].active) subs[i].dirty=TRUE;
}
static BOOL snapshot(char *event, size_t size)
{
    const volatile struct AROSAppleBatteryInfo *published=(APTR)KrnGetSystemAttr(AROS_APPLE_KATTR_BATTERY);
    struct AROSAppleBatteryInfo sample;
    uint64_t counter;
    if (!published || published==(APTR)-1 || !AROSAppleBatteryRead(published,&sample)) return FALSE;
    __asm__ volatile("mrs %0, cntvct_el0":"=r"(counter));
    if (!(sample.flags&AROS_APPLE_BATTERY_PRESENT) || !sample.counter_frequency ||
        counter<sample.sample_counter || (counter-sample.sample_counter)/sample.counter_frequency>=10) return FALSE;
    {
        struct IndicatorBattery b={sample.charge_percent,
            !!(sample.flags&AROS_APPLE_BATTERY_ADAPTER),!!(sample.flags&AROS_APPLE_BATTERY_CHARGING),
            !!(sample.flags&AROS_APPLE_BATTERY_CAPACITY),!!(sample.flags&AROS_APPLE_BATTERY_CURRENT),
            sample.remaining_mah,sample.current_ma};
        return IndicatorBatteryEvent(&b,event,size);
    }
}
static void answer(struct RexxMsg *m, LONG error, const char *text)
{
    m->rm_Result1=error; m->rm_Result2=0;
    if (!error && (m->rm_Action&RXFF_RESULT)) {
        m->rm_Result2=(IPTR)CreateArgstring((CONST_STRPTR)text,strlen(text));
        if (!m->rm_Result2) m->rm_Result1=10;
    }
    ReplyMsg((struct Message *)m);
}
int main(void)
{
    struct TileSources requests={0};
    struct IndicatorSubscriber subs[SUBSCRIBERS]={0};
    struct MsgPort *clock=NULL,*commands=NULL;
    struct timerequest *timer=NULL;
    BOOL deviceOpen=FALSE,registered=FALSE,stopping=FALSE,available=FALSE,events=TRUE;
    ULONG last=0,lastCpu=0;
    unsigned cpuOwner=0;
    int exitCode=RETURN_FAIL;
    char event[192]="",command[256];
    TaskResBase=OpenResource((CONST_STRPTR)"task.resource");
    KernelBase=OpenResource((CONST_STRPTR)"kernel.resource");
    RexxSysBase=(struct RxsLib *)OpenLibrary((CONST_STRPTR)"rexxsyslib.library",0);
    if (!KernelBase || !RexxSysBase || !TileSourcesInit(&requests)) goto cleanup;
    clock=CreateMsgPort(); commands=CreateMsgPort(); if (!clock || !commands) goto cleanup;
    timer=(struct timerequest *)CreateIORequest(clock,sizeof(*timer));
    if (!timer || OpenDevice((CONST_STRPTR)TIMERNAME,UNIT_MICROHZ,(struct IORequest *)timer,0)) goto cleanup;
    deviceOpen=TRUE; TimerBase=timer->tr_node.io_Device;
    commands->mp_Node.ln_Name="INDICATOR.SOURCE.battery";
    Forbid();
    if (FindPort((CONST_STRPTR)commands->mp_Node.ln_Name)) { Permit(); goto cleanup; }
    AddPort(commands); registered=TRUE; Permit();
    while (!stopping || !TileSourcesIdle(&requests)) {
        ULONG now=now_ms(),signals;
        struct RexxMsg *m;
        unsigned budget=32;
        TileSourcesCollect(&requests,result,subs); TileSourcesExpire(&requests,now,result,subs);
        if (!stopping && (ULONG)(now-last)>=250) {
            char next[192];
            BOOL fresh=snapshot(next,sizeof(next));
            last=now;
            if (fresh) {
                if (!available || strcmp(event,next))
                    for (unsigned i=0;i<SUBSCRIBERS;++i) if (subs[i].active) subs[i].dirty=TRUE;
                strcpy(event,next);
            }
            available=fresh;
        }
        while (budget-- && (m=(struct RexxMsg *)GetMsg(commands))) {
            const char *p,*reply="Accepted";
            LONG error=0;
            if (!IsRexxMsg(m) || !m->rm_Args[0]) { ReplyMsg((struct Message *)m); continue; }
            p=(const char *)m->rm_Args[0];
            if (stopping) error=10;
            else if (!strcmp(p,"QUIT")) stopping=TRUE;
            else if (!strcmp(p,"GET")) { if (available) reply=event; else error=10; }
            else if (!strcmp(p,"STATUS")) {
                unsigned active=0;
                for (unsigned i=0;i<SUBSCRIBERS;++i) active+=!!subs[i].active;
                snprintf(command,sizeof(command),"available=%u events=%u subscribers=%u",(unsigned)available,(unsigned)events,active);
                reply=command;
            } else if (!strcmp(p,"EVENTS off")) events=FALSE;
            else if (!strcmp(p,"EVENTS on")) {
                events=TRUE;
                for (unsigned i=0;i<SUBSCRIBERS;++i) if (subs[i].active) subs[i].dirty=TRUE;
            } else if (!strncmp(p,"SUBSCRIBE ",10)) {
                unsigned pending=0;
                for (unsigned i=0;i<SUBSCRIBERS;++i)
                    if (TileSourcePending(&requests,&subs[i])) pending|=1U<<i;
                if (IndicatorSubscribe(subs,p+10,pending)<0) error=10;
            } else if (!strncmp(p,"UNSUBSCRIBE ",12)) {
                if (!IndicatorUnsubscribe(subs,p+12)) error=10;
            } else error=10;
            answer(m,error,reply);
        }
        if (commands->mp_MsgList.lh_Head->ln_Succ) Signal(FindTask(NULL),1UL<<commands->mp_SigBit);
        if (!stopping && available && events) for (unsigned i=0;i<SUBSCRIBERS;++i) {
            struct IndicatorSubscriber *sub=&subs[i];
            if (!IndicatorSubscriptionDue(sub,now,TileSourcePending(&requests,sub))) continue;
            snprintf(command,sizeof(command),"UPDATE %s",event);
            sub->sent=now; sub->tried=TRUE;
            if (TileSourceSend(&requests,sub,FALSE,(CONST_STRPTR)sub->port,(CONST_STRPTR)command,now)) {
                sub->dirty=FALSE; sub->failures=0;
            } else IndicatorSubscriptionMissing(sub);
        }
        if (!stopping && (ULONG)(now-lastCpu)>=2000 && !TileSourcePending(&requests,&cpuOwner)) {
            struct IndicatorConsumers cpu={0};
            char text[192];
            lastCpu=now;
            if (IndicatorConsumersRead(&cpu) && IndicatorConsumersFormat(&cpu,text,sizeof(text))) {
                /* This Apple kernel has no per-task usage publisher yet. Do
                 * not call its all-zero placeholders a measured idle sample. */
                if (!cpu.top[0].usage) snprintf(text,sizeof(text),"Per-task CPU accounting unavailable (Apple kernel)");
                snprintf(command,sizeof(command),"TOP %s",text);
                TileSourceSend(&requests,&cpuOwner,FALSE,(CONST_STRPTR)"INDICATOR.battery",(CONST_STRPTR)command,now);
            }
        }
        if (stopping && TileSourcesIdle(&requests)) break;
        timer->tr_node.io_Command=TR_ADDREQUEST;
        timer->tr_time.tv_secs=0; timer->tr_time.tv_micro=250000;
        SendIO((struct IORequest *)timer);
        signals=Wait(1UL<<clock->mp_SigBit|1UL<<commands->mp_SigBit|1UL<<requests.replies->mp_SigBit|SIGBREAKF_CTRL_C);
        if (!CheckIO((struct IORequest *)timer)) AbortIO((struct IORequest *)timer);
        WaitIO((struct IORequest *)timer); SetSignal(0,1UL<<clock->mp_SigBit);
        if (signals&SIGBREAKF_CTRL_C) stopping=TRUE;
    }
    exitCode=RETURN_OK;
cleanup:
    if (registered) RemPort(commands);
    if (commands) { struct RexxMsg *m; while ((m=(struct RexxMsg *)GetMsg(commands))) {
        if (IsRexxMsg(m)) answer(m,10,""); else ReplyMsg((struct Message *)m);
    } }
    if (deviceOpen) CloseDevice((struct IORequest *)timer);
    if (timer) DeleteIORequest((struct IORequest *)timer);
    if (clock) DeleteMsgPort(clock);
    if (commands) DeleteMsgPort(commands);
    if (requests.replies) TileSourcesClose(&requests);
    if (RexxSysBase) CloseLibrary((struct Library *)RexxSysBase);
    return exitCode;
}
