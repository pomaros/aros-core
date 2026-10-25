/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License, version 1.1. */
#define MUIMASTER_YES_INLINE_STDARG
#include <libraries/mui.h>
#include <zune/prefseditor.h>
#include <proto/alib.h>
#include <proto/muimaster.h>
#include <proto/intuition.h>
#include "editor.h"
#include "status.h"
#include LC_LIBDEFS_FILE

IPTR SoftwareUpdateEditor__OM_NEW(Class *CLASS,Object *self,struct opSet *message)
{
    Object *installed,*previous,*last,*pending,*delivered,*refresh;
    self=(Object *)DoSuperNewTags(CLASS,self,NULL,
        MUIA_PrefsEditor_Name,(IPTR)"Software update",
        MUIA_PrefsEditor_CanUse,FALSE,
        MUIA_PrefsEditor_CanTest,FALSE,
        Child,(IPTR)ColGroup(2),
            Child,(IPTR)Label2("Installed system"),Child,(IPTR)(installed=(Object *)TextObject,MUIA_ObjectID,MAKE_ID('U','P','S','I'),MUIA_ShortHelp,(IPTR)"The aros-system package version the last system update installed",MUIA_Text_Contents,(IPTR)"Refresh to read the system volume",End),
            Child,(IPTR)Label2("Previous version"),Child,(IPTR)(previous=(Object *)TextObject,MUIA_ObjectID,MAKE_ID('U','P','S','O'),MUIA_ShortHelp,(IPTR)"The version the last system update replaced",MUIA_Text_Contents,(IPTR)"Not queried",End),
            Child,(IPTR)Label2("Last update"),Child,(IPTR)(last=(Object *)TextObject,MUIA_ObjectID,MAKE_ID('U','P','S','L'),MUIA_ShortHelp,(IPTR)"What the last system update did and how it ended, from its receipt",MUIA_Text_Contents,(IPTR)"Not queried",End),
            Child,(IPTR)Label2("Update operation"),Child,(IPTR)(pending=(Object *)TextObject,MUIA_ObjectID,MAKE_ID('U','P','S','P'),MUIA_Text_Contents,(IPTR)"Not queried",End),
            Child,(IPTR)Label2("Delivered update"),Child,(IPTR)(delivered=(Object *)TextObject,MUIA_ObjectID,MAKE_ID('U','P','S','D'),MUIA_Text_Contents,(IPTR)"Not queried",End),
        End,
        Child,(IPTR)(refresh=(Object *)SimpleButton("Refresh update status")),
        Child,(IPTR)TextObject,MUIA_Text_Contents,(IPTR)"Updates are delivered to WORK:PkgDepot and applied before installed-system startup.\nA system update keeps no previous version to return to, so there is no rollback yet,\nand system updates come from that depot, not from a channel.",End,
        TAG_MORE,(IPTR)message->ops_AttrList);
    if(self) {
        struct SoftwareUpdateEditor_DATA *data=INST_DATA(CLASS,self);
        data->installed=installed; data->previous=previous; data->last=last; data->pending=pending; data->delivered=delivered;
        SET(refresh,MUIA_ObjectID,MAKE_ID('U','P','S','R'));
        DoMethod(refresh,MUIM_Notify,MUIA_Pressed,FALSE,self,1,MUIM_SoftwareUpdateEditor_Refresh);
    }
    return (IPTR)self;
}
IPTR SoftwareUpdateEditor__MUIM_SoftwareUpdateEditor_Refresh(Class *CLASS,Object *self,Msg message)
{
    struct SoftwareUpdateEditor_DATA *data=INST_DATA(CLASS,self);
    struct UpdateStatus status;
    (void)message;
    SoftwareUpdateReadStatus(&status);
    SET(data->installed,MUIA_Text_Contents,(IPTR)status.installed);
    SET(data->previous,MUIA_Text_Contents,(IPTR)status.previous);
    SET(data->last,MUIA_Text_Contents,(IPTR)status.last);
    SET(data->pending,MUIA_Text_Contents,(IPTR)status.pending);
    SET(data->delivered,MUIA_Text_Contents,(IPTR)status.delivered);
    return TRUE;
}
IPTR SoftwareUpdateEditor__MUIM_PrefsEditor_ImportFromDirectory(Class *CLASS,Object *self,Msg message)
{
    (void)CLASS; (void)self; (void)message; return TRUE;
}
IPTR SoftwareUpdateEditor__MUIM_PrefsEditor_ExportToDirectory(Class *CLASS,Object *self,Msg message)
{
    (void)CLASS; (void)self; (void)message; return TRUE;
}

IPTR SoftwareUpdateEditor__OM_GET(Class *CLASS, Object *self, struct opGet *message)
{
    if (message->opg_AttrID == MUIA_PrefsEditor_CanSave)
    {
        *message->opg_Storage = FALSE;
        return TRUE;
    }
    return DoSuperMethodA(CLASS, self, (Msg)message);
}
IPTR SoftwareUpdateEditor__MUIM_Setup(Class *CLASS, Object *self, Msg message)
{
    if (!DoSuperMethodA(CLASS, self, message)) return FALSE;
    DoMethod(self, MUIM_SoftwareUpdateEditor_Refresh);
    return TRUE;
}
