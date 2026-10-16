/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
*/
#include <config.h>

#include "library.h"
#include "DriverData.h"

BOOL
DriverInit(struct DriverBase *AHIsubBase)
{
    struct BTA2DPAudioBase *BTA2DPAudioBase = (struct BTA2DPAudioBase *) AHIsubBase;

    BTA2DPAudioBase->dosbase = (struct DosLibrary *) OpenLibrary(DOSNAME, 37);
    if(BTA2DPAudioBase->dosbase == NULL) {
        Req("Unable to open 'dos.library' version 37.\n");
        return FALSE;
    }
    /* No hardware check: the headset connects and leaves at any time, and
       the mode stays usable (silent, timer-paced) without one. */
    return TRUE;
}

VOID
DriverCleanup(struct DriverBase *AHIsubBase)
{
    struct BTA2DPAudioBase *BTA2DPAudioBase = (struct BTA2DPAudioBase *) AHIsubBase;

    CloseLibrary((struct Library *) DOSBase);
}
