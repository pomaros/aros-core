/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    FWEditor built as FirewallEditor.mcc for the settings window: the program's
    source with its methods renamed, the module's methods around them, and
    one live instance, since the editor keeps its prefs in globals.
*/
#define FIREWALLEDITOR_MCC
#define FWEditor__OM_NEW FWEditor__OM_NEW_inner
#define FWEditor__MUIM_Setup FWEditor__MUIM_Setup_inner
#define FWEditor__MUIM_Cleanup FWEditor__MUIM_Cleanup_inner
#define FWEditor__MUIM_PrefsEditor_ImportFH FWEditor__MUIM_PrefsEditor_ImportFH_inner
#define FWEditor__MUIM_PrefsEditor_ExportFH FWEditor__MUIM_PrefsEditor_ExportFH_inner
#define FWEditor__MUIM_PrefsEditor_Save FWEditor__MUIM_PrefsEditor_Save_inner
#define FWEditor__MUIM_PrefsEditor_Use FWEditor__MUIM_PrefsEditor_Use_inner
#define FWEditor__MUIM_FWEditor_AddFilter FWEditor__MUIM_FWEditor_AddFilter_inner
#define FWEditor__MUIM_FWEditor_EditFilter FWEditor__MUIM_FWEditor_EditFilter_inner
#define FWEditor__MUIM_FWEditor_UseFilterEdit FWEditor__MUIM_FWEditor_UseFilterEdit_inner
#define FWEditor__MUIM_FWEditor_CancelFilterEdit FWEditor__MUIM_FWEditor_CancelFilterEdit_inner
#define FWEditor__MUIM_FWEditor_RemFilter FWEditor__MUIM_FWEditor_RemFilter_inner
#define FWEditor__MUIM_FWEditor_UpFilter FWEditor__MUIM_FWEditor_UpFilter_inner
#define FWEditor__MUIM_FWEditor_DownFilter FWEditor__MUIM_FWEditor_DownFilter_inner
#define FWEditor__MUIM_FWEditor_AddNAT FWEditor__MUIM_FWEditor_AddNAT_inner
#define FWEditor__MUIM_FWEditor_EditNAT FWEditor__MUIM_FWEditor_EditNAT_inner
#define FWEditor__MUIM_FWEditor_UseNATEdit FWEditor__MUIM_FWEditor_UseNATEdit_inner
#define FWEditor__MUIM_FWEditor_CancelNATEdit FWEditor__MUIM_FWEditor_CancelNATEdit_inner
#define FWEditor__MUIM_FWEditor_RemNAT FWEditor__MUIM_FWEditor_RemNAT_inner
#include "fweditor.c"
#undef FWEditor__OM_NEW
#undef FWEditor__MUIM_Setup
#undef FWEditor__MUIM_Cleanup
#undef FWEditor__MUIM_PrefsEditor_ImportFH
#undef FWEditor__MUIM_PrefsEditor_ExportFH
#undef FWEditor__MUIM_PrefsEditor_Save
#undef FWEditor__MUIM_PrefsEditor_Use
#undef FWEditor__MUIM_FWEditor_AddFilter
#undef FWEditor__MUIM_FWEditor_EditFilter
#undef FWEditor__MUIM_FWEditor_UseFilterEdit
#undef FWEditor__MUIM_FWEditor_CancelFilterEdit
#undef FWEditor__MUIM_FWEditor_RemFilter
#undef FWEditor__MUIM_FWEditor_UpFilter
#undef FWEditor__MUIM_FWEditor_DownFilter
#undef FWEditor__MUIM_FWEditor_AddNAT
#undef FWEditor__MUIM_FWEditor_EditNAT
#undef FWEditor__MUIM_FWEditor_UseNATEdit
#undef FWEditor__MUIM_FWEditor_CancelNATEdit
#undef FWEditor__MUIM_FWEditor_RemNAT

static ULONG live_instances;
static Object *live_object;      /* the instance counted, so a failed
                                    construction never uncounts it */

IPTR FWEditor__OM_NEW(Class *CLASS, Object *self, Msg message)
{
    Object *object;
    if (live_instances != 0)
        return 0;
    object = (Object *)FWEditor__OM_NEW_inner(CLASS, self, (APTR)message);
    if (object != NULL)
    {
        live_instances++;
        live_object = object;
    }
    return (IPTR)object;
}

IPTR FWEditor__OM_DISPOSE(Class *CLASS, Object *self, Msg message)
{
    if (self == live_object)
    {
        live_object = NULL;
        live_instances--;
    }
    return DoSuperMethodA(CLASS, self, message);
}

IPTR FWEditor__MUIM_Setup(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_Setup_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_Cleanup(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_Cleanup_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_PrefsEditor_ImportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_PrefsEditor_ImportFH_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_PrefsEditor_ExportFH(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_PrefsEditor_ExportFH_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_PrefsEditor_Save(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_PrefsEditor_Save_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_PrefsEditor_Use(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_PrefsEditor_Use_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_FWEditor_AddFilter(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_FWEditor_AddFilter_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_FWEditor_EditFilter(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_FWEditor_EditFilter_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_FWEditor_UseFilterEdit(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_FWEditor_UseFilterEdit_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_FWEditor_CancelFilterEdit(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_FWEditor_CancelFilterEdit_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_FWEditor_RemFilter(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_FWEditor_RemFilter_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_FWEditor_UpFilter(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_FWEditor_UpFilter_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_FWEditor_DownFilter(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_FWEditor_DownFilter_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_FWEditor_AddNAT(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_FWEditor_AddNAT_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_FWEditor_EditNAT(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_FWEditor_EditNAT_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_FWEditor_UseNATEdit(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_FWEditor_UseNATEdit_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_FWEditor_CancelNATEdit(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_FWEditor_CancelNATEdit_inner(CLASS, self, (APTR)message);
}

IPTR FWEditor__MUIM_FWEditor_RemNAT(Class *CLASS, Object *self, Msg message)
{
    return (IPTR)FWEditor__MUIM_FWEditor_RemNAT_inner(CLASS, self, (APTR)message);
}
