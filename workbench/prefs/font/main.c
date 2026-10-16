/*
    Copyright (C) 2003-2022, The AROS Development Team. All rights reserved.
*/

#define MUIMASTER_YES_INLINE_STDARG

#include <proto/alib.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/exec.h>

#include <libraries/mui.h>
#include <zune/systemprefswindow.h>

#include "locale.h"
#include "args.h"
#include "fpeditor.h"
#include "prefs.h"

/* settings.library 1 when present: the editor then keeps ENV: and ENVARC:
 * through it (fontsettings.c); without it the classic IFF path is used. */
struct Library *SettingsBase;

#define VERSION "$VER: Fonts 1.1 ("ADATE") \xa9 AROS Dev Team"

int main(int argc, char **argv)
{
    Object *application, *window;

    Locale_Initialize();
    SettingsBase = OpenLibrary("settings.library", 1);

    if (ReadArguments(argc, argv))
    {
        if (ARG(USE) || ARG(SAVE))
        {
            Prefs_HandleArgs((STRPTR)ARG(FROM), ARG(USE), ARG(SAVE));
        }
        else
        {
            struct Screen *pScreen = NULL;

            if (ARG(PUBSCREEN))
                pScreen = LockPubScreen((CONST_STRPTR)ARG(PUBSCREEN));

            application = (Object *)ApplicationObject,
                MUIA_Application_Title,  __(MSG_NAME),
                MUIA_Application_Version, (IPTR) VERSION,
                MUIA_Application_Description,  __(MSG_NAME),
                MUIA_Application_SingleTask, TRUE,
                MUIA_Application_Base, (IPTR) "FONTPREF",
                SubWindow, (IPTR) (window = (Object *)SystemPrefsWindowObject,
                    MUIA_Window_Screen, (IPTR)pScreen,
                    MUIA_Window_ID, MAKE_ID('F','W','I','N'),
                    WindowContents, (IPTR) FontEditorObject,
                    End,
                End),
            End;

            if (application != NULL)
            {
                SET(window, MUIA_Window_Open, TRUE);
                DoMethod(application, MUIM_Application_Execute);
                SET(window, MUIA_Window_Open, FALSE);

                MUI_DisposeObject(application);
            }
            if (pScreen)
                UnlockPubScreen(NULL, pScreen);
        }
        FreeArguments();
    }

    if (SettingsBase != NULL)
        CloseLibrary(SettingsBase);
    Locale_Deinitialize();

    return 0;
}
