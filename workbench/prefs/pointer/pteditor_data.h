#ifndef PTEDITOR_DATA_H
#define PTEDITOR_DATA_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    PTEditor's instance data, shared by the program's class and PointerEditor.mcc.
*/
#ifndef PTEDITOR_DATA_H_IN_SOURCE
/* The module dispatcher needs these; the class source has them. */
#include <libraries/asl.h>
#include <zune/prefseditor.h>
#include <proto/alib.h>
#include <proto/utility.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <aros/debug.h>
#include "locale.h"
#include "ppreview.h"
#include "pteditor.h"
#include "prefs.h"
#endif

struct PTEditor_DATA
{
    ULONG   pded_oldentry;
    Object *pted_previewImage;
    Object *pted_typeCycle;
    Object *pted_fileString;
    Object *pted_alphaSlider;
    Object *pted_hotspotButton;
    struct Hook pted_cycleHook;
    struct Hook pted_filenameHook;
};

#endif
