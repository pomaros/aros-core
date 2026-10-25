/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    PagesProbe: what the service pages of Settings show in the guest. The
    Battery page lists the tasks using the most processor time, read from
    task.resource. The Brightness page, on a machine without dcp.resource,
    disables its controls and says there is no built-in panel. The
    Software update page reads the package receipt of AFSSYS:, here a RAM
    drawer holding one.
*/
#define MUIMASTER_YES_INLINE_STDARG
#include <string.h>
#include <libraries/mui.h>
#include <zune/prefseditor.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#define PROBE_TAG "PAGESPROBE"
#include "probe.h"

/* The page in an open window, so it is set up as Settings sets it up. */
static Object *open_page(CONST_STRPTR class_name, Object **app, Object **window)
{
    Object *editor;
    *app = ApplicationObject,
        MUIA_Application_Title, (IPTR)"PagesProbe",
        SubWindow, (IPTR)(*window = WindowObject,
            MUIA_Window_Title, (IPTR)"PagesProbe",
            WindowContents, (IPTR)(editor = MUI_NewObject(class_name, TAG_DONE)),
        End),
    End;
    if (!*app)
        return NULL;
    SET(*window, MUIA_Window_Open, TRUE);
    return editor;
}

static BOOL write_file(CONST_STRPTR path, CONST_STRPTR text)
{
    BPTR fh = Open(path, MODE_NEWFILE);
    LONG n = strlen((const char *)text);
    BOOL done = fh && Write(fh, (APTR)text, n) == n;
    if (fh)
        Close(fh);
    return done;
}

static CONST_STRPTR text_of(Object *editor, ULONG id)
{
    Object *o = find_id(editor, id);
    return o ? (CONST_STRPTR)XGET(o, MUIA_Text_Contents) : (CONST_STRPTR)"(none)";
}

static void close_page(Object *app, Object *window)
{
    SET(window, MUIA_Window_Open, FALSE);
    MUI_DisposeObject(app);
}

int main(void)
{
    Object *app, *window, *editor, *text;
    CONST_STRPTR shown;

    editor = open_page((CONST_STRPTR)"BatteryEditor.mcc", &app, &window);
    CHECK(editor != NULL, "BatteryEditor.mcc instantiated");
    if (editor)
    {
        text = find_id(editor, MAKE_ID('B','A','C','O'));
        shown = text ? (CONST_STRPTR)XGET(text, MUIA_Text_Contents) : NULL;
        CHECK(shown != NULL && strncmp((const char *)shown, "CPU", 3) == 0,
            "Battery top consumers: %s", shown ? (const char *)shown : "(none)");
        close_page(app, window);
    }
    editor = open_page((CONST_STRPTR)"BrightnessEditor.mcc", &app, &window);
    CHECK(editor != NULL, "BrightnessEditor.mcc instantiated");
    if (editor)
    {
        Object *panel = find_id(editor, MAKE_ID('B','R','P','N'));
        Object *keyboard = find_id(editor, MAKE_ID('B','R','K','B'));
        BOOL has_panel = OpenResource("dcp.resource") != NULL;
        CHECK(panel && keyboard && XGET(panel, MUIA_Disabled) == !has_panel &&
            XGET(keyboard, MUIA_Disabled) == !has_panel,
            "Brightness controls %s: dcp.resource %s", has_panel ? "enabled" : "disabled",
            has_panel ? "present" : "absent");
        close_page(app, window);
    }
    /* A system volume whose last update upgraded 0641 to 0720. */
    {
        BPTR dir = CreateDir("RAM:AFSSYS"), s;
        if (dir) UnLock(dir);
        s = CreateDir("RAM:AFSSYS/S");
        if (s) UnLock(s);
        CHECK(write_file((CONST_STRPTR)"RAM:AFSSYS/S/PkgInstall.receipt", (CONST_STRPTR)
            "schema: pkginstall-receipt 1\nverdict: PASS\nfailure: -\noperation: UPGRADE\n"
            "previous-version: 1.0.0.261017.0641\nselected-version: 1.0.0.261017.0720\n") &&
            AssignLock("AFSSYS", Lock("RAM:AFSSYS", SHARED_LOCK)), "AFSSYS: with a receipt");
    }
    editor = open_page((CONST_STRPTR)"SoftwareUpdateEditor.mcc", &app, &window);
    CHECK(editor != NULL, "SoftwareUpdateEditor.mcc instantiated");
    if (editor)
    {
        CHECK(strcmp((const char *)text_of(editor, MAKE_ID('U','P','S','I')), "aros-system 1.0.0.261017.0720") == 0,
            "installed: %s", (const char *)text_of(editor, MAKE_ID('U','P','S','I')));
        CHECK(strcmp((const char *)text_of(editor, MAKE_ID('U','P','S','O')), "aros-system 1.0.0.261017.0641") == 0,
            "previous: %s", (const char *)text_of(editor, MAKE_ID('U','P','S','O')));
        CHECK(strcmp((const char *)text_of(editor, MAKE_ID('U','P','S','L')), "UPGRADE PASS") == 0,
            "last: %s", (const char *)text_of(editor, MAKE_ID('U','P','S','L')));
        close_page(app, window);
    }
    AssignLock("AFSSYS", BNULL);
    return PROBE_RESULT();
}
