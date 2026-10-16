/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#define MUIMASTER_YES_INLINE_STDARG
#include <libraries/mui.h>
#include <zune/prefseditor.h>
#include <libraries/bluetooth.h>
#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/bluetooth.h>
#include <aros/symbolsets.h>
#include "bluetoothprefs.h"
#include "ActionClass.h"
#include "IconListClass.h"
#include "ScanWinClass.h"
#include "DevWinClass.h"
#include "editor.h"
#include LC_LIBDEFS_FILE

struct Library *BluetoothBase;
struct MUI_CustomClass *IconListClass, *ActionClass, *ScanWinClass, *DevWinClass;

static IPTR config_hash(void)
{
    IPTR hash = 0;
    if (BluetoothBase) btGetAttrs(BGA_STACK, NULL, BSA_CurrConfigHash, &hash, TAG_END);
    return hash;
}

/* Stable IDs cover the existing panel's settings and actions. */
static void identify(Object *object, ULONG id, CONST_STRPTR help)
{
    if (object) SetAttrs(object, MUIA_ObjectID, id, MUIA_ShortHelp, (IPTR)help, TAG_DONE);
}

IPTR BluetoothEditor__OM_NEW(Class *CLASS, Object *self, struct opSet *message)
{
    Object *panel = NULL, *contents;
    if (ActionClass) panel = NewObject(ActionClass->mcc_Class, NULL, TAG_DONE);
    if (ActionClass && !panel) return 0;
    contents = panel;
    if (!contents) contents = TextObject,
        MUIA_ObjectID, MAKE_ID('B','T','S','T'),
        MUIA_ShortHelp, (IPTR)"Bluetooth service availability",
        MUIA_Text_Contents, (IPTR)"Bluetooth service unavailable",
    End;
    self = (Object *)DoSuperNewTags(CLASS, self, NULL,
        MUIA_PrefsEditor_Name, (IPTR)"Bluetooth",
        MUIA_PrefsEditor_CanTest, FALSE,
        MUIA_PrefsEditor_CanUse, panel != NULL,
        Child, (IPTR)contents,
        TAG_MORE, (IPTR)message->ops_AttrList);
    if (self)
    {
        struct BluetoothEditor_DATA *data = INST_DATA(CLASS, self);
        data->panel = panel;
        data->baseline = config_hash();
        if (panel)
        {
            struct BtActionData *action = INST_DATA(ActionClass->mcc_Class, panel);
            action->editor = self;
            /* The enclosing PrefsWindow owns Save/Use. All Online/Offline in
               the classic panel only refresh lists, so do not expose them. */
            SET(action->bt_save, MUIA_ShowMe, FALSE);
            SET(action->bt_use, MUIA_ShowMe, FALSE);
            SET(action->bt_allon, MUIA_ShowMe, FALSE);
            SET(action->bt_alloff, MUIA_ShowMe, FALSE);
#define ID(field, a, b, help) identify(action->field, MAKE_ID('B','T',a,b), (CONST_STRPTR)help)
            ID(hwlist,'H','W',"Bluetooth radios");
            ID(hwdevobj,'H','D',"HCI device name"); ID(hwunitobj,'H','U',"HCI device unit");
            ID(bt_hwadd,'H','A',"Add a Bluetooth radio"); ID(bt_hwremove,'H','R',"Remove a Bluetooth radio");
            ID(devlist,'D','V',"Known Bluetooth devices and pairing state");
            ID(bt_adddev,'S','C',"Discover nearby Bluetooth devices");
            ID(bt_pair,'P','A',"Pair the selected device");
            ID(bt_register,'R','G',"Register the selected device");
            ID(bt_unregister,'U','R',"Unregister the selected device");
            ID(bt_connect,'C','N',"Connect the selected device");
            ID(bt_disconnect,'D','C',"Disconnect the selected device");
            ID(bt_forget,'F','G',"Forget the selected device");
            ID(bt_info,'I','N',"Device information and services");
            ID(opt_discoverable,'D','S',"Allow other devices to discover this computer");
            ID(opt_connectable,'C','B',"Allow incoming Bluetooth connections");
            ID(opt_autoconnect,'A','C',"Reconnect registered devices automatically");
            ID(opt_popuppairing,'P','P',"Show pairing requests");
            ID(opt_localname,'N','M',"Name shown to nearby Bluetooth devices");
            ID(opt_disctime,'D','T',"Discovery duration");
            ID(opt_loginfo,'L','I',"Log Bluetooth information");
            ID(opt_logwarn,'L','W',"Log Bluetooth warnings");
            ID(opt_logerr,'L','E',"Log Bluetooth errors");
            ID(opt_logfail,'L','F',"Log Bluetooth failures");
            ID(opt_popupnew,'P','N',"New device notification policy");
            ID(opt_popupgone,'P','G',"Notify when a device disappears");
            ID(opt_popupdelay,'P','D',"Notification close delay");
            ID(opt_popupactivate,'P','V',"Activate notification windows");
            ID(opt_popuptofront,'P','F',"Bring notifications to the front");
            ID(cfglist,'C','F',"Bluetooth configuration entries");
#undef ID
            data->snapshot = btWriteCfg(NULL);
        }
    }
    return (IPTR)self;
}

IPTR BluetoothEditor__OM_GET(Class *CLASS, Object *self, struct opGet *message)
{
    struct BluetoothEditor_DATA *data = INST_DATA(CLASS, self);
    if (message->opg_AttrID == MUIA_PrefsEditor_CanSave)
    {
        *message->opg_Storage = data->panel != NULL;
        return TRUE;
    }
    return DoSuperMethodA(CLASS, self, (Msg)message);
}

IPTR BluetoothEditor__MUIM_BluetoothEditor_Sync(Class *CLASS, Object *self, Msg message)
{
    struct BluetoothEditor_DATA *data = INST_DATA(CLASS, self);
    SET(self, MUIA_PrefsEditor_Changed, data->panel && config_hash() != data->baseline);
    return 0;
}

IPTR BluetoothEditor__MUIM_Setup(Class *CLASS, Object *self, Msg message)
{
    struct BluetoothEditor_DATA *data = INST_DATA(CLASS, self);
    if (!DoSuperMethodA(CLASS, self, message)) return FALSE;
    if (data->panel)
    {
        data->timer.ihn_Object = self;
        data->timer.ihn_Flags = MUIIHNF_TIMER;
        data->timer.ihn_Millis = 500;
        data->timer.ihn_Method = MUIM_BluetoothEditor_Sync;
        DoMethod(_app(self), MUIM_Application_AddInputHandler, (IPTR)&data->timer);
        data->timer_added = TRUE;
    }
    return TRUE;
}

IPTR BluetoothEditor__MUIM_Cleanup(Class *CLASS, Object *self, Msg message)
{
    struct BluetoothEditor_DATA *data = INST_DATA(CLASS, self);
    if (data->timer_added)
        DoMethod(_app(self), MUIM_Application_RemInputHandler, (IPTR)&data->timer);
    data->timer_added = FALSE;
    return DoSuperMethodA(CLASS, self, message);
}

static IPTR persist(Class *CLASS, Object *self, STRPTR path)
{
    struct BluetoothEditor_DATA *data = INST_DATA(CLASS, self);
    if (!data->panel || !btSaveCfgToDisk(path, FALSE)) return FALSE;
    data->baseline = config_hash();
    SET(self, MUIA_PrefsEditor_Changed, FALSE);
    return TRUE;
}
IPTR BluetoothEditor__MUIM_PrefsEditor_Save(Class *CLASS, Object *self, Msg message)
{
    return persist(CLASS, self, NULL);
}
IPTR BluetoothEditor__MUIM_PrefsEditor_Use(Class *CLASS, Object *self, Msg message)
{
    return persist(CLASS, self, (STRPTR)"ENV:Sys/bluetooth.prefs");
}
IPTR BluetoothEditor__MUIM_PrefsEditor_Revert(Class *CLASS, Object *self, Msg message)
{
    struct BluetoothEditor_DATA *data = INST_DATA(CLASS, self);
    if (!data->snapshot || !btReadCfg(NULL, data->snapshot)) return FALSE;
    btParseCfg();
    DoMethod(data->panel, MUIM_BtA_RefreshConfig);
    DoMethod(self, MUIM_BluetoothEditor_Sync);
    return TRUE;
}
/* The running service already loaded its configuration. */
IPTR BluetoothEditor__MUIM_PrefsEditor_ImportFromDirectory(Class *CLASS, Object *self, Msg message)
{
    return TRUE;
}
IPTR BluetoothEditor__MUIM_PrefsEditor_ExportToDirectory(Class *CLASS, Object *self, struct MUIP_PrefsEditor_ExportToDirectory *message)
{
    char path[256] = "";
    if (!message->directory || !AddPart((STRPTR)path, message->directory, sizeof(path)) ||
        !AddPart((STRPTR)path, (CONST_STRPTR)"Sys/bluetooth.prefs", sizeof(path))) return FALSE;
    return persist(CLASS, self, (STRPTR)path);
}
IPTR BluetoothEditor__OM_DISPOSE(Class *CLASS, Object *self, Msg message)
{
    struct BluetoothEditor_DATA *data = INST_DATA(CLASS, self);
    if (data->snapshot) btFreeVec(data->snapshot);
    return DoSuperMethodA(CLASS, self, message);
}

static int editor_expunge(LIBBASETYPEPTR LIBBASE);

static int editor_init(LIBBASETYPEPTR LIBBASE)
{
    BluetoothBase = OpenLibrary((CONST_STRPTR)"bluetooth.library", 1);
    if (!BluetoothBase) return TRUE;
    IconListClass = MUI_CreateCustomClass(NULL, MUIC_List, NULL, sizeof(struct IconListData), IconListDispatcher);
    ScanWinClass = MUI_CreateCustomClass(NULL, MUIC_Window, NULL, sizeof(struct ScanWinData), ScanWinDispatcher);
    DevWinClass = MUI_CreateCustomClass(NULL, MUIC_Window, NULL, sizeof(struct DevWinData), DevWinDispatcher);
    ActionClass = MUI_CreateCustomClass(NULL, MUIC_Group, NULL, sizeof(struct BtActionData), ActionDispatcher);
    if (IconListClass && ScanWinClass && DevWinClass && ActionClass) return TRUE;
    editor_expunge(LIBBASE);
    return FALSE;
}
static int editor_expunge(LIBBASETYPEPTR LIBBASE)
{
    if (ActionClass) MUI_DeleteCustomClass(ActionClass);
    if (DevWinClass) MUI_DeleteCustomClass(DevWinClass);
    if (ScanWinClass) MUI_DeleteCustomClass(ScanWinClass);
    if (IconListClass) MUI_DeleteCustomClass(IconListClass);
    if (BluetoothBase) CloseLibrary(BluetoothBase);
    ActionClass = DevWinClass = ScanWinClass = IconListClass = NULL;
    BluetoothBase = NULL;
    return TRUE;
}
ADD2INITLIB(editor_init, 0);
ADD2EXPUNGELIB(editor_expunge, 0);
