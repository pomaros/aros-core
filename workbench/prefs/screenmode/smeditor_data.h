#ifndef SMEDITOR_DATA_H
#define SMEDITOR_DATA_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    SMEditor's instance data, shared by the program's class and ScreenModeEditor.mcc.
*/
#ifndef SMEDITOR_DATA_H_IN_SOURCE
/* The module dispatcher needs these; the class source has them. */
#include <libraries/mui.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/iffparse.h>
#include <zune/customclasses.h>
#include <zune/prefseditor.h>
#include <zune/systemprefswindow.h>
#include <prefs/screenmode.h>
#include <prefs/prefhdr.h>
#include <stdio.h>
#include <string.h>
#include "locale.h"
#include "prefs.h"
#include "smeditor.h"
#include "smselector.h"
#include "smproperties.h"
#include "smattributes.h"
#endif
#include "../../include/scaleresult.h"

struct SMEditor_DATA
{
    Object *selector, *properties, *attributes, *scale;
    struct Hook scale_hook, raster_hook, gesture_hook;
    struct ScaleAttempt pending_attempt;
    struct ScreenModePrefs imported_prefs;
    BOOL raster_changed, importing;
    IPTR initial_live_scale;
    BOOL setup_complete, initial_live_captured;
    BOOL gesture_held, pending_failure;
};

#endif
