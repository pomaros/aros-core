#ifndef APPEARANCEEDITOR_DATA_H
#define APPEARANCEEDITOR_DATA_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    AppearanceEditor's instance data, shared by the program's class and AppearanceEditor.mcc.
*/
#ifndef APPEARANCEEDITOR_DATA_H_IN_SOURCE
/* The module dispatcher needs these; the class source has them. */
#include <aros/debug.h>
#include <zune/customclasses.h>
#include <zune/prefseditor.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/timer.h>
#include <proto/muimaster.h>
#include <proto/alib.h>
#include <libraries/mui.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "locale.h"
#include "appearanceeditor.h"
#include "themepreview.h"
#endif

struct AppearanceEditor_DATA
{
    APTR *ae_readBuff;

    Object *ae_ThemePreview;
    Object *ae_ThemeEnable;
    Object *ae_ThemeChoice;
    Object *ae_ThemePalette;
    Object *ae_OptionPalette;
    Object *ae_ThemeFont;
    Object *ae_OptionFont;
    Object *ae_ThemePointer;
    Object *ae_OptionPointer;
    Object *ae_ThemeZune;
    Object *ae_OptionZune;
    Object *ae_ThemeWand;
    Object *ae_OptionWand;

    Object *ae_ThemeDescGrp;
    Object *ae_ThemeDescA;
    Object *ae_ThemeDescB;
    
    Object *ae_CompEnable;
    Object *ae_CompBelow;
    Object *ae_CompLeft;
    Object *ae_CompRight;
    Object *ae_CompAlpha;

    IPTR   *ae_ThemeArray;
    struct Hook ae_PreviewHook;
};

#endif
