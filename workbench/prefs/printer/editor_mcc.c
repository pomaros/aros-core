/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    PrinterEditor built as PrinterEditor.mcc for the settings window: the program's
    source with its methods renamed, the module's methods around them, and
    one live instance, since the editor keeps its prefs in globals.
*/
#define PRINTEREDITOR_MCC
#define PrinterEditor__OM_NEW PrinterEditor__OM_NEW_inner
#define PrinterEditor__OM_DISPOSE PrinterEditor__OM_DISPOSE_inner
#define PrinterEditor__MUIM_PrefsEditor_ImportFH PrinterEditor__MUIM_PrefsEditor_ImportFH_inner
#define PrinterEditor__MUIM_PrefsEditor_ExportFH PrinterEditor__MUIM_PrefsEditor_ExportFH_inner
#define PrinterEditor__MUIM_PrefsEditor_SetDefaults PrinterEditor__MUIM_PrefsEditor_SetDefaults_inner
#define PrinterEditor__MUIM_PrinterEditor_Sync PrinterEditor__MUIM_PrinterEditor_Sync_inner
#define PrinterEditor__MUIM_PrinterEditor_Refresh PrinterEditor__MUIM_PrinterEditor_Refresh_inner
#include "editor.c"
#undef PrinterEditor__OM_NEW
#undef PrinterEditor__OM_DISPOSE
#undef PrinterEditor__MUIM_PrefsEditor_ImportFH
#undef PrinterEditor__MUIM_PrefsEditor_ExportFH
#undef PrinterEditor__MUIM_PrefsEditor_SetDefaults
#undef PrinterEditor__MUIM_PrinterEditor_Sync
#undef PrinterEditor__MUIM_PrinterEditor_Refresh

static ULONG live_instances;
static Object *live_object;      /* the instance counted, so a failed
                                    construction never uncounts it */

IPTR PrinterEditor__OM_NEW(Class *CLASS, Object *self, Msg message)
{
    Object *object;
    if (live_instances != 0)
        return 0;
    object = (Object *)PrinterEditor__OM_NEW_inner(CLASS, self, (APTR)message);
    if (object != NULL)
    {
        live_instances++;
        live_object = object;
    }
    return (IPTR)object;
}

IPTR PrinterEditor__OM_DISPOSE(Class *CLASS, Object *self, Msg message)
{
    if (self == live_object)
    {
        live_object = NULL;
        live_instances--;
    }
    return (IPTR)PrinterEditor__OM_DISPOSE_inner(CLASS, self, message);
}

IPTR PrinterEditor__MUIM_PrefsEditor_ImportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)PrinterEditor__MUIM_PrefsEditor_ImportFH_inner(CLASS, self, (APTR)message);
}

IPTR PrinterEditor__MUIM_PrefsEditor_ExportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)PrinterEditor__MUIM_PrefsEditor_ExportFH_inner(CLASS, self, (APTR)message);
}

IPTR PrinterEditor__MUIM_PrefsEditor_SetDefaults(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)PrinterEditor__MUIM_PrefsEditor_SetDefaults_inner(CLASS, self, (APTR)message);
}

IPTR PrinterEditor__MUIM_PrinterEditor_Sync(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)PrinterEditor__MUIM_PrinterEditor_Sync_inner(CLASS, self, (APTR)message);
}

IPTR PrinterEditor__MUIM_PrinterEditor_Refresh(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)PrinterEditor__MUIM_PrinterEditor_Refresh_inner(CLASS, self, (APTR)message);
}
