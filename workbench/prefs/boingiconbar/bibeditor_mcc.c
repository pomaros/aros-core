/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    BibEditor built as BoingIconBarEditor.mcc for the settings window: the program's
    source with its methods renamed, the module's methods around them, and
    one live instance, since the editor keeps its prefs in globals.
*/
#define BOINGICONBAREDITOR_MCC
#define BibEditor__OM_NEW BibEditor__OM_NEW_inner
#define BibEditor__MUIM_PrefsEditor_ImportFH BibEditor__MUIM_PrefsEditor_ImportFH_inner
#define BibEditor__MUIM_PrefsEditor_ExportFH BibEditor__MUIM_PrefsEditor_ExportFH_inner
#define BibEditor__MUIM_PrefsEditor_SetDefaults BibEditor__MUIM_PrefsEditor_SetDefaults_inner
#include "bibeditor.c"
#undef BibEditor__OM_NEW
#undef BibEditor__MUIM_PrefsEditor_ImportFH
#undef BibEditor__MUIM_PrefsEditor_ExportFH
#undef BibEditor__MUIM_PrefsEditor_SetDefaults

static ULONG live_instances;
static Object *live_object;      /* the instance counted, so a failed
                                    construction never uncounts it */

IPTR BibEditor__OM_NEW(Class *CLASS, Object *self, Msg message)
{
    Object *object;
    if (live_instances != 0)
        return 0;
    object = (Object *)BibEditor__OM_NEW_inner(CLASS, self, (APTR)message);
    if (object != NULL)
    {
        live_instances++;
        live_object = object;
    }
    return (IPTR)object;
}

IPTR BibEditor__OM_DISPOSE(Class *CLASS, Object *self, Msg message)
{
    if (self == live_object)
    {
        live_object = NULL;
        live_instances--;
    }
    return DoSuperMethodA(CLASS, self, message);
}

IPTR BibEditor__MUIM_PrefsEditor_ImportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)BibEditor__MUIM_PrefsEditor_ImportFH_inner(CLASS, self, (APTR)message);
}

IPTR BibEditor__MUIM_PrefsEditor_ExportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)BibEditor__MUIM_PrefsEditor_ExportFH_inner(CLASS, self, (APTR)message);
}

IPTR BibEditor__MUIM_PrefsEditor_SetDefaults(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)BibEditor__MUIM_PrefsEditor_SetDefaults_inner(CLASS, self, (APTR)message);
}
