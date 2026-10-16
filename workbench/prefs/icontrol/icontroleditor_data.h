#ifndef ICONTROLEDITOR_DATA_H
#define ICONTROLEDITOR_DATA_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    IControlEditor's instance data, shared by the program's class and IControlEditor.mcc.
*/
#ifndef ICONTROLEDITOR_DATA_H_IN_SOURCE
/* The module dispatcher needs these; the class source has them. */
#include <aros/debug.h>
#include <devices/rawkeycodes.h>
#include <mui/HotkeyString_mcc.h>
#include <zune/prefseditor.h>
#include <zune/customclasses.h>
#include <proto/alib.h>
#include <proto/commodities.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/alib.h>
#include <string.h>
#include <stdio.h>
#include "icontroleditor.h"
#include "prefs.h"
#include "locale.h"
#endif

struct IControlEditor_DATA
{
    Object  *menutypeobj;
    Object  *menulookobj;
    Object  *menustickobj;
    Object  *menutitlepullobj;
    Object  *offscreenobj;
    Object  *defpubscrobj;
    Object  *scrleftdragobj;
    Object  *scrrightdragobj;
    Object  *scrtopdragobj;
    Object  *scrbotdragobj;
    Object  *metadragobj;
    Object  *previewpage;
};

#endif
