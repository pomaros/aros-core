#ifndef PALEDITOR_DATA_H
#define PALEDITOR_DATA_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    PalEditor's instance data, shared by the program's class and PaletteEditor.mcc.
*/
#ifndef PALEDITOR_DATA_H_IN_SOURCE
/* The module dispatcher needs these; the class source has them. */
#include <aros/debug.h>
#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/muimaster.h>
#include <proto/dos.h>
#include <proto/iffparse.h>
#include <exec/types.h>
#include <utility/tagitem.h>
#include <libraries/asl.h>
#include <libraries/mui.h>
#include <prefs/palette.h>
#include <prefs/prefhdr.h>
#include <zune/customclasses.h>
#include <zune/prefseditor.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "locale.h"
#include "paleditor.h"
#include "palette.h"
#include "prefs.h"
#endif

struct PalEditor_DATA
{
    Object                      *palpe_palette;
    ULONG                       *penmap4;
    ULONG                       *penmap8;
    struct MUI_Palette_Entry    *pens;
    UWORD                       *origcols;
    UWORD                       count;
    BOOL                        restore;
};

#endif
