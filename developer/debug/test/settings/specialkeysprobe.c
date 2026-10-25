/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    SpecialKeysProbe: C:SpecialKeys and the Special keys page in the guest.
    The command edits ENV:Sys/SpecialKeys; the daemon's input handler
    translates rawkeys injected through input.device, which a second handler
    behind it records: F12 raises ENV:Sys/AudioVolume and reaches nobody,
    fn+F12 is F12, F8 is the media play key, fn+Left is Home, and fn lock
    swaps the layers. The page's Use writes the same file, which the daemon
    follows. QUIT stops the daemon.
*/
#define MUIMASTER_YES_INLINE_STDARG
#include <string.h>
#include <aros/asmcall.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <exec/interrupts.h>
#include <libraries/mui.h>
#include <zune/prefseditor.h>
#include <aros/apple/specialkeys.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#define PROBE_TAG "SPECIALKEYSPROBE"
#include "probe.h"

#define SEEN_MAX 64
static volatile UWORD seen[SEEN_MAX];
static volatile ULONG seen_count;

AROS_UFH2(struct InputEvent *, observe,
    AROS_UFHA(struct InputEvent *, events, A0),
    AROS_UFHA(APTR, data, A1))
{
    AROS_USERFUNC_INIT
    struct InputEvent *e;
    for (e = events; e; e = e->ie_NextEvent)
        if (e->ie_Class == IECLASS_RAWKEY && seen_count < SEEN_MAX)
            seen[seen_count++] = e->ie_Code;
    return events;
    AROS_USERFUNC_EXIT
}

static struct IOStdReq *input;

static void inject(UWORD code)
{
    struct InputEvent event;
    memset(&event, 0, sizeof(event));
    event.ie_Class = IECLASS_RAWKEY;
    event.ie_Code = code;
    input->io_Command = IND_WRITEEVENT;
    input->io_Data = &event;
    input->io_Length = sizeof(event);
    DoIO((struct IORequest *)input);
}

/* Injects the keys, gives the handlers a moment, returns what came out. */
static ULONG keys(const UWORD *codes, ULONG count)
{
    ULONG i;
    seen_count = 0;
    for (i = 0; i < count; i++)
        inject(codes[i]);
    Delay(5);
    return seen_count;
}

static LONG number(CONST_STRPTR name)
{
    char text[16];
    LONG value = -1;
    if (GetVar(name, text, sizeof(text), GVF_GLOBAL_ONLY) > 0)
        StrToLong((CONST_STRPTR)text, &value);
    return value;
}

static BOOL setting(const char *line)
{
    char text[1024];
    LONG n = GetVar("Sys/SpecialKeys", text, sizeof(text), GVF_GLOBAL_ONLY | GVF_BINARY_VAR);
    return n > 0 && strstr(text, line) != NULL;
}

static LONG run(CONST_STRPTR command)
{
    return SystemTags(command, TAG_DONE);
}

static BOOL daemon_up(BOOL wanted)
{
    int i;
    for (i = 0; i < 50; i++) {
        struct MsgPort *port;
        Forbid();
        port = FindPort("SpecialKeys");
        Permit();
        if ((port != NULL) == wanted)
            return TRUE;
        Delay(5);
    }
    return FALSE;
}

/* Waits for a value the daemon sets asynchronously. */
static LONG settle(CONST_STRPTR name, LONG wanted)
{
    int i;
    LONG value = number(name);
    for (i = 0; i < 50 && value != wanted; i++) {
        Delay(2);
        value = number(name);
    }
    return value;
}

int main(void)
{
    struct MsgPort *port;
    struct Interrupt handler;
    BOOL added = FALSE, opened = FALSE;
    LONG volume;

    CHECK(run((CONST_STRPTR)"C:SpecialKeys DEFAULTS") == 0, "DEFAULTS");
    CHECK(setting("F1 brightness-down") && setting("F12 volume-up") && setting("fnlock 0"),
        "the defaults are the printed functions");
    CHECK(run((CONST_STRPTR)"C:SpecialKeys KEY F5 ACTION mute TIMEOUT 30") == 0, "KEY F5 ACTION mute TIMEOUT 30");
    CHECK(setting("F5 mute") && setting("light-timeout 30"), "one key and the timeout changed");
    CHECK(run((CONST_STRPTR)"C:SpecialKeys KEY F13 ACTION mute") != 0, "an unknown key is refused");
    CHECK(setting("F5 mute"), "and changes nothing");
    run((CONST_STRPTR)"C:SpecialKeys DEFAULTS");

    CHECK(SystemTags((CONST_STRPTR)"C:SpecialKeys DAEMON",
        SYS_Asynch, TRUE, SYS_Input, (IPTR)Open("NIL:", MODE_OLDFILE),
        SYS_Output, (IPTR)Open("NIL:", MODE_NEWFILE), TAG_DONE) == 0, "daemon started");
    CHECK(daemon_up(TRUE), "the daemon's port is up");

    port = CreateMsgPort();
    input = port ? (struct IOStdReq *)CreateIORequest(port, sizeof(*input)) : NULL;
    if (input && OpenDevice("input.device", 0, (struct IORequest *)input, 0) == 0) {
        opened = TRUE;
        memset(&handler, 0, sizeof(handler));
        handler.is_Node.ln_Type = NT_INTERRUPT;
        handler.is_Node.ln_Pri = 55;      /* behind SpecialKeys (60) */
        handler.is_Code = (VOID_FUNC)AROS_ASMSYMNAME(observe);
        input->io_Command = IND_ADDHANDLER;
        input->io_Data = &handler;
        DoIO((struct IORequest *)input);
        added = TRUE;
    }
    CHECK(added, "observer added behind the daemon");
    if (added) {
        static const UWORD f12[] = { 0x6F, 0xEF };
        static const UWORD fn_f12[] = { AROS_APPLE_RAWKEY_FN, 0x6F, 0xEF, AROS_APPLE_RAWKEY_FN | 0x80 };
        static const UWORD f8[] = { 0x57, 0xD7 };
        static const UWORD fn_left[] = { AROS_APPLE_RAWKEY_FN, 0x4F, AROS_APPLE_RAWKEY_FN | 0x80, 0xCF };
        static const UWORD letter[] = { 0x20, 0xA0 };

        SetVar("Sys/AudioVolume", "50", 2, GVF_GLOBAL_ONLY);
        CHECK(keys(f12, 2) == 0, "F12 reaches nobody (%lu events)", (unsigned long)seen_count);
        volume = settle((CONST_STRPTR)"Sys/AudioVolume", 60);
        CHECK(volume == 60, "and raised the volume from 50 to %ld", (long)volume);
        CHECK(keys(fn_f12, 4) == 2 && seen[0] == 0x6F && seen[1] == 0xEF,
            "fn+F12 is F12 and fn itself reaches nobody (%lu events)", (unsigned long)seen_count);
        CHECK(number((CONST_STRPTR)"Sys/AudioVolume") == 60, "fn+F12 left the volume");
        CHECK(keys(f8, 2) == 2 && seen[0] == 0x73 && seen[1] == 0xF3, "F8 is the media play key");
        CHECK(keys(fn_left, 4) == 2 && seen[0] == 0x70 && seen[1] == 0xF0,
            "fn+Left is Home, released as Home after fn");
        CHECK(keys(letter, 2) == 2 && seen[0] == 0x20, "an ordinary key passes");

        CHECK(run((CONST_STRPTR)"C:SpecialKeys FNLOCK ON") == 0 && setting("fnlock 1"), "FNLOCK ON");
        Delay(10);
        CHECK(keys(f12, 2) == 2 && seen[0] == 0x6F, "with fn lock F12 is F12");
        CHECK(keys(fn_f12, 4) == 0, "and fn+F12 is the volume");
        volume = settle((CONST_STRPTR)"Sys/AudioVolume", 70);
        CHECK(volume == 70, "which went to %ld", (long)volume);

        /* The page: built, then Use with the top row back to the functions. */
        {
            Object *app, *window, *editor = NULL, *layer;
            app = ApplicationObject,
                MUIA_Application_Title, (IPTR)"SpecialKeysProbe",
                SubWindow, (IPTR)(window = WindowObject,
                    MUIA_Window_Title, (IPTR)"SpecialKeysProbe",
                    WindowContents, (IPTR)(editor = MUI_NewObject("SpecialKeysEditor.mcc", TAG_DONE)),
                End),
            End;
            CHECK(app != NULL && editor != NULL, "SpecialKeysEditor.mcc instantiated");
            if (app && editor) {
                int i, ids = 0;
                SET(window, MUIA_Window_Open, TRUE);
                for (i = 0; i < SK_KEYS; i++)
                    ids += find_id(editor, MAKE_ID('S','K','A','a' + i)) != NULL;
                CHECK(ids == SK_KEYS && find_id(editor, MAKE_ID('S','K','L','V')) &&
                    find_id(editor, MAKE_ID('S','K','L','T')), "all %d key gadgets and the light carry ids", ids);
                layer = find_id(editor, MAKE_ID('S','K','F','L'));
                CHECK(layer && XGET(layer, MUIA_Cycle_Active) == 1, "the page shows fn lock on");
                if (layer)
                    SET(layer, MUIA_Cycle_Active, 0);
                CHECK(XGET(editor, MUIA_PrefsEditor_Changed), "the edit raises Changed");
                CHECK(DoMethod(editor, MUIM_PrefsEditor_Use), "Use");
                CHECK(setting("fnlock 0"), "Use wrote fn lock off");
                Delay(10);
                CHECK(keys(f12, 2) == 0, "the daemon followed: F12 is the volume again");
                SET(window, MUIA_Window_Open, FALSE);
            }
            if (app)
                MUI_DisposeObject(app);
        }

        input->io_Command = IND_REMHANDLER;
        input->io_Data = &handler;
        DoIO((struct IORequest *)input);
    }
    if (opened)
        CloseDevice((struct IORequest *)input);
    if (input)
        DeleteIORequest((struct IORequest *)input);
    if (port)
        DeleteMsgPort(port);
    CHECK(run((CONST_STRPTR)"C:SpecialKeys QUIT") == 0, "QUIT");
    CHECK(daemon_up(FALSE), "the daemon is gone");
    run((CONST_STRPTR)"C:SpecialKeys DEFAULTS");
    return PROBE_RESULT();
}
