/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    WiFiEditor.mcc: the Wi-Fi chooser as a PrefsEditor page, the settings
    window's Network page. The worker process (worker.c) is the program's,
    unchanged: it owns every blocking call and writes Wireless.prefs when a
    network is joined, so there is nothing to Use or Save. Its replies reach
    the page through a MUI input handler on the reply port's signal. One
    live page: the worker and its message are module-wide.
*/
#define MUIMASTER_YES_INLINE_STDARG

#include <exec/types.h>
#include <libraries/mui.h>
#include <utility/hooks.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <zune/prefseditor.h>
#include <aros/symbolsets.h>

#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>

#include <stdio.h>
#include <string.h>

#include LC_LIBDEFS_FILE

#include "wifigui.h"
#include "locale.h"
#include "wifieditor.h"

static struct MsgPort *replyport;
static struct WifiMsg *msg;
static struct Process *worker;
static BOOL busy, wireless = TRUE, details_pending;
static TEXT pending_ssid[WIFI_SSID_MAX];
static LONG pending_protected;
static CONST_STRPTR devnames[WIFI_MAX_DEVS + 1];
static TEXT devlabels[WIFI_MAX_DEVS][WIFI_DEV_MAX + 16];
static ULONG live_instances;
static Object *live_object;      /* the instance counted, so a failed
                                    construction never uncounts it */

/*** Worker *****************************************************************/
static BOOL start_worker(void)
{
    replyport = CreateMsgPort();
    msg = AllocVec(sizeof(struct WifiMsg), MEMF_PUBLIC | MEMF_CLEAR);
    if (replyport == NULL || msg == NULL)
        return FALSE;
    WifiMainTask = FindTask(NULL);
    SetSignal(0, SIGF_SINGLE);
    worker = CreateNewProcTags(NP_Entry, (IPTR)WifiWorker, NP_Name, (IPTR)"WiFi worker", TAG_END);
    if (worker == NULL)
        return FALSE;
    Wait(SIGF_SINGLE);
    return WifiWorkerPort != NULL;
}

static void stop_worker(void)
{
    if (worker != NULL && WifiWorkerPort != NULL)
    {
        /* The one message may still be out with a command: take its
         * reply first, so QUIT reuses a message nobody else holds. */
        if (busy)
        {
            WaitPort(replyport);
            GetMsg(replyport);
            busy = FALSE;
        }
        /* QUIT is answered like any command; wait for it, then for the
         * process to have gone (it signals SIGF_SINGLE on exit). */
        msg->wm_Cmd = WCMD_QUIT;
        msg->wm_Msg.mn_Node.ln_Type = NT_MESSAGE;
        msg->wm_Msg.mn_Length = sizeof(*msg);
        msg->wm_Msg.mn_ReplyPort = replyport;
        SetSignal(0, SIGF_SINGLE);
        PutMsg(WifiWorkerPort, &msg->wm_Msg);
        WaitPort(replyport);
        GetMsg(replyport);
        Wait(SIGF_SINGLE);
    }
    worker = NULL;
    if (replyport != NULL)
        DeleteMsgPort(replyport);
    replyport = NULL;
    if (msg != NULL)
        FreeVec(msg);
    msg = NULL;
}

#include "wifieditor_data.h"

#define SETUP_INST_DATA struct WiFiEditor_DATA *data = INST_DATA(CLASS, self)

static void set_status(struct WiFiEditor_DATA *data, CONST_STRPTR text)
{
    SET(data->statustext, MUIA_Text_Contents, (IPTR)text);
}

static void update_buttons(struct WiFiEditor_DATA *data)
{
    SET(data->scanbtn, MUIA_Disabled, busy || !wireless);
    SET(data->connectbtn, MUIA_Disabled, busy || !wireless);
    SET(data->disconnectbtn, MUIA_Disabled, busy || !wireless);
    SET(data->detailsbtn, MUIA_Disabled, busy);
}

static void send(struct WiFiEditor_DATA *data, ULONG cmd)
{
    if (busy || WifiWorkerPort == NULL)
        return;
    msg->wm_Cmd = cmd;
    msg->wm_Msg.mn_Node.ln_Type = NT_MESSAGE;
    msg->wm_Msg.mn_Length = sizeof(*msg);
    msg->wm_Msg.mn_ReplyPort = replyport;
    busy = TRUE;
    update_buttons(data);
    PutMsg(WifiWorkerPort, &msg->wm_Msg);
}

AROS_UFH3(static IPTR, display_func,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(STRPTR *, columns, A2),
    AROS_UFHA(struct WifiNet *, net, A1))
{
    AROS_USERFUNC_INIT
    static TEXT signal[8], channel[8];

    if (net == NULL)
    {
        columns[0] = "";
        columns[1] = (STRPTR)_(MSG_COL_NETWORK);
        columns[2] = (STRPTR)_(MSG_COL_SECURITY);
        columns[3] = (STRPTR)_(MSG_COL_SIGNAL);
        columns[4] = (STRPTR)_(MSG_COL_CHANNEL);
        return 0;
    }
    snprintf((char *)signal, sizeof(signal), "%ld", (long)net->wn_Signal);
    snprintf((char *)channel, sizeof(channel), "%ld", (long)net->wn_Channel);
    columns[0] = (msg->wm_Associated && strcmp((const char *)msg->wm_SSID, (const char *)net->wn_SSID) == 0)
        ? "\xBB" : (net->wn_Known ? "\xB7" : " ");
    columns[1] = net->wn_SSID;
    columns[2] = net->wn_Protection;
    columns[3] = signal;
    columns[4] = channel;
    return 0;
    AROS_USERFUNC_EXIT
}

static void fill_list(struct WiFiEditor_DATA *data)
{
    LONG i;
    SET(data->list, MUIA_List_Quiet, TRUE);
    DoMethod(data->list, MUIM_List_Clear);
    for (i = 0; i < msg->wm_Count; i++)
        DoMethod(data->list, MUIM_List_InsertSingle, (IPTR)&msg->wm_Nets[i], MUIV_List_Insert_Bottom);
    SET(data->list, MUIA_List_Quiet, FALSE);
}

static void start_connect(struct WiFiEditor_DATA *data, CONST_STRPTR key)
{
    TEXT text[128];
    snprintf((char *)msg->wm_SSID, sizeof(msg->wm_SSID), "%s", pending_ssid);
    snprintf((char *)msg->wm_Key, sizeof(msg->wm_Key), "%s", key != NULL ? (const char *)key : "");
    msg->wm_Protected = pending_protected;
    snprintf((char *)text, sizeof(text), (const char *)_(MSG_CONNECTING), pending_ssid);
    set_status(data, text);
    send(data, WCMD_CONNECT);
}

static void set_field(Object *obj, CONST_STRPTR value)
{
    SET(obj, MUIA_Text_Contents, (IPTR)((value != NULL && value[0] != '\0') ? value : (CONST_STRPTR)"-"));
}

static void show_details(struct WiFiEditor_DATA *data)
{
    CONST_STRPTR security = (CONST_STRPTR)"";
    LONG i;
    for (i = 0; i < msg->wm_Count; i++)
        if (strcmp((const char *)msg->wm_Nets[i].wn_SSID, (const char *)msg->wm_SSID) == 0)
        {
            security = (CONST_STRPTR)msg->wm_Nets[i].wn_Protection;
            break;
        }
    set_field(data->dv_network, msg->wm_Associated ? (CONST_STRPTR)msg->wm_SSID : (CONST_STRPTR)"");
    set_field(data->dv_security, msg->wm_Associated ? security : (CONST_STRPTR)"");
    set_field(data->dv_ap, (CONST_STRPTR)msg->wm_BSSID);
    set_field(data->dv_iface, (CONST_STRPTR)msg->wm_Interface);
    set_field(data->dv_mac, (CONST_STRPTR)msg->wm_MAC);
    set_field(data->dv_address, (CONST_STRPTR)msg->wm_Address);
    set_field(data->dv_netmask, (CONST_STRPTR)msg->wm_Netmask);
    set_field(data->dv_broadcast, (CONST_STRPTR)msg->wm_Broadcast);
    set_field(data->dv_gateway, (CONST_STRPTR)msg->wm_Gateway);
    set_field(data->dv_dns, (CONST_STRPTR)msg->wm_DNS);
    SET(data->detailswindow, MUIA_Window_Open, TRUE);
}

/*** Methods ****************************************************************/
IPTR WiFiEditor__OM_NEW(Class *CLASS, Object *self, struct opSet *message)
{
    Object *list, *scanbtn, *connectbtn, *statustext, *detailsbtn, *disconnectbtn, *devcycle;
    LONG i;

    if (live_instances != 0 || !start_worker())
    {
        if (live_instances == 0)
            stop_worker();
        return 0;
    }
    /* The device list is needed to build the chooser: one synchronous
     * request, before the page is on screen. */
    msg->wm_Cmd = WCMD_DEVICES;
    msg->wm_Msg.mn_Node.ln_Type = NT_MESSAGE;
    msg->wm_Msg.mn_Length = sizeof(*msg);
    msg->wm_Msg.mn_ReplyPort = replyport;
    PutMsg(WifiWorkerPort, &msg->wm_Msg);
    WaitPort(replyport);
    GetMsg(replyport);
    for (i = 0; i < msg->wm_DeviceCount; i++)
    {
        if (msg->wm_DeviceUnits[i] != 0)
            snprintf((char *)devlabels[i], sizeof(devlabels[i]), "%s unit %ld",
                msg->wm_Devices[i], (long)msg->wm_DeviceUnits[i]);
        else
            snprintf((char *)devlabels[i], sizeof(devlabels[i]), "%s", msg->wm_Devices[i]);
        devnames[i] = (CONST_STRPTR)devlabels[i];
    }
    if (msg->wm_DeviceCount == 0)
        devnames[0] = _(MSG_NO_DEVICE);
    devnames[msg->wm_DeviceCount ? msg->wm_DeviceCount : 1] = NULL;

    self = (Object *)DoSuperNewTags
    (
        CLASS, self, NULL,
        MUIA_PrefsEditor_Name, __(MSG_WIN_MAIN),
        MUIA_PrefsEditor_Path, (IPTR)"Sys/Wireless.prefs",
        MUIA_PrefsEditor_CanSave, FALSE,
        MUIA_PrefsEditor_CanTest, FALSE,
        Child, (IPTR)HGroup,
            Child, (IPTR)Label((char *)_(MSG_LAB_DEVICE)),
            Child, (IPTR)(devcycle = CycleObject,
                MUIA_Cycle_Entries, (IPTR)devnames,
                MUIA_CycleChain, 1,
                MUIA_ObjectID, MAKE_ID('W','F','D','V'),
            End),
        End,
        Child, (IPTR)ListviewObject,
            MUIA_CycleChain, 1,
            MUIA_Listview_List, (IPTR)(list = ListObject,
                InputListFrame,
                MUIA_List_Format, (IPTR)"BAR,WEIGHT=300 BAR,BAR,P=\33r BAR,P=\33r",
                MUIA_List_Title, TRUE,
                MUIA_ObjectID, MAKE_ID('W','F','N','L'),
            End),
        End,
        Child, (IPTR)(statustext = TextObject,
            TextFrame,
            MUIA_Background, MUII_TextBack,
            MUIA_Text_Contents, __(MSG_STARTING),
        End),
        Child, (IPTR)HGroup,
            Child, (IPTR)(scanbtn = SimpleButton((char *)_(MSG_BTN_SCAN))),
            Child, (IPTR)(detailsbtn = SimpleButton((char *)_(MSG_BTN_DETAILS))),
            Child, (IPTR)HVSpace,
            Child, (IPTR)(disconnectbtn = SimpleButton((char *)_(MSG_BTN_DISCONNECT))),
            Child, (IPTR)(connectbtn = SimpleButton((char *)_(MSG_BTN_CONNECT))),
        End,
        TAG_MORE, (IPTR)message->ops_AttrList
    );
    if (self == NULL)
    {
        stop_worker();
        return 0;
    }
    {
        SETUP_INST_DATA;
        data->list = list;
        data->scanbtn = scanbtn;
        data->connectbtn = connectbtn;
        data->statustext = statustext;
        data->detailsbtn = detailsbtn;
        data->disconnectbtn = disconnectbtn;
        data->devcycle = devcycle;
        data->display_hook.h_Entry = (HOOKFUNC)display_func;
        SET(list, MUIA_List_DisplayHook, (IPTR)&data->display_hook);

        data->detailswindow = WindowObject,
            MUIA_Window_Title, __(MSG_WIN_DETAILS),
            WindowContents, (IPTR)VGroup,
                Child, (IPTR)ColGroup(2),
                    GroupFrameT((char *)_(MSG_GRP_WIRELESS)),
                    Child, (IPTR)Label((char *)_(MSG_LAB_NETWORK)),
                    Child, (IPTR)(data->dv_network = TextObject, End),
                    Child, (IPTR)Label((char *)_(MSG_LAB_SECURITY)),
                    Child, (IPTR)(data->dv_security = TextObject, End),
                    Child, (IPTR)Label((char *)_(MSG_LAB_AP)),
                    Child, (IPTR)(data->dv_ap = TextObject, End),
                    Child, (IPTR)Label((char *)_(MSG_LAB_MAC)),
                    Child, (IPTR)(data->dv_mac = TextObject, End),
                End,
                Child, (IPTR)ColGroup(2),
                    GroupFrameT((char *)_(MSG_GRP_NETWORK)),
                    Child, (IPTR)Label((char *)_(MSG_LAB_INTERFACE)),
                    Child, (IPTR)(data->dv_iface = TextObject, End),
                    Child, (IPTR)Label((char *)_(MSG_LAB_ADDRESS)),
                    Child, (IPTR)(data->dv_address = TextObject, End),
                    Child, (IPTR)Label((char *)_(MSG_LAB_NETMASK)),
                    Child, (IPTR)(data->dv_netmask = TextObject, End),
                    Child, (IPTR)Label((char *)_(MSG_LAB_BROADCAST)),
                    Child, (IPTR)(data->dv_broadcast = TextObject, End),
                    Child, (IPTR)Label((char *)_(MSG_LAB_ROUTER)),
                    Child, (IPTR)(data->dv_gateway = TextObject, End),
                    Child, (IPTR)Label((char *)_(MSG_LAB_DNS)),
                    Child, (IPTR)(data->dv_dns = TextObject, End),
                End,
                Child, (IPTR)HGroup,
                    Child, (IPTR)HVSpace,
                    Child, (IPTR)(data->detailsok = SimpleButton((char *)_(MSG_BTN_OK))),
                End,
            End,
        End;
        data->keywindow = WindowObject,
            MUIA_Window_Title, __(MSG_WIN_PASSWORD),
            MUIA_Window_Activate, TRUE,
            WindowContents, (IPTR)VGroup,
                Child, (IPTR)(data->keytext = TextObject,
                    MUIA_Text_Contents, __(MSG_LAB_PASSWORD),
                End),
                Child, (IPTR)(data->keystring = StringObject,
                    StringFrame,
                    MUIA_ObjectID, MAKE_ID('W','F','K','Y'),
                    MUIA_String_Secret, TRUE,
                    MUIA_String_MaxLen, WIFI_KEY_MAX,
                    MUIA_CycleChain, 1,
                End),
                Child, (IPTR)HGroup,
                    Child, (IPTR)(data->keycancel = SimpleButton((char *)_(MSG_BTN_CANCEL))),
                    Child, (IPTR)HVSpace,
                    Child, (IPTR)(data->keyok = SimpleButton((char *)_(MSG_BTN_OK))),
                End,
            End,
        End;

        DoMethod(devcycle, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime,
            (IPTR)self, 1, MUIM_WiFiEditor_Device);
        DoMethod(scanbtn, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)self, 1, MUIM_WiFiEditor_Scan);
        DoMethod(connectbtn, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)self, 1, MUIM_WiFiEditor_Connect);
        DoMethod(list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, (IPTR)self, 1, MUIM_WiFiEditor_Connect);
        DoMethod(detailsbtn, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)self, 1, MUIM_WiFiEditor_Details);
        DoMethod(disconnectbtn, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)self, 1, MUIM_WiFiEditor_Disconnect);
        if (data->detailsok != NULL)
            DoMethod(data->detailsok, MUIM_Notify, MUIA_Pressed, FALSE,
                (IPTR)data->detailswindow, 3, MUIM_Set, MUIA_Window_Open, FALSE);
        if (data->keyok != NULL)
        {
            DoMethod(data->keyok, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)self, 1, MUIM_WiFiEditor_KeyOk);
            DoMethod(data->keystring, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime,
                (IPTR)self, 1, MUIM_WiFiEditor_KeyOk);
            DoMethod(data->keycancel, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)self, 1, MUIM_WiFiEditor_KeyCancel);
        }
        live_instances++;
        live_object = self;
        if (msg->wm_DeviceCount > 0)
            DoMethod(self, MUIM_WiFiEditor_Device);
        else
        {
            wireless = FALSE;
            update_buttons(data);
            set_status(data, _(MSG_NO_DEVICE));
        }
    }
    return (IPTR)self;
}

IPTR WiFiEditor__OM_DISPOSE(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    if (!data->windows_added)
    {
        if (data->detailswindow != NULL)
            MUI_DisposeObject(data->detailswindow);
        if (data->keywindow != NULL)
            MUI_DisposeObject(data->keywindow);
    }
    if (live_object == NULL || self == live_object)
        stop_worker();
    if (self == live_object)
    {
        live_object = NULL;
        live_instances--;
    }
    return DoSuperMethodA(CLASS, self, message);
}

/* The page's sub-windows and the reply handler live while it is set up. */
IPTR WiFiEditor__MUIM_Setup(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    if (!DoSuperMethodA(CLASS, self, message))
        return FALSE;
    if (!data->windows_added)
    {
        if (data->detailswindow != NULL)
            DoMethod(_app(self), OM_ADDMEMBER, (IPTR)data->detailswindow);
        if (data->keywindow != NULL)
            DoMethod(_app(self), OM_ADDMEMBER, (IPTR)data->keywindow);
        data->windows_added = TRUE;
    }
    data->handler.ihn_Object = self;
    data->handler.ihn_Signals = 1UL << replyport->mp_SigBit;
    data->handler.ihn_Flags = 0;
    data->handler.ihn_Method = MUIM_WiFiEditor_Reply;
    DoMethod(_app(self), MUIM_Application_AddInputHandler, (IPTR)&data->handler);
    data->handler_added = TRUE;
    return TRUE;
}

IPTR WiFiEditor__MUIM_Cleanup(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    if (data->handler_added)
        DoMethod(_app(self), MUIM_Application_RemInputHandler, (IPTR)&data->handler);
    data->handler_added = FALSE;
    return DoSuperMethodA(CLASS, self, message);
}

/* No prefs file of its own to import or export: the worker writes
 * Wireless.prefs when a network is joined. */
IPTR WiFiEditor__MUIM_PrefsEditor_ImportFromDirectory(Class *CLASS, Object *self, Msg message)
{
    return TRUE;
}

IPTR WiFiEditor__MUIM_PrefsEditor_ExportToDirectory(Class *CLASS, Object *self, Msg message)
{
    return TRUE;
}

IPTR WiFiEditor__MUIM_WiFiEditor_Device(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    LONG active = XGET(data->devcycle, MUIA_Cycle_Active);
    if (active < 0 || active >= msg->wm_DeviceCount)
        return 0;
    DoMethod(data->list, MUIM_List_Clear);
    msg->wm_Count = 0;
    snprintf((char *)msg->wm_Device, sizeof(msg->wm_Device), "%s", msg->wm_Devices[active]);
    msg->wm_Unit = msg->wm_DeviceUnits[active];
    send(data, WCMD_STATUS);
    return 0;
}

IPTR WiFiEditor__MUIM_WiFiEditor_Scan(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    set_status(data, _(MSG_SCANNING));
    send(data, WCMD_SCAN);
    return 0;
}

IPTR WiFiEditor__MUIM_WiFiEditor_Connect(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    struct WifiNet *net = NULL;
    LONG active = XGET(data->list, MUIA_List_Active);
    if (active == MUIV_List_Active_Off)
    {
        set_status(data, _(MSG_SELECT_FIRST));
        return 0;
    }
    DoMethod(data->list, MUIM_List_GetEntry, active, (IPTR)&net);
    if (net == NULL)
        return 0;
    snprintf((char *)pending_ssid, sizeof(pending_ssid), "%s", net->wn_SSID);
    pending_protected = strcmp((const char *)net->wn_Protection, "open") != 0;
    start_connect(data, NULL);
    return 0;
}

IPTR WiFiEditor__MUIM_WiFiEditor_KeyOk(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    STRPTR key = (STRPTR)XGET(data->keystring, MUIA_String_Contents);
    SET(data->keywindow, MUIA_Window_Open, FALSE);
    start_connect(data, key);
    SET(data->keystring, MUIA_String_Contents, (IPTR)"");
    return 0;
}

IPTR WiFiEditor__MUIM_WiFiEditor_KeyCancel(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    SET(data->keywindow, MUIA_Window_Open, FALSE);
    SET(data->keystring, MUIA_String_Contents, (IPTR)"");
    set_status(data, _(MSG_CANCELLED));
    return 0;
}

IPTR WiFiEditor__MUIM_WiFiEditor_Details(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    details_pending = TRUE;
    send(data, WCMD_STATUS);
    return 0;
}

IPTR WiFiEditor__MUIM_WiFiEditor_Disconnect(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    set_status(data, _(MSG_DISCONNECTING));
    send(data, WCMD_DISCONNECT);
    return 0;
}

IPTR WiFiEditor__MUIM_WiFiEditor_Reply(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    struct WifiMsg *m;

    while ((m = (struct WifiMsg *)GetMsg(replyport)) != NULL)
    {
        busy = FALSE;
        update_buttons(data);
        switch (m->wm_Cmd)
        {
        case WCMD_SCAN:
            fill_list(data);
            set_status(data, m->wm_Status);
            break;
        case WCMD_CONNECT:
            set_status(data, m->wm_Status);
            if (m->wm_Result == WRES_NEEDKEY)
            {
                TEXT prompt[128];
                snprintf((char *)prompt, sizeof(prompt), (const char *)_(MSG_PASSWORD_FOR), pending_ssid);
                SET(data->keytext, MUIA_Text_Contents, (IPTR)prompt);
                SET(data->keystring, MUIA_String_Contents, (IPTR)"");
                SET(data->keywindow, MUIA_Window_Open, TRUE);
                SET(data->keywindow, MUIA_Window_ActiveObject, (IPTR)data->keystring);
                break;
            }
            DoMethod(data->list, MUIM_List_Redraw, MUIV_List_Redraw_All);
            break;
        case WCMD_STATUS:
            wireless = m->wm_Wireless;
            update_buttons(data);
            set_status(data, m->wm_Status);
            DoMethod(data->list, MUIM_List_Redraw, MUIV_List_Redraw_All);
            if (details_pending)
            {
                details_pending = FALSE;
                show_details(data);
            }
            break;
        case WCMD_DISCONNECT:
            set_status(data, m->wm_Status);
            DoMethod(data->list, MUIM_List_Redraw, MUIV_List_Redraw_All);
            break;
        }
    }
    return 0;
}

/*** Module setup ***********************************************************/
static int WiFiEditor_Init(LIBBASETYPEPTR LIBBASE)
{
    Locale_Initialize();
    return TRUE;
}

static int WiFiEditor_Expunge(LIBBASETYPEPTR LIBBASE)
{
    Locale_Deinitialize();
    return TRUE;
}

ADD2INITLIB(WiFiEditor_Init, 0);
ADD2EXPUNGELIB(WiFiEditor_Expunge, 0);
