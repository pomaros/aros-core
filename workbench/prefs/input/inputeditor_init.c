/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    InputEditor.mcc setup: what the program's main does before creating the
    editor. Helper classes defined with the in-program class macros are in
    the INIT symbol set, which the module's startup runs first.
*/
#include <exec/types.h>
#include <aros/symbolsets.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <exec/memory.h>
#include LC_LIBDEFS_FILE
#include "locale.h"
#include "prefs.h"


static int InputEditor_Init(LIBBASETYPEPTR LIBBASE)
{
    Locale_Initialize();
    NewList(&keymap_list);
    Prefs_Default();
    mempool = CreatePool(MEMF_PUBLIC | MEMF_CLEAR, 2048, 2048);
    if (mempool == NULL) return FALSE;
    Prefs_ScanDirectory("DEVS:Keymaps/~(#?.info|#?.dbg)",
        &keymap_list, sizeof(struct KeymapEntry));
    return TRUE;
}

static int InputEditor_Expunge(LIBBASETYPEPTR LIBBASE)
{
    if (mempool != NULL)
    {
        DeletePool(mempool);
        mempool = NULL;
    }
    NewList(&keymap_list);
    Locale_Deinitialize();
    return TRUE;
}

ADD2INITLIB(InputEditor_Init, 0);
ADD2EXPUNGELIB(InputEditor_Expunge, 0);
