/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1 */
#include <dos/dos.h>
#include <libraries/commodities.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <proto/commodities.h>
#include <proto/rexxsyslib.h>
#include <proto/arossupport.h>
#include <workbench/workbench.h>
#include <stdio.h>
#include <string.h>
#include "source.h"
#include "consumers.h"
APTR TaskResBase;
/* UP witness lines must not be split by another task's desktop trace. */
#define PROBE_LOG(...) do { Forbid(); kprintf(__VA_ARGS__); Permit(); } while (0)
struct RxsLib *RexxSysBase;
static struct TileSources requests;
static BOOL received, succeeded;
static char answer[256];
static const char *targetPort="INDICATOR.battery";
static struct MsgPort *fakeSource;
static struct RexxMsg *fakeHeld;
static unsigned fakeMode, fakeCalls, pushedOwner;
static BOOL pushed;
static void fake_reply(struct RexxMsg *m, const char *text)
{
    m->rm_Result1=0; m->rm_Result2=0;
    if (m->rm_Action&RXFF_RESULT) m->rm_Result2=(IPTR)CreateArgstring((CONST_STRPTR)text,strlen(text));
    ReplyMsg((struct Message *)m);
}
static void reply(APTR owner, BOOL setting, BOOL success, CONST_STRPTR text, APTR context)
{
    (void)setting; (void)context;
    if (owner==&pushedOwner) { pushed=TRUE; return; }
    received=TRUE; succeeded=success;
    snprintf(answer,sizeof(answer),"%s",text?(const char *)text:"");
}
static void fake_work(void)
{
    struct RexxMsg *m;
    if (!fakeSource) return;
    if (fakeHeld && fakeMode==3 && pushed) {
        fake_reply(fakeHeld,"LEVEL 90 Older-confirmation"); fakeHeld=NULL;
    }
    while ((m=(struct RexxMsg *)GetMsg(fakeSource))) {
        unsigned n=0; char event[96];
        ++fakeCalls;
        if (fakeMode==1 || fakeMode==3) {
            fakeHeld=m;
            if (fakeMode==3) {
                pushed=FALSE;
                TileSourceSend(&requests,&pushedOwner,FALSE,(CONST_STRPTR)targetPort,
                               (CONST_STRPTR)"UPDATE LEVEL 37 Newer-push",0);
            }
            continue;
        }
        sscanf((const char *)m->rm_Args[0],"SET LEVEL %u",&n);
        if (fakeMode==4) fake_reply(m,"LEVEL 5 Wrong-confirmation");
        else if (fakeMode==2) fake_reply(m,"LEVEL 101 Invalid-confirmation");
        else {
            snprintf(event,sizeof(event),"LEVEL %u Output confirmed",n);
            fake_reply(m,event);
        }
    }
}
static BOOL command(const char *text, BOOL expected, const char *contains)
{
    unsigned ticks=0;
    received=FALSE; succeeded=FALSE; answer[0]=0;
    if (!TileSourceSend(&requests,&requests,FALSE,(CONST_STRPTR)targetPort,(CONST_STRPTR)text,0)) return FALSE;
    while (!received && ticks++<200) { fake_work(); TileSourcesCollect(&requests,reply,NULL); if (!received) Delay(1); }
    PROBE_LOG("[INDPROBE] command=%s success=%u answer=%s\n",text,(unsigned)succeeded,answer);
    return received && succeeded==expected && (!contains || strstr(answer,contains));
}
int main(void)
{
    struct DiskObject *icon=NULL;
    struct Window *witness=NULL;
    BOOL ok=FALSE, registered=FALSE;
    struct MsgPort *settings=NULL;
    struct RexxMsg *held=NULL;
    RexxSysBase=(struct RxsLib *)OpenLibrary((CONST_STRPTR)"rexxsyslib.library",0);
    if (!RexxSysBase || !TileSourcesInit(&requests)) goto done;
    {
        BPTR control=Lock((CONST_STRPTR)"SYS:System/TilesControl",ACCESS_READ);
        if (control) {
            const char *names[]={"network","sound","battery"};
            const char *gets[]={"INDICATOR.network GET RADIO","INDICATOR.sound GET LEVEL","INDICATOR.battery GET PERCENT"};
            UnLock(control);
            if (SystemTags((CONST_STRPTR)"SYS:System/TilesControl RAM:IndicatorTiles",TAG_DONE)!=0) goto done;
            for (unsigned i=0;i<3;++i) {
                char path[96]; CONST_STRPTR get;
                snprintf(path,sizeof(path),"RAM:IndicatorTiles/Control/%s",names[i]);
                icon=GetDiskObject((CONST_STRPTR)path);
                if (!icon) goto done;
                get=FindToolType(icon->do_ToolTypes,(CONST_STRPTR)"GET");
                if (!get || strcmp((const char *)get,gets[i])) goto done;
                FreeDiskObject(icon); icon=NULL;
            }
            icon=GetDiskObject((CONST_STRPTR)"RAM:IndicatorTiles/Control/battery");
            PROBE_LOG("[INDPROBE] control-icons installer=1 bindings=3 battery-fallback=1\n");
        } else icon=GetDefDiskObject(WBTOOL);
    }
    if (!icon) goto done;
    icon->do_CurrentX=64; icon->do_CurrentY=64;
    if (!PutDiskObject((CONST_STRPTR)"RAM:Indicator",icon)) goto done;
    FreeDiskObject(icon); icon=NULL;
    if (SystemTags((CONST_STRPTR)"Run >NIL: SYS:System/Indicator battery RAM:Indicator \"lcommand b\"",TAG_DONE)<0) goto done;
    for (unsigned i=0;i<300 && !FindPort((CONST_STRPTR)"INDICATOR.battery");++i) Delay(1);
    if (!command("COMMANDS",TRUE,"GET PERCENT") || !command("GET",FALSE,NULL) || !command("GET PERCENT",FALSE,NULL)) goto done;
    if (!command("GET TOP",TRUE,"CPU data unavailable") ||
        !command("TOP CPU: fixture 12.50%",TRUE,NULL) ||
        !command("GET TOP",TRUE,"fixture 12.50%") ||
        !command("TOP invalid\nline",FALSE,NULL)) goto done;
    if (!command("UPDATE 75 1 Charging",TRUE,NULL)) goto done;
    Delay(50);
    if (!command("GET PERCENT",TRUE,"75") || !command("GET LEVEL",FALSE,NULL) ||
        !command("SET LEVEL 10",FALSE,NULL) || !command("GET",TRUE,"Charging") || !command("STATS",TRUE,"healthy=1")) goto done;
    if (!command("UPDATE 101 1 invalid",FALSE,NULL) || !command("GET",TRUE,"Charging")) goto done;
    if (!command("PANEL",TRUE,NULL) || !command("STATS",TRUE,"panel=1")) goto done;
    PROBE_LOG("[INDPROBE] escape-ready\n");
    Delay(150);
    if (!command("STATS",TRUE,"panel=0")) goto done;
    if (!command("UPDATE 75 1 Charging",TRUE,NULL)) goto done;
    PROBE_LOG("[INDPROBE] hotkey-ready\n"); Delay(100);
    if (!command("STATS",TRUE,"panel=1")) goto done;
    PROBE_LOG("[INDPROBE] outside-ready\n"); Delay(100);
    if (!command("STATS",TRUE,"panel=0")) goto done;
    if (!command("UPDATE 75 1 Charging",TRUE,NULL)) goto done;
    PROBE_LOG("[INDPROBE] glyph-ready\n"); Delay(100);
    if (!command("STATS",TRUE,"panel=1")) goto done;
    if (!command("UPDATE 75 1 Charging",TRUE,NULL)) goto done;
    PROBE_LOG("[INDPROBE] glyph-second-ready\n"); Delay(150);
    if (!command("STATS",TRUE,"panel=0")) goto done;
    if (!command("PANEL",TRUE,NULL) || !command("PANEL",TRUE,NULL) || !command("STATS",TRUE,"panel=0")) goto done;
    if (!command("UPDATE 75 1 Charging",TRUE,NULL)) goto done;
    Delay(30);
    if (!command("PANEL",TRUE,NULL) || !command("STATS",TRUE,"panel=1")) goto done;
    witness=OpenWindowTags(NULL,WA_Left,700,WA_Top,500,WA_Width,100,WA_Height,60,
        WA_Activate,TRUE,WA_Title,(IPTR)"Indicator focus witness",TAG_DONE);
    if (!witness) goto done;
    Delay(10);
    if (!command("STATS",TRUE,"panel=0")) goto done;
    PROBE_LOG("[INDPROBE] programmatic-ready\n"); Delay(80);
    if (!command("STATS",TRUE,"panel=1") || !command("HIDE",TRUE,NULL)) goto done;
    CloseWindow(witness); witness=NULL;
    PROBE_LOG("[INDPROBE] programmatic-glyph opened=1\n");
    if (!command("UPDATE 75 1 Charging",TRUE,NULL) || !command("PANEL",TRUE,NULL) ||
        BrokerCommand((STRPTR)targetPort,CXCMD_DISABLE)!=0) goto done;
    Delay(10);
    if (!command("STATS",TRUE,"panel=0") || !strstr(answer,"disabled=1") ||
        !command("PANEL",FALSE,NULL)) goto done;
    PROBE_LOG("[INDPROBE] disabled-glyph-ready\n"); Delay(80);
    if (!command("STATS",TRUE,"panel=0") || BrokerCommand((STRPTR)targetPort,CXCMD_ENABLE)!=0) goto done;
    Delay(10);
    if (!command("UPDATE 75 1 Charging",TRUE,NULL) || !command("STATS",TRUE,"disabled=0")) goto done;
    PROBE_LOG("[INDPROBE] enabled-glyph-ready\n"); Delay(80);
    if (!command("STATS",TRUE,"panel=1") || !command("HIDE",TRUE,NULL)) goto done;
    PROBE_LOG("[INDPROBE] exchange disabled-closes=1 disabled-click-ignored=1 enabled-click-opens=1 same-registration=1\n");
    if (!command("SETTINGS",FALSE,NULL)) goto done;
    settings=CreateMsgPort(); if (!settings) goto done;
    settings->mp_Node.ln_Name="SETTINGS";
    Forbid();
    if (FindPort((CONST_STRPTR)"SETTINGS")) { Permit(); goto done; }
    AddPort(settings); registered=TRUE; Permit();
    if (!command("SETTINGS",TRUE,NULL)) goto done;
    for (unsigned i=0;i<100 && !held;++i) { held=(struct RexxMsg *)GetMsg(settings); if (!held) Delay(1); }
    if (!held) goto done;
    PROBE_LOG("[INDPROBE] settings-held\n");
    Delay(75);
    if (!command("PANEL",TRUE,NULL) || !command("STATS",TRUE,"panel=1") || !command("HIDE",TRUE,NULL)) goto done;
    held->rm_Result1=0; held->rm_Result2=0; ReplyMsg((struct Message *)held); held=NULL;
    PROBE_LOG("[INDPROBE] settings-returned\n");
    Delay(150);
    if (!command("GET TOP",TRUE,"CPU data unavailable") ||
        !command("TOP CPU: independent 25.00%",TRUE,NULL) ||
        !command("GET TOP",TRUE,"independent 25.00%") ||
        !command("GET PERCENT",FALSE,NULL) || !command("GET",FALSE,NULL) || !command("STATS",TRUE,"healthy=0")) goto done;
    PROBE_LOG("[INDPROBE] cpu-aux independent-lease=1 invalid-rejected=1 expired=1\n");
    {
        struct IndicatorConsumers cpu={0};
        char text[192], event[208];
        TaskResBase=OpenResource((CONST_STRPTR)"task.resource");
        if (!TaskResBase || !IndicatorConsumersRead(&cpu) || !cpu.scanned || cpu.scanned>128 ||
            !IndicatorConsumersFormat(&cpu,text,sizeof(text))) goto done;
        snprintf(event,sizeof(event),"TOP %s",text);
        if (!command(event,TRUE,NULL) || !command("GET TOP",TRUE,text) ||
            !command("GET PERCENT",FALSE,NULL)) goto done;
        PROBE_LOG("[INDPROBE] cpu-resource scanned=%u bounded=1 no-task-pointers=1\n",cpu.scanned);
    }
    if (!command("UPDATE 25 0 Battery",TRUE,NULL)) goto done;
    Delay(50);
    if (!command("GET",TRUE,"Battery") || !command("STATS",TRUE,"healthy=1")) goto done;
    if (SystemTags((CONST_STRPTR)"Run >NIL: SYS:System/BatterySource",TAG_DONE)<0) goto done;
    for (unsigned i=0;i<150 && !FindPort((CONST_STRPTR)"INDICATOR.SOURCE.battery");++i) Delay(1);
    Delay(110);
    targetPort="INDICATOR.SOURCE.battery";
    if (!command("STATUS",TRUE,"subscribers=1")) goto done;
    targetPort="INDICATOR.battery";
    if (!command("QUIT",TRUE,NULL)) goto done;
    for (unsigned i=0;i<100 && FindPort((CONST_STRPTR)"INDICATOR.battery");++i) Delay(1);
    if (FindPort((CONST_STRPTR)"INDICATOR.battery")) goto done;
    targetPort="INDICATOR.SOURCE.battery";
    if (!command("STATUS",TRUE,"available=0 events=1 subscribers=0") ||
        !command("GET",FALSE,NULL) || !command("EVENTS off",TRUE,NULL) ||
        !command("STATUS",TRUE,"events=0") || !command("SUBSCRIBE bad port",FALSE,NULL) ||
        !command("SET PERCENT 50",FALSE,NULL)) goto done;
    for (unsigned i=0;i<8;++i) {
        char cmd[64]; snprintf(cmd,sizeof(cmd),"SUBSCRIBE INDPROBE.BAT%u",i);
        if (!command(cmd,TRUE,NULL)) goto done;
    }
    if (!command("STATUS",TRUE,"subscribers=8") || !command("SUBSCRIBE INDPROBE.BAT8",FALSE,NULL) ||
        !command("UNSUBSCRIBE INDPROBE.BAT0",TRUE,NULL) || !command("SUBSCRIBE INDPROBE.BAT8",TRUE,NULL) ||
        !command("UNSUBSCRIBE INDPROBE.absent",FALSE,NULL) || !command("EVENTS on",TRUE,NULL) ||
        !command("STATUS",TRUE,"events=1 subscribers=8") || !command("QUIT",TRUE,NULL)) goto done;
    for (unsigned i=0;i<100 && FindPort((CONST_STRPTR)targetPort);++i) Delay(1);
    if (FindPort((CONST_STRPTR)targetPort)) goto done;
    PROBE_LOG("[INDPROBE] battery-service absent=1 subscriptions-bound=8 events-switch=1 ui-subscribe-unsubscribe=1 shutdown-complete=1\n");
    PROBE_LOG("[INDPROBE] typed-protocol\n");
    targetPort="INDICATOR.sound";
    if (SystemTags((CONST_STRPTR)"Run >NIL: SYS:System/Indicator sound RAM:Indicator \"lcommand s\"",TAG_DONE)<0) goto done;
    for (unsigned i=0;i<300 && !FindPort((CONST_STRPTR)targetPort);++i) Delay(1);
    if (!command("GET LEVEL",FALSE,NULL) || !command("SET LEVEL 44",FALSE,NULL) ||
        !command("UPDATE LEVEL 20 Speakers",TRUE,NULL) || !command("GET LEVEL",TRUE,"20")) goto done;
    if (!command("TOP CPU: wrong-kind",FALSE,NULL) || !command("GET TOP",FALSE,NULL)) goto done;
    fakeSource=CreateMsgPort(); if (!fakeSource) goto done;
    fakeSource->mp_Node.ln_Name="INDICATOR.SOURCE.sound";
    AddPort(fakeSource);
    if (!command("SET LEVEL 42",TRUE,"Confirmed") || !command("GET LEVEL",TRUE,"42")) goto done;
    fakeMode=4;
    if (!command("SET LEVEL 43",FALSE,NULL) || !command("GET LEVEL",TRUE,"42")) goto done;
    PROBE_LOG("[INDPROBE] mismatched-confirmation rejected=1 preserved=42\n");
    fakeMode=2;
    if (!command("SET LEVEL 70",FALSE,NULL) || !command("GET LEVEL",TRUE,"42")) goto done;
    fakeMode=1;
    if (!command("SET LEVEL 80",FALSE,NULL) || !fakeHeld || !command("GET LEVEL",TRUE,"42") ||
        !command("SET LEVEL 19",FALSE,NULL) || !command("UPDATE LEVEL 37 Headphones",TRUE,NULL)) goto done;
    fake_reply(fakeHeld,"LEVEL 80 Late-confirmation"); fakeHeld=NULL; Delay(10);
    if (!command("GET LEVEL",TRUE,"37")) goto done;
    fakeMode=3;
    if (!command("SET LEVEL 90",TRUE,"Confirmed") || !command("GET LEVEL",TRUE,"37") || fakeCalls!=5) goto done;
    PROBE_LOG("[INDPROBE] typed-confirmation calls=%u timeout=1 late-ignored=1 newer-preserved=1\n",fakeCalls);
    fakeMode=0;
    if (!command("UPDATE LEVEL 37 Speakers",TRUE,NULL)) goto done;
    Delay(30);
    if (!command("PANEL",TRUE,NULL) || !command("STATS",TRUE,"panel=1")) goto done;
    PROBE_LOG("[INDPROBE] sound-mute-ready\n");
    for (unsigned i=0;i<150;++i) { fake_work(); TileSourcesCollect(&requests,reply,NULL); Delay(1); }
    if (!command("GET LEVEL",TRUE,"0") || fakeCalls!=6 || !command("HIDE",TRUE,NULL)) goto done;
    PROBE_LOG("[INDPROBE] sound-mute confirmed=0 calls=6\n");
    if (!command("QUIT",TRUE,NULL)) goto done;
    for (unsigned i=0;i<100 && FindPort((CONST_STRPTR)targetPort);++i) Delay(1);
    if (FindPort((CONST_STRPTR)targetPort)) goto done;
    RemPort(fakeSource); DeleteMsgPort(fakeSource); fakeSource=NULL;
    targetPort="INDICATOR.SOURCE.sound";
    if (SystemTags((CONST_STRPTR)"Run >NIL: SYS:System/SoundSource",TAG_DONE)<0) goto done;
    for (unsigned i=0;i<150 && !FindPort((CONST_STRPTR)targetPort);++i) Delay(1);
    if (!command("STATUS",TRUE,"available=0") || !command("GET LEVEL",FALSE,NULL) ||
        !command("SET LEVEL 33",FALSE,NULL) || !command("SET LEVEL 101",FALSE,NULL) ||
        !command("QUIT",TRUE,NULL)) goto done;
    for (unsigned i=0;i<100 && FindPort((CONST_STRPTR)targetPort);++i) Delay(1);
    if (FindPort((CONST_STRPTR)targetPort)) goto done;
    PROBE_LOG("[INDPROBE] sound-real-source unavailable=1 shutdown-complete=1\n");
    PROBE_LOG("[INDPROBE] typed-radio-start\n");
    targetPort="INDICATOR.network";
    if (SystemTags((CONST_STRPTR)"Run >NIL: SYS:System/Indicator network RAM:Indicator \"lcommand n\"",TAG_DONE)<0) goto done;
    for (unsigned i=0;i<300 && !FindPort((CONST_STRPTR)targetPort);++i) Delay(1);
    Delay(25);
    if (!command("WIFI STATUS",TRUE,"wireless=0") || !strstr(answer,"reserved=0") ||
        !command("WIFI SCAN",FALSE,NULL) || !command("WIFI DISCONNECT",FALSE,NULL) ||
        !command("WIFI CONNECT",FALSE,NULL)) goto done;
    PROBE_LOG("[INDPROBE] wifi-wake allocated=1 reserved=0\n");
    if (!command("COMMANDS",TRUE,"SETTINGS") || strstr(answer,"WIFI ") ||
        !command("WIFI OTHER",FALSE,NULL) || !command("PANEL",TRUE,NULL)) goto done;
    Delay(5);
    if (!command("STATS",TRUE,"network-actions=0")) goto done;
    PROBE_LOG("[INDPROBE] wifi-unavailable-ready\n"); Delay(25);
    if (!command("HIDE",TRUE,NULL)) goto done;
    PROBE_LOG("[INDPROBE] wifi-real-worker unavailable=1 ui-responsive=1\n");
    if (!command("GET RADIO",FALSE,NULL) || !command("UPDATE 100 0 ambiguous",FALSE,NULL) ||
        !command("UPDATE LINK on Joined-but-radio-unknown",TRUE,NULL) || !command("GET RADIO",FALSE,NULL) ||
        !command("UPDATE RADIO off Disabled",TRUE,NULL) || !command("GET RADIO",TRUE,"off") ||
        !command("SET RADIO on",FALSE,NULL) || !command("UPDATE RADIO on Enabled",TRUE,NULL) ||
        !command("GET RADIO",TRUE,"on") || !command("GET LEVEL",FALSE,NULL)) goto done;
    Delay(30);
    if (!command("COMMANDS",TRUE,"WIFI SCAN") || strstr(answer,"SET RADIO") ||
        !strstr(answer,"WIFI CONNECT") || !strstr(answer,"WIFI OTHER") || !strstr(answer,"QUIT") ||
        !command("STATS",TRUE,"network-actions=1")) goto done;
    if (!command("WIFI OTHER",TRUE,NULL) || !command("STATS",TRUE,"panel=1")) goto done;
    PROBE_LOG("[INDPROBE] wifi-palette other-opens=1 connect-unavailable=1 commands-complete=1\n");
    PROBE_LOG("[INDPROBE] wifi-panel-ready\n"); Delay(50);
    /* Stop renewing the fixture: controls disappear with the source lease,
     * while its original AppIcon registration remains alive. */
    Delay(210);
    if (!command("STATS",TRUE,"healthy=0") || !strstr(answer,"network-actions=0") ||
        !command("COMMANDS",TRUE,"SETTINGS") || strstr(answer,"WIFI ") ||
        !command("WIFI OTHER",FALSE,NULL) ||
        !command("UPDATE LINK off Recovered",TRUE,NULL)) goto done;
    Delay(5);
    if (!command("STATS",TRUE,"network-actions=1")) goto done;
    PROBE_LOG("[INDPROBE] wifi-negative settings-only=1 lease-expired=1 recovered=1 same-registration=1\n");
    if (!command("HIDE",TRUE,NULL) || !command("QUIT",TRUE,NULL)) goto done;
    for (unsigned i=0;i<150 && FindPort((CONST_STRPTR)targetPort);++i) Delay(1);
    if (FindPort((CONST_STRPTR)targetPort)) goto done;
    PROBE_LOG("[INDPROBE] wifi-real-worker shutdown-complete=1\n");
    PROBE_LOG("[INDPROBE] typed-radio unavailable-and-off-distinct=1\n");
    ok=TRUE;
done:
    if (witness) CloseWindow(witness);
    PROBE_LOG("[INDPROBE] %s\n",ok?"PASS":"FAIL");
    if (fakeHeld) { fake_reply(fakeHeld,"LEVEL 10 Cleanup"); fakeHeld=NULL; }
    if (fakeSource) { RemPort(fakeSource); DeleteMsgPort(fakeSource); }
    if (held) { held->rm_Result1=10; held->rm_Result2=0; ReplyMsg((struct Message *)held); }
    if (registered) RemPort(settings);
    if (settings) { struct Message *m; while ((m=GetMsg(settings))) ReplyMsg(m); DeleteMsgPort(settings); }
    if (icon) FreeDiskObject(icon);
    if (requests.replies) {
        /* A failed fixture must not free a message borrowed by the program. */
        while (!TileSourcesIdle(&requests)) { TileSourcesCollect(&requests,reply,NULL); Delay(1); }
        TileSourcesClose(&requests);
    }
    if (RexxSysBase) CloseLibrary((struct Library *)RexxSysBase);
    return ok?0:20;
}
