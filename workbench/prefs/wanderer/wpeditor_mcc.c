/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    WPEditor built as WandererEditor.mcc for the settings window: the program's
    source with its methods renamed, the module's methods around them, and
    one live instance, since the editor keeps its prefs in globals.
*/
#define WANDEREREDITOR_MCC
#define WPEditor__OM_NEW WPEditor__OM_NEW_inner
#define WPEditor__OM_GET WPEditor__OM_GET_inner
#define WPEditor__MUIM_Show WPEditor__MUIM_Show_inner
#define WPEditor__MUIM_PrefsEditor_ImportFH WPEditor__MUIM_PrefsEditor_ImportFH_inner
#define WPEditor__MUIM_PrefsEditor_ExportFH WPEditor__MUIM_PrefsEditor_ExportFH_inner
#define WPEditor__MUIM_PrefsEditor_Use WPEditor__MUIM_PrefsEditor_Use_inner
#define WPEditor__MUIM_PrefsEditor_Save WPEditor__MUIM_PrefsEditor_Save_inner
#include "wpeditor.c"
#undef WPEditor__OM_NEW
#undef WPEditor__OM_GET
#undef WPEditor__MUIM_Show
#undef WPEditor__MUIM_PrefsEditor_ImportFH
#undef WPEditor__MUIM_PrefsEditor_ExportFH
#undef WPEditor__MUIM_PrefsEditor_Use
#undef WPEditor__MUIM_PrefsEditor_Save

static ULONG live_instances;
static Object *live_object;      /* the instance counted, so a failed
                                    construction never uncounts it */

IPTR WPEditor__OM_NEW(Class *CLASS, Object *self, Msg message)
{
    Object *object;
    if (live_instances != 0)
        return 0;
    object = (Object *)WPEditor__OM_NEW_inner(CLASS, self, (APTR)message);
    if (object != NULL)
    {
        live_instances++;
        live_object = object;
    }
    return (IPTR)object;
}

IPTR WPEditor__OM_DISPOSE(Class *CLASS, Object *self, Msg message)
{
    if (self == live_object)
    {
        live_object = NULL;
        live_instances--;
    }
    return DoSuperMethodA(CLASS, self, message);
}

IPTR WPEditor__OM_GET(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)WPEditor__OM_GET_inner(CLASS, self, (APTR)message);
}

IPTR WPEditor__MUIM_Show(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)WPEditor__MUIM_Show_inner(CLASS, self, (APTR)message);
}

IPTR WPEditor__MUIM_PrefsEditor_ImportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)WPEditor__MUIM_PrefsEditor_ImportFH_inner(CLASS, self, (APTR)message);
}

IPTR WPEditor__MUIM_PrefsEditor_ExportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)WPEditor__MUIM_PrefsEditor_ExportFH_inner(CLASS, self, (APTR)message);
}

IPTR WPEditor__MUIM_PrefsEditor_Use(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)WPEditor__MUIM_PrefsEditor_Use_inner(CLASS, self, (APTR)message);
}

IPTR WPEditor__MUIM_PrefsEditor_Save(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)WPEditor__MUIM_PrefsEditor_Save_inner(CLASS, self, (APTR)message);
}
