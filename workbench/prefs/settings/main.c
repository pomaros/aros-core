/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Settings: every settings editor in one window. Usage:
        Settings [PAGE <class>] [PUBSCREEN <name>] [SEARCH <text>] [LEVEL <name>] [CHECK]
    The groups and rows come from SYS:Prefs/Index/Groups and
    SYS:Prefs/Index/Rows; a page is a PrefsEditor created by class name.
*/
#define MUIMASTER_YES_INLINE_STDARG

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/exall.h>
#include <libraries/mui.h>

#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include <libraries/settings.h>
#include <proto/settings.h>
#include <string.h>

#include "settingswindow.h"

static const char version[] __attribute__((used)) = "$VER: Settings 1.0 (16.10.2026)";

#define GROUPS_DIR "SYS:Prefs/Index/Groups"
#define ROWS_DIR   "SYS:Prefs/Index/Rows"

static struct catalogue catalogue;
struct Library *SettingsBase;
static Object *settings_window;
static char port_result[2048];

/*** The ARexx port: GET and SET through settings.library, FIND over the
 *** window's catalogue and gadget index. ***********************************/
AROS_UFH3(static IPTR, port_get,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(Object *, app, A2),
    AROS_UFHA(IPTR *, args, A1))
{
    AROS_USERFUNC_INIT
    if (SettingsBase == NULL ||
        GetSettingA((CONST_STRPTR)args[0], (STRPTR)port_result, sizeof(port_result), NULL) != SETTING_OK)
        return RETURN_ERROR;
    SET(app, MUIA_Application_RexxString, (IPTR)port_result);
    return RETURN_OK;
    AROS_USERFUNC_EXIT
}

AROS_UFH3(static IPTR, port_set,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(Object *, app, A2),
    AROS_UFHA(IPTR *, args, A1))
{
    AROS_USERFUNC_INIT
    if (SettingsBase == NULL || SetSettingA((CONST_STRPTR)args[0], (CONST_STRPTR)args[1], NULL) != SETTING_OK)
        return RETURN_ERROR;
    return RETURN_OK;
    AROS_USERFUNC_EXIT
}

AROS_UFH3(static IPTR, port_find,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(Object *, app, A2),
    AROS_UFHA(IPTR *, args, A1))
{
    AROS_USERFUNC_INIT
    DoMethod(settings_window, MUIM_SettingsWindow_Find, args[0], (IPTR)port_result, sizeof(port_result));
    SET(app, MUIA_Application_RexxString, (IPTR)port_result);
    return port_result[0] != '\0' ? RETURN_OK : RETURN_WARN;
    AROS_USERFUNC_EXIT
}

AROS_UFH3(static IPTR, port_level,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(Object *, app, A2),
    AROS_UFHA(IPTR *, args, A1))
{
    AROS_USERFUNC_INIT
    return DoMethod(settings_window, MUIM_SettingsWindow_SetLevel, args[0]) ? RETURN_OK : RETURN_ERROR;
    AROS_USERFUNC_EXIT
}

static struct Hook level_hook = { { NULL, NULL }, (HOOKFUNC)port_level, NULL, NULL };
static struct Hook get_hook = { { NULL, NULL }, (HOOKFUNC)port_get, NULL, NULL };
static struct Hook set_hook = { { NULL, NULL }, (HOOKFUNC)port_set, NULL, NULL };
static struct Hook find_hook = { { NULL, NULL }, (HOOKFUNC)port_find, NULL, NULL };

static struct MUI_Command commands[] =
{
    { "GET",  "KEY/A",         1, &get_hook,  {0} },
    { "SET",  "KEY/A,VALUE/A/F", 2, &set_hook, {0} },
    { "FIND", "TEXT/A/F",      1, &find_hook, {0} },
    { "LEVEL", "LEVEL/A",      1, &level_hook, {0} },
    { NULL,   NULL,            0, NULL,       {0} }
};

static char *read_text(CONST_STRPTR path, LONG *length)
{
    BPTR file = Open(path, MODE_OLDFILE);
    char *text = NULL;
    LONG size;

    if (file == BNULL)
        return NULL;
    Seek(file, 0, OFFSET_END);
    size = Seek(file, 0, OFFSET_BEGINNING);
    if (size >= 0 && size < 16384 && (text = AllocVec(size + 1, MEMF_ANY)) != NULL)
    {
        if (Read(file, text, size) == size)
        {
            text[size] = '\0';
            *length = size;
        }
        else
        {
            FreeVec(text);
            text = NULL;
        }
    }
    Close(file);
    return text;
}

/* Every file of a directory, parsed as a group or a row. A file that does
 * not parse is skipped with a line on the console. */
static void load_dir(CONST_STRPTR dir, BOOL rows)
{
    BPTR lock = Lock(dir, SHARED_LOCK);
    struct FileInfoBlock *fib;

    if (lock == BNULL)
        return;
    if ((fib = AllocDosObject(DOS_FIB, NULL)) != NULL)
    {
        if (Examine(lock, fib))
            while (ExNext(lock, fib))
            {
                char path[256];
                char *text;
                LONG length = 0;

                if (fib->fib_DirEntryType > 0)
                    continue;
                strlcpy(path, (const char *)dir, sizeof(path));
                AddPart((STRPTR)path, fib->fib_FileName, sizeof(path));
                if ((text = read_text((CONST_STRPTR)path, &length)) == NULL)
                    continue;
                if (rows)
                {
                    if (catalogue.row_count < CAT_ROWS &&
                        cat_parse_row(text, length, &catalogue.rows[catalogue.row_count]) == 0)
                        catalogue.row_count++;
                    else
                        Printf("Settings: row file %s is not valid\n", (IPTR)path);
                }
                else
                {
                    if (catalogue.group_count < CAT_GROUPS &&
                        cat_parse_group(text, length, &catalogue.groups[catalogue.group_count]) == 0)
                        catalogue.group_count++;
                    else
                        Printf("Settings: group file %s is not valid\n", (IPTR)path);
                }
                FreeVec(text);
            }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
}

int main(void)
{
    IPTR args[5] = { 0, 0, 0, 0, 0 };
    struct RDArgs *rda = ReadArgs((CONST_STRPTR)"PAGE/K,PUBSCREEN/K,SEARCH/K,LEVEL/K,CHECK/S", args, NULL);
    struct Screen *screen = NULL;
    Object *application, *window;
    IPTR check_failures = 0;

    SettingsBase = OpenLibrary("settings.library", 1);
    load_dir((CONST_STRPTR)GROUPS_DIR, FALSE);
    load_dir((CONST_STRPTR)ROWS_DIR, TRUE);
    cat_finish(&catalogue);
    if (catalogue.orphan_rows != 0)
        Printf("Settings: %ld row(s) name a group that does not exist; they are reachable by search only\n",
            (IPTR)catalogue.orphan_rows);

    if (args[1])
        screen = LockPubScreen((CONST_STRPTR)args[1]);

    application = (Object *)ApplicationObject,
        MUIA_Application_Title, (IPTR)"Settings",
        MUIA_Application_Version, (IPTR)version,
        MUIA_Application_Description, (IPTR)"Every settings editor in one window",
        MUIA_Application_SingleTask, TRUE,
        MUIA_Application_Base, (IPTR)"SETTINGS",
        MUIA_Application_Commands, (IPTR)commands,
        SubWindow, (IPTR)(window = (Object *)SettingsWindowObject,
            MUIA_Window_Screen, (IPTR)screen,
            MUIA_SettingsWindow_Catalogue, (IPTR)&catalogue,
            MUIA_SettingsWindow_Initial, args[0],
        End),
    End;

    if (application != NULL)
    {
        settings_window = window;
        SET(window, MUIA_Window_Open, TRUE);
        if (args[3] && !DoMethod(window, MUIM_SettingsWindow_SetLevel, args[3]))
            Printf("Settings: unknown level %s\n", args[3]);
        if (args[4])
            check_failures = DoMethod(window, MUIM_SettingsWindow_Check, args[2]);
        else if (args[2])
            DoMethod(window, MUIM_SettingsWindow_SetSearch, args[2]);
        DoMethod(application, MUIM_Application_Execute);
        SET(window, MUIA_Window_Open, FALSE);
        MUI_DisposeObject(application);
    }
    if (screen != NULL)
        UnlockPubScreen(NULL, screen);
    if (rda != NULL)
        FreeArgs(rda);
    if (SettingsBase != NULL)
        CloseLibrary(SettingsBase);
    if (application == NULL)
        return RETURN_FAIL;
    return check_failures != 0 ? RETURN_ERROR : RETURN_OK;
}
