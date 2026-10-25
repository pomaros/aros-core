/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    SMEditor built as ScreenModeEditor.mcc for the settings window: the program's
    source with its methods renamed, the module's methods around them, and
    one live instance, since the editor keeps its prefs in globals.
*/
#define SCREENMODEEDITOR_MCC
#define SMEditor__OM_NEW SMEditor__OM_NEW_inner
#define SMEditor__MUIM_PrefsEditor_ImportFH SMEditor__MUIM_PrefsEditor_ImportFH_inner
#define SMEditor__MUIM_PrefsEditor_Export SMEditor__MUIM_PrefsEditor_Export_inner
#define SMEditor__MUIM_PrefsEditor_ExportFH SMEditor__MUIM_PrefsEditor_ExportFH_inner
#define SMEditor__MUIM_PrefsEditor_Test SMEditor__MUIM_PrefsEditor_Test_inner
#define SMEditor__MUIM_PrefsEditor_SetDefaults SMEditor__MUIM_PrefsEditor_SetDefaults_inner
#define SMEditor__MUIM_PrefsEditor_ImportFromDirectory SMEditor__MUIM_PrefsEditor_ImportFromDirectory_inner
#define SMEditor__MUIM_PrefsEditor_ExportToDirectory SMEditor__MUIM_PrefsEditor_ExportToDirectory_inner
#define SMEditor__MUIM_Setup SMEditor__MUIM_Setup_inner
#define SMEditor__MUIM_PrefsEditor_Cancel SMEditor__MUIM_PrefsEditor_Cancel_inner
#define SMEditor__MUIM_PrefsEditor_Revert SMEditor__MUIM_PrefsEditor_Revert_inner
#include "smeditor.c"
#undef SMEditor__MUIM_PrefsEditor_Cancel
#undef SMEditor__MUIM_PrefsEditor_Revert
#undef SMEditor__MUIM_Setup
#undef SMEditor__MUIM_PrefsEditor_ImportFromDirectory
#undef SMEditor__MUIM_PrefsEditor_ExportToDirectory
#undef SMEditor__OM_NEW
#undef SMEditor__MUIM_PrefsEditor_ImportFH
#undef SMEditor__MUIM_PrefsEditor_Export
#undef SMEditor__MUIM_PrefsEditor_ExportFH
#undef SMEditor__MUIM_PrefsEditor_Test
#undef SMEditor__MUIM_PrefsEditor_SetDefaults

static ULONG live_instances;
static Object *live_object;      /* the instance counted, so a failed
                                    construction never uncounts it */

IPTR SMEditor__OM_NEW(Class *CLASS, Object *self, Msg message)
{
    Object *object;
    if (live_instances != 0)
        return 0;
    object = (Object *)SMEditor__OM_NEW_inner(CLASS, self, (APTR)message);
    if (object != NULL)
    {
        live_instances++;
        live_object = object;
    }
    return (IPTR)object;
}

IPTR SMEditor__OM_DISPOSE(Class *CLASS, Object *self, Msg message)
{
    if (self == live_object)
    {
        live_object = NULL;
        live_instances--;
    }
    return DoSuperMethodA(CLASS, self, message);
}

IPTR SMEditor__MUIM_PrefsEditor_ImportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)SMEditor__MUIM_PrefsEditor_ImportFH_inner(CLASS, self, (APTR)message);
}

IPTR SMEditor__MUIM_PrefsEditor_Export(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)SMEditor__MUIM_PrefsEditor_Export_inner(CLASS, self, (APTR)message);
}

IPTR SMEditor__MUIM_PrefsEditor_ExportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)SMEditor__MUIM_PrefsEditor_ExportFH_inner(CLASS, self, (APTR)message);
}

IPTR SMEditor__MUIM_PrefsEditor_Test(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)SMEditor__MUIM_PrefsEditor_Test_inner(CLASS, self, (APTR)message);
}

IPTR SMEditor__MUIM_PrefsEditor_SetDefaults(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)SMEditor__MUIM_PrefsEditor_SetDefaults_inner(CLASS, self, (APTR)message);
}

IPTR SMEditor__MUIM_PrefsEditor_ImportFromDirectory(Class *CLASS, Object *self, Msg message)
{
    return SMEditor__MUIM_PrefsEditor_ImportFromDirectory_inner(CLASS, self, (APTR)message);
}

IPTR SMEditor__MUIM_PrefsEditor_ExportToDirectory(Class *CLASS, Object *self, Msg message)
{
    return SMEditor__MUIM_PrefsEditor_ExportToDirectory_inner(CLASS, self, (APTR)message);
}

IPTR SMEditor__MUIM_Setup(Class *CLASS, Object *self, Msg message)
{
    return SMEditor__MUIM_Setup_inner(CLASS, self, (APTR)message);
}

IPTR SMEditor__MUIM_PrefsEditor_Cancel(Class *CLASS, Object *self, Msg message)
{
    return SMEditor__MUIM_PrefsEditor_Cancel_inner(CLASS, self, message);
}

IPTR SMEditor__MUIM_PrefsEditor_Revert(Class *CLASS, Object *self, Msg message)
{
    return SMEditor__MUIM_PrefsEditor_Revert_inner(CLASS, self, message);
}
