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
#include <proto/graphics.h>
#include <proto/workbench.h>
#include <proto/intuition.h>
#include <proto/rexxsyslib.h>
#include <proto/arossupport.h>
#include <proto/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define LOG(...) do { Forbid(); kprintf(__VA_ARGS__); Permit(); } while (0)
#define AT(ms,bit) (elapsed>=(ms) && !(phases&(1UL<<(bit))) && (phases|=1UL<<(bit),TRUE))
struct Device *TimerBase;
struct RxsLib *RexxSysBase;
static struct MsgPort *clockPort,*sourcePort,*replyPort,*stalledPort;
static struct timerequest *timer;
static BOOL timerOpen,sourceAdded,holdWeather,ready;
static struct RexxMsg *held;
static unsigned sent,replies;
static const char *weather="18|cloudy";
static ULONG clock_ms(void)
{
    struct EClockVal e;ULONG frequency=ReadEClock(&e);
    unsigned long long ticks=((unsigned long long)e.ev_hi<<32)|e.ev_lo;
    return frequency?(ULONG)(ticks/frequency*1000+ticks%frequency*1000/frequency):0;
}
static BOOL command(const char *text)
{
    struct RexxMsg *m=CreateRexxMsg(replyPort,NULL,NULL);struct MsgPort *p;
    if(!m)return FALSE;
    m->rm_Action=RXCOMM|RXFF_RESULT;m->rm_Args[0]=(IPTR)CreateArgstring(text,strlen(text));
    if(!m->rm_Args[0]){DeleteRexxMsg(m);return FALSE;}
    Forbid();p=FindPort("TILES");if(p)PutMsg(p,(struct Message *)m);Permit();
    if(p){++sent;return TRUE;}
    DeleteArgstring((STRPTR)m->rm_Args[0]);DeleteRexxMsg(m);return FALSE;
}
static void answer(struct RexxMsg *m,const char *value)
{
    m->rm_Result1=0;m->rm_Result2=(IPTR)CreateArgstring(value,strlen(value));ReplyMsg((struct Message *)m);
}
static void messages(void)
{
    struct RexxMsg *m;
    while((m=(struct RexxMsg *)GetMsg(sourcePort))) {
        unsigned id=99;
        if(sscanf((const char *)m->rm_Args[0],"GET %u",&id)!=1){m->rm_Result1=10;m->rm_Result2=0;ReplyMsg((struct Message *)m);continue;}
        LOG("[HOVERPROBE] GET id=%u tick=%lu\n",id,(unsigned long)clock_ms());
        if(id==2 && holdWeather){if(held)LOG("[HOVERPROBE] FAIL duplicate borrowed request\n");held=m;continue;}
        answer(m,id==2?weather:"healthy");
    }
    while((m=(struct RexxMsg *)GetMsg(replyPort))) {
        LOG("[HOVERPROBE] reply command=%s result=%ld text=%s\n",(const char *)m->rm_Args[0],(long)m->rm_Result1,
            !m->rm_Result1 && m->rm_Result2?(char *)m->rm_Result2:"");
        if(!m->rm_Result1 && m->rm_Result2) {
            unsigned draws=0;if(sscanf((const char *)m->rm_Result2,"draws=%u",&draws)==1 && draws>=5)ready=TRUE;
            DeleteArgstring((char *)m->rm_Result2);
        }
        DeleteArgstring((STRPTR)m->rm_Args[0]);DeleteRexxMsg(m);++replies;
    }
}
static void wait_events(void)
{
    timer->tr_node.io_Command=TR_ADDREQUEST;timer->tr_time.tv_secs=0;timer->tr_time.tv_micro=20000;
    SendIO((struct IORequest *)timer);
    Wait((1UL<<sourcePort->mp_SigBit)|(1UL<<replyPort->mp_SigBit)|(1UL<<clockPort->mp_SigBit));
    if(!CheckIO((struct IORequest *)timer))AbortIO((struct IORequest *)timer);
    WaitIO((struct IORequest *)timer);SetSignal(0,1UL<<clockPort->mp_SigBit);
}
static BOOL create_icon(unsigned id)
{
    struct DiskObject icon={0};struct Image image={0};UWORD bits[64];
    char path[64],kind[32],get[64],label[64];STRPTR types[8];unsigned count=0;
    icon.do_Magic=0xe310;icon.do_Version=1;icon.do_Type=WBTOOL;
    icon.do_CurrentX=icon.do_CurrentY=NO_ICON_POSITION;
    icon.do_Gadget.Width=icon.do_Gadget.Height=32;icon.do_Gadget.Flags=GFLG_GADGIMAGE;icon.do_Gadget.GadgetRender=&image;
    image.Width=image.Height=32;image.Depth=1;image.PlanePick=1;image.ImageData=bits;
    for(unsigned y=0;y<32;++y)bits[y*2]=bits[y*2+1]=(y/8)&1?0x00ff:0xff00;
    snprintf(path,sizeof(path),"RAM:hover%02u",id);
    snprintf(kind,sizeof(kind),"TILE=%s",id<2?"eyes":(id==2||id==4)?"weather":"value");
    snprintf(get,sizeof(get),"GET=HOVERPROBE GET %u",id);
    snprintf(label,sizeof(label),"LABEL=%s",id==2?"Fixture Rome":"Eyes");
    types[count++]=kind;types[count++]="SIZE=1x1";types[count++]=label;
    if(id<2)types[count++]=id==0?"HOVER=FAST":"HOVER=ON";else types[count++]=id==4?"ENV=TilesWeatherDemo":get;
    types[count++]="EVERY=60";types[count]=NULL;icon.do_ToolTypes=types;
    return PutDiskObject(path,&icon);
}
int main(void)
{
    struct Screen *screen=NULL;struct Window *cover=NULL;
    struct DiskObject *fallback=NULL;struct BitMap *bitmap=NULL;struct AppIcon *stalled=NULL;
    ULONG phases=0,lastBeat=0;BOOL failed=TRUE;
    RexxSysBase=(struct RxsLib *)OpenLibrary("rexxsyslib.library",0);if(!RexxSysBase)goto cleanup;
    sourcePort=CreateMsgPort();replyPort=CreateMsgPort();clockPort=CreateMsgPort();stalledPort=CreateMsgPort();
    if(!sourcePort||!replyPort||!clockPort||!stalledPort)goto cleanup;
    sourcePort->mp_Node.ln_Name="HOVERPROBE";AddPort(sourcePort);sourceAdded=TRUE;
    timer=(struct timerequest *)CreateIORequest(clockPort,sizeof(*timer));
    if(!timer||OpenDevice(TIMERNAME,UNIT_MICROHZ,(struct IORequest *)timer,0))goto cleanup;
    timerOpen=TRUE;TimerBase=timer->tr_node.io_Device;
    screen=LockPubScreen(NULL);if(!screen)goto cleanup;
    fallback=GetDefDiskObject(WBTOOL);
    bitmap=AllocBitMap(64,64,GetBitMapAttr(screen->RastPort.BitMap,BMA_DEPTH),BMF_CLEAR,screen->RastPort.BitMap);
    if(!fallback||!bitmap)goto cleanup;
    {struct RastPort rp;InitRastPort(&rp);rp.BitMap=bitmap;SetRast(&rp,2);}
    {struct TagItem tags[]={{WBAPPICONA_Cells,WBAPPICON_CELLS(1,1)},{WBAPPICONA_Surface,(IPTR)bitmap},
        {WBAPPICONA_Pointer,TRUE},{WBAPPICONA_HoverCadence,33},{TAG_DONE,0}};
     stalled=AddAppIconA(90,0,"Stalled receiver",stalledPort,0,fallback,tags);}
    if(!stalled)goto cleanup;
    if(SystemTags("SYS:System/TilesExamples RAM:",TAG_DONE)!=0)goto cleanup;
    {struct DiskObject *eyes=GetDiskObject("RAM:Eyes"),*sample=GetDiskObject("RAM:WeatherOffline");
     BOOL valid=eyes && sample && FindToolType(eyes->do_ToolTypes,"HOVER") && FindToolType(sample->do_ToolTypes,"ENV");
     if(eyes)FreeDiskObject(eyes);if(sample)FreeDiskObject(sample);
     if(!valid)goto cleanup;
     LOG("[HOVERPROBE] examples serialized and reopened\n");}
    for(unsigned i=0;i<5;++i)if(!create_icon(i))goto cleanup;
    if(SystemTags("Run >NIL: SYS:System/Tiles TRACE RAM:hover00 RAM:hover01 RAM:hover02 RAM:hover03 RAM:hover04",TAG_DONE)<0)goto cleanup;
    failed=FALSE;
    for(ULONG began=clock_ms(),lastStats=began-1000;!ready && clock_ms()-began<30000;) {
        messages();if(clock_ms()-lastStats>=1000 && FindPort("TILES")){command("TILE STATS");lastStats=clock_ms();}wait_events();
        struct AppIconPublish beat={stalled,NULL};struct TagItem tags[]={{WBCTRLA_PublishAppIcon,(IPTR)&beat},{TAG_DONE,0}};WorkbenchControlA(NULL,tags);
    }
    if(!ready){failed=TRUE;LOG("[HOVERPROBE] FAIL initialization\n");goto cleanup;}
    LOG("[HOVERPROBE] pointer-ready tick=%lu\n",(unsigned long)clock_ms());
    for(ULONG began=clock_ms(),elapsed=0;elapsed<30000;elapsed=clock_ms()-began) {
        messages();
        if(clock_ms()-lastBeat>=2000){struct AppIconPublish beat={stalled,NULL};struct TagItem tags[]={{WBCTRLA_PublishAppIcon,(IPTR)&beat},{TAG_DONE,0}};WorkbenchControlA(NULL,tags);lastBeat=clock_ms();}
        if(AT(5000,9)){command("TILE SET hover04 20|rain");LOG("[HOVERPROBE] ENV SET requested\n");}
        if(AT(6000,10))command("TILE GET hover04");
        if(AT(8000,0)){
            cover=OpenWindowTags(NULL,WA_PubScreen,(IPTR)screen,WA_Left,0,WA_Top,0,WA_Width,screen->Width,WA_Height,screen->Height,WA_Borderless,TRUE,WA_Activate,TRUE,TAG_DONE);
            if(!cover){failed=TRUE;LOG("[HOVERPROBE] FAIL cover\n");}
            LOG("[HOVERPROBE] covered tick=%lu\n",(unsigned long)clock_ms());
        }
        if(AT(11000,1)){if(cover){CloseWindow(cover);cover=NULL;}LOG("[HOVERPROBE] uncovered tick=%lu\n",(unsigned long)clock_ms());}
        if(AT(12000,2)){weather="19|sunny";command("TILE CHANGED hover02");LOG("[HOVERPROBE] weather-update tick=%lu\n",(unsigned long)clock_ms());}
        if(AT(14000,3)){holdWeather=TRUE;ULONG before=clock_ms();command("TILE CHANGED hover02");LOG("[HOVERPROBE] silent-weather tick=%lu\n",(unsigned long)before);}
        if(AT(15000,4))LOG("[HOVERPROBE] fault-pointer-ready\n");
        if(AT(23000,5)){if(!held){failed=TRUE;LOG("[HOVERPROBE] FAIL missing weather request\n");}else{answer(held,weather);held=NULL;LOG("[HOVERPROBE] late-reply\n");}}
        if(AT(25000,6)){
            unsigned pointers=0,other=0;struct AppMessage *m;
            while((m=(struct AppMessage *)GetMsg(stalledPort))){if(m->am_Class>=AMCLASSICON_PointerEnter && m->am_Class<=AMCLASSICON_PointerLeave)++pointers;else ++other;ReplyMsg(&m->am_Message);}
            LOG("[HOVERPROBE] stalled-queue pointers=%u other=%u\n",pointers,other);
            if(pointers!=1){failed=TRUE;LOG("[HOVERPROBE] FAIL pointer queue bound\n");}
        }
        if(AT(27000,7))command("TILE STATS");
        if(AT(28000,8))command("TILE QUIT");
        wait_events();
    }
    messages();if(sent!=replies||FindPort("TILES"))failed=TRUE;
    LOG("[HOVERPROBE] %s sent=%u replies=%u\n",failed?"FAIL":"COMPLETE",sent,replies);
cleanup:
    if(cover)CloseWindow(cover);
    if(stalled)RemoveAppIcon(stalled);
    if(stalledPort){struct Message *m;while((m=GetMsg(stalledPort)))ReplyMsg(m);DeleteMsgPort(stalledPort);}
    if(held){answer(held,weather);held=NULL;}
    /* Preserve the source/reply ports until the service has returned every
     * borrowed command and stopped admitting source requests, even on failure. */
    if(sourcePort && replyPort && FindPort("TILES"))command("TILE QUIT");
    while(sourcePort && replyPort && (sent!=replies || FindPort("TILES"))){messages();Delay(1);}
    if(bitmap)FreeBitMap(bitmap);if(fallback)FreeDiskObject(fallback);if(screen)UnlockPubScreen(NULL,screen);
    if(timerOpen)CloseDevice((struct IORequest *)timer);if(timer)DeleteIORequest((struct IORequest *)timer);
    if(sourceAdded)RemPort(sourcePort);if(sourcePort)DeleteMsgPort(sourcePort);if(replyPort)DeleteMsgPort(replyPort);if(clockPort)DeleteMsgPort(clockPort);
    if(RexxSysBase)CloseLibrary((struct Library *)RexxSysBase);
    return failed?20:0;
}
