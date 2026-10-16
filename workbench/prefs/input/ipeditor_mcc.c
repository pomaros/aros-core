/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    IPEditor built as InputEditor.mcc for the settings window: the program's
    source with its methods renamed, the module's methods around them, and
    one live instance, since the editor keeps its prefs in globals.
*/
#define INPUTEDITOR_MCC
#define IPEditor__OM_NEW IPEditor__OM_NEW_inner
#define IPEditor__MUIM_PrefsEditor_ImportFH IPEditor__MUIM_PrefsEditor_ImportFH_inner
#define IPEditor__MUIM_PrefsEditor_ExportFH IPEditor__MUIM_PrefsEditor_ExportFH_inner
#define IPEditor__MUIM_PrefsEditor_SetDefaults IPEditor__MUIM_PrefsEditor_SetDefaults_inner
#include "ipeditor.c"
#undef IPEditor__OM_NEW
#undef IPEditor__MUIM_PrefsEditor_ImportFH
#undef IPEditor__MUIM_PrefsEditor_ExportFH
#undef IPEditor__MUIM_PrefsEditor_SetDefaults

static ULONG live_instances;
static Object *live_object;      /* the instance counted, so a failed
                                    construction never uncounts it */

IPTR IPEditor__OM_NEW(Class *CLASS, Object *self, Msg message)
{
    Object *object;
    if (live_instances != 0)
        return 0;
    object = (Object *)IPEditor__OM_NEW_inner(CLASS, self, (APTR)message);
    if (object != NULL)
    {
        live_instances++;
        live_object = object;
    }
    return (IPTR)object;
}

IPTR IPEditor__OM_DISPOSE(Class *CLASS, Object *self, Msg message)
{
    if (self == live_object)
    {
        live_object = NULL;
        live_instances--;
    }
    return DoSuperMethodA(CLASS, self, message);
}

IPTR IPEditor__MUIM_PrefsEditor_ImportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)IPEditor__MUIM_PrefsEditor_ImportFH_inner(CLASS, self, (APTR)message);
}

IPTR IPEditor__MUIM_PrefsEditor_ExportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)IPEditor__MUIM_PrefsEditor_ExportFH_inner(CLASS, self, (APTR)message);
}

IPTR IPEditor__MUIM_PrefsEditor_SetDefaults(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)IPEditor__MUIM_PrefsEditor_SetDefaults_inner(CLASS, self, (APTR)message);
}
