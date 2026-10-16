/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 * The existing chooser worker owns device/configuration operations. This
 * adapter never waits for it, and retains the shared message until returned.
 */
#define MUIMASTER_YES_INLINE_STDARG
#include "wifi_panel.h"
#include "wifigui.h"
#include "locale.h"
#include <exec/memory.h>
#include <dos/dostags.h>
#include <libraries/mui.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/muimaster.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <string.h>
struct IndicatorWifi {
    Object *group,*list,*ssid,*key,*scan,*connect,*disconnect,*other,*status,*protectedChoice;
    struct MsgPort *replies;
    struct WifiMsg message, snapshot;
    BOOL started, starting, busy, stopping, quitSent, dead, childExited;
    BOOL wireless;
    ULONG last;
    BYTE wakeBit;
};
static void buttons(struct IndicatorWifi *s)
{
    BOOL unavailable=s->busy || s->starting || s->stopping || s->dead;
    SetAttrs(s->scan,MUIA_Disabled,unavailable || !s->wireless,TAG_DONE);
    SetAttrs(s->connect,MUIA_Disabled,unavailable || !s->wireless,TAG_DONE);
    SetAttrs(s->disconnect,MUIA_Disabled,unavailable || !s->wireless,TAG_DONE);
    SetAttrs(s->list,MUIA_Disabled,unavailable,TAG_DONE);
    SetAttrs(s->other,MUIA_Disabled,unavailable,TAG_DONE);
}
static BOOL send(struct IndicatorWifi *s, ULONG cmd)
{
    if (s->busy || s->starting || s->dead || !WifiWorkerPort) return FALSE;
    s->message.wm_Cmd=cmd;
    s->message.wm_Msg.mn_Node.ln_Type=NT_MESSAGE;
    s->message.wm_Msg.mn_Length=sizeof(s->message);
    s->message.wm_Msg.mn_ReplyPort=s->replies;
    s->busy=TRUE;
    if (cmd==WCMD_SCAN) SetAttrs(s->status,MUIA_Text_Contents,(IPTR)"Scanning networks...",TAG_DONE);
    else if (cmd==WCMD_CONNECT) SetAttrs(s->status,MUIA_Text_Contents,(IPTR)"Connecting...",TAG_DONE);
    PutMsg(WifiWorkerPort,&s->message.wm_Msg);
    buttons(s);
    return TRUE;
}
static void safe_text(char *out, size_t size, const char *in)
{
    size_t i;
    for (i=0;i+1<size && in[i];++i) {
        unsigned char c=(unsigned char)in[i];
        out[i]=c>=32 && c<=126?(char)c:'?';
    }
    out[i]=0;
}
static void fill(struct IndicatorWifi *s)
{
    LONG pass,i;
    DoMethod(s->list,MUIM_List_Clear);
    /* List strings are independent copies, never pointers into a borrowed msg. */
    for (pass=1;pass>=0;--pass) for (i=0;i<s->snapshot.wm_Count && i<WIFI_MAX_NETS;++i) {
        struct WifiNet *n=&s->snapshot.wm_Nets[i];
        char row[128], name[WIFI_SSID_MAX], protection[8];
        if (!!n->wn_Known!=pass) continue;
        safe_text(name,sizeof(name),(const char *)n->wn_SSID);
        safe_text(protection,sizeof(protection),(const char *)n->wn_Protection);
        snprintf(row,sizeof(row),"%s%s%s [%s] %ld dBm",n->wn_Known?"Known: ":"",name,
            !strcmp((const char *)s->snapshot.wm_SSID,(const char *)n->wn_SSID)?" (joined)":"",
            protection,(long)n->wn_Signal);
        DoMethod(s->list,MUIM_List_InsertSingle,(IPTR)row,MUIV_List_Insert_Bottom);
    }
}
Object *IndicatorWifiCreate(struct IndicatorWifi **out)
{
    struct IndicatorWifi *s=AllocVec(sizeof(*s),MEMF_PUBLIC|MEMF_CLEAR);
    if (!s) return NULL;
    s->wakeBit=AllocSignal(-1);
    if (s->wakeBit<0) { FreeVec(s); return NULL; }
    s->replies=CreateMsgPort();
    if (!s->replies) { FreeSignal(s->wakeBit); FreeVec(s); return NULL; }
    s->group=(Object *)VGroup,
        Child,(IPTR)(Object *)TextObject,MUIA_Text_Contents,(IPTR)"Radio control unavailable",End,
        Child,(IPTR)(s->status=(Object *)TextObject,MUIA_Text_Contents,(IPTR)"Starting Wi-Fi service...",End),
        Child,(IPTR)(Object *)ListviewObject,MUIA_VertWeight,0,
            MUIA_FixHeight,100,MUIA_Listview_List,(IPTR)(s->list=(Object *)ListObject,InputListFrame,
                MUIA_List_ConstructHook,MUIV_List_ConstructHook_String,
                MUIA_List_DestructHook,MUIV_List_DestructHook_String,
                MUIA_List_MinLineHeight,12,End),End,
        Child,(IPTR)(s->scan=(Object *)SimpleButton("Scan networks")),
        Child,(IPTR)(Object *)TextObject,MUIA_Text_Contents,(IPTR)"Network name",End,
        Child,(IPTR)(s->ssid=(Object *)StringObject,StringFrame,MUIA_String_MaxLen,WIFI_SSID_MAX,End),
        Child,(IPTR)(Object *)TextObject,MUIA_Text_Contents,(IPTR)"Password (blank for a remembered network)",End,
        Child,(IPTR)(Object *)HGroup,
            Child,(IPTR)(s->protectedChoice=(Object *)MUI_MakeObject(MUIO_Checkmark,(IPTR)"Password required")),
            Child,(IPTR)(Object *)TextObject,MUIA_Text_Contents,(IPTR)"Password required",End,
        End,
        Child,(IPTR)(s->key=(Object *)StringObject,StringFrame,MUIA_String_MaxLen,WIFI_KEY_MAX,MUIA_String_Secret,TRUE,End),
        Child,(IPTR)(s->connect=(Object *)SimpleButton("Connect")),
        Child,(IPTR)(s->other=(Object *)SimpleButton("Other network...")),
        Child,(IPTR)(s->disconnect=(Object *)SimpleButton("Disconnect")),
    End;
    if (!s->group) { DeleteMsgPort(s->replies); FreeSignal(s->wakeBit); FreeVec(s); return NULL; }
    SetAttrs(s->protectedChoice,MUIA_Selected,TRUE,TAG_DONE);
    *out=s; s->starting=TRUE; buttons(s);
    return s->group;
}
void IndicatorWifiBind(struct IndicatorWifi *s, Object *app)
{
    if (!s) return;
    DoMethod(s->scan,MUIM_Notify,MUIA_Pressed,FALSE,app,2,MUIM_Application_ReturnID,100);
    DoMethod(s->connect,MUIM_Notify,MUIA_Pressed,FALSE,app,2,MUIM_Application_ReturnID,101);
    DoMethod(s->disconnect,MUIM_Notify,MUIA_Pressed,FALSE,app,2,MUIM_Application_ReturnID,102);
    DoMethod(s->list,MUIM_Notify,MUIA_List_Active,MUIV_EveryTime,app,2,MUIM_Application_ReturnID,103);
    DoMethod(s->other,MUIM_Notify,MUIA_Pressed,FALSE,app,2,MUIM_Application_ReturnID,104);
}
void IndicatorWifiStart(struct IndicatorWifi *s)
{
    if (!s) return;
    Locale_Initialize();
    WifiMainTask=FindTask(NULL); WifiWorkerPort=NULL;
    WifiMainSignal=1UL<<s->wakeBit;
    SetSignal(0,WifiMainSignal|SIGF_CHILD);
    s->started=CreateNewProcTags(NP_Entry,(IPTR)WifiWorker,NP_Name,(IPTR)"Indicator Wi-Fi worker",
        NP_NotifyOnDeath,TRUE,TAG_DONE)!=NULL;
    if (!s->started) {
        s->dead=TRUE; s->starting=FALSE;
        SetAttrs(s->status,MUIA_Text_Contents,(IPTR)"Wi-Fi service unavailable",TAG_DONE);
        buttons(s);
    }
}
BOOL IndicatorWifiAction(struct IndicatorWifi *s, IPTR action)
{
    IPTR value=0;
    if (!s || s->busy || s->starting || s->stopping || s->dead) return FALSE;
    if (action==100) return s->wireless && send(s,WCMD_SCAN);
    else if (action==102) return s->wireless && send(s,WCMD_DISCONNECT);
    else if (action==104) {
        SetAttrs(s->ssid,MUIA_String_Contents,(IPTR)"",TAG_DONE);
        SetAttrs(s->key,MUIA_String_Contents,(IPTR)"",TAG_DONE);
        return TRUE;
    } else if (action==103) {
        LONG i,pass,index=0;
        GetAttr(MUIA_List_Active,s->list,&value);
        for (pass=1;pass>=0;--pass) for (i=0;i<s->snapshot.wm_Count && i<WIFI_MAX_NETS;++i) {
            struct WifiNet *n=&s->snapshot.wm_Nets[i];
            if (!!n->wn_Known!=pass) continue;
            if (index++==(LONG)value) {
                SetAttrs(s->ssid,MUIA_String_Contents,(IPTR)n->wn_SSID,TAG_DONE);
                SetAttrs(s->key,MUIA_String_Contents,(IPTR)"",TAG_DONE);
                return TRUE;
            }
        }
    } else if (action==101 && s->wireless) {
        LONG i;
        GetAttr(MUIA_String_Contents,s->ssid,&value);
        if (!value || !*(const char *)value) return FALSE;
        snprintf((char *)s->message.wm_SSID,sizeof(s->message.wm_SSID),"%s",value?(const char *)value:"");
        GetAttr(MUIA_Selected,s->protectedChoice,&value);
        s->message.wm_Protected=value!=0;
        for (i=0;i<s->snapshot.wm_Count && i<WIFI_MAX_NETS;++i)
            if (!strcmp((char *)s->message.wm_SSID,(char *)s->snapshot.wm_Nets[i].wn_SSID))
                s->message.wm_Protected=strcmp((char *)s->snapshot.wm_Nets[i].wn_Protection,"open")!=0;
        GetAttr(MUIA_String_Contents,s->key,&value);
        snprintf((char *)s->message.wm_Key,sizeof(s->message.wm_Key),"%s",value?(const char *)value:"");
        SetAttrs(s->key,MUIA_String_Contents,(IPTR)"",TAG_DONE);
        if (send(s,WCMD_CONNECT)) return TRUE;
        memset(s->message.wm_Key,0,sizeof(s->message.wm_Key));
    }
    return FALSE;
}
BOOL IndicatorWifiStep(struct IndicatorWifi *s, ULONG now, ULONG signals, BOOL stop, char *event, ULONG size)
{
    struct WifiMsg *m;
    BOOL updated=FALSE;
    if (!s) return FALSE;
    if (stop) s->stopping=TRUE;
    if (signals&SIGF_CHILD) s->childExited=TRUE;
    if (s->starting && (signals&(1UL<<s->wakeBit))) {
        s->starting=FALSE;
        if (WifiWorkerPort && !s->stopping) send(s,WCMD_STATUS);
    }
    while ((m=(struct WifiMsg *)GetMsg(s->replies))) {
        char summary[96];
        s->busy=FALSE;
        memset(m->wm_Key,0,sizeof(m->wm_Key));
        s->last=now;
        if (m->wm_Cmd==WCMD_QUIT) continue;
        s->snapshot=*m;
        s->wireless=m->wm_Wireless;
        safe_text(summary,sizeof(summary),(const char *)m->wm_Status);
        SetAttrs(s->status,MUIA_Text_Contents,(IPTR)summary,TAG_DONE);
        if (m->wm_Cmd==WCMD_SCAN) fill(s);
        if (s->wireless) {
            char ssid[WIFI_SSID_MAX];
            safe_text(ssid,sizeof(ssid),(const char *)m->wm_SSID);
            snprintf(event,size,"LINK %s %s%s",m->wm_Associated?"on":"off",
                m->wm_Associated?"Joined: ":"Disconnected",m->wm_Associated?ssid:"");
            updated=TRUE;
        }
    }
    if (s->childExited && !WifiWorkerPort && !s->busy) {
        s->dead=TRUE; s->starting=FALSE;
        if (!s->stopping) SetAttrs(s->status,MUIA_Text_Contents,(IPTR)"Wi-Fi service unavailable",TAG_DONE);
    }
    if (s->stopping && !s->starting && !s->busy && !s->dead && !s->quitSent) {
        if (send(s,WCMD_QUIT)) s->quitSent=TRUE;
    } else if (!s->stopping && !s->starting && !s->busy && !s->dead && (ULONG)(now-s->last)>=2000) {
        send(s,WCMD_STATUS);
    }
    buttons(s);
    return updated;
}
ULONG IndicatorWifiSignals(struct IndicatorWifi *s)
{
    return s ? (1UL<<s->replies->mp_SigBit)|(1UL<<s->wakeBit)|SIGF_CHILD : 0;
}
BOOL IndicatorWifiIdle(struct IndicatorWifi *s) { return !s || !s->started || (s->dead && !s->busy); }
void IndicatorWifiFree(struct IndicatorWifi *s)
{
    if (!s) return;
    Locale_Deinitialize();
    DeleteMsgPort(s->replies); FreeSignal(s->wakeBit); FreeVec(s);
}

BOOL IndicatorWifiCommand(struct IndicatorWifi *s, const char *command, char *answer, ULONG size)
{
    if (!s) return FALSE;
    if (!strcmp(command,"WIFI STATUS")) {
        snprintf(answer,size,"ready=%u busy=%u wireless=%u dead=%u wake=%lu reserved=%u",!s->starting && !s->dead,
            s->busy,s->wireless,s->dead,(unsigned long)(1UL<<s->wakeBit),
            (unsigned)!!((1UL<<s->wakeBit)&SIGF_SINGLE));
        return TRUE;
    }
    if (!strcmp(command,"WIFI OTHER")) return IndicatorWifiAction(s,104);
    if (!strcmp(command,"WIFI CONNECT")) return IndicatorWifiAction(s,101);
    if (!strcmp(command,"WIFI SCAN")) return IndicatorWifiAction(s,100);
    if (!strcmp(command,"WIFI DISCONNECT")) return IndicatorWifiAction(s,102);
    return FALSE;
}
