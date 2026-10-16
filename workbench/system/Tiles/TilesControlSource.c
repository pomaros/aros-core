/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <dos/dos.h>
#include <dos/datetime.h>
#include <rexx/storage.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/rexxsyslib.h>
#include <string.h>
#include <stdio.h>
struct RxsLib *RexxSysBase;
/* Clock is an actual local OS source. No brightness/DND state is synthesized. */
static void answer(struct RexxMsg *message, BOOL stopping)
{
    if(!IsRexxMsg(message)){ReplyMsg((struct Message *)message);return;}
    message->rm_Result1=10;message->rm_Result2=0;
    if(!stopping && message->rm_Args[0] && !strcmp((const char *)message->rm_Args[0],"GET TIME")) {
        struct DateTime time={0};char value[32]={0};
        DateStamp(&time.dat_Stamp);time.dat_Format=FORMAT_DOS;time.dat_StrTime=value;
        if(DateToStr(&time)) {
            if(message->rm_Action & RXFF_RESULT) {
                message->rm_Result2=(IPTR)CreateArgstring(value,strlen(value));
                if(message->rm_Result2)message->rm_Result1=0;
            } else message->rm_Result1=0;
        }
    }
    ReplyMsg((struct Message *)message);
}
int main(void)
{
    struct MsgPort *port=NULL;BOOL added=FALSE;int result=RETURN_FAIL;
    RexxSysBase=(struct RxsLib *)OpenLibrary("rexxsyslib.library",0);
    if(!RexxSysBase)goto done;
    port=CreateMsgPort();if(!port)goto done;
    port->mp_Node.ln_Name="INDICATOR.clock";
    Forbid();if(!FindPort(port->mp_Node.ln_Name)){AddPort(port);added=TRUE;}Permit();
    if(!added){fprintf(stderr,"TilesControlSource: INDICATOR.clock is already owned\n");goto done;}
    for(;;){
        ULONG signals=Wait((1UL<<port->mp_SigBit)|SIGBREAKF_CTRL_C);
        if(signals & SIGBREAKF_CTRL_C)break;
        struct RexxMsg *message;
        unsigned handled=0;
        while(handled<32 && (message=(struct RexxMsg *)GetMsg(port))) {answer(message,FALSE);++handled;}
        /* Revisit pending work without starving Ctrl-C under continuous load. */
        if(handled==32)Signal(FindTask(NULL),1UL<<port->mp_SigBit);
    }
    result=RETURN_OK;
done:
    if(added)RemPort(port);
    if(port){struct RexxMsg *message;while((message=(struct RexxMsg *)GetMsg(port)))answer(message,TRUE);DeleteMsgPort(port);}
    if(RexxSysBase)CloseLibrary((struct Library *)RexxSysBase);
    return result;
}
