/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    LocaleEditor.mcc setup: the catalog and the editor's prefs state. The
    helper classes the editor defines with the in-program class macros are
    in the INIT symbol set, which the module's startup runs before this.
*/
#include <exec/types.h>
#include <aros/symbolsets.h>
#include <proto/exec.h>

#include LC_LIBDEFS_FILE

#include "locale.h"
#include "prefs.h"

static int LocaleEditor_Init(LIBBASETYPEPTR LIBBASE)
{
    Locale_Initialize();
    return Prefs_Initialize() ? TRUE : FALSE;
}

static int LocaleEditor_Expunge(LIBBASETYPEPTR LIBBASE)
{
    Prefs_Deinitialize();
    Locale_Deinitialize();
    return TRUE;
}

ADD2INITLIB(LocaleEditor_Init, 0);
ADD2EXPUNGELIB(LocaleEditor_Expunge, 0);
