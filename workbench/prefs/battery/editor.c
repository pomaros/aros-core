/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#define MUIMASTER_YES_INLINE_STDARG
#include <libraries/mui.h>
#include <zune/prefseditor.h>
#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
#include "editor.h"
#include LC_LIBDEFS_FILE
#include <libraries/power.h>
#include <proto/power.h>
#include "../../system/Indicators/consumers.h"
#define PowerBase data->power

APTR TaskResBase;

/* The tasks taking the most CPU now, as the battery indicator shows them:
   what drains the battery, in the only measure the system keeps. */
static void show_consumers(struct BatteryEditor_DATA *data)
{
    struct IndicatorConsumers list;
    char text[192];
    if (!TaskResBase)
        TaskResBase = OpenResource("task.resource");
    if (!IndicatorConsumersRead(&list) || !IndicatorConsumersFormat(&list, text, sizeof(text)))
        snprintf(text, sizeof(text), "CPU: per-task accounting unavailable");
    SET(data->consumers, MUIA_Text_Contents, (IPTR)text);
}

static void refresh(struct BatteryEditor_DATA *data)
{
    struct PowerState state;
    show_consumers(data);
    char text[384], estimate[64], current[64], capacity[80];
    if (!data->power || GetPowerStateA(&state, NULL) != POWER_OK ||
        !(state.pst_Flags & POWERSTF_KNOWN))
    {
        SET(data->status, MUIA_Text_Contents, (IPTR)"Battery information unavailable");
        return;
    }
    if (state.pst_Flags & POWERSTF_STALE)
    {
        SET(data->status, MUIA_Text_Contents, (IPTR)"Battery information is stale; waiting for the power service");
        return;
    }
    if (!(state.pst_Flags & POWERSTF_BATTERY))
    {
        SET(data->status, MUIA_Text_Contents, (IPTR)((state.pst_Flags & POWERSTF_MAINS) ?
            "Mains power; no battery present" : "No battery present"));
        return;
    }
    if (state.pst_MinutesLeft)
        snprintf(estimate, sizeof(estimate), "%lu minutes to %s", (unsigned long)state.pst_MinutesLeft,
            state.pst_Flags & POWERSTF_CHARGING ? "full" : "empty");
    else
        strcpy(estimate, "Time estimate unavailable");
    if (state.pst_Flags & POWERSTF_CURRENT)
        snprintf(current, sizeof(current), "Current: %ld mA", (long)state.pst_CurrentMa);
    else
        strcpy(current, "Current unavailable");
    if (state.pst_Flags & POWERSTF_CAPACITY)
        snprintf(capacity, sizeof(capacity), "Capacity: %lu / %lu mAh",
            (unsigned long)state.pst_RemainingMah, (unsigned long)state.pst_FullMah);
    else
        strcpy(capacity, "Capacity unavailable");
    snprintf(text, sizeof(text), "Charge: %lu%%\nPower: %s%s\n%s\n%s\n%s",
        (unsigned long)state.pst_Percent, state.pst_Flags & POWERSTF_MAINS ? "Mains" : "Battery",
        state.pst_Flags & POWERSTF_CHARGING ? " (charging)" : "", estimate, current, capacity);
    SET(data->status, MUIA_Text_Contents, (IPTR)text);
}

IPTR BatteryEditor__OM_NEW(Class *CLASS, Object *self, struct opSet *message)
{
    Object *status, *consumers, *button;
    self = (Object *)DoSuperNewTags(CLASS, self, NULL,
        MUIA_PrefsEditor_Name, (IPTR)"Battery",
        MUIA_PrefsEditor_CanSave, FALSE,
        MUIA_PrefsEditor_CanUse, FALSE,
        MUIA_PrefsEditor_CanTest, FALSE,
        Child, (IPTR)(status = TextObject,
            MUIA_ObjectID, MAKE_ID('B','A','S','T'),
            MUIA_ShortHelp, (IPTR)"Battery charge, source, current, capacity and time estimate",
            MUIA_Text_Contents, (IPTR)"Battery information unavailable",
        End),
        Child, (IPTR)VGroup,
            GroupFrameT("Top consumers"),
            Child, (IPTR)(consumers = TextObject,
                MUIA_ObjectID, MAKE_ID('B','A','C','O'),
                MUIA_ShortHelp, (IPTR)"The tasks using the most processor time now, which drain the battery most",
                MUIA_Text_Contents, (IPTR)"Not measured yet",
            End),
        End,
        Child, (IPTR)(button = SimpleButton("Refresh")),
        TAG_MORE, (IPTR)message->ops_AttrList);
    if (self)
    {
        struct BatteryEditor_DATA *data = INST_DATA(CLASS, self);
        data->status = status;
        data->consumers = consumers;
        SET(button, MUIA_ObjectID, MAKE_ID('B','A','R','F'));
        SET(button, MUIA_ShortHelp, (IPTR)"Read the latest power service sample");
        DoMethod(button, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)self, 1, MUIM_BatteryEditor_Refresh);
    }
    return (IPTR)self;
}

IPTR BatteryEditor__MUIM_Setup(Class *CLASS, Object *self, Msg message)
{
    struct BatteryEditor_DATA *data = INST_DATA(CLASS, self);
    struct TagItem tags[] = {{POWER_Events, 1UL << POWER_EVENT_SOURCE}, {TAG_DONE, 0}};
    if (!DoSuperMethodA(CLASS, self, message)) return FALSE;
    data->power = OpenLibrary((CONST_STRPTR)"power.library", 1);
    if (data->power && (data->port = CreateMsgPort()))
    {
        data->subscription = AddPowerNotifyA(data->port, tags);
        if (data->subscription)
        {
            data->handler.ihn_Object = self;
            data->handler.ihn_Signals = 1UL << data->port->mp_SigBit;
            data->handler.ihn_Flags = 0;
            data->handler.ihn_Method = MUIM_BatteryEditor_Refresh;
            DoMethod(_app(self), MUIM_Application_AddInputHandler, (IPTR)&data->handler);
            data->handler_added = TRUE;
        }
    }
    /* Source notifications omit staleness and current-only changes. Re-read
       the published sample periodically; this does not perform hardware I/O. */
    data->timer.ihn_Object = self;
    data->timer.ihn_Flags = MUIIHNF_TIMER;
    data->timer.ihn_Millis = 2000;
    data->timer.ihn_Method = MUIM_BatteryEditor_Refresh;
    DoMethod(_app(self), MUIM_Application_AddInputHandler, (IPTR)&data->timer);
    refresh(data);
    return TRUE;
}

IPTR BatteryEditor__MUIM_BatteryEditor_Refresh(Class *CLASS, Object *self, Msg message)
{
    struct BatteryEditor_DATA *data = INST_DATA(CLASS, self);
    struct Message *event;
    if (data->port)
        while ((event = GetMsg(data->port))) ReplyMsg(event);
    refresh(data);
    return 0;
}

IPTR BatteryEditor__MUIM_Cleanup(Class *CLASS, Object *self, Msg message)
{
    struct BatteryEditor_DATA *data = INST_DATA(CLASS, self);
    struct Message *event;
    DoMethod(_app(self), MUIM_Application_RemInputHandler, (IPTR)&data->timer);
    if (data->handler_added)
        DoMethod(_app(self), MUIM_Application_RemInputHandler, (IPTR)&data->handler);
    data->handler_added = FALSE;
    /* Remove first: no producer can enqueue again after RemPowerNotify.
       Drain all borrowed messages before deleting our port. */
    if (data->subscription) RemPowerNotify(data->subscription);
    data->subscription = NULL;
    if (data->port)
    {
        while ((event = GetMsg(data->port))) ReplyMsg(event);
        DeleteMsgPort(data->port);
        data->port = NULL;
    }
    if (data->power) CloseLibrary(data->power);
    data->power = NULL;
    return DoSuperMethodA(CLASS, self, message);
}

IPTR BatteryEditor__MUIM_PrefsEditor_ImportFromDirectory(Class *CLASS, Object *self, Msg message)
{
    return TRUE;
}
IPTR BatteryEditor__MUIM_PrefsEditor_ExportToDirectory(Class *CLASS, Object *self, Msg message)
{
    return TRUE;
}

IPTR BatteryEditor__OM_GET(Class *CLASS, Object *self, struct opGet *message)
{
    if (message->opg_AttrID == MUIA_PrefsEditor_CanSave)
    {
        *message->opg_Storage = FALSE;
        return TRUE;
    }
    return DoSuperMethodA(CLASS, self, (Msg)message);
}
