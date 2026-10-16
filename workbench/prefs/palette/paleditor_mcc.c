/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    PalEditor built as PaletteEditor.mcc for the settings window: the program's
    source with its methods renamed, the module's methods around them, and
    one live instance, since the editor keeps its prefs in globals.
*/
#define PALETTEEDITOR_MCC
#define PalEditor__OM_NEW PalEditor__OM_NEW_inner
#define PalEditor__OM_DISPOSE PalEditor__OM_DISPOSE_inner
#define PalEditor__MUIM_Setup PalEditor__MUIM_Setup_inner
#define PalEditor__MUIM_Cleanup PalEditor__MUIM_Cleanup_inner
#define PalEditor__MUIM_PrefsEditor_ImportFH PalEditor__MUIM_PrefsEditor_ImportFH_inner
#define PalEditor__MUIM_PrefsEditor_ExportFH PalEditor__MUIM_PrefsEditor_ExportFH_inner
#define PalEditor__MUIM_PrefsEditor_SetDefaults PalEditor__MUIM_PrefsEditor_SetDefaults_inner
#include "paleditor.c"
#undef PalEditor__OM_NEW
#undef PalEditor__OM_DISPOSE
#undef PalEditor__MUIM_Setup
#undef PalEditor__MUIM_Cleanup
#undef PalEditor__MUIM_PrefsEditor_ImportFH
#undef PalEditor__MUIM_PrefsEditor_ExportFH
#undef PalEditor__MUIM_PrefsEditor_SetDefaults

static ULONG live_instances;
static Object *live_object;      /* the instance counted, so a failed
                                    construction never uncounts it */

IPTR PalEditor__OM_NEW(Class *CLASS, Object *self, Msg message)
{
    Object *object;
    if (live_instances != 0)
        return 0;
    object = (Object *)PalEditor__OM_NEW_inner(CLASS, self, (APTR)message);
    if (object != NULL)
    {
        live_instances++;
        live_object = object;
    }
    return (IPTR)object;
}

IPTR PalEditor__OM_DISPOSE(Class *CLASS, Object *self, Msg message)
{
    if (self == live_object)
    {
        live_object = NULL;
        live_instances--;
    }
    return (IPTR)PalEditor__OM_DISPOSE_inner(CLASS, self, message);
}

IPTR PalEditor__MUIM_Setup(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)PalEditor__MUIM_Setup_inner(CLASS, self, (APTR)message);
}

IPTR PalEditor__MUIM_Cleanup(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)PalEditor__MUIM_Cleanup_inner(CLASS, self, (APTR)message);
}

IPTR PalEditor__MUIM_PrefsEditor_ImportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)PalEditor__MUIM_PrefsEditor_ImportFH_inner(CLASS, self, (APTR)message);
}

IPTR PalEditor__MUIM_PrefsEditor_ExportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)PalEditor__MUIM_PrefsEditor_ExportFH_inner(CLASS, self, (APTR)message);
}

IPTR PalEditor__MUIM_PrefsEditor_SetDefaults(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)PalEditor__MUIM_PrefsEditor_SetDefaults_inner(CLASS, self, (APTR)message);
}
