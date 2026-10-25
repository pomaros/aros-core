/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    TrackpadProbe: C:Trackpad and the Trackpad page in the guest. The
    command edits ENV:Sys/Trackpad and refuses bad values; with no driver
    APPLY warns. The probe then stands in for spihid.hidd by adding the
    AppleTrackpad semaphore itself: APPLY, a change from the command and the
    page's Use must each reach it with a new generation and leave the
    driver's own tuning alone.
*/
#define MUIMASTER_YES_INLINE_STDARG
#include <string.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <exec/memory.h>
#include <libraries/mui.h>
#include <zune/prefseditor.h>
#include <aros/apple/trackpadcontrol.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#define PROBE_TAG "TRACKPADPROBE"
#include "probe.h"

static BOOL setting(const char *line)
{
    char text[512];
    LONG n = GetVar("Sys/Trackpad", text, sizeof(text), GVF_GLOBAL_ONLY | GVF_BINARY_VAR);
    return n > 0 && strstr(text, line) != NULL;
}

static LONG run(CONST_STRPTR command)
{
    BPTR out = Open("NIL:", MODE_NEWFILE);
    LONG rc = SystemTags(command, SYS_Output, (IPTR)out, TAG_DONE);
    if (out)
        Close(out);
    return rc;
}

int main(void)
{
    struct AppleTrackpadControl *control;
    ULONG generation;

    CHECK(run((CONST_STRPTR)"C:Trackpad DEFAULTS") == 0, "DEFAULTS");
    CHECK(setting("clickfinger 1\n") && setting("tap 0\n") && setting("scroll 1\n") &&
        setting("natural 1\n") && setting("scroll-speed 5\n") && setting("click-delay 40\n"),
        "the defaults: two-finger right click, no tap, natural scrolling");
    CHECK(run((CONST_STRPTR)"C:Trackpad TAP ON SCROLLSPEED 8 CLICKDELAY 60") == 0,
        "TAP ON SCROLLSPEED 8 CLICKDELAY 60");
    CHECK(setting("tap 1\n") && setting("scroll-speed 8\n") && setting("click-delay 60\n"),
        "written to ENV:Sys/Trackpad");
    CHECK(run((CONST_STRPTR)"C:Trackpad SCROLLSPEED 11") != 0, "SCROLLSPEED 11 is refused");
    CHECK(run((CONST_STRPTR)"C:Trackpad TAP MAYBE") != 0, "TAP MAYBE is refused");
    CHECK(run((CONST_STRPTR)"C:Trackpad CLICKDELAY 500") != 0, "CLICKDELAY 500 is refused");
    CHECK(setting("tap 1\n") && setting("scroll-speed 8\n"), "and change nothing");
    CHECK(run((CONST_STRPTR)"C:Trackpad APPLY") == RETURN_WARN, "APPLY without a driver warns");

    /* The probe as the driver. */
    control = AllocMem(sizeof(*control), MEMF_PUBLIC | MEMF_CLEAR);
    CHECK(control != NULL, "control block");
    if (control == NULL)
        return PROBE_RESULT();
    control->tc_Lock.ss_Link.ln_Name = (char *)APPLE_TRACKPAD_CONTROL_NAME;
    control->tc_Version = APPLE_TRACKPAD_CONTROL_VERSION;
    control->tc_Size = sizeof(*control);
    tp_defaults(&control->tc_Config);
    control->tc_Config.divisor = 7;
    control->tc_Config.match_distance = 1234;
    AddSemaphore(&control->tc_Lock);

    generation = control->tc_Generation;
    CHECK(run((CONST_STRPTR)"C:Trackpad APPLY") == 0, "APPLY with the driver");
    CHECK(control->tc_Generation == generation + 1, "the generation moved on");
    CHECK(control->tc_Config.tap == 1 && control->tc_Config.scroll_speed == 8 &&
        control->tc_Config.settle_us == 60000U, "the driver has the file's settings");
    CHECK(control->tc_Config.divisor == 7 && control->tc_Config.match_distance == 1234,
        "and kept its own tuning");
    CHECK(run((CONST_STRPTR)"C:Trackpad NATURAL OFF") == 0 && control->tc_Config.natural == 0 &&
        control->tc_Generation == generation + 2, "a change reaches the driver at once");
    CHECK(run((CONST_STRPTR)"C:Trackpad LIST") == 0, "LIST");

    /* The page: built, the file's settings shown, Use reaches the driver. */
    {
        Object *app, *window, *editor = NULL, *tap, *speed;
        app = ApplicationObject,
            MUIA_Application_Title, (IPTR)"TrackpadProbe",
            SubWindow, (IPTR)(window = WindowObject,
                MUIA_Window_Title, (IPTR)"TrackpadProbe",
                WindowContents, (IPTR)(editor = MUI_NewObject("TrackpadEditor.mcc", TAG_DONE)),
            End),
        End;
        CHECK(app != NULL && editor != NULL, "TrackpadEditor.mcc instantiated");
        if (app && editor) {
            SET(window, MUIA_Window_Open, TRUE);
            tap = find_id(editor, MAKE_ID('T','P','T','P'));
            speed = find_id(editor, MAKE_ID('T','P','S','S'));
            CHECK(tap && speed && find_id(editor, MAKE_ID('T','P','C','F')) &&
                find_id(editor, MAKE_ID('T','P','C','D')) && find_id(editor, MAKE_ID('T','P','S','C')) &&
                find_id(editor, MAKE_ID('T','P','N','A')), "all six gadgets carry ids");
            CHECK(tap && XGET(tap, MUIA_Selected) && speed && XGET(speed, MUIA_Numeric_Value) == 8,
                "the page shows the file: tap on, speed 8");
            if (tap)
                SET(tap, MUIA_Selected, FALSE);
            if (speed)
                SET(speed, MUIA_Numeric_Value, 3);
            CHECK(XGET(editor, MUIA_PrefsEditor_Changed), "the edit raises Changed");
            generation = control->tc_Generation;
            CHECK(DoMethod(editor, MUIM_PrefsEditor_Use), "Use");
            CHECK(setting("tap 0\n") && setting("scroll-speed 3\n") && setting("natural 0\n"),
                "Use wrote the file");
            CHECK(control->tc_Generation == generation + 1 && control->tc_Config.tap == 0 &&
                control->tc_Config.scroll_speed == 3, "and the driver has it");
            SET(window, MUIA_Window_Open, FALSE);
        }
        if (app)
            MUI_DisposeObject(app);
    }

    RemSemaphore(&control->tc_Lock);
    FreeMem(control, sizeof(*control));
    run((CONST_STRPTR)"C:Trackpad DEFAULTS");
    return PROBE_RESULT();
}
