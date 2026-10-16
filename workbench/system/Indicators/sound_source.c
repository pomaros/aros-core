/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 * A2DP controller adapter. Device operations execute only on this provider
 * task, never the desktop task. Readback confirms controller state, not a
 * physical AVRCP acknowledgement from the headset.
 */
#include "state.h"
#include "source.h"
#include <devices/bta2dp.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/rexxsyslib.h>
#include <proto/timer.h>
#include <stdio.h>
#include <string.h>
struct RxsLib *RexxSysBase;
struct Device *TimerBase;
static ULONG now_ms(void)
{
    struct EClockVal v;
    ULONG f=ReadEClock(&v);
    uint64_t t=((uint64_t)v.ev_hi<<32)|v.ev_lo;
    return f?(ULONG)((t/f)*1000+(t%f)*1000/f):0;
}
static void outgoing(APTR owner, BOOL setting, BOOL success, CONST_STRPTR text, APTR context)
{
    (void)owner; (void)setting; (void)success; (void)text; (void)context;
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
/* QUERY distinguishes a live configured output from a remembered volume.
 * GETVOLUME is the device's shared controller state, not ENV or a new AHI
 * allocation. All I/O is completed before changing/reusing its request. */
static BOOL sample(struct IOStdReq *io, unsigned *level)
{
    io->io_Command=BTA2DPCMD_QUERY;
    if (DoIO((struct IORequest *)io) || !io->io_Actual) return FALSE;
    io->io_Command=BTA2DPCMD_GETVOLUME;
    if (DoIO((struct IORequest *)io) || io->io_Offset>100) return FALSE;
    *level=(unsigned)io->io_Offset;
    return TRUE;
}
int main(void)
{
    struct TileSources pushes={0};
    struct MsgPort *commands=NULL,*clock=NULL,*audioReplies=NULL;
    struct timerequest *timer=NULL;
    struct IOStdReq *audio=NULL;
    BOOL timerOpen=FALSE,audioOpen=FALSE,registered=FALSE,quit=FALSE,available=FALSE;
    ULONG last=0,lastOpen=0;
    unsigned level=0;
    int rc=RETURN_FAIL;
    RexxSysBase=(struct RxsLib *)OpenLibrary((CONST_STRPTR)"rexxsyslib.library",0);
    if (!RexxSysBase || !TileSourcesInit(&pushes)) goto cleanup;
    commands=CreateMsgPort(); clock=CreateMsgPort(); audioReplies=CreateMsgPort();
    if (!commands || !clock || !audioReplies) goto cleanup;
    timer=(struct timerequest *)CreateIORequest(clock,sizeof(*timer));
    audio=(struct IOStdReq *)CreateIORequest(audioReplies,sizeof(*audio));
    if (!timer || !audio || OpenDevice((CONST_STRPTR)TIMERNAME,UNIT_MICROHZ,(struct IORequest *)timer,0)) goto cleanup;
    timerOpen=TRUE; TimerBase=timer->tr_node.io_Device;
    commands->mp_Node.ln_Name="INDICATOR.SOURCE.sound";
    Forbid();
    if (FindPort((CONST_STRPTR)commands->mp_Node.ln_Name)) { Permit(); goto cleanup; }
    AddPort(commands); registered=TRUE; Permit();
    while (!quit || !TileSourcesIdle(&pushes)) {
        ULONG now=now_ms(),signals;
        struct RexxMsg *m;
        unsigned budget=32;
        char payload[96],update[112];
        TileSourcesCollect(&pushes,outgoing,NULL); TileSourcesExpire(&pushes,now,outgoing,NULL);
        if (!quit && !audioOpen && (!lastOpen || (ULONG)(now-lastOpen)>=2000)) {
            lastOpen=now;
            audioOpen=OpenDevice((CONST_STRPTR)"bta2dp.device",0,(struct IORequest *)audio,0)==0;
        }
        if (!quit && (ULONG)(now-last)>=1000) {
            available=audioOpen && sample(audio,&level); last=now;
            if (available && !TileSourcePending(&pushes,&pushes)) {
                snprintf(update,sizeof(update),"UPDATE LEVEL %u Bluetooth audio",level);
                TileSourceSend(&pushes,&pushes,FALSE,(CONST_STRPTR)"INDICATOR.sound",(CONST_STRPTR)update,now);
            }
        }
        while (budget-- && (m=(struct RexxMsg *)GetMsg(commands))) {
            const char *p;
            unsigned target;
            BOOL ok=FALSE;
            if (!IsRexxMsg(m) || !m->rm_Args[0]) { ReplyMsg((struct Message *)m); continue; }
            p=(const char *)m->rm_Args[0]; payload[0]=0;
            if (quit) { answer(m,10,""); continue; }
            if (!strcmp(p,"QUIT")) { quit=TRUE; answer(m,0,"Stopping"); continue; }
            if (!strcmp(p,"STATUS")) {
                snprintf(payload,sizeof(payload),"available=%u",(unsigned)available);
                answer(m,0,payload); continue;
            }
            if (!strcmp(p,"GET LEVEL")) {
                available=audioOpen && sample(audio,&level); ok=available;
            } else if (IndicatorSetParse(IND_SOUND,p,&target)) {
                available=audioOpen && sample(audio,&level);
                if (available) {
                    audio->io_Command=BTA2DPCMD_SETVOLUME; audio->io_Offset=target;
                    available=!DoIO((struct IORequest *)audio) && sample(audio,&level);
                    ok=available && level==target;
                }
            }
            if (ok) {
                snprintf(payload,sizeof(payload),"LEVEL %u Bluetooth audio",level);
                /* The borrowed UI caller receives this authoritative readback;
                 * its generation guard preserves a newer independently pushed sample. */
            }
            answer(m,ok?0:10,payload);
        }
        if (commands->mp_MsgList.lh_Head->ln_Succ) Signal(FindTask(NULL),1UL<<commands->mp_SigBit);
        if (quit && TileSourcesIdle(&pushes)) break;
        timer->tr_node.io_Command=TR_ADDREQUEST;
        timer->tr_time.tv_secs=0; timer->tr_time.tv_micro=500000;
        SendIO((struct IORequest *)timer);
        signals=Wait(1UL<<clock->mp_SigBit|1UL<<commands->mp_SigBit|
                     1UL<<pushes.replies->mp_SigBit|SIGBREAKF_CTRL_C);
        if (!CheckIO((struct IORequest *)timer)) AbortIO((struct IORequest *)timer);
        WaitIO((struct IORequest *)timer); SetSignal(0,1UL<<clock->mp_SigBit);
        if (signals&SIGBREAKF_CTRL_C) quit=TRUE;
    }
    rc=RETURN_OK;
cleanup:
    if (registered) RemPort(commands);
    if (commands) { struct RexxMsg *m; while ((m=(struct RexxMsg *)GetMsg(commands))) {
        if (IsRexxMsg(m)) answer(m,10,""); else ReplyMsg((struct Message *)m);
    } }
    if (audioOpen) CloseDevice((struct IORequest *)audio);
    if (audio) DeleteIORequest((struct IORequest *)audio);
    if (timerOpen) CloseDevice((struct IORequest *)timer);
    if (timer) DeleteIORequest((struct IORequest *)timer);
    if (audioReplies) DeleteMsgPort(audioReplies);
    if (clock) DeleteMsgPort(clock);
    if (commands) DeleteMsgPort(commands);
    if (pushes.replies) TileSourcesClose(&pushes);
    if (RexxSysBase) CloseLibrary((struct Library *)RexxSysBase);
    return rc;
}
