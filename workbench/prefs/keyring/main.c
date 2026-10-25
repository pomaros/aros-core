/* Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1 */
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
        MUIA_Application_Title,(IPTR)"keyring",
        MUIA_Application_Base,(IPTR)"KEYRING",
        SubWindow,(IPTR)(window=(Object *)SystemPrefsWindowObject,
            WindowContents,(IPTR)MUI_NewObject((ClassID)"KeyringEditor.mcc",TAG_DONE),
        End),
    End;
    if (!app) return 20;
    SET(window,MUIA_Window_Open,TRUE);
    DoMethod(app,MUIM_Application_Execute);
    SET(window,MUIA_Window_Open,FALSE);
    MUI_DisposeObject(app); return 0;
}
