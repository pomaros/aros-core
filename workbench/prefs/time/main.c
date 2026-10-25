/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#define MUIMASTER_YES_INLINE_STDARG
#include <libraries/mui.h>
#include <zune/systemprefswindow.h>
#include <proto/alib.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

int main(void)
{
    Object *app, *window;
    app = ApplicationObject,
        MUIA_Application_Title, (IPTR)"Time",
        MUIA_Application_Base, (IPTR)"TIMEPREF",
        SubWindow, (IPTR)(window = SystemPrefsWindowObject,
            MUIA_Window_ID, MAKE_ID('T','W','I','N'),
            WindowContents, (IPTR)MUI_NewObject((ClassID)"TimeEditor.mcc", TAG_DONE),
        End),
    End;
    if (!app) return 20;
    SET(window, MUIA_Window_Open, TRUE);
    DoMethod(app, MUIM_Application_Execute);
    SET(window, MUIA_Window_Open, FALSE);
    MUI_DisposeObject(app);
    return 0;
}
