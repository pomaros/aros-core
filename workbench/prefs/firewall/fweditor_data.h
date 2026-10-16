#ifndef FWEDITOR_DATA_H
#define FWEDITOR_DATA_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    FWEditor's instance data, shared by the program's class and FirewallEditor.mcc.
*/
#ifndef FWEDITOR_DATA_H_IN_SOURCE
/* The module dispatcher needs these; the class source has them. */
#include <exec/types.h>
#include <exec/memory.h>
#include <libraries/mui.h>
#include <utility/hooks.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include <zune/customclasses.h>
#include <zune/prefseditor.h>
#include <stdio.h>
#include <string.h>
#include "fweditor.h"
#include "prefsdata.h"
#include "locale.h"
#endif

struct FWEditor_DATA
{
    /* Main window lists */
    Object *filterList;
    Object *natList;

    /* Filter rule editing sub-window and its gadgets */
    Object *filterEditWin;
    Object *fActionCycle;
    Object *fDirCycle;
    Object *fProtoCycle;
    Object *fIfStr;
    Object *fSrcStr;
    Object *fSrcPortStr;
    Object *fDstStr;
    Object *fDstPortStr;
    Object *fQuickChk;
    Object *fKeepStateChk;
    Object *fLogChk;
    BOOL    filterIsEdit;   /* TRUE = editing existing entry */

    /* NAT rule editing sub-window and its gadgets */
    Object *natEditWin;
    Object *nTypeCycle;
    Object *nProtoCycle;
    Object *nIfStr;
    Object *nSrcStr;
    Object *nSrcPortStr;
    Object *nNatStr;
    Object *nNatPortStr;
    BOOL    natIsEdit;      /* TRUE = editing existing entry */
};

#endif
