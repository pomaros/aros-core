#ifndef ASLEDITOR_DATA_H
#define ASLEDITOR_DATA_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    AslEditor's instance data, shared by the program's class and AslEditor.mcc.
*/
#ifndef ASLEDITOR_DATA_H_IN_SOURCE
/* The module dispatcher needs these; the class source has them. */
#include <aros/debug.h>
#include <devices/rawkeycodes.h>
#include <zune/prefseditor.h>
#include <zune/customclasses.h>
#include <proto/alib.h>
#include <proto/commodities.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/alib.h>
#include <string.h>
#include <stdio.h>
#include "asleditor.h"
#include "aslstringify.h"
#include "prefs.h"
#include "locale.h"
#endif

struct AslEditor_DATA
{
    Object *drawersfirstobj;

    Object *positionobj;
    Object *offsetxobj;
    Object *offsetyobj;

    Object *sizeobj;
    Object *heightpercentobj;
    Object *widthpercentobj;

    BOOL noupdatehook;
};

#endif
