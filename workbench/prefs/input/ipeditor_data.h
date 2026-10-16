#ifndef IPEDITOR_DATA_H
#define IPEDITOR_DATA_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    IPEditor's instance data, shared by the program's class and InputEditor.mcc.
*/
#ifndef IPEDITOR_DATA_H_IN_SOURCE
/* The module dispatcher needs these; the class source has them. */
#include <devices/rawkeycodes.h>
#include <libraries/kms.h>
#include <mui/HotkeyString_mcc.h>
#include <zune/customclasses.h>
#include <zune/prefseditor.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/commodities.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <aros/debug.h>
#include "misc.h"
#include "keymap.h"
#include "locale.h"
#include "ipeditor.h"
#include "prefs.h"
#include "stringify.h"
#endif

struct IPEditor_DATA
{
    Object *iped_KeyTypesView;
    Object *iped_KeyTypes;
    Object *iped_DefKey;
    Object *iped_AltKey;
    Object *iped_SetDefKey;
    Object *iped_SetAltKey;
    Object *iped_RepeatRate;
    Object *iped_RepeatDelay;
    Object *iped_SwitchEnable;
    Object *iped_SwitchKey;
    Object *iped_Accelerated;
    Object *iped_MouseSpeed;
    Object *iped_DoubleClickDelay;
    Object *iped_LeftHandedMouse;

    struct Hook iped_setHook;
    struct Hook iped_setEnableHook;
    struct Hook iped_switchEnableHook;
};

#endif
