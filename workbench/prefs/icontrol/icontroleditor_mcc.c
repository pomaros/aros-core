/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    IControlEditor built as IControlEditor.mcc for the settings window: the program's
    source with its methods renamed, the module's methods around them, and
    one live instance, since the editor keeps its prefs in globals.
*/
#define ICONTROLEDITOR_MCC
#define IControlEditor__OM_NEW IControlEditor__OM_NEW_inner
#define IControlEditor__MUIM_PrefsEditor_ImportFH IControlEditor__MUIM_PrefsEditor_ImportFH_inner
#define IControlEditor__MUIM_PrefsEditor_ExportFH IControlEditor__MUIM_PrefsEditor_ExportFH_inner
#define IControlEditor__MUIM_PrefsEditor_SetDefaults IControlEditor__MUIM_PrefsEditor_SetDefaults_inner
#include "icontroleditor.c"
#undef IControlEditor__OM_NEW
#undef IControlEditor__MUIM_PrefsEditor_ImportFH
#undef IControlEditor__MUIM_PrefsEditor_ExportFH
#undef IControlEditor__MUIM_PrefsEditor_SetDefaults

static ULONG live_instances;
static Object *live_object;      /* the instance counted, so a failed
                                    construction never uncounts it */

IPTR IControlEditor__OM_NEW(Class *CLASS, Object *self, Msg message)
{
    Object *object;
    if (live_instances != 0)
        return 0;
    object = (Object *)IControlEditor__OM_NEW_inner(CLASS, self, (APTR)message);
    if (object != NULL)
    {
        live_instances++;
        live_object = object;
    }
    return (IPTR)object;
}

IPTR IControlEditor__OM_DISPOSE(Class *CLASS, Object *self, Msg message)
{
    if (self == live_object)
    {
        live_object = NULL;
        live_instances--;
    }
    return DoSuperMethodA(CLASS, self, message);
}

IPTR IControlEditor__MUIM_PrefsEditor_ImportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)IControlEditor__MUIM_PrefsEditor_ImportFH_inner(CLASS, self, (APTR)message);
}

IPTR IControlEditor__MUIM_PrefsEditor_ExportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)IControlEditor__MUIM_PrefsEditor_ExportFH_inner(CLASS, self, (APTR)message);
}

IPTR IControlEditor__MUIM_PrefsEditor_SetDefaults(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)IControlEditor__MUIM_PrefsEditor_SetDefaults_inner(CLASS, self, (APTR)message);
}
