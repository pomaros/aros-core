#ifndef REQTOOLSEDITOR_DATA_H
#define REQTOOLSEDITOR_DATA_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    ReqToolsEditor's instance data, shared by the program's class and ReqToolsEditor.mcc.
*/
#ifndef REQTOOLSEDITOR_DATA_H_IN_SOURCE
/* The module dispatcher needs these; the class source has them. */
#include <aros/debug.h>
#include <zune/prefseditor.h>
#include <zune/customclasses.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/alib.h>
#include "reqtoolseditor.h"
#include "reqtoolsstringify.h"
#include "prefs.h"
#include "locale.h"
#endif

struct ReqToolsEditor_DATA
{
    Object *poptofrontobj;
    Object *usesysfontobj;
    Object *usefunckeysobj;
    Object *colorwheelstyleobj;

    Object *immediatesortobj;
    Object *drawersfirstobj;
    Object *drawersmixedobj;
    Object *diskactivityobj;
    Object *mmbobj;

    Object *defaultsforobj;
    Object *sizepercentobj;
    Object *minvisobj;
    Object *maxvisobj;
    Object *positionobj;
    Object *offsetxobj;
    Object *offsetyobj;

    BOOL noupdatehook;
};

#endif
