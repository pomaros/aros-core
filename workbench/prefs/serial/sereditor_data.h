#ifndef SEREDITOR_DATA_H
#define SEREDITOR_DATA_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    SerEditor's instance data, shared by the program's class and SerialEditor.mcc.
*/
#ifndef SEREDITOR_DATA_H_IN_SOURCE
/* The module dispatcher needs these; the class source has them. */
#include <zune/customclasses.h>
#include <zune/prefseditor.h>
#include <proto/alib.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <stdlib.h>
#include <aros/debug.h>
#include "locale.h"
#include "sereditor.h"
#include "prefs.h"
#endif

struct SerEditor_DATA
{
    int i;

    CONST_STRPTR ParityLabels[6];
    Object *child;
    Object *baudrate;
    Object *stopbits;
    Object *databits;
    Object *parity;
    Object *inputbuffersize;
    Object *outputbuffersize;
};

#endif
