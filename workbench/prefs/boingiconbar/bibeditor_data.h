#ifndef BIBEDITOR_DATA_H
#define BIBEDITOR_DATA_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    BibEditor's instance data, shared by the program's class and BoingIconBarEditor.mcc.
*/
#ifndef BIBEDITOR_DATA_H_IN_SOURCE
/* The module dispatcher needs these; the class source has them. */
#include <zune/customclasses.h>
#include <zune/prefseditor.h>
#include <proto/alib.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/asl.h>
#include <proto/dos.h>
#include <stdlib.h>
#include <string.h>
#include <aros/debug.h>
#include "locale.h"
#include "bibeditor.h"
#include "prefs.h"
#endif

struct BibEditor_DATA
{
    Object *dock_lst, *add_dock_btn, *del_dock_btn;
    Object *dock_name_str;
    Object *program_lst, *add_program_btn, *del_program_btn;
    struct Hook add_dock_hook, del_dock_hook, change_dock_hook;
    struct Hook add_program_hook, del_program_hook, change_program_hook;
};

#endif
