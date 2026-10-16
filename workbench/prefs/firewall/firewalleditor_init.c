/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    FirewallEditor.mcc setup: what the program's main does before creating the
    editor. Helper classes defined with the in-program class macros are in
    the INIT symbol set, which the module's startup runs first.
*/
#include <exec/types.h>
#include <aros/symbolsets.h>
#include <proto/exec.h>
#include LC_LIBDEFS_FILE
#include "locale.h"


static int FirewallEditor_Init(LIBBASETYPEPTR LIBBASE)
{
    Locale_Initialize();
    return TRUE;
}

static int FirewallEditor_Expunge(LIBBASETYPEPTR LIBBASE)
{
    Locale_Deinitialize();
    return TRUE;
}

ADD2INITLIB(FirewallEditor_Init, 0);
ADD2EXPUNGELIB(FirewallEditor_Expunge, 0);
