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
    LIBBASE->sb_DOSBase = OpenLibrary("dos.library", 50);
    /* ReplaceObject is vector 227, introduced in DOS 50.84. Refuse an
       older installed generation before publishing an openable library. */
    if (LIBBASE->sb_DOSBase == NULL)
        return FALSE;
    if (LIBBASE->sb_DOSBase->lib_Version == 50 &&
        LIBBASE->sb_DOSBase->lib_Revision < 84) {
        CloseLibrary(LIBBASE->sb_DOSBase);
        LIBBASE->sb_DOSBase = NULL;
        return FALSE;
    }
    LIBBASE->sb_UtilityBase = OpenLibrary("utility.library", 36);
    if (LIBBASE->sb_UtilityBase == NULL) {
        CloseLibrary(LIBBASE->sb_DOSBase);
        LIBBASE->sb_DOSBase = NULL;
        return FALSE;
    }
    return TRUE;
}

ADD2INITLIB(Settings_Init, 0)
