/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <aros/symbolsets.h>
#include <proto/exec.h>

#include "settings_private.h"
#include LC_LIBDEFS_FILE

static int Settings_Init(LIBBASETYPEPTR LIBBASE)
{
    InitSemaphore(&LIBBASE->sb_Lock);
    NEWLIST(&LIBBASE->sb_Schemas);
    NEWLIST(&LIBBASE->sb_Watches);
    NEWLIST(&LIBBASE->sb_Subscriptions);
    LIBBASE->sb_DOSBase = OpenLibrary("dos.library", 36);
    LIBBASE->sb_UtilityBase = OpenLibrary("utility.library", 36);
    if (LIBBASE->sb_DOSBase == NULL || LIBBASE->sb_UtilityBase == NULL) {
        if (LIBBASE->sb_DOSBase != NULL)
            CloseLibrary(LIBBASE->sb_DOSBase);
        if (LIBBASE->sb_UtilityBase != NULL)
            CloseLibrary(LIBBASE->sb_UtilityBase);
        return FALSE;
    }
    return TRUE;
}

ADD2INITLIB(Settings_Init, 0)
