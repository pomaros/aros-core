/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>. All rights reserved.
    SPDX-License-Identifier: APL-1.1

    Actual Control/lift fixture. The external collector supplies keyboard and
    pointer input; this task owns focus and a deliberately silent source.
*/
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/intuitionbase.h>
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
static struct MsgPort *clockPort,*sourcePort,*replyPort,*appPort;
static struct timerequest *timer;
static BOOL timerOpen,sourceAdded,holdSource,ready;
static struct RexxMsg *held;
static unsigned sent,replies;
static struct Window *focusWindow;
static unsigned focusKeys,foreignClicks;
static struct AppIcon *foreignIcon;
static struct BitMap *foreignBitmap;
static BOOL failed;
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
static void foreign_paint(unsigned pen)
{
    struct RastPort rp;InitRastPort(&rp);rp.BitMap=foreignBitmap;SetRast(&rp,pen);
    SetAPen(&rp,1);RectFill(&rp,8,8,23,55);WaitBlit();
}
static BOOL foreign_publish(BOOL frame)
{
    struct AppIconPublish update={foreignIcon,frame?foreignBitmap:NULL};
    struct TagItem tags[]={{WBCTRLA_PublishAppIcon,(IPTR)&update},{TAG_DONE,0}};
    return foreignIcon&&WorkbenchControlA(NULL,tags);
}
static void messages(void)
{
    struct RexxMsg *m;
    struct AppMessage *app;
    while(appPort&&(app=(struct AppMessage *)GetMsg(appPort))) {
        if(foreignIcon&&app->am_Class==AMCLASSICON_Region) {
            ++foreignClicks;foreign_paint(foreignClicks&1?3:2);BOOL ok=foreign_publish(TRUE);
            LOG("[CONTROLPROBE] foreign-click count=%u published=%u tick=%lu\n",foreignClicks,ok,(unsigned long)clock_ms());
            if(!ok)failed=TRUE;
        }
        ReplyMsg(&app->am_Message);
    }
    while((m=(struct RexxMsg *)GetMsg(sourcePort))) {
        unsigned id=99;
        if(sscanf((const char *)m->rm_Args[0],"GET %u",&id)!=1){m->rm_Result1=10;m->rm_Result2=0;ReplyMsg((struct Message *)m);continue;}
        LOG("[CONTROLPROBE] GET id=%u tick=%lu\n",id,(unsigned long)clock_ms());
        if(id==1 && holdSource){if(held)LOG("[CONTROLPROBE] FAIL duplicate borrowed request\n");held=m;LOG("[CONTROLPROBE] source-held tick=%lu\n",(unsigned long)clock_ms());continue;}
        answer(m,"healthy");
    }
    while((m=(struct RexxMsg *)GetMsg(replyPort))) {
        if(m->rm_Result1) failed=TRUE;
        LOG("[CONTROLPROBE] reply command=%s result=%ld text=%s\n",(const char *)m->rm_Args[0],(long)m->rm_Result1,
            !m->rm_Result1 && m->rm_Result2?(char *)m->rm_Result2:"");
        if(!m->rm_Result1 && m->rm_Result2) {
            unsigned draws=0;if(sscanf((const char *)m->rm_Result2,"draws=%u",&draws)==1 && draws>=3)ready=TRUE;
            DeleteArgstring((char *)m->rm_Result2);
        }
        DeleteArgstring((STRPTR)m->rm_Args[0]);DeleteRexxMsg(m);++replies;
    }
}
static void wait_events(void)
{
    timer->tr_node.io_Command=TR_ADDREQUEST;timer->tr_time.tv_secs=0;timer->tr_time.tv_micro=20000;
    SendIO((struct IORequest *)timer);
    Wait((1UL<<sourcePort->mp_SigBit)|(1UL<<replyPort->mp_SigBit)|(1UL<<clockPort->mp_SigBit)|(focusWindow?(1UL<<focusWindow->UserPort->mp_SigBit):0)|(appPort?(1UL<<appPort->mp_SigBit):0));
    if(!CheckIO((struct IORequest *)timer))AbortIO((struct IORequest *)timer);
    WaitIO((struct IORequest *)timer);SetSignal(0,1UL<<clockPort->mp_SigBit);
}
static void focus_events(void)
{
    struct IntuiMessage *m;
    if(!focusWindow)return;
    while((m=(struct IntuiMessage *)GetMsg(focusWindow->UserPort))) {
        if(m->Class==IDCMP_RAWKEY && m->Code==0x50)++focusKeys;
        LOG("[CONTROLPROBE] focus-event class=%lu code=%u qualifier=%u tick=%lu\n",
            (unsigned long)m->Class,m->Code,m->Qualifier,(unsigned long)clock_ms());
        ReplyMsg((struct Message *)m);
    }
}
static void phase(const char *name)
{
    ULONG lock=LockIBase(0);BOOL active=IntuitionBase->ActiveWindow==focusWindow;UnlockIBase(lock);
    LOG("[CONTROLPROBE] phase=%s active=%u keys=%u tick=%lu\n",name,active,focusKeys,(unsigned long)clock_ms());
    if(!active)failed=TRUE;
}
static BOOL create_icon(const char *path, STRPTR *types, BOOL group, LONG x, LONG y)
{
    struct DiskObject icon={0};struct Image image={0};UWORD bits[64];
    icon.do_Magic=0xe310;icon.do_Version=1;icon.do_Type=group?WBDRAWER:WBTOOL;
    icon.do_CurrentX=x;icon.do_CurrentY=y;
    icon.do_Gadget.Width=icon.do_Gadget.Height=32;icon.do_Gadget.Flags=GFLG_GADGIMAGE;icon.do_Gadget.GadgetRender=&image;
    image.Width=image.Height=32;image.Depth=1;image.PlanePick=1;image.ImageData=bits;
    for(unsigned y=0;y<32;++y)bits[y*2]=bits[y*2+1]=(y/8)&1?0x00ff:0xff00;
    icon.do_ToolTypes=types;return PutDiskObject(path,&icon);
}
static BOOL provisioned_defaults(void)
{
    const char *names[]={"network","bluetooth","sound","battery","brightness","dnd","clock"};
    const char *kinds[]={"toggle","toggle","slider","gauge","slider","toggle","value"};
    const char *gets[]={"INDICATOR.network GET RADIO","INDICATOR.bluetooth GET RADIO","INDICATOR.sound GET LEVEL","INDICATOR.battery GET PERCENT","INDICATOR.brightness GET LEVEL","INDICATOR.notifications GET DND","INDICATOR.clock GET TIME"};
    const char *sets[]={"INDICATOR.network SET RADIO %v","INDICATOR.bluetooth SET RADIO %v","INDICATOR.sound SET LEVEL %v",NULL,"INDICATOR.brightness SET LEVEL %v","INDICATOR.notifications SET DND %v",NULL};
    const char *root="RAM:SeedControlRoot/Control";
    struct DiskObject *icon; char path[128];
    if(SystemTags("SYS:System/TilesControl RAM:SeedControlRoot",TAG_DONE)!=0)return FALSE;
    icon=GetDiskObject((STRPTR)root);if(!icon)return FALSE;
    STRPTR kind=FindToolType(icon->do_ToolTypes,"TILE");
    BOOL ok=kind&&!strcmp((const char *)kind,"group");FreeDiskObject(icon);if(!ok)return FALSE;
    for(unsigned i=0;i<7;++i) {
        snprintf(path,sizeof(path),"%s/%s",root,names[i]);icon=GetDiskObject(path);if(!icon)return FALSE;
        kind=FindToolType(icon->do_ToolTypes,"TILE");ok=kind&&!strcmp((const char *)kind,kinds[i]);
        STRPTR get=FindToolType(icon->do_ToolTypes,"GET"),set=FindToolType(icon->do_ToolTypes,"SET");
        ok=ok&&get&&!strcmp((const char *)get,gets[i])&&(sets[i]?(set&&!strcmp((const char *)set,sets[i])):!set);
        if(i==6){STRPTR every=FindToolType(icon->do_ToolTypes,"EVERY");ok=ok&&every&&!strcmp((const char *)every,"60");}
        if(i==0&&ok) {
            STRPTR custom[]={"TILE=toggle","ENV=KeepMyControl","USER_CUSTOM=preserve",NULL};
            STRPTR *original=icon->do_ToolTypes;icon->do_ToolTypes=custom;
            ok=PutDiskObject(path,icon);icon->do_ToolTypes=original;
        }
        FreeDiskObject(icon);if(!ok)return FALSE;
    }
    if(SystemTags("SYS:System/TilesControl RAM:SeedControlRoot",TAG_DONE)!=0)return FALSE;
    icon=GetDiskObject("RAM:SeedControlRoot/Control/network");if(!icon)return FALSE;
    STRPTR custom=FindToolType(icon->do_ToolTypes,"USER_CUSTOM");
    STRPTR env=FindToolType(icon->do_ToolTypes,"ENV");
    ok=custom&&env&&!strcmp((const char *)custom,"preserve")&&!strcmp((const char *)env,"KeepMyControl");
    FreeDiskObject(icon);return ok;
}
/* Separate request accounting keeps the existing Control command proof intact. */
static BOOL clock_provider(void)
{
    struct MsgPort *port=CreateMsgPort(),*destination=NULL;
    struct RexxMsg *request=NULL,*reply=NULL;BOOL ok=FALSE;
    if(!port)return FALSE;
    request=CreateRexxMsg(port,NULL,NULL);if(!request)goto done;
    request->rm_Action=RXCOMM|RXFF_RESULT;
    request->rm_Args[0]=(IPTR)CreateArgstring("GET TIME",8);
    if(!request->rm_Args[0])goto done;
    ULONG began=clock_ms(),sentTick=0;
    while(clock_ms()-began<5000) {
        Forbid();destination=FindPort("INDICATOR.clock");
        if(destination){sentTick=clock_ms();PutMsg(destination,(struct Message *)request);}
        Permit();if(destination)break;messages();focus_events();wait_events();
    }
    if(!destination)goto done;
    while(clock_ms()-sentTick<5000) {
        reply=(struct RexxMsg *)GetMsg(port);if(reply)break;messages();focus_events();wait_events();
    }
    if(!reply) {
        /* A borrowed request cannot be freed. The bounded FAIL makes the
         * collector stop the guest; retain ownership until an eventual reply. */
        LOG("[CONTROLPROBE] FAIL clock-provider timeout\n");
        while(!(reply=(struct RexxMsg *)GetMsg(port)))Wait(1UL<<port->mp_SigBit);
        goto done;
    }
    if(reply!=request)goto done;
    const char *value=!reply->rm_Result1&&reply->rm_Result2?(const char *)reply->rm_Result2:NULL;
    if(value && LengthArgstring((STRPTR)value)==8 && value[2]==':' && value[5]==':') {
        ok=TRUE;for(unsigned i=0;i<8;++i)if(i!=2&&i!=5&&(value[i]<'0'||value[i]>'9'))ok=FALSE;
        if(ok && ((value[0]-'0')*10+value[1]-'0'>23 || (value[3]-'0')*10+value[4]-'0'>59 || (value[6]-'0')*10+value[7]-'0'>59))ok=FALSE;
    }
    if(ok)LOG("[CONTROLPROBE] clock-provider result=0 value=%s requested=%lu replied=%lu\n",value,(unsigned long)sentTick,(unsigned long)clock_ms());
done:
    if(reply && !reply->rm_Result1 && reply->rm_Result2)DeleteArgstring((STRPTR)reply->rm_Result2);
    if(request){if(request->rm_Args[0])DeleteArgstring((STRPTR)request->rm_Args[0]);DeleteRexxMsg(request);}
    DeleteMsgPort(port);return ok;
}
int main(int argc, char **argv)
{
    struct Screen *screen=NULL;struct DiskObject *foreignFallback=NULL;ULONG phases=0,lastBeat=0;BOOL serviceStarted=FALSE;
    STRPTR groupTypes[]={"TILE=group","SHOW=lifted","COLUMNS=2","TITLE=Control fixture","LIFT=LAmiga C",NULL};
    STRPTR toggleTypes[]={"TILE=toggle","SIZE=1x1","LABEL=Radio","ENV=ControlToggle",NULL};
    STRPTR heldTypes[]={"TILE=value","SIZE=1x1","LABEL=Remote","GET=CONTROLPROBE GET 1","EVERY=60",NULL};
    STRPTR desktopTypes[]={"TILE=value","SIZE=1x1","LABEL=Desktop","ENV=ControlDesktop",NULL};
    failed=TRUE;
    RexxSysBase=(struct RxsLib *)OpenLibrary("rexxsyslib.library",0);if(!RexxSysBase)goto cleanup;
    sourcePort=CreateMsgPort();replyPort=CreateMsgPort();clockPort=CreateMsgPort();if(!sourcePort||!replyPort||!clockPort)goto cleanup;
    sourcePort->mp_Node.ln_Name="CONTROLPROBE";AddPort(sourcePort);sourceAdded=TRUE;
    timer=(struct timerequest *)CreateIORequest(clockPort,sizeof(*timer));
    if(!timer||OpenDevice(TIMERNAME,UNIT_MICROHZ,(struct IORequest *)timer,0))goto cleanup;
    timerOpen=TRUE;TimerBase=timer->tr_node.io_Device;
    if(argc>1&&!strcmp(argv[1],"WAITREADY")) {
        BOOL bootReady=FALSE;
        for(ULONG began=clock_ms();clock_ms()-began<30000;) {
            BPTR marker=Lock("RAM:TilesDesktopReady",ACCESS_READ);
            if(marker){UnLock(marker);bootReady=TRUE;break;}
            wait_events();
        }
        if(!bootReady){LOG("[CONTROLPROBE] FAIL bootstrap readiness timeout\n");goto cleanup;}
        LOG("[CONTROLPROBE] bootstrap-ready tick=%lu\n",(unsigned long)clock_ms());
    }
    screen=LockPubScreen(NULL);if(!screen)goto cleanup;
    if(!provisioned_defaults()){LOG("[CONTROLPROBE] FAIL Control provisioner roundtrip\n");goto cleanup;}
    LOG("[CONTROLPROBE] defaults-roundtrip icons=7 customization=preserved\n");
    BPTR directory=CreateDir("RAM:Control");if(!directory)goto cleanup;UnLock(directory);
    if(!SetVar("ControlToggle","off",-1,GVF_GLOBAL_ONLY)||!SetVar("ControlDesktop","Desktop cached",-1,GVF_GLOBAL_ONLY))goto cleanup;
    if(!create_icon("RAM:Control",groupTypes,TRUE,NO_ICON_POSITION,NO_ICON_POSITION)||
       !create_icon("RAM:Control/radio",toggleTypes,FALSE,0,0)||
       !create_icon("RAM:Control/remote",heldTypes,FALSE,64,0)||
       !create_icon("RAM:desktopcontrol",desktopTypes,FALSE,NO_ICON_POSITION,NO_ICON_POSITION))goto cleanup;
    focusWindow=OpenWindowTags(NULL,WA_PubScreen,(IPTR)screen,WA_Left,60,WA_Top,100,WA_Width,420,WA_Height,220,
        WA_Title,(IPTR)"Control focus witness",WA_Activate,TRUE,WA_IDCMP,IDCMP_RAWKEY|IDCMP_ACTIVEWINDOW|IDCMP_INACTIVEWINDOW,TAG_DONE);
    if(!focusWindow)goto cleanup;
    appPort=CreateMsgPort();foreignFallback=GetDefDiskObject(WBTOOL);
    foreignBitmap=AllocBitMap(64,64,GetBitMapAttr(screen->RastPort.BitMap,BMA_DEPTH),BMF_CLEAR,screen->RastPort.BitMap);
    if(!appPort||!foreignFallback||!foreignBitmap)goto cleanup;
    foreign_paint(2);
    {struct AppIconRegions regions={1,{{0,0,64,64,"foreign"}}};
     struct TagItem tags[]={{WBAPPICONA_Cells,WBAPPICON_CELLS(1,1)},{WBAPPICONA_Surface,(IPTR)foreignBitmap},
         {WBAPPICONA_Regions,(IPTR)&regions},{WBAPPICONA_Cadence,500},{TAG_DONE,0}};
     foreignIcon=AddAppIconA(90,0,"foreigncontrol",appPort,0,foreignFallback,tags);}
    if(!foreignIcon)goto cleanup;
    SetAPen(focusWindow->RPort,1);RectFill(focusWindow->RPort,16,32,399,199);
    SetAPen(focusWindow->RPort,2);RectFill(focusWindow->RPort,32,48,191,175);WaitBlit();
    if(SystemTags("Run >NIL: SYS:System/Tiles TRACE RAM:Control RAM:desktopcontrol",TAG_DONE)<0)goto cleanup;
    serviceStarted=TRUE;
    if(!clock_provider()){LOG("[CONTROLPROBE] FAIL clock provider proof\n");goto cleanup;}
    failed=FALSE;
    for(ULONG began=clock_ms(),lastStats=began-1000;!ready && clock_ms()-began<30000;) {
        messages();focus_events();if(clock_ms()-lastStats>=1000&&FindPort("TILES")){command("TILE STATS");lastStats=clock_ms();}wait_events();
    }
    if(!ready){failed=TRUE;LOG("[CONTROLPROBE] FAIL initialization\n");goto cleanup;}
    phase("ready");
    for(ULONG began=clock_ms(),elapsed=0;elapsed<38000;elapsed=clock_ms()-began) {
        messages();focus_events();
        if(clock_ms()-lastBeat>=1000){if(!foreign_publish(FALSE))failed=TRUE;lastBeat=clock_ms();}
        if(AT(1500,0)){phase("command-lift");command("TILE STATS");command("TILE LIFT Control");}
        if(AT(4000,1)){command("TILE DROP");phase("command-drop");}
        if(AT(6000,2))phase("hotkey-control");
        if(AT(10000,3)){command("TILE GET Control/radio");phase("escape");}
        if(AT(13000,4))phase("hotkey-all");
        if(AT(16000,5))phase("outside");
        if(AT(19000,6))phase("same-key");
        if(AT(23000,7)){holdSource=TRUE;command("TILE CHANGED Control/remote");phase("source-blocked");}
        if(AT(24500,8))phase("blocked-lift");
        if(AT(28500,9))phase("blocked-drop");
        if(AT(31000,10)){
            if(!held){failed=TRUE;LOG("[CONTROLPROBE] FAIL missing held request\n");}
            else{answer(held,"healthy");held=NULL;holdSource=FALSE;LOG("[CONTROLPROBE] late-reply tick=%lu\n",(unsigned long)clock_ms());}
        }
        if(AT(33000,11)){command("TILE GET Control/radio");command("TILE STATS");phase("final-focus");}
        if(AT(35000,12))command("TILE QUIT");
        wait_events();
    }
    messages();focus_events();if(sent!=replies||FindPort("TILES")||focusKeys<4||foreignClicks!=1)failed=TRUE;
    LOG("[CONTROLPROBE] %s sent=%u replies=%u focuskeys=%u\n",failed?"FAIL":"COMPLETE",sent,replies,focusKeys);
cleanup:
    if(held){answer(held,"healthy");held=NULL;}holdSource=FALSE;
    if(serviceStarted&&FindPort("TILES"))command("TILE QUIT");
    while(serviceStarted&&(sent!=replies||FindPort("TILES"))){messages();focus_events();Delay(1);}
    if(foreignIcon){RemoveAppIcon(foreignIcon);foreignIcon=NULL;}
    if(appPort){struct Message *m;while((m=GetMsg(appPort)))ReplyMsg(m);DeleteMsgPort(appPort);}
    WaitBlit();if(foreignBitmap)FreeBitMap(foreignBitmap);if(foreignFallback)FreeDiskObject(foreignFallback);
    if(focusWindow)CloseWindow(focusWindow);if(screen)UnlockPubScreen(NULL,screen);
    if(timerOpen)CloseDevice((struct IORequest *)timer);if(timer)DeleteIORequest((struct IORequest *)timer);
    if(sourceAdded)RemPort(sourcePort);if(sourcePort)DeleteMsgPort(sourcePort);if(replyPort)DeleteMsgPort(replyPort);if(clockPort)DeleteMsgPort(clockPort);
    if(RexxSysBase)CloseLibrary((struct Library *)RexxSysBase);
    return failed?20:0;
}
