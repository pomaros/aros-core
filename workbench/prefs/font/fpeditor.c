/*
    Copyright (C) 2003-2011, The AROS Development Team. All rights reserved.
*/

#define MUIMASTER_YES_INLINE_STDARG

#include <libraries/asl.h>
#include <libraries/mui.h>
#include <zune/customclasses.h>
#include <zune/prefseditor.h>

#include <proto/alib.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <aros/symbolsets.h>
#include LC_LIBDEFS_FILE

#include <string.h>
#include <stdio.h>

#include "misc.h"
#include "locale.h"
#include "fpeditor.h"
#include "prefs.h"
#include "fontsettings.h"

#include <libraries/settings.h>
#include <proto/settings.h>

#include "fpeditor_private.h"

/*** Macros *****************************************************************/
#define SETUP_INST_DATA struct FPEditor_DATA *data = INST_DATA(CLASS, self)
#define FP(i) (&(data->fped_FontPrefs[(i)]))

/*** Utility Functions ******************************************************/
static VOID FontPrefs2FontString
(
    STRPTR buffer, ULONG buffersize, struct FontPrefs *fp
)
{
    snprintf
    (
        buffer, buffersize, "%.*s/%d",
        (int)strlen(fp->fp_TextAttr.ta_Name) - 5 /* strlen(".font") */,
        fp->fp_TextAttr.ta_Name, fp->fp_TextAttr.ta_YSize
    );
}

static BOOL FontString2FontPrefs(struct FontPrefs *fp, CONST_STRPTR buffer)
{
    STRPTR separator    = PathPart((STRPTR) buffer);
    ULONG  nameLength   = separator - buffer;
    ULONG  suffixLength = 5; /* strlen(".font") */
    ULONG  size;

    if (nameLength + suffixLength >= FONTNAMESIZE)
    {
        /* Not enough space for the font name */
        return FALSE;
    }

    snprintf
    (
        fp->fp_Name, nameLength + suffixLength + 1, "%.*s.font",
        (int) nameLength, buffer
    );
    fp->fp_TextAttr.ta_Name = fp->fp_Name;

    StrToLong(FilePart((STRPTR) buffer), &size);
    fp->fp_TextAttr.ta_YSize = size;

    return TRUE;
}

static BOOL Gadgets2FontPrefs
(
    struct FPEditor_DATA *data
)
{
    STRPTR str = NULL;

    // FIXME: error checking
    GET(data->fped_IconsString, MUIA_String_Contents, &str);
    FontString2FontPrefs(FP(FP_WBFONT), str);
    FP(FP_WBFONT)->fp_Type = FP_WBFONT;

    GET(data->fped_SystemString, MUIA_String_Contents, &str);
    FontString2FontPrefs(FP(FP_SYSFONT), str);
    FP(FP_SYSFONT)->fp_Type = FP_SYSFONT;

    GET(data->fped_ScreenString, MUIA_String_Contents, &str);
    FontString2FontPrefs(FP(FP_SCREENFONT), str);
    FP(FP_SCREENFONT)->fp_Type = FP_SCREENFONT;

    return TRUE;
}

static BOOL FontPrefs2Gadgets
(
    struct FPEditor_DATA *data
)
{
    TEXT buffer[FONTNAMESIZE + 8];

    // FIXME: error checking
    FontPrefs2FontString(buffer, FONTNAMESIZE + 8, FP(FP_WBFONT));
    NNSET(data->fped_IconsString, MUIA_String_Contents, (IPTR) buffer);

    FontPrefs2FontString(buffer, FONTNAMESIZE + 8, FP(FP_SYSFONT));
    NNSET(data->fped_SystemString, MUIA_String_Contents, (IPTR) buffer);

    FontPrefs2FontString(buffer, FONTNAMESIZE + 8, FP(FP_SCREENFONT));
    NNSET(data->fped_ScreenString, MUIA_String_Contents, (IPTR) buffer);

    return TRUE;
}

/*** Methods ****************************************************************/
IPTR FPEditor__OM_NEW(Class *CLASS, Object *self, struct opSet *message)
{
    Object *iconsString, *screenString, *systemString;

    self = (Object *) DoSuperNewTags
    (
        CLASS, self, NULL,

        MUIA_PrefsEditor_Name,        __(MSG_NAME),
        MUIA_PrefsEditor_Path, (IPTR) "SYS/font.prefs",
        MUIA_PrefsEditor_IconTool, (IPTR) "SYS:Prefs/Font",

        Child, (IPTR) ColGroup(2),
            Child, (IPTR) Label2(_(MSG_ICONS)),
            Child, (IPTR) PopaslObject,
                MUIA_Popasl_Type,              ASL_FontRequest,
                ASLFO_MaxHeight,               100,
                MUIA_Popstring_String,  (IPTR) (iconsString = (Object *)StringObject,
                    TextFrame,
                    MUIA_ObjectID, MAKE_ID('F','N','I','C'),
                    MUIA_Background, MUII_TextBack,
                End),
                MUIA_Popstring_Button,  (IPTR) PopButton(MUII_PopUp),
            End,
            Child, (IPTR) Label2(_(MSG_SCREEN)),
            Child, (IPTR) PopaslObject,
                MUIA_Popasl_Type,              ASL_FontRequest,
                ASLFO_MaxHeight,               100,
                MUIA_Popstring_String,  (IPTR) (screenString = (Object *)StringObject,
                    TextFrame,
                    MUIA_ObjectID, MAKE_ID('F','N','S','C'),
                    MUIA_Background, MUII_TextBack,
                End),
                MUIA_Popstring_Button,  (IPTR) PopButton(MUII_PopUp),
            End,
            Child, (IPTR) Label2(_(MSG_SYSTEM)),
            Child, (IPTR) PopaslObject,
                MUIA_Popasl_Type,              ASL_FontRequest,
                ASLFO_FixedWidthOnly,          TRUE,
                ASLFO_MaxHeight,               100,
                MUIA_Popstring_String,  (IPTR) (systemString = (Object *)StringObject,
                    TextFrame,
                    MUIA_ObjectID, MAKE_ID('F','N','S','Y'),
                    MUIA_Background, MUII_TextBack,
                End),
                MUIA_Popstring_Button,  (IPTR) PopButton(MUII_PopUp),
            End,
        End,

        TAG_DONE
    );

    if (self != NULL)
    {
        SETUP_INST_DATA;
        data->fped_IconsString  = iconsString;
        data->fped_ScreenString = screenString;
        data->fped_SystemString = systemString;

        /*-- Setup notifications -------------------------------------------*/
        DoMethod
        (
            iconsString, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime,
            (IPTR) self, 3, MUIM_Set, MUIA_PrefsEditor_Changed, TRUE
        );
        DoMethod
        (
            screenString, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime,
            (IPTR) self, 3, MUIM_Set, MUIA_PrefsEditor_Changed, TRUE
        );
        DoMethod
        (
            systemString, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime,
            (IPTR) self, 3, MUIM_Set, MUIA_PrefsEditor_Changed, TRUE
        );
    }

    return (IPTR)self;
}

IPTR FPEditor__MUIM_PrefsEditor_ImportFH
(
    Class *CLASS, Object *self,
    struct MUIP_PrefsEditor_ImportFH *message
)
{
    SETUP_INST_DATA;
    BOOL success = TRUE;

    success = Prefs_ImportFH(message->fh, data->fped_FontPrefs);
    if (success) FontPrefs2Gadgets(data);

    return success;
}

IPTR FPEditor__MUIM_PrefsEditor_ExportFH
(
    Class *CLASS, Object *self,
    struct MUIP_PrefsEditor_ExportFH *message
)
{
    SETUP_INST_DATA;
    BOOL success = TRUE;

    Gadgets2FontPrefs(data);
    success = Prefs_ExportFH(message->fh, data->fped_FontPrefs);

    return success;
}

/* ENV: is the applied layer, ENVARC: the machine's. A save goes to the
 * machine layer until the per-user layer is composed at boot ([LAY7]). */
static ULONG DirectoryLayer(CONST_STRPTR directory)
{
    static const char *const names[] = { "ENV:", "ENVARC:" };
    static const ULONG layers[] = { SETTING_LAYER_APPLIED, SETTING_LAYER_MACHINE };
    ULONG n, i;

    for (n = 0; n < 2; n++)
    {
        for (i = 0; names[n][i] != '\0'; i++)
        {
            char c = directory[i];
            if (c >= 'a' && c <= 'z')
                c -= 'a' - 'A';
            if (c != names[n][i])
                break;
        }
        if (names[n][i] == '\0' && directory[i] == '\0')
            return layers[n];
    }
    return (ULONG)~0;
}

IPTR FPEditor__MUIM_PrefsEditor_ImportFromDirectory
(
    Class *CLASS, Object *self,
    struct MUIP_PrefsEditor_ImportFromDirectory *message
)
{
    SETUP_INST_DATA;
    ULONG layer = DirectoryLayer(message->directory);
    struct FontPrefs fp[FP_COUNT];
    UBYTE i;

    if (SettingsBase == NULL || layer == (ULONG)~0)
        return DoSuperMethodA(CLASS, self, (Msg)message);

    Prefs_Default(fp);
    if (FontSettings_Load(fp, layer, NULL) != SETTING_OK)
        return FALSE;
    for (i = 0; i < FP_COUNT; i++)
    {
        data->fped_FontPrefs[i] = fp[i];
        FP(i)->fp_TextAttr.ta_Name = (STRPTR)FP(i)->fp_Name;
    }
    FontPrefs2Gadgets(data);
    return TRUE;
}

IPTR FPEditor__MUIM_PrefsEditor_ExportToDirectory
(
    Class *CLASS, Object *self,
    struct MUIP_PrefsEditor_ExportToDirectory *message
)
{
    SETUP_INST_DATA;
    ULONG layer = DirectoryLayer(message->directory);

    if (SettingsBase == NULL || layer == (ULONG)~0)
        return DoSuperMethodA(CLASS, self, (Msg)message);

    Gadgets2FontPrefs(data);
    return FontSettings_Store(data->fped_FontPrefs, layer) == SETTING_OK;
}

IPTR FPEditor__MUIM_PrefsEditor_SetDefaults
(
    Class *CLASS, Object *self,
    Msg message
)
{
    SETUP_INST_DATA;
    BOOL success = TRUE;

    success = Prefs_Default(data->fped_FontPrefs);
    if (success) FontPrefs2Gadgets(data);

    return success;
}

/*** Module setup *********************************************************/
/* FontEditor.mcc: one class for the Font program and the settings window.
 * The catalog and settings.library are opened once per module load;
 * without settings.library the editor keeps the classic IFF path. */
struct Library *SettingsBase;

static int FontEditor_Init(LIBBASETYPEPTR LIBBASE)
{
    Locale_Initialize();
    SettingsBase = OpenLibrary("settings.library", 1);
    return TRUE;
}

static int FontEditor_Expunge(LIBBASETYPEPTR LIBBASE)
{
    if (SettingsBase != NULL)
        CloseLibrary(SettingsBase);
    SettingsBase = NULL;
    Locale_Deinitialize();
    return TRUE;
}

ADD2INITLIB(FontEditor_Init, 0);
ADD2EXPUNGELIB(FontEditor_Expunge, 0);
