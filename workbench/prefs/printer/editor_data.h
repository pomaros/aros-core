#ifndef EDITOR_DATA_H
#define EDITOR_DATA_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    PrinterEditor's instance data, shared by the program's class and PrinterEditor.mcc.
*/
#ifndef EDITOR_DATA_H_IN_SOURCE
/* The module dispatcher needs these; the class source has them. */
#include <stdlib.h>
#include <stdio.h>      /* for sprintf */
#include <aros/debug.h>
#include <zune/customclasses.h>
#include <zune/prefseditor.h>
#include <proto/alib.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/dos.h>
#include <string.h>
#include "locale.h"
#include "editor.h"
#include "prefs.h"
#endif

#ifndef UNIT_COUNT
#define UNIT_COUNT 10
#endif

struct PrinterEditor_DATA
{
    STRPTR UnitLabels[UNIT_COUNT];
    int PrinterDeviceUnit;

    Object *child;

    Object *pd_UnitNum;         /* Cycle */
    Object *pd_UnitName;        /* String */

    Object *pt_DriverList;      /* Dirlist */
    Object *pt_Driver;          /* Text */
    Object *pt_Port;            /* Cycle */
    Object *pt_PaperType;       /* Cycle */
    Object *pt_PaperSize;       /* Cycle */
    Object *pt_PaperLength;     /* String (integer) */
    Object *pt_Pitch;           /* Cycle */
    Object *pt_Spacing;         /* Cycle */
    Object *pt_LeftMargin;      /* String (integer) */
    Object *pt_RightMargin;     /* String (integer) */
    Object *pt_Quality;         /* Cycle */

    Object *pu_UnitNum;         /* String (integer) */
    Object *pu_DeviceName;      /* String */
    Object *pu_DeviceNameCustom;/* CheckMark */

    Object *pg_Aspect;          /* Cycle */
    Object *pg_Shade;           /* Cycle */
    Object *pg_Image;           /* Cycle */
    Object *pg_Threshold;       /* Slider */
    Object *pg_Dimensions;      /* Cycle */
    Object *pg_Dithering;       /* Cycle */
    Object *pg_GraphicFlags_Center;     /* CheckMark */
    Object *pg_GraphicFlags_AntiAlias;  /* CheckMark */
    Object *pg_GraphicFlags_IntScaling; /* CheckMark */
    Object *pg_PrintDensity;    /* Slider */

    Object *pg_MaxUnits;        /* Cycle */
    Object *pg_PrintMaxWidth;   /* String (integer) */
    Object *pg_PrintMaxHeight;  /* String (integer) */

    Object *pg_OffsetUnits;     /* Label */
    Object *pg_PrintXOffset;    /* String (integer) */
    Object *pg_PrintYOffset;    /* String (integer) */
};

#endif
