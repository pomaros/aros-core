/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    AppearanceEditor built as AppearanceEditor.mcc for the settings window: the program's
    source with its methods renamed, the module's methods around them, and
    one live instance, since the editor keeps its prefs in globals.
*/
#define APPEARANCEEDITOR_MCC
#define AppearanceEditor__OM_NEW AppearanceEditor__OM_NEW_inner
#define AppearanceEditor__OM_DISPOSE AppearanceEditor__OM_DISPOSE_inner
#define AppearanceEditor__MUIM_PrefsEditor_Import AppearanceEditor__MUIM_PrefsEditor_Import_inner
#define AppearanceEditor__MUIM_PrefsEditor_ImportFH AppearanceEditor__MUIM_PrefsEditor_ImportFH_inner
#define AppearanceEditor__MUIM_PrefsEditor_Export AppearanceEditor__MUIM_PrefsEditor_Export_inner
#define AppearanceEditor__MUIM_PrefsEditor_ExportFH AppearanceEditor__MUIM_PrefsEditor_ExportFH_inner
#define AppearanceEditor__MUIM_PrefsEditor_SetDefaults AppearanceEditor__MUIM_PrefsEditor_SetDefaults_inner
#include "appearanceeditor.c"
#undef AppearanceEditor__OM_NEW
#undef AppearanceEditor__OM_DISPOSE
#undef AppearanceEditor__MUIM_PrefsEditor_Import
#undef AppearanceEditor__MUIM_PrefsEditor_ImportFH
#undef AppearanceEditor__MUIM_PrefsEditor_Export
#undef AppearanceEditor__MUIM_PrefsEditor_ExportFH
#undef AppearanceEditor__MUIM_PrefsEditor_SetDefaults

static ULONG live_instances;
static Object *live_object;      /* the instance counted, so a failed
                                    construction never uncounts it */

IPTR AppearanceEditor__OM_NEW(Class *CLASS, Object *self, Msg message)
{
    Object *object;
    if (live_instances != 0)
        return 0;
    object = (Object *)AppearanceEditor__OM_NEW_inner(CLASS, self, (APTR)message);
    if (object != NULL)
    {
        live_instances++;
        live_object = object;
    }
    return (IPTR)object;
}

IPTR AppearanceEditor__OM_DISPOSE(Class *CLASS, Object *self, Msg message)
{
    if (self == live_object)
    {
        live_object = NULL;
        live_instances--;
    }
    return (IPTR)AppearanceEditor__OM_DISPOSE_inner(CLASS, self, message);
}

IPTR AppearanceEditor__MUIM_PrefsEditor_Import(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)AppearanceEditor__MUIM_PrefsEditor_Import_inner(CLASS, self, (APTR)message);
}

IPTR AppearanceEditor__MUIM_PrefsEditor_ImportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)AppearanceEditor__MUIM_PrefsEditor_ImportFH_inner(CLASS, self, (APTR)message);
}

IPTR AppearanceEditor__MUIM_PrefsEditor_Export(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)AppearanceEditor__MUIM_PrefsEditor_Export_inner(CLASS, self, (APTR)message);
}

IPTR AppearanceEditor__MUIM_PrefsEditor_ExportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)AppearanceEditor__MUIM_PrefsEditor_ExportFH_inner(CLASS, self, (APTR)message);
}

IPTR AppearanceEditor__MUIM_PrefsEditor_SetDefaults(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)AppearanceEditor__MUIM_PrefsEditor_SetDefaults_inner(CLASS, self, (APTR)message);
}
