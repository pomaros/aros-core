/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>. All rights reserved.
    SPDX-License-Identifier: APL-1.1

    Actual guest sources for the declarative service. The external collector
    supplies real pointer input; this program supplies source/coverage faults.
*/
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <workbench/workbench.h>
#include <rexx/storage.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <proto/rexxsyslib.h>
#include <proto/arossupport.h>
#include <proto/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AT(ms,bit) (elapsed >= (ms) && !(phases & (1ULL << (bit))) && (phases |= 1ULL << (bit), TRUE))
#define TILE_LOG(...) do { Forbid(); kprintf(__VA_ARGS__); Permit(); } while (0)
struct Device *TimerBase;
struct RxsLib *RexxSysBase;
static struct MsgPort *clockPort;
static struct timerequest *clockRequest;
static BOOL clockOpen;
static ULONG clock_ms(void)
{
    struct EClockVal clock; ULONG frequency=ReadEClock(&clock);
    unsigned long long ticks=((unsigned long long)clock.ev_hi<<32)|clock.ev_lo;
    return frequency?(ULONG)((ticks/frequency)*1000+((ticks%frequency)*1000)/frequency):0;
}
static struct Task *supervisor;
static ULONG readySignal;
static struct Process *child;
static struct MsgPort *sourcePort,*replyPort;
static struct RexxMsg *held;
static struct RexxMsg *volatile deadHeld;
static volatile BOOL dieOnGet;
static volatile ULONG deadExitTick;
static BOOL radio,holdSource,initialFrames;
static unsigned sent,returned,sets;
static void reply_value(struct RexxMsg *message,const char *value)
{
    message->rm_Result1=0; message->rm_Result2=0;
    if (message->rm_Action & RXFF_RESULT)
        message->rm_Result2=(IPTR)CreateArgstring(value,strlen(value));
    ReplyMsg((struct Message *)message);
}
static LONG dead_source(void)
{
    struct MsgPort *port=CreateMsgPort();
    if (!port) { Signal(supervisor,readySignal); return 20; }
    port->mp_Node.ln_Name="TILEDEAD"; AddPort(port);
    Signal(supervisor,readySignal);
    for (;;) {
        ULONG signals=Wait((1UL<<port->mp_SigBit)|SIGBREAKF_CTRL_C);
        struct RexxMsg *message;
        while((message=(struct RexxMsg *)GetMsg(port))) {
            if(dieOnGet) { deadHeld=message; break; }
            reply_value(message,"alive");
        }
        if (deadHeld || (signals & SIGBREAKF_CTRL_C)) break;
    }
    RemPort(port);DeleteMsgPort(port);
    deadExitTick=clock_ms();
    return 0;
}
static BOOL send_command(const char *command)
{
    struct MsgPort *port;
    struct RexxMsg *message=CreateRexxMsg(replyPort,NULL,"TILEPROBE");
    if (!message) return FALSE;
    message->rm_Args[0]=(IPTR)CreateArgstring(command,strlen(command));
    if (!message->rm_Args[0]) { DeleteRexxMsg(message);return FALSE; }
    message->rm_Action=RXCOMM|RXFF_RESULT;
    Forbid();port=FindPort("TILES");if(port)PutMsg(port,(struct Message *)message);Permit();
    if (!port) { DeleteArgstring((STRPTR)message->rm_Args[0]);DeleteRexxMsg(message);return FALSE; }
    ++sent;return TRUE;
}
static void changed(unsigned id)
{
    ULONG before=clock_ms();
    char command[64];snprintf(command,sizeof(command),"TILE CHANGED tile%02u",id);
    if(!send_command(command)) TILE_LOG("[TILE2PROBE] FAIL sending change\n");
    TILE_LOG("[TILE2PROBE] changed id=tile%02u tick=%lu\n",id,(unsigned long)before);
}
static void messages(void)
{
    struct RexxMsg *message;
    while ((message=(struct RexxMsg *)GetMsg(sourcePort))) {
        unsigned id=99;char value[64];
        if (sscanf((const char *)message->rm_Args[0],"GET %u",&id)==1) {
            TILE_LOG("[TILE2PROBE] GET id=%u\n",id);
            if (id==1 && holdSource) { if(held)TILE_LOG("[TILE2PROBE] FAIL duplicate held GET\n");held=message;continue; }
            if(id%5==0)strcpy(value,radio?"on":"off");
            else if(id%5==1 || id%5==2)strcpy(value,"40");
            else strcpy(value,id%5==3?"ready":"alive");
            reply_value(message,value);
        } else if(sscanf((const char *)message->rm_Args[0],"SET %u %63s",&id,value)==2) {
            radio=!strcmp(value,"on");++sets;
            TILE_LOG("[TILE2PROBE] SET id=%u value=%s count=%u\n",id,value,sets);
            reply_value(message,"ok");
        } else { message->rm_Result1=10;message->rm_Result2=0;ReplyMsg((struct Message *)message); }
    }
    while ((message=(struct RexxMsg *)GetMsg(replyPort))) {
        TILE_LOG("[TILE2PROBE] reply command=%s result=%ld text=%s\n",(const char *)message->rm_Args[0],(long)message->rm_Result1,
            message->rm_Result1==0 && message->rm_Result2?(char *)message->rm_Result2:"");
        if(message->rm_Result1==0 && message->rm_Result2 && !strcmp((const char *)message->rm_Args[0],"TILE STATS")) {
            unsigned draws=0;if(sscanf((const char *)message->rm_Result2,"draws=%u",&draws)==1 && draws>=10)initialFrames=TRUE;
        }
        if(message->rm_Result1==0 && message->rm_Result2)DeleteArgstring((char *)message->rm_Result2);
        DeleteArgstring((STRPTR)message->rm_Args[0]);DeleteRexxMsg(message);++returned;
    }
}
static void wait_events(void)
{
        clockRequest->tr_node.io_Command=TR_ADDREQUEST;
        clockRequest->tr_time.tv_secs=0;clockRequest->tr_time.tv_micro=20000;
        SendIO((struct IORequest *)clockRequest);
        Wait((1UL<<sourcePort->mp_SigBit)|(1UL<<replyPort->mp_SigBit)|(1UL<<clockPort->mp_SigBit));
        if(!CheckIO((struct IORequest *)clockRequest))AbortIO((struct IORequest *)clockRequest);
        WaitIO((struct IORequest *)clockRequest);
        SetSignal(0,1UL<<clockPort->mp_SigBit);
}
static BOOL create_icon(unsigned id)
{
    static const char *templates[]={"toggle","gauge","slider","value","glyph"};
    struct DiskObject object={0}, *icon=&object;
    struct Image image={0}; UWORD bits[64];
    icon->do_Magic=0xe310;icon->do_Version=1;icon->do_Type=WBTOOL;
    icon->do_CurrentX=icon->do_CurrentY=NO_ICON_POSITION;
    icon->do_Gadget.Width=icon->do_Gadget.Height=32;
    icon->do_Gadget.Flags=GFLG_GADGIMAGE;icon->do_Gadget.GadgetRender=&image;
    image.Width=image.Height=32;image.Depth=1;image.PlanePick=1;image.ImageData=bits;
    for(unsigned y=0;y<32;++y)bits[y*2]=bits[y*2+1]=id==10?((y/4)&1?0x0f0f:0xf0f0):((y/8)&1?0x00ff:0xff00);
    STRPTR *saved;
    char path[64],kind[32],get[64],set[64],label[32];
    STRPTR types[10];unsigned count=0;
    snprintf(path,sizeof(path),"RAM:tile%02u",id);
    snprintf(kind,sizeof(kind),"TILE=%s",templates[id%5]);
    snprintf(get,sizeof(get),"GET=%s GET %u",id==9?"TILEDEAD":"TILEPROBE",id);
    snprintf(set,sizeof(set),"SET=TILEPROBE SET %u %%v",id);
    snprintf(label,sizeof(label),"LABEL=tile%02u",id);
    types[count++]=kind;types[count++]="SIZE=1x1";types[count++]=label;
    types[count++]="GLYPH=wifi";types[count++]="EVERY=1";
    types[count++]=id==8?"ENV:unused":get;
    if(id==8)types[count-1]="ENV=TilesProbe";
    if(id==10)types[count-1]="GET=malformed";
    if(id%5==0 || id%5==2)types[count++]=set;
    types[count]=NULL;
    saved=icon->do_ToolTypes;icon->do_ToolTypes=types;
    BOOL ok=PutDiskObject(path,icon);
    icon->do_ToolTypes=saved;return ok;
}
int main(void)
{
    LONG signal=-1;struct Window *cover=NULL;
    struct Screen *screen=NULL;
    unsigned i;unsigned long long phases=0;BOOL failed=TRUE,sourceAdded=FALSE;
    char command[1024]="Run >NIL: SYS:System/Tiles TRACE";
    RexxSysBase=(struct RxsLib *)OpenLibrary("rexxsyslib.library",0);
    if(!RexxSysBase)goto cleanup;
    sourcePort=CreateMsgPort();replyPort=CreateMsgPort();
    if(!sourcePort || !replyPort)goto cleanup;
    sourcePort->mp_Node.ln_Name="TILEPROBE";AddPort(sourcePort);sourceAdded=TRUE;
    clockPort=CreateMsgPort();if(!clockPort)goto cleanup;
    clockRequest=(struct timerequest *)CreateIORequest(clockPort,sizeof(*clockRequest));
    if(!clockRequest || OpenDevice(TIMERNAME,UNIT_MICROHZ,(struct IORequest *)clockRequest,0))goto cleanup;
    clockOpen=TRUE;TimerBase=clockRequest->tr_node.io_Device;
    supervisor=FindTask(NULL);signal=AllocSignal(-1);if(signal<0)goto cleanup;
    readySignal=1UL<<signal;
    child=CreateNewProcTags(NP_Name,(IPTR)"Tile source owner",NP_Entry,(IPTR)dead_source,NP_StackSize,16384,TAG_DONE);
    if(!child)goto cleanup;
    Wait(readySignal);
    if(!SetVar("TilesProbe","ready",-1,GVF_GLOBAL_ONLY)){TILE_LOG("[TILE2PROBE] FAIL initial ENV write\n");goto cleanup;}
    for(i=0;i<11;++i) {
        char path[64];if(!create_icon(i))goto cleanup;
        snprintf(path,sizeof(path)," RAM:tile%02u",i);strcat(command,path);
    }
    if(SystemTags(command,TAG_DONE)<0)goto cleanup;
    failed=FALSE;
    TILE_LOG("[TILE2PROBE] launched ten valid tiles plus malformed control\n");
    for(ULONG began=clock_ms(),lastStats=began-1000;!initialFrames && clock_ms()-began<30000;) {
        messages();
        if(clock_ms()-lastStats>=1000 && FindPort("TILES")){send_command("TILE STATS");lastStats=clock_ms();}
        wait_events();
    }
    if(!initialFrames){failed=TRUE;TILE_LOG("[TILE2PROBE] FAIL initial frames timeout\n");goto cleanup;}
    TILE_LOG("[TILE2PROBE] initial frames ready\n");
    for(ULONG began=clock_ms(),elapsed=0;elapsed<36000;elapsed=clock_ms()-began) {
        messages();
        if(AT(7000,0))TILE_LOG("[TILE2PROBE] pointer-ready tile=tile00\n");
        if(AT(12000,1)){radio=!radio;changed(0);TILE_LOG("[TILE2PROBE] latency-change\n");}
        if(AT(14000,2)){holdSource=TRUE;changed(1);TILE_LOG("[TILE2PROBE] silent-source tick=%lu\n",(unsigned long)clock_ms());}
        if(AT(15000,3) || AT(16000,4) || AT(17000,5)){radio=!radio;changed(0);}
        if(AT(18000,6) && !SetVar("TilesProbe","updated",-1,GVF_GLOBAL_ONLY)){failed=TRUE;TILE_LOG("[TILE2PROBE] FAIL ENV update\n");}
        if(AT(20000,7)){holdSource=FALSE;if(!held){failed=TRUE;TILE_LOG("[TILE2PROBE] FAIL missing held request\n");}else{reply_value(held,"40");held=NULL;TILE_LOG("[TILE2PROBE] late-reply\n");}}
        if(AT(22000,8)){dieOnGet=TRUE;changed(9);}
        if(AT(23000,9)){if(FindTask("Tile source owner")){failed=TRUE;TILE_LOG("[TILE2PROBE] FAIL source still alive\n");}else{child=NULL;TILE_LOG("[TILE2PROBE] source-exited pending=%u tick=%lu\n",deadHeld!=NULL,(unsigned long)deadExitTick);if(!deadHeld)failed=TRUE;}}
        if(AT(23500,10) || AT(24500,11)){radio=!radio;changed(0);}
        if(AT(26000,12)){
            screen=LockPubScreen(NULL);
            if(screen)cover=OpenWindowTags(NULL,WA_PubScreen,(IPTR)screen,WA_Left,0,WA_Top,0,
                WA_Width,screen->Width,WA_Height,screen->Height,WA_Borderless,TRUE,WA_Backdrop,FALSE,
                WA_Activate,TRUE,WA_RMBTrap,TRUE,TAG_DONE);
            if(!cover){failed=TRUE;TILE_LOG("[TILE2PROBE] FAIL cover window\n");}
            else TILE_LOG("[TILE2PROBE] covered tick=%lu\n",(unsigned long)clock_ms());
        }
        if(AT(27000,13)){radio=!radio;changed(0);}
        if(AT(28000,14) && !SetVar("TilesProbe","covered",-1,GVF_GLOBAL_ONLY)){failed=TRUE;TILE_LOG("[TILE2PROBE] FAIL covered ENV update\n");}
        if(AT(30000,15)){if(cover){CloseWindow(cover);cover=NULL;}if(screen){UnlockPubScreen(NULL,screen);screen=NULL;}TILE_LOG("[TILE2PROBE] uncovered tick=%lu\n",(unsigned long)clock_ms());}
        if(AT(32000,16) && deadHeld){reply_value(deadHeld,"alive");deadHeld=NULL;TILE_LOG("[TILE2PROBE] exited-source late-reply\n");}
        if(AT(33000,17))send_command("TILE STATS");
        if(AT(34000,18))send_command("TILE QUIT");
        wait_events();
    }
    messages();
    if(!sets || sent!=returned || FindPort("TILES"))failed=TRUE;
    TILE_LOG("[TILE2PROBE] %s sets=%u sent=%u replies=%u\n",failed?"FAIL":"COMPLETE",sets,sent,returned);
cleanup:
    if(cover)CloseWindow(cover);if(screen)UnlockPubScreen(NULL,screen);
    if(child)Signal((struct Task *)child,SIGBREAKF_CTRL_C);
    while(FindTask("Tile source owner"))Delay(1);
    if(held)reply_value(held,"40");
    if(deadHeld)reply_value(deadHeld,"alive");
    if(signal>=0)FreeSignal(signal);
    if(sourcePort && sourceAdded)RemPort(sourcePort);
    /* Keep the fixture alive while outstanding commands can still reply.
     * The external strict collector bounds a failed run and owns VM shutdown. */
    while(replyPort && sent!=returned){messages();Delay(1);}
    if(sourcePort)DeleteMsgPort(sourcePort);
    if(clockOpen)CloseDevice((struct IORequest *)clockRequest);
    if(clockRequest)DeleteIORequest((struct IORequest *)clockRequest);
    if(clockPort)DeleteMsgPort(clockPort);
    /* On an incomplete run, retain rather than free an in-flight reply port. */
    if(replyPort && sent==returned)DeleteMsgPort(replyPort);
    if(RexxSysBase)CloseLibrary((struct Library *)RexxSysBase);
    return failed?RETURN_FAIL:RETURN_OK;
}
