/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 * Shared desktop indicator. Providers push bounded events to its public port;
 * no provider code executes on the UI task and no request waits for a reply.
 */
#define MUIMASTER_YES_INLINE_STDARG
#include "state.h"
#include "consumers.h"
#include "wifi_panel.h"
#include "source.h"
#include <devices/timer.h>
#include <devices/inputevent.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <libraries/commodities.h>
#include <libraries/mui.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>
#include <workbench/workbench.h>
#include <proto/alib.h>
#include <proto/commodities.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/rexxsyslib.h>
#include <proto/timer.h>
#include <proto/workbench.h>
#include <stdio.h>
#include <string.h>
struct Device *TimerBase;
struct RxsLib *RexxSysBase;
#include "prefs.h"

struct Indicator {
    ULONG prefsAt;
    unsigned prefsMask;
    BOOL prefsRead;
    struct IndicatorState state;
    enum IndicatorKind kind;
    ULONG generation, controlGeneration;
    struct RexxMsg *controlCaller;
    unsigned controlOwner, notifyOwner, controlValue, subscribeOwner;
    BOOL subscribed, subscribeTried, unsubscribeSent;
    ULONG subscribeAt;
    BOOL notifyDirty, notifyTried;
    ULONG notifyAttempt;
    char providerPort[64], tileId[64];
    struct TileSources outgoing;
    char settingsCommand[64], settingsPort[64];
    struct MsgPort *commands, *apps, *cx, *clock;
    struct timerequest *timer;
    struct Screen *screen;
    struct DiskObject *fallback;
    struct BitMap *bitmap;
    struct AppIcon *icon;
    CxObj *broker;
    Object *application, *window, *text, *settings, *notice, *consumers;
    Object *volume, *mute;
    Object *wifiGroup, *extensions;
    BOOL wifiVisible;
    unsigned soundWanted;
    BOOL soundDirty;
    struct IndicatorAux cpu;
    struct IndicatorWifi *wifi;
    char cpuDisplay[192];
    ULONG lastDraw, draws, suspends;
    LONG anchorLeft, anchorTop, anchorGlyphLeft, anchorGlyphBottom;
    BOOL glyphDismissed, inactiveValid, eventAttached;
    UWORD inactiveQualifier;
    ULONG inactiveEvents, inactiveSeconds, inactiveMicros, downSeconds, downMicros, ownDownSeconds, ownDownMicros;
    BOOL downKnown, ownDownKnown, inactivePending;
    LONG pressDelta;
    LONG inactiveX, inactiveY;
    struct MUI_CustomClass *eventClass;
    Object *eventObject;
    struct MUI_EventHandlerNode eventNode;
    BOOL pending;
    BOOL registered, timerOpen, timerPending, quit, open, healthy, dirty, disabled;
    char portName[64];
};
/* Capture the actual deactivation message, not later global pointer state.
 * All fields are scalars; the borrowed IntuiMessage is never retained. */
BOOPSI_DISPATCHER(IPTR, IndicatorEvents, cl, object, message)
{
    if (message->MethodID==MUIM_HandleEvent) {
        struct MUIP_HandleEvent *event=(struct MUIP_HandleEvent *)message;
        IPTR data=0;
        GetAttr(MUIA_UserData,object,&data);
        if (data && event->imsg && event->imsg->Class==IDCMP_MOUSEBUTTONS && event->imsg->Code==SELECTDOWN) {
            struct Indicator *s=(struct Indicator *)data;
            s->ownDownKnown=TRUE; s->ownDownSeconds=event->imsg->Seconds; s->ownDownMicros=event->imsg->Micros;
        }
        if (data && event->imsg && event->imsg->Class==IDCMP_INACTIVEWINDOW) {
            struct Indicator *s=(struct Indicator *)data;
            s->inactiveSeconds=event->imsg->Seconds; s->inactiveMicros=event->imsg->Micros;
            ++s->inactiveEvents; s->inactiveValid=TRUE; s->inactiveQualifier=event->imsg->Qualifier;
            s->inactiveX=event->imsg->MouseX+event->imsg->IDCMPWindow->LeftEdge;
            s->inactiveY=event->imsg->MouseY+event->imsg->IDCMPWindow->TopEdge;
        }
        return 0;
    }
    return DoSuperMethodA(cl,object,message);
}
BOOPSI_DISPATCHER_END
static ULONG now_ms(void)
{
    struct EClockVal value;
    ULONG frequency=ReadEClock(&value);
    unsigned long long ticks=((unsigned long long)value.ev_hi<<32)|value.ev_lo;
    return frequency ? (ULONG)((ticks/frequency)*1000+(ticks%frequency)*1000/frequency) : 0;
}
static BOOL glyph_rectangle(struct Indicator *s, LONG *left, LONG *top, ULONG *width, ULONG *height)
{
    struct AppIconGeometry geometry={0};
    struct TagItem tags[]={{WBCTRLA_QueryAppIconGeometry,(IPTR)&geometry},{TAG_DONE,0}};
    geometry.aig_Icon=s->icon;
    if (WorkbenchControlA(NULL,tags) && (geometry.aig_Flags&APPICONGF_VALID)) {
        *left=geometry.aig_Left; *top=geometry.aig_Top;
        *width=geometry.aig_Width; *height=geometry.aig_Height;
        return TRUE;
    }
    return FALSE;
}
static void panel(struct Indicator *s, BOOL open)
{
    int left, top;
    if (open && s->disabled) return;
    if (open) {
        LONG x,y,gx,gy;
        s->glyphDismissed=FALSE; s->inactiveValid=FALSE; s->downKnown=FALSE; s->ownDownKnown=FALSE;
        ULONG gw,gh;
        if (!glyph_rectangle(s,&gx,&gy,&gw,&gh)) {
            SetAttrs(s->notice,MUIA_Text_Contents,(IPTR)"Indicator position unavailable",TAG_DONE);
            return;
        }
        x=gx; y=gy+(LONG)gh;
        IndicatorAnchor(x,y,s->screen->Width,s->screen->Height,320,100,&left,&top);
        s->anchorGlyphLeft=gx; s->anchorGlyphBottom=gy+(LONG)gh;
        SetAttrs(s->window,MUIA_Window_LeftEdge,left,MUIA_Window_TopEdge,top,TAG_DONE);
    }
    SetAttrs(s->window,MUIA_Window_Open,open,TAG_DONE);
    { IPTR actual=FALSE; GetAttr(MUIA_Window_Open,s->window,&actual); s->open=actual!=0; }
    if (s->open) {
        struct Window *native=NULL;
        GetAttr(MUIA_Window_Window,s->window,(IPTR *)&native);
        if (native) {
            IndicatorAnchor(s->anchorGlyphLeft,s->anchorGlyphBottom,s->screen->Width,
                s->screen->Height,native->Width,native->Height,&left,&top);
            MoveWindow(native,left-native->LeftEdge,top-native->TopEdge);
            s->anchorLeft=native->LeftEdge; s->anchorTop=native->TopEdge;
        }
    }
}
/* ENV is the RAM-backed applied preferences store. Sample at most twice a
 * second; preserve commodity changes until a different preference is applied. */
static void apply_preferences(struct Indicator *s)
{
    unsigned char bytes[INDICATORS_PREFS_SIZE+1];
    unsigned mask=INDICATORS_ALL_ENABLED;
    ULONG now=now_ms();
    BPTR fh;
    LONG n;
    if (s->prefsRead && (ULONG)(now-s->prefsAt)<500) return;
    s->prefsAt=now;
    fh=Open("ENV:" INDICATORS_PREFS_PATH, MODE_OLDFILE);
    if (fh) {
        n=Read(fh,bytes,sizeof(bytes)); Close(fh);
        if (n<0 || !IndicatorsPrefsDecode(bytes,(size_t)n,&mask)) return;
    } else if (IoErr()!=ERROR_OBJECT_NOT_FOUND && IoErr()!=ERROR_DIR_NOT_FOUND) return;
    if (!s->prefsRead || mask!=s->prefsMask) {
        BOOL disabled=!(mask & (1U << s->kind));
        s->prefsRead=TRUE; s->prefsMask=mask;
        if (s->disabled!=disabled) {
            s->disabled=disabled; s->soundDirty=FALSE; s->pending=TRUE; s->dirty=TRUE;
            if (disabled) panel(s,FALSE);
            ActivateCxObj(s->broker,!disabled);
        }
    }
}
static void refresh(struct Indicator *s)
{
    struct RastPort rp;
    struct AppIconPublish publish={s->icon,s->bitmap};
    struct TagItem tags[]={{WBCTRLA_PublishAppIcon,(IPTR)&publish},{TAG_DONE,0}};
    BOOL healthy=!s->disabled && IndicatorHealthy(&s->state,now_ms());
    if (s->healthy!=healthy) {
        s->dirty=TRUE;
        if (!healthy) {
            struct TagItem suspend[]={{WBCTRLA_SuspendAppIcon,(IPTR)s->icon},{TAG_DONE,0}};
            if (WorkbenchControlA(NULL,suspend)) ++s->suspends;
        }
    }
    s->healthy=healthy;
    if (s->kind==IND_NETWORK && s->wifiVisible!=healthy) {
        DoMethod(s->extensions,MUIM_Group_InitChange);
        SetAttrs(s->wifiGroup,MUIA_ShowMe,healthy,TAG_DONE);
        DoMethod(s->extensions,MUIM_Group_ExitChange);
        s->wifiVisible=healthy;
    }
    if (s->kind==IND_SOUND) {
        BOOL valid=healthy && (s->state.fields&IND_FIELD_VALUE);
        SetAttrs(s->volume,MUIA_Disabled,!valid,TAG_DONE);
        SetAttrs(s->mute,MUIA_Disabled,!valid,TAG_DONE);
        if (valid && !s->soundDirty && !TileSourcePending(&s->outgoing,&s->controlOwner))
            SetAttrs(s->volume,MUIA_Numeric_Value,s->state.value,TAG_DONE);
    }
    if (s->kind==IND_BATTERY) {
        char display[192];
        unsigned i;
        snprintf(display,sizeof(display),"%s",IndicatorAuxText(&s->cpu,now_ms()));
        /* Keep each consumer on its own row, including bounded long names. */
        for (i=0;display[i];++i) if (display[i]==';' && display[i+1]==' ') display[i]='\n';
        if (strcmp(display,s->cpuDisplay)) {
            memcpy(s->cpuDisplay,display,strlen(display)+1);
            SetAttrs(s->consumers,MUIA_Text_Contents,(IPTR)s->cpuDisplay,TAG_DONE);
        }
    }
    if (s->dirty) {
        SetAttrs(s->text,MUIA_Text_Contents,(IPTR)(healthy?s->state.text:"Service unavailable"),TAG_DONE);
        s->dirty=FALSE;
    }
    /* Fresh source events publish frames; lease expiry explicitly suspends
     * the same registration without renewing stale pixels. */
    if (!healthy || !s->pending || (ULONG)(now_ms()-s->lastDraw)<500) return;
    {
        struct AppIconQuery query={0};
        struct TagItem queryTags[]={{WBCTRLA_QueryAppIcon,(IPTR)&query},{TAG_DONE,0}};
        query.aiq_Icon=s->icon;
        if (!WorkbenchControlA(NULL,queryTags) || !(query.aiq_Flags&APPICONQF_VISIBLE)) return;
    }
    InitRastPort(&rp); rp.BitMap=s->bitmap;
    SetAPen(&rp,0); RectFill(&rp,0,0,63,63);
    SetAPen(&rp,1);
    if (!(s->state.fields&(IND_FIELD_VALUE|IND_FIELD_LINK))) {
        RectFill(&rp,29,15,34,40); RectFill(&rp,29,46,34,51);
    } else if (s->kind==IND_SOUND) {
        RectFill(&rp,10,26,20,38);
        Move(&rp,20,26); Draw(&rp,29,18); Draw(&rp,29,46); Draw(&rp,20,38);
        if (!s->state.value) { Move(&rp,38,26); Draw(&rp,50,38); Move(&rp,38,38); Draw(&rp,50,26); }
        else for (unsigned bar=0;bar<3;++bar)
            if (s->state.value>bar*33) RectFill(&rp,36+bar*7,25-bar*5,39+bar*7,39+bar*5);
    } else if (s->kind==IND_BLUETOOTH) {
        Move(&rp,32,12); Draw(&rp,44,24); Draw(&rp,20,44);
        Move(&rp,32,12); Draw(&rp,32,52); Draw(&rp,44,40); Draw(&rp,20,20);
        if (!s->state.value) { Move(&rp,14,48); Draw(&rp,50,12); }
    } else if (s->kind==IND_NETWORK) {
        Move(&rp,16,24); Draw(&rp,32,16); Draw(&rp,48,24);
        Move(&rp,23,33); Draw(&rp,32,28); Draw(&rp,41,33);
        RectFill(&rp,30,40,34,44);
        if (!s->state.value) { Move(&rp,14,48); Draw(&rp,50,12); }
    } else {
        RectFill(&rp,8,20,55,43); SetAPen(&rp,0); RectFill(&rp,10,22,53,41);
        SetAPen(&rp,1);
        if (s->state.charging) { RectFill(&rp,29,12,34,51); RectFill(&rp,21,25,42,30); }
        else if (s->state.value) RectFill(&rp,12,24,12+(s->state.value*39)/100,39);
    }
    if (WorkbenchControlA(NULL,tags)) { s->pending=FALSE; s->lastDraw=now_ms(); ++s->draws; }
}
static void reply_command(struct RexxMsg *m, LONG error, const char *text)
{
    m->rm_Result1=error; m->rm_Result2=0;
    if (!error && (m->rm_Action&RXFF_RESULT)) {
        m->rm_Result2=(IPTR)CreateArgstring((CONST_STRPTR)text,strlen(text));
        if (!m->rm_Result2) m->rm_Result1=20;
    }
    ReplyMsg((struct Message *)m);
}
static BOOL apply_event(struct Indicator *s, const char *event)
{
    struct IndicatorState next=s->state;
    BOOL changed=!IndicatorHealthy(&s->state,now_ms());
    if (!IndicatorApply(&next,s->kind,event,now_ms())) return FALSE;
    changed|=next.fields!=s->state.fields || next.value!=s->state.value || next.charging!=s->state.charging ||
             strcmp(next.text,s->state.text)!=0;
    s->state=next; ++s->generation; s->dirty=TRUE; s->pending=TRUE;
    if (changed) s->notifyDirty=TRUE;
    return TRUE;
}
static void source_result(APTR owner, BOOL setting, BOOL success, CONST_STRPTR text, APTR context)
{
    struct Indicator *s=context;
    (void)setting;
    if (owner==&s->subscribeOwner) { s->subscribed=success && !s->quit; return; }
    if (owner==&s->notifyOwner) {
        if (!success) s->notifyDirty=TRUE;
        return;
    }
    if (owner==&s->controlOwner) {
        struct IndicatorState confirmed=s->state;
        BOOL valid=success && IndicatorApply(&confirmed,s->kind,(const char *)text,now_ms()) &&
                   (confirmed.fields&IND_FIELD_VALUE) && confirmed.value==s->controlValue;
        /* A newer push is authoritative; a delayed SET reply cannot regress it. */
        if (valid && s->generation==s->controlGeneration) valid=apply_event(s,(const char *)text);
        if (s->controlCaller) {
            reply_command(s->controlCaller,valid?0:10,valid?"Confirmed":"");
            s->controlCaller=NULL;
        }
        SetAttrs(s->notice,MUIA_Text_Contents,(IPTR)(valid?"":"Control unavailable"),TAG_DONE);
        return;
    }
    SetAttrs(s->notice,MUIA_Text_Contents,(IPTR)(success?"":"Settings unavailable"),TAG_DONE);
}
static BOOL set_control(struct Indicator *s, struct RexxMsg *caller, const char *command)
{
    unsigned value;
    if (!IndicatorSetParse(s->kind,command,&value) || s->controlCaller ||
        TileSourcePending(&s->outgoing,&s->controlOwner)) return FALSE;
    s->controlGeneration=s->generation; s->controlValue=value;
    if (!TileSourceSend(&s->outgoing,&s->controlOwner,TRUE,
                       (CONST_STRPTR)s->providerPort,(CONST_STRPTR)command,now_ms())) return FALSE;
    s->controlCaller=caller;
    return TRUE;
}
static Object *sound_panel(struct Indicator *s)
{
    if (s->kind!=IND_SOUND) return (Object *)VGroup,MUIA_ShowMe,FALSE,End;
    return (Object *)VGroup,
        Child,(IPTR)(Object *)TextObject,MUIA_Text_Contents,(IPTR)"Volume",End,
        Child,(IPTR)(s->volume=(Object *)SliderObject,SliderFrame,
            MUIA_Numeric_Min,0,MUIA_Numeric_Max,100,MUIA_Slider_Horiz,TRUE,
            MUIA_Disabled,TRUE,End),
        Child,(IPTR)(s->mute=(Object *)SimpleButton("Mute")),
        Child,(IPTR)(Object *)TextObject,MUIA_Text_Contents,(IPTR)"Output selection unavailable",End,
    End;
}
static void sound_command(struct Indicator *s, IPTR action)
{
    IPTR value=0;
    if (s->kind!=IND_SOUND || !IndicatorHealthy(&s->state,now_ms()) ||
        !(s->state.fields&IND_FIELD_VALUE)) return;
    if (action==200) GetAttr(MUIA_Numeric_Value,s->volume,&value);
    if (value>100) return;
    if (s->state.value!=(unsigned)value || TileSourcePending(&s->outgoing,&s->controlOwner)) {
        s->soundWanted=(unsigned)value; s->soundDirty=TRUE;
    }
}
static void sound_send(struct Indicator *s)
{
    char command[32];
    if (!s->soundDirty || s->quit || s->controlCaller ||
        TileSourcePending(&s->outgoing,&s->controlOwner)) return;
    s->soundDirty=FALSE;
    if (!IndicatorHealthy(&s->state,now_ms()) || !(s->state.fields&IND_FIELD_VALUE)) return;
    snprintf(command,sizeof(command),"SET LEVEL %u",s->soundWanted);
    if (set_control(s,NULL,command))
        SetAttrs(s->notice,MUIA_Text_Contents,(IPTR)"Changing volume...",TAG_DONE);
    else SetAttrs(s->notice,MUIA_Text_Contents,(IPTR)"Audio control unavailable",TAG_DONE);
}
static void battery_subscription(struct Indicator *s)
{
    char command[96];
    if (s->kind!=IND_BATTERY || TileSourcePending(&s->outgoing,&s->subscribeOwner)) return;
    if (s->quit) {
        if (s->unsubscribeSent) return;
        s->unsubscribeSent=TRUE;
        snprintf(command,sizeof(command),"UNSUBSCRIBE %s",s->portName);
    } else {
        if ((s->subscribed && IndicatorHealthy(&s->state,now_ms())) ||
            (s->subscribeTried && (ULONG)(now_ms()-s->subscribeAt)<2000)) return;
        s->subscribeTried=TRUE; s->subscribeAt=now_ms();
        snprintf(command,sizeof(command),"SUBSCRIBE %s",s->portName);
    }
    TileSourceSend(&s->outgoing,&s->subscribeOwner,FALSE,(CONST_STRPTR)s->providerPort,
                   (CONST_STRPTR)command,now_ms());
}
static void notify_tiles(struct Indicator *s)
{
    char command[96];
    if (!s->notifyDirty || !s->tileId[0] || s->quit ||
        TileSourcePending(&s->outgoing,&s->notifyOwner) ||
        (s->notifyTried && (ULONG)(now_ms()-s->notifyAttempt)<1000)) return;
    s->notifyAttempt=now_ms(); s->notifyTried=TRUE;
    snprintf(command,sizeof(command),"TILE CHANGED %s",s->tileId);
    if (TileSourceSend(&s->outgoing,&s->notifyOwner,FALSE,(CONST_STRPTR)"TILES",
                       (CONST_STRPTR)command,now_ms())) s->notifyDirty=FALSE;
}
/* Settings already running is asked to show the page; otherwise it is
   started on it, in the background. */
static BOOL show_settings(struct Indicator *s)
{
    BOOL running, sent=FALSE;
    Forbid();
    running=FindPort((CONST_STRPTR)s->settingsPort)!=NULL;
    Permit();
    if (running)
        sent=TileSourceSend(&s->outgoing,s,TRUE,(CONST_STRPTR)s->settingsPort,
                            (CONST_STRPTR)s->settingsCommand,now_ms());
    else {
        char command[96];
        BPTR in=Open((CONST_STRPTR)"NIL:",MODE_OLDFILE), out=Open((CONST_STRPTR)"NIL:",MODE_NEWFILE);
        /* settingsCommand is "SHOW PAGE <kind>" */
        snprintf(command,sizeof(command),"SYS:Prefs/Settings PAGE %s",s->settingsCommand+10);
        if (in && out && SystemTags((CONST_STRPTR)command,SYS_Input,(IPTR)in,SYS_Output,(IPTR)out,
                                    SYS_Asynch,TRUE,TAG_DONE)!=-1) { sent=TRUE; in=out=BNULL; }
        if (in) Close(in);
        if (out) Close(out);
    }
    if (!sent) SetAttrs(s->notice,MUIA_Text_Contents,(IPTR)"Settings unavailable or busy",TAG_DONE);
    return sent;
}
static void commands(struct Indicator *s)
{
    struct RexxMsg *m;
    unsigned budget=32;
    while (budget-- && (m=(struct RexxMsg *)GetMsg(s->commands))) {
        char stats[320];
        const char *p, *answer="Accepted";
        LONG error=0;
        if (!IsRexxMsg(m) || !m->rm_Args[0]) { ReplyMsg((struct Message *)m); continue; }
        p=(const char *)m->rm_Args[0];
        if (s->quit) error=10;
        else if (!strcmp(p,"PANEL")) { if (s->disabled) error=10; else panel(s,!s->open); }
        else if (!strcmp(p,"HIDE")) panel(s,FALSE);
        else if (!strcmp(p,"SETTINGS")) { if (!show_settings(s)) error=10; }
        else if (!strcmp(p,"QUIT")) s->quit=TRUE;
        else if (!strncmp(p,"WIFI ",5)) {
            if ((IndicatorHealthy(&s->state,now_ms()) || !strcmp(p,"WIFI STATUS")) &&
                IndicatorWifiCommand(s->wifi,p,stats,sizeof(stats))) {
                if (!strcmp(p,"WIFI STATUS")) answer=stats;
                else if (!strcmp(p,"WIFI OTHER")) panel(s,TRUE);
            } else error=10;
        }
        else if (!strcmp(p,"COMMANDS")) {
            BOOL provider;
            Forbid(); provider=FindPort((CONST_STRPTR)s->providerPort)!=NULL; Permit();
            const char *typed=s->kind==IND_BATTERY?"GET PERCENT\nGET TOP":s->kind==IND_SOUND?
                "GET LEVEL\nSET LEVEL <0..100>":s->kind==IND_NETWORK?
                (!IndicatorHealthy(&s->state,now_ms())?"":provider?"GET RADIO\nSET RADIO on\nSET RADIO off\nWIFI STATUS\nWIFI SCAN\nWIFI CONNECT\nWIFI OTHER\nWIFI DISCONNECT":
                          "GET RADIO\nWIFI STATUS\nWIFI SCAN\nWIFI CONNECT\nWIFI OTHER\nWIFI DISCONNECT"):
                (provider?"GET RADIO\nSET RADIO on\nSET RADIO off":"GET RADIO");
            snprintf(stats,sizeof(stats),"PANEL\nHIDE\nGET\n%s\nSETTINGS\nCOMMANDS\nQUIT",typed);
            answer=stats;
        }
        else if (!strcmp(p,"STATS")) {
            snprintf(stats,sizeof(stats),"healthy=%u panel=%u draws=%lu suspends=%lu anchor=%ld,%ld glyph=%ld,%ld disabled=%u",
                (unsigned)IndicatorHealthy(&s->state,now_ms()),(unsigned)s->open,(unsigned long)s->draws,(unsigned long)s->suspends,
                (long)s->anchorLeft,(long)s->anchorTop,(long)s->anchorGlyphLeft,(long)s->anchorGlyphBottom,(unsigned)s->disabled);
            if (s->kind==IND_SOUND && s->open) {
                IPTR x=0,y=0,w=0,h=0;
                size_t n=strlen(stats);
                GetAttr(MUIA_LeftEdge,s->mute,&x); GetAttr(MUIA_TopEdge,s->mute,&y);
                GetAttr(MUIA_Width,s->mute,&w); GetAttr(MUIA_Height,s->mute,&h);
                snprintf(stats+n,sizeof(stats)-n," mute=%ld,%ld,%lu,%lu",(long)(s->anchorLeft+(LONG)x),
                    (long)(s->anchorTop+(LONG)y),(unsigned long)w,(unsigned long)h);
            }
            if (s->kind==IND_NETWORK) {
                IPTR visible=0; size_t n=strlen(stats);
                GetAttr(MUIA_ShowMe,s->wifiGroup,&visible);
                snprintf(stats+n,sizeof(stats)-n," network-actions=%u",(unsigned)!!visible);
            }
            {
                size_t n=strlen(stats);
                snprintf(stats+n,sizeof(stats)-n," deact=%lu q=%u xy=%ld,%ld pressdelta=%ld",(unsigned long)s->inactiveEvents,
                    (unsigned)s->inactiveQualifier,(long)s->inactiveX,(long)s->inactiveY,(long)s->pressDelta);
            }
            answer=stats;
        }
        else if (!strcmp(p,"GET TOP") && s->kind==IND_BATTERY) {
            answer=IndicatorAuxText(&s->cpu,now_ms());
        }
        else if (!strncmp(p,"GET ",4)) {
            if (IndicatorTypedGet(&s->state,s->kind,p,now_ms(),stats,sizeof(stats))) answer=stats;
            else error=10;
        }
        else if (!strncmp(p,"SET ",4)) {
            if (set_control(s,m,p)) continue; /* caller remains borrowed until confirmation/timeout */
            error=10;
        }
        else if (!strcmp(p,"GET")) {
            if (IndicatorHealthy(&s->state,now_ms())) answer=s->state.text;
            else error=10;
        } else if (!strncmp(p,"TOP ",4)) {
            if (s->kind!=IND_BATTERY || !IndicatorAuxUpdate(&s->cpu,p+4,now_ms())) error=10;
        } else if (!strncmp(p,"UPDATE ",7)) {
            if (!apply_event(s,p+7)) error=10;
        } else error=10;
        reply_command(m,error,answer);
    }
    /* A bounded drain must preserve its wake-up when messages remain. */
    if (s->commands->mp_MsgList.lh_Head->ln_Succ) Signal(FindTask(NULL),1UL<<s->commands->mp_SigBit);
}
static void stop_timer(struct Indicator *s)
{
    if (!s->timerPending) return;
    if (!CheckIO((struct IORequest *)s->timer)) AbortIO((struct IORequest *)s->timer);
    WaitIO((struct IORequest *)s->timer); s->timerPending=FALSE;
    SetSignal(0,1UL<<s->clock->mp_SigBit);
}
int main(int argc, char **argv)
{
    Object *wifiGroup=NULL, *soundGroup=NULL, *extensions=NULL;
    ULONG wifiSignals=0;
    struct Indicator s={0};
    struct NewBroker nb={NB_VERSION,NULL,(CONST_STRPTR)"System indicator",(CONST_STRPTR)"Desktop system indicator",NBU_UNIQUE|NBU_NOTIFY,0,0,NULL,0};
    CxObj *filter, *sender, *translate, *mouseFilter, *mouseSender;
    int result=RETURN_FAIL;
    if (argc!=4 || (strcmp(argv[1],"battery") && strcmp(argv[1],"network") &&
                    strcmp(argv[1],"bluetooth") && strcmp(argv[1],"sound"))) {
        puts("Usage: Indicator battery|network|bluetooth|sound icon-path hotkey\nProvider port: INDICATOR.NAME; UPDATE percent charging text");
        return RETURN_ERROR;
    }
    s.kind=!strcmp(argv[1],"battery")?IND_BATTERY:!strcmp(argv[1],"network")?IND_NETWORK:
           !strcmp(argv[1],"bluetooth")?IND_BLUETOOTH:IND_SOUND;
    snprintf(s.providerPort,sizeof(s.providerPort),"INDICATOR.SOURCE.%s",argv[1]);
    {
        char key[96], value[64]; LONG length;
        snprintf(key,sizeof(key),"Indicators/%s/port",argv[1]);
        length=GetVar((CONST_STRPTR)key,(STRPTR)value,sizeof(value),GVF_GLOBAL_ONLY);
        if (length>0 && length<(LONG)sizeof(value)-1) {
            BOOL valid=TRUE;
            for (LONG i=0;i<length;++i) if ((unsigned char)value[i]<33 || (unsigned char)value[i]>126) valid=FALSE;
            if (valid) { value[length]=0; strcpy(s.providerPort,value); }
        }
        snprintf(key,sizeof(key),"Indicators/%s/tile",argv[1]);
        length=GetVar((CONST_STRPTR)key,(STRPTR)value,sizeof(value),GVF_GLOBAL_ONLY);
        if (length>0 && length<(LONG)sizeof(value)-1) {
            BOOL valid=TRUE;
            for (LONG i=0;i<length;++i) if (!((value[i]>='a' && value[i]<='z') ||
                (value[i]>='A' && value[i]<='Z') || (value[i]>='0' && value[i]<='9') ||
                value[i]=='_' || value[i]=='-' || value[i]=='.')) valid=FALSE;
            if (valid) { value[length]=0; strcpy(s.tileId,value); }
        }
    }
    snprintf(s.portName,sizeof(s.portName),"INDICATOR.%s",argv[1]);
    snprintf(s.settingsCommand,sizeof(s.settingsCommand),"SHOW PAGE %s",argv[1]);
    strcpy(s.settingsPort,"SETTINGS");
    {
        char name[64];
        LONG length=GetVar((CONST_STRPTR)"Indicators/settings/port",(STRPTR)name,sizeof(name),GVF_GLOBAL_ONLY);
        if (length>0 && length<(LONG)sizeof(name)-1) {
            BOOL valid=TRUE;
            for (LONG i=0;i<length;++i) if ((unsigned char)name[i]<33 || (unsigned char)name[i]>126) valid=FALSE;
            if (valid) { name[length]=0; strcpy(s.settingsPort,name); }
        }
    }
    RexxSysBase=(struct RxsLib *)OpenLibrary((CONST_STRPTR)"rexxsyslib.library",0);
    if (!RexxSysBase) goto cleanup;
    if (!TileSourcesInit(&s.outgoing)) goto cleanup;
    s.commands=CreateMsgPort(); s.apps=CreateMsgPort(); s.cx=CreateMsgPort(); s.clock=CreateMsgPort();
    if (!s.commands || !s.apps || !s.cx || !s.clock) goto cleanup;
    s.timer=(struct timerequest *)CreateIORequest(s.clock,sizeof(*s.timer));
    if (!s.timer || OpenDevice((CONST_STRPTR)TIMERNAME,UNIT_MICROHZ,(struct IORequest *)s.timer,0)) goto cleanup;
    s.timerOpen=TRUE; TimerBase=s.timer->tr_node.io_Device;
    s.screen=LockPubScreen(NULL); if (!s.screen) goto cleanup;
    s.fallback=GetDiskObject((CONST_STRPTR)argv[2]); if (!s.fallback) goto cleanup;
    s.bitmap=AllocBitMap(64,64,GetBitMapAttr(s.screen->RastPort.BitMap,BMA_DEPTH),BMF_CLEAR,s.screen->RastPort.BitMap);
    if (!s.bitmap) goto cleanup;
    {
        struct RastPort rp;
        InitRastPort(&rp); rp.BitMap=s.bitmap;
        SetAPen(&rp,1); RectFill(&rp,29,15,34,40); RectFill(&rp,29,46,34,51);
    }
    s.lastDraw=now_ms();
    {
        struct AppIconRegions regions={1,{{0,0,64,64,"panel"}}};
        struct TagItem tags[]={{WBAPPICONA_Cells,WBAPPICON_CELLS(1,1)},
            {WBAPPICONA_Surface,(IPTR)s.bitmap},{WBAPPICONA_Regions,(IPTR)&regions},
            {WBAPPICONA_Cadence,500},{TAG_DONE,0}};
        s.icon=AddAppIconA(1+s.kind,0,argv[1],s.apps,0,s.fallback,tags);
    }
    if (!s.icon) goto cleanup;
    s.application=(Object *)ApplicationObject,
        MUIA_Application_Title,(IPTR)argv[1],MUIA_Application_Base,(IPTR)s.portName,
        SubWindow,(IPTR)(s.window=(Object *)WindowObject,
            MUIA_Window_Title,(IPTR)argv[1],MUIA_Window_Borderless,TRUE,
            MUIA_Window_Screen,(IPTR)s.screen,
            MUIA_Window_Width,320,
            WindowContents,(IPTR)VGroup,
                Child,(IPTR)(s.text=(Object *)TextObject,MUIA_Text_Contents,(IPTR)"Service unavailable",End),
                Child,(IPTR)(s.consumers=(Object *)TextObject,MUIA_ShowMe,s.kind==IND_BATTERY,
                    MUIA_Text_Contents,(IPTR)"CPU data unavailable",End),
                Child,(IPTR)(extensions=(Object *)VGroup,End),
                Child,(IPTR)(s.notice=(Object *)TextObject,MUIA_Text_Contents,(IPTR)"",End),
                Child,(IPTR)(s.settings=(Object *)SimpleButton("Settings...")),
            End,
        End),
    End;
    if (!s.application) goto cleanup;
    wifiGroup=s.kind==IND_NETWORK?IndicatorWifiCreate(&s.wifi):
        (Object *)VGroup,MUIA_ShowMe,FALSE,End;
    if (!wifiGroup) goto cleanup;
    s.wifiGroup=wifiGroup; s.extensions=extensions;
    if (s.kind==IND_NETWORK) SetAttrs(wifiGroup,MUIA_ShowMe,FALSE,TAG_DONE);
    DoMethod(extensions,OM_ADDMEMBER,(IPTR)wifiGroup);
    soundGroup=sound_panel(&s); if (!soundGroup) goto cleanup;
    DoMethod(extensions,OM_ADDMEMBER,(IPTR)soundGroup);
    s.eventClass=MUI_CreateCustomClass(NULL,(ClassID)MUIC_Area,NULL,0,(APTR)IndicatorEvents);
    if (!s.eventClass) goto cleanup;
    s.eventObject=NewObject(s.eventClass->mcc_Class,NULL,MUIA_UserData,(IPTR)&s,MUIA_ShowMe,FALSE,TAG_DONE);
    if (!s.eventObject) goto cleanup;
    DoMethod(extensions,OM_ADDMEMBER,(IPTR)s.eventObject);
    s.eventNode.ehn_Events=IDCMP_INACTIVEWINDOW|IDCMP_MOUSEBUTTONS; s.eventNode.ehn_Priority=10;
    s.eventNode.ehn_Class=s.eventClass->mcc_Class; s.eventNode.ehn_Object=s.eventObject;
    DoMethod(s.window,MUIM_Window_AddEventHandler,(IPTR)&s.eventNode); s.eventAttached=TRUE;

    DoMethod(s.window,MUIM_Notify,MUIA_Window_CloseRequest,TRUE,s.application,2,MUIM_Application_ReturnID,1);
    DoMethod(s.window,MUIM_Notify,MUIA_Window_Activate,FALSE,s.application,2,MUIM_Application_ReturnID,3);
    DoMethod(s.settings,MUIM_Notify,MUIA_Pressed,FALSE,s.application,2,MUIM_Application_ReturnID,2);
    IndicatorWifiBind(s.wifi,s.application);
    if (s.kind==IND_SOUND) {
        DoMethod(s.volume,MUIM_Notify,MUIA_Numeric_Value,MUIV_EveryTime,s.application,2,MUIM_Application_ReturnID,200);
        DoMethod(s.mute,MUIM_Notify,MUIA_Pressed,FALSE,s.application,2,MUIM_Application_ReturnID,201);
    }
    nb.nb_Name=(CONST_STRPTR)s.portName; nb.nb_Port=s.cx;
    s.broker=CxBroker(&nb,NULL); if (!s.broker) goto cleanup;
    filter=CxFilter(argv[3]); if (!filter) goto cleanup;
    AttachCxObj(s.broker,filter);
    sender=CxSender(s.cx,1); if (!sender) goto cleanup; AttachCxObj(filter,sender);
    translate=CxTranslate(NULL); if (!translate) goto cleanup; AttachCxObj(filter,translate);
    if (CxObjError(s.broker) || CxObjError(filter)) goto cleanup;
    {
        struct InputXpression expression={IX_VERSION,IECLASS_RAWMOUSE,SELECTDOWN,0xffff,0,0,0};
        mouseFilter=CxFilter(NULL); if (!mouseFilter) goto cleanup;
        AttachCxObj(s.broker,mouseFilter); SetFilterIX(mouseFilter,&expression);
        mouseSender=CxSender(s.cx,2); if (!mouseSender) goto cleanup;
        AttachCxObj(mouseFilter,mouseSender);
        if (CxObjError(mouseFilter)) goto cleanup;
    }
    ActivateCxObj(s.broker,TRUE);
    s.commands->mp_Node.ln_Name=s.portName;
    Forbid();
    if (FindPort((CONST_STRPTR)s.portName)) { Permit(); goto cleanup; }
    AddPort(s.commands); s.registered=TRUE; Permit();
    s.dirty=TRUE;
    IndicatorWifiStart(s.wifi);
    while (!s.quit || !TileSourcesIdle(&s.outgoing) || !IndicatorWifiIdle(s.wifi)) {
        ULONG muiSignals=0, signals;
        struct AppMessage *appMessage;
        CxMsg *cxMessage;
        IPTR action;
        while ((action=DoMethod(s.application,MUIM_Application_NewInput,&muiSignals))) {
            if ((action==200 || action==201) && !s.quit && !s.disabled) sound_command(&s,action);
            else if (action>=100 && action<=104 && !s.quit && !s.disabled &&
                     IndicatorHealthy(&s.state,now_ms())) IndicatorWifiAction(s.wifi,action);
            else if (action==2 && !s.quit) show_settings(&s);
            else {
                if (action==3 && s.open) s.inactivePending=s.inactiveValid;
                panel(&s,FALSE);
            }
        }
        TileSourcesCollect(&s.outgoing,source_result,&s);
        TileSourcesExpire(&s.outgoing,now_ms(),source_result,&s);
        while ((cxMessage=(CxMsg *)GetMsg(s.cx))) {
            ULONG type=CxMsgType(cxMessage), id=CxMsgID(cxMessage);
            if (type==CXM_IEVENT && id==2) {
                struct InputEvent *event=CxMsgData(cxMessage);
                if (event && event->ie_Class==IECLASS_RAWMOUSE && event->ie_Code==SELECTDOWN) {
                    s.downKnown=TRUE; s.downSeconds=event->ie_TimeStamp.tv_secs; s.downMicros=event->ie_TimeStamp.tv_micro;
                }
                ReplyMsg((struct Message *)cxMessage); continue;
            }
            ReplyMsg((struct Message *)cxMessage);
            if ((type==CXM_IEVENT && id==1) || (type==CXM_COMMAND && id==CXCMD_APPEAR)) panel(&s,!s.open);
            else if (type==CXM_COMMAND && id==CXCMD_DISAPPEAR) panel(&s,FALSE);
            else if (type==CXM_COMMAND && id==CXCMD_KILL) s.quit=TRUE;
            else if (type==CXM_COMMAND && id==CXCMD_DISABLE) {
                s.disabled=TRUE; s.soundDirty=FALSE; panel(&s,FALSE);
                ActivateCxObj(s.broker,FALSE);
            } else if (type==CXM_COMMAND && id==CXCMD_ENABLE) {
                s.disabled=FALSE; s.pending=TRUE; s.dirty=TRUE;
                ActivateCxObj(s.broker,TRUE);
            }
        }
        if (s.inactivePending) {
            uint64_t deactivated=(uint64_t)s.inactiveSeconds*1000000+s.inactiveMicros;
            uint64_t down=(uint64_t)s.downSeconds*1000000+s.downMicros;
            uint64_t own=(uint64_t)s.ownDownSeconds*1000000+s.ownDownMicros;
            LONG gx,gy; ULONG gw,gh;
            /* Intuition may defer activation and omit the pressed qualifier.
             * Correlate event creation times, independent of release duration
             * or UI queue delay; reject clicks delivered inside our own panel. */
            BOOL mouse=s.downKnown && deactivated>=down && deactivated-down<50000 &&
                !(s.ownDownKnown && own>=down && own-down<50000);
            s.pressDelta=s.downKnown && deactivated>=down && deactivated-down<1000000?
                (LONG)(deactivated-down):-1;
            s.glyphDismissed=mouse && glyph_rectangle(&s,&gx,&gy,&gw,&gh) &&
                s.inactiveX>=gx && s.inactiveX<gx+(LONG)gw && s.inactiveY>=gy && s.inactiveY<gy+(LONG)gh;
            s.inactivePending=FALSE; s.inactiveValid=FALSE;
        }
        while ((appMessage=(struct AppMessage *)GetMsg(s.apps))) {
            if (appMessage->am_Class==AMCLASSICON_Region) {
                BOOL dismiss=s.glyphDismissed;
                s.glyphDismissed=FALSE;
                panel(&s,dismiss?FALSE:!s.open);
            }
            ReplyMsg(&appMessage->am_Message);
        }
        commands(&s);
        {
            char event[192];
            if (IndicatorWifiStep(s.wifi,now_ms(),wifiSignals,s.quit,event,sizeof(event))) apply_event(&s,event);
            wifiSignals=0;
        }
        apply_preferences(&s); battery_subscription(&s); sound_send(&s); refresh(&s); notify_tiles(&s);
        if (s.quit && TileSourcesIdle(&s.outgoing) && IndicatorWifiIdle(s.wifi)) break;
        s.timer->tr_node.io_Command=TR_ADDREQUEST;
        s.timer->tr_time.tv_secs=0; s.timer->tr_time.tv_micro=500000;
        SendIO((struct IORequest *)s.timer); s.timerPending=TRUE;
        signals=Wait(muiSignals|1UL<<s.commands->mp_SigBit|1UL<<s.apps->mp_SigBit|
                     1UL<<s.cx->mp_SigBit|1UL<<s.clock->mp_SigBit|
                     1UL<<s.outgoing.replies->mp_SigBit|IndicatorWifiSignals(s.wifi)|SIGBREAKF_CTRL_C);
        wifiSignals=signals;
        stop_timer(&s);
        if (signals&SIGBREAKF_CTRL_C) s.quit=TRUE;
    }
    result=RETURN_OK;
cleanup:
    s.quit=TRUE; stop_timer(&s);
    if (s.outgoing.replies) TileSourcesClose(&s.outgoing);
    if (s.registered) RemPort(s.commands);
    if (s.broker) DeleteCxObjAll(s.broker);
    if (s.icon) RemoveAppIcon(s.icon);
    if (s.commands) while (s.commands->mp_MsgList.lh_Head->ln_Succ) commands(&s);
    if (s.apps) { struct Message *m; while ((m=GetMsg(s.apps))) ReplyMsg(m); }
    if (s.cx) { struct Message *m; while ((m=GetMsg(s.cx))) ReplyMsg(m); }
    if (s.eventAttached) DoMethod(s.window,MUIM_Window_RemEventHandler,(IPTR)&s.eventNode);
    if (s.application) MUI_DisposeObject(s.application);
    if (s.eventClass) MUI_DeleteCustomClass(s.eventClass);
    IndicatorWifiFree(s.wifi);
    if (s.bitmap) FreeBitMap(s.bitmap);
    if (s.fallback) FreeDiskObject(s.fallback);
    if (s.screen) UnlockPubScreen(NULL,s.screen);
    if (s.timerOpen) CloseDevice((struct IORequest *)s.timer);
    if (s.timer) DeleteIORequest((struct IORequest *)s.timer);
    if (s.clock) DeleteMsgPort(s.clock);
    if (s.cx) DeleteMsgPort(s.cx);
    if (s.apps) DeleteMsgPort(s.apps);
    if (s.commands) DeleteMsgPort(s.commands);
    if (RexxSysBase) CloseLibrary((struct Library *)RexxSysBase);
    return result;
}
