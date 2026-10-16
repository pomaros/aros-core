/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License, version 1.1. */
#define MUIMASTER_YES_INLINE_STDARG
#include <libraries/mui.h>
#include <zune/systemprefswindow.h>
#include <proto/alib.h>
#include <proto/muimaster.h>
#include <proto/intuition.h>
int main(void)
{
    Object *app, *window;
    app=(Object *)ApplicationObject,
        MUIA_Application_Title,(IPTR)"indicators",
        MUIA_Application_Base,(IPTR)"INDICATORS",
        SubWindow,(IPTR)(window=(Object *)SystemPrefsWindowObject,
            WindowContents,(IPTR)MUI_NewObject((ClassID)"IndicatorsEditor.mcc",TAG_DONE),
        End),
    End;
    if (!app) return 20;
    SET(window,MUIA_Window_Open,TRUE);
    DoMethod(app,MUIM_Application_Execute);
    SET(window,MUIA_Window_Open,FALSE);
    MUI_DisposeObject(app); return 0;
}
