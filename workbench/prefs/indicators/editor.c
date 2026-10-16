/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License, version 1.1. */
#define MUIMASTER_YES_INLINE_STDARG
#include <libraries/mui.h>
#include <zune/prefseditor.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/muimaster.h>
#include <proto/intuition.h>
#include "editor.h"
#include "../../system/Indicators/prefs.h"
#include LC_LIBDEFS_FILE

IPTR IndicatorsEditor__OM_NEW(Class *CLASS,Object *self,struct opSet *message)
{
    Object *enabled[4];
    unsigned i;
    self=(Object *)DoSuperNewTags(CLASS,self,NULL,
        MUIA_PrefsEditor_Name,(IPTR)"Indicators",
        MUIA_PrefsEditor_Path,(IPTR)INDICATORS_PREFS_PATH,
        Child,(IPTR)TextObject,MUIA_Text_Contents,(IPTR)"Enable live desktop indicators",End,
        Child,(IPTR)ColGroup(2),
            Child,(IPTR)Label2("Battery"),Child,(IPTR)(enabled[0]=(Object *)MUI_MakeObject(MUIO_Checkmark, (IPTR)NULL)),
            Child,(IPTR)Label2("Network"),Child,(IPTR)(enabled[1]=(Object *)MUI_MakeObject(MUIO_Checkmark, (IPTR)NULL)),
            Child,(IPTR)Label2("Bluetooth"),Child,(IPTR)(enabled[2]=(Object *)MUI_MakeObject(MUIO_Checkmark, (IPTR)NULL)),
            Child,(IPTR)Label2("Sound"),Child,(IPTR)(enabled[3]=(Object *)MUI_MakeObject(MUIO_Checkmark, (IPTR)NULL)),
        End,
        Child,(IPTR)TextObject,MUIA_Text_Contents,(IPTR)"Use applies to running indicators. Save also keeps this choice at boot.\nDisabled indicators keep their static icon; their panel and hotkey are inactive.\nTitle-bar presentation is unavailable.",End,
        TAG_MORE,(IPTR)message->ops_AttrList);
    if (self) {
        struct IndicatorsEditor_DATA *data=INST_DATA(CLASS,self);
        for(i=0;i<4;i++) {
            data->enabled[i]=enabled[i];
            SET(enabled[i],MUIA_ObjectID,MAKE_ID('I','N','D','B') + i);
            DoMethod(enabled[i],MUIM_Notify,MUIA_Selected,MUIV_EveryTime,self,3,MUIM_Set,MUIA_PrefsEditor_Changed,TRUE);
        }
    }
    return (IPTR)self;
}
IPTR IndicatorsEditor__MUIM_PrefsEditor_ImportFH(Class *CLASS,Object *self,struct MUIP_PrefsEditor_ImportFH *message)
{
    struct IndicatorsEditor_DATA *data=INST_DATA(CLASS,self);
    unsigned char bytes[INDICATORS_PREFS_SIZE+1]; unsigned mask,i;
    LONG n=Read(message->fh,bytes,sizeof(bytes));
    if (n<0 || !IndicatorsPrefsDecode(bytes,(size_t)n,&mask)) return FALSE;
    for(i=0;i<4;i++) NNSET(data->enabled[i],MUIA_Selected,!!(mask&(1U<<i)));
    return TRUE;
}
IPTR IndicatorsEditor__MUIM_PrefsEditor_ExportFH(Class *CLASS,Object *self,struct MUIP_PrefsEditor_ExportFH *message)
{
    struct IndicatorsEditor_DATA *data=INST_DATA(CLASS,self);
    unsigned char bytes[INDICATORS_PREFS_SIZE]; unsigned mask=0,i;
    for(i=0;i<4;i++) if(XGET(data->enabled[i],MUIA_Selected)) mask|=1U<<i;
    IndicatorsPrefsEncode(bytes,mask);
    return Write(message->fh,bytes,sizeof(bytes))==(LONG)sizeof(bytes);
}
IPTR IndicatorsEditor__MUIM_PrefsEditor_SetDefaults(Class *CLASS,Object *self,Msg message)
{
    struct IndicatorsEditor_DATA *data=INST_DATA(CLASS,self); unsigned i;
    (void)message;
    for(i=0;i<4;i++) NNSET(data->enabled[i],MUIA_Selected,TRUE);
    return TRUE;
}
