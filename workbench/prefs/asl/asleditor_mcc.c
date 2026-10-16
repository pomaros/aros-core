/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    AslEditor built as AslEditor.mcc for the settings window: the program's
    source with its methods renamed, the module's methods around them, and
    one live instance, since the editor keeps its prefs in globals.
*/
#define ASLEDITOR_MCC
#define AslEditor__OM_NEW AslEditor__OM_NEW_inner
#define AslEditor__MUIM_PrefsEditor_ImportFH AslEditor__MUIM_PrefsEditor_ImportFH_inner
#define AslEditor__MUIM_PrefsEditor_ExportFH AslEditor__MUIM_PrefsEditor_ExportFH_inner
#define AslEditor__MUIM_PrefsEditor_SetDefaults AslEditor__MUIM_PrefsEditor_SetDefaults_inner
#include "asleditor.c"
#undef AslEditor__OM_NEW
#undef AslEditor__MUIM_PrefsEditor_ImportFH
#undef AslEditor__MUIM_PrefsEditor_ExportFH
#undef AslEditor__MUIM_PrefsEditor_SetDefaults

static ULONG live_instances;
static Object *live_object;      /* the instance counted, so a failed
                                    construction never uncounts it */

IPTR AslEditor__OM_NEW(Class *CLASS, Object *self, Msg message)
{
    Object *object;
    if (live_instances != 0)
        return 0;
    object = (Object *)AslEditor__OM_NEW_inner(CLASS, self, (APTR)message);
    if (object != NULL)
    {
        live_instances++;
        live_object = object;
    }
    return (IPTR)object;
}

IPTR AslEditor__OM_DISPOSE(Class *CLASS, Object *self, Msg message)
{
    if (self == live_object)
    {
        live_object = NULL;
        live_instances--;
    }
    return DoSuperMethodA(CLASS, self, message);
}

IPTR AslEditor__MUIM_PrefsEditor_ImportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)AslEditor__MUIM_PrefsEditor_ImportFH_inner(CLASS, self, (APTR)message);
}

IPTR AslEditor__MUIM_PrefsEditor_ExportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)AslEditor__MUIM_PrefsEditor_ExportFH_inner(CLASS, self, (APTR)message);
}

IPTR AslEditor__MUIM_PrefsEditor_SetDefaults(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)AslEditor__MUIM_PrefsEditor_SetDefaults_inner(CLASS, self, (APTR)message);
}
