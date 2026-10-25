/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    AslProbe: the Asl prefs position and size, end to end in the guest.
    It sets the position and size cycles of AslEditor.mcc, exports its
    file, then opens real file requesters and reads where asl.library put
    them: at the screen corner plus the saved offset, then centred after
    settings.library changed only the position bits. One line per check,
    and "[ASLPROBE] result=PASS" when every check holds.
*/
#define MUIMASTER_YES_INLINE_STDARG
#include <stdio.h>
#include <string.h>
#include <exec/execbase.h>
#include <dos/dostags.h>
#include <intuition/intuitionbase.h>
#include <libraries/asl.h>
#include <libraries/mui.h>
#include <libraries/settings.h>
#include <prefs/asl.h>
#include <zune/prefseditor.h>
#include <proto/alib.h>
#include <proto/asl.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/settings.h>

#define PROBE_TAG "ASLPROBE"
#include "probe.h"

struct Library *SettingsBase;

#define PROBE_TITLE "AslProbe requester"
#define PROBE_FILE  "ENV:Sys/Asl.prefs"

/* The ASL chunk of an exported Asl.prefs: FORM PREF, PRHD, ASL. */
static BOOL read_chunk(CONST_STRPTR path, UBYTE *chunk)
{
    UBYTE file[128];
    LONG n, at = 12;
    BPTR fh = Open(path, MODE_OLDFILE);

    if (!fh)
        return FALSE;
    n = Read(fh, file, sizeof(file));
    Close(fh);
    while (at + 8 <= n) {
        ULONG size = (file[at + 4] << 24) | (file[at + 5] << 16) | (file[at + 6] << 8) | file[at + 7];
        if (memcmp(file + at, "ASL ", 4) == 0 && size == sizeof(struct AslPrefs) && at + 8 + size <= (ULONG)n) {
            memcpy(chunk, file + at + 8, size);
            return TRUE;
        }
        at += 8 + ((size + 1) & ~1);
    }
    return FALSE;
}

static struct FileRequester *requester;
static struct Task *parent;

static void requester_task(void)
{
    AslRequestTags(requester, ASLFR_TitleText, (IPTR)PROBE_TITLE, TAG_DONE);
    Signal(parent, SIGBREAKF_CTRL_F);
}

/* Opens a requester in its own process, finds its window and reads the
   window's place and the screen it opened on, then aborts it. */
static BOOL open_and_measure(struct IBox *box, struct IBox *screen)
{
    struct Process *task;
    struct Window *window = NULL;
    int tries;

    requester = AllocAslRequestTags(ASL_FileRequest, TAG_DONE);
    if (!requester)
        return FALSE;
    parent = FindTask(NULL);
    SetSignal(0, SIGBREAKF_CTRL_F);
    task = CreateNewProcTags(NP_Entry, (IPTR)requester_task, NP_Name, (IPTR)"AslProbe requester",
        NP_Output, (IPTR)Output(), NP_CloseOutput, FALSE, TAG_DONE);
    if (!task) {
        FreeAslRequest(requester);
        return FALSE;
    }
    for (tries = 0; tries < 250 && !window; tries++) {
        ULONG lock;
        struct Screen *scr;
        Delay(2);
        lock = LockIBase(0);
        for (scr = IntuitionBase->FirstScreen; scr && !window; scr = scr->NextScreen) {
            struct Window *w;
            for (w = scr->FirstWindow; w; w = w->NextWindow)
                if (w->Title && strcmp((const char *)w->Title, PROBE_TITLE) == 0) {
                    window = w;
                    box->Left = w->LeftEdge; box->Top = w->TopEdge;
                    box->Width = w->Width; box->Height = w->Height;
                    screen->Left = scr->LeftEdge; screen->Top = scr->TopEdge;
                    screen->Width = scr->ViewPort.DWidth; screen->Height = scr->ViewPort.DHeight;
                    break;
                }
        }
        UnlockIBase(lock);
    }
    AbortAslRequest(requester);
    Wait(SIGBREAKF_CTRL_F);
    FreeAslRequest(requester);
    return window != NULL;
}

int main(void)
{
    Object *app, *editor, *position, *size, *left, *top, *width;
    UBYTE chunk[sizeof(struct AslPrefs)];
    struct IBox box, screen;
    char value[SETTING_VALUE_SIZE];
    LONG result;
    BPTR fh;

    SettingsBase = OpenLibrary("settings.library", 1);
    if (!SettingsBase || !AslBase || !MUIMasterBase) {
        say("result=FAIL reason=libraries");
        return RETURN_FAIL;
    }

    /* The editor: cycles read into ap_SizePosition, offsets enabled only
       for the corner positions. */
    app = ApplicationObject,
        MUIA_Application_Title, (IPTR)"AslProbe",
        SubWindow, (IPTR)WindowObject,
            WindowContents, (IPTR)(editor = MUI_NewObject("AslEditor.mcc", TAG_DONE)),
        End,
    End;
    CHECK(app && editor, "AslEditor.mcc instantiated");
    if (!app || !editor) {
        say("result=FAIL failures=%u", failures);
        return RETURN_FAIL;
    }
    position = find_id(editor, MAKE_ID('A','S','P','O'));
    size = find_id(editor, MAKE_ID('A','S','S','Z'));
    left = find_id(editor, MAKE_ID('A','S','O','X'));
    top = find_id(editor, MAKE_ID('A','S','O','Y'));
    width = find_id(editor, MAKE_ID('A','S','W','P'));
    CHECK(position && size && left && top && width, "position, size, offset and width gadgets found");
    if (position && size && left && top && width) {
        CHECK(!XGET(position, MUIA_Disabled) && !XGET(size, MUIA_Disabled), "both cycles enabled");
        SET(position, MUIA_Cycle_Active, ASLPREFS_POS_CENTERSCR);
        CHECK(XGET(left, MUIA_Disabled) && XGET(top, MUIA_Disabled), "offsets disabled when centred");
        SET(position, MUIA_Cycle_Active, ASLPREFS_POS_TOPLEFTSCR);
        CHECK(!XGET(left, MUIA_Disabled) && !XGET(top, MUIA_Disabled), "offsets enabled at the screen corner");
        SET(left, MUIA_String_Integer, 40);
        SET(top, MUIA_String_Integer, 30);
        SET(size, MUIA_Cycle_Active, 0);
        CHECK(XGET(width, MUIA_Disabled), "percentages disabled at the default size");
        CHECK(XGET(editor, MUIA_PrefsEditor_Changed), "the edits raise Changed");

        {
            BPTR dir = CreateDir("ENV:Sys");
            if (dir)
                UnLock(dir);
        }
        fh = Open(PROBE_FILE, MODE_NEWFILE);
        CHECK(fh && DoMethod(editor, MUIM_PrefsEditor_ExportFH, (IPTR)fh), "exported to " PROBE_FILE);
        if (fh)
            Close(fh);
        CHECK(read_chunk(PROBE_FILE, chunk), "ASL chunk read back");
        CHECK((chunk[19] & (ASLPREFS_POS_MASK | ASLPREFS_SIZE_RELATIVE)) == ASLPREFS_POS_TOPLEFTSCR,
            "ap_SizePosition=0x%02x holds the corner position and the default size", chunk[19]);
        CHECK(chunk[20] == 0 && chunk[21] == 40 && chunk[22] == 0 && chunk[23] == 30,
            "offsets saved as 40,30");
    }
    MUI_DisposeObject(app);

    value[0] = 0;
    result = GetSettingA("asl/requester/position", value, sizeof(value), NULL);
    CHECK(result == SETTING_OK && strcmp(value, "topleftscreen") == 0,
        "settings.library reads position %s (%ld, ioerr %ld)", value, (long)result, (long)IoErr());
    value[0] = 0;
    result = GetSettingA("asl/requester/size", value, sizeof(value), NULL);
    CHECK(result == SETTING_OK && strcmp(value, "default") == 0,
        "settings.library reads size %s (%ld)", value, (long)result);

    /* asl.library: the corner plus the saved offsets. */
    if (open_and_measure(&box, &screen))
        CHECK(box.Left == 40 - screen.Left && box.Top == 30 - screen.Top,
            "corner requester at %d,%d on a %dx%d screen", box.Left, box.Top, screen.Width, screen.Height);
    else
        CHECK(0, "corner requester opened");

    /* Only the position bits change; the next requester reads the new
       file without a reboot and is centred. */
    Delay(2);
    CHECK(SetSettingA("asl/requester/position", "centerscreen", NULL) == SETTING_OK,
        "settings.library sets position centerscreen");
    CHECK(read_chunk(PROBE_FILE, chunk) &&
        (chunk[19] & (ASLPREFS_POS_MASK | ASLPREFS_SIZE_RELATIVE)) == ASLPREFS_POS_CENTERSCR,
        "ap_SizePosition=0x%02x keeps the size bit", chunk[19]);
    if (open_and_measure(&box, &screen)) {
        WORD centre_left = (screen.Width - box.Width) / 2 - screen.Left;
        WORD centre_top = (screen.Height - box.Height) / 2 - screen.Top;
        CHECK(box.Left >= centre_left - 1 && box.Left <= centre_left + 1 &&
            box.Top >= centre_top - 1 && box.Top <= centre_top + 1,
            "centred requester at %d,%d, expected %d,%d", box.Left, box.Top, centre_left, centre_top);
    } else
        CHECK(0, "centred requester opened");

    CloseLibrary(SettingsBase);
    return PROBE_RESULT();
}
