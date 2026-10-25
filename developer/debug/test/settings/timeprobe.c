/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    TimeProbe: the Date and time page and C:TimeZone in the guest. A
    TimeZone WATCH started with no zone waits; the page opens with no zone
    and nothing to save; choosing Europe/Berlin and noon on 15 July, then
    Use, sets the clock, writes the zone and applies CEST, two hours east.
    A zone file written afterwards (America/New_York) is picked up by the
    running WATCH; choosing None removes the file.
*/
#define MUIMASTER_YES_INLINE_STDARG
#include <string.h>
#include <dos/dostags.h>
#include <libraries/mui.h>
#include <zune/prefseditor.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#define PROBE_TAG "TIMEPROBE"
#include "probe.h"

#define ZONE_FILE    "ENV:Sys/timezone"
#define APPLIED_FILE "ENV:TimeZone/Applied"
#define WATCH_NAME   "TimeProbe watch"

static LONG read_text(CONST_STRPTR path, char *text, LONG size)
{
    BPTR fh = Open(path, MODE_OLDFILE);
    LONG n;
    if (!fh)
        return -1;
    n = Read(fh, text, size - 1);
    Close(fh);
    if (n < 0)
        return -1;
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r'))
        n--;
    text[n] = 0;
    return n;
}

static BOOL write_text(CONST_STRPTR path, CONST_STRPTR text)
{
    BPTR fh = Open(path, MODE_NEWFILE);
    LONG n = strlen((const char *)text);
    BOOL done = fh && Write(fh, (APTR)text, n) == n;
    if (fh)
        Close(fh);
    return done;
}

/* "<utc> <seconds east> <abbreviation>", as TimeZone records it. */
static BOOL applied(LONG east, CONST_STRPTR abbreviation, char *line, LONG size)
{
    char want[32];
    const char *space;
    if (read_text(APPLIED_FILE, line, size) <= 0 || !(space = strchr(line, ' ')))
        return FALSE;
    snprintf(want, sizeof(want), " %ld %s", (long)east, (const char *)abbreviation);
    return strcmp(space, want) == 0;
}

static LONG clock_hour(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return ds.ds_Minute / 60;
}

static LONG clock_day(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return ds.ds_Days;
}

int main(void)
{
    Object *app, *window, *editor, *zone, *month, *year, *hour, *minute, *second;
    char line[128];
    BPTR in, out, lock;
    LONG i, berlin = -1, n;

    /* TimeZone WATCH with no zone set: it must wait, not exit. */
    DeleteFile(ZONE_FILE);
    in = Open("NIL:", MODE_OLDFILE);
    out = Open("NIL:", MODE_NEWFILE);
    CHECK(SystemTags("C:TimeZone WATCH QUIET", SYS_Input, (IPTR)in, SYS_Output, (IPTR)out,
        SYS_Asynch, TRUE, NP_Name, (IPTR)WATCH_NAME, TAG_DONE) != -1, "TimeZone WATCH started with no zone");
    Delay(100);

    app = ApplicationObject,
        MUIA_Application_Title, (IPTR)"TimeProbe",
        SubWindow, (IPTR)(window = WindowObject,
            MUIA_Window_Title, (IPTR)"TimeProbe",
            WindowContents, (IPTR)(editor = MUI_NewObject("TimeEditor.mcc", TAG_DONE)),
        End),
    End;
    CHECK(app && editor, "TimeEditor.mcc instantiated");
    if (!app || !editor)
        return PROBE_RESULT();
    SET(window, MUIA_Window_Open, TRUE);
    zone = find_id(editor, MAKE_ID('T','M','Z','N'));
    month = find_id(editor, MAKE_ID('T','M','M','O'));
    year = find_id(editor, MAKE_ID('T','M','Y','R'));
    hour = find_id(editor, MAKE_ID('T','M','H','R'));
    minute = find_id(editor, MAKE_ID('T','M','M','N'));
    second = find_id(editor, MAKE_ID('T','M','S','C'));
    CHECK(zone && month && year && hour && minute && second, "zone, date and time gadgets found");
    if (!(zone && month && year && hour && minute && second))
        return PROBE_RESULT();
    CHECK(XGET(zone, MUIA_Cycle_Active) == 0 && !XGET(editor, MUIA_PrefsEditor_Changed),
        "no zone file: None, nothing to save");

    /* Each choice exports its zone name; find Berlin's. */
    for (i = 1; i < 64 && berlin < 0; i++)
    {
        SET(zone, MUIA_Cycle_Active, i);
        if (XGET(zone, MUIA_Cycle_Active) != (IPTR)i)
            break;
        if (DoMethod(editor, MUIM_PrefsEditor_Export, (IPTR)"RAM:TimeProbe.zone") &&
            read_text("RAM:TimeProbe.zone", line, sizeof(line)) > 0 &&
            strcmp(line, "Europe/Berlin") == 0)
            berlin = i;
    }
    CHECK(berlin > 0, "Europe/Berlin offered and exported at %ld", (long)berlin);
    SET(zone, MUIA_Cycle_Active, berlin);
    SET(year, MUIA_String_Integer, 2026);
    SET(year, MUIA_String_Acknowledge, TRUE);
    SET(month, MUIA_Cycle_Active, 6);           /* July */
    SET(hour, MUIA_String_Integer, 12);
    SET(minute, MUIA_String_Integer, 0);
    SET(second, MUIA_String_Integer, 0);
    SET(second, MUIA_String_Acknowledge, TRUE);
    CHECK(XGET(editor, MUIA_PrefsEditor_Changed), "zone and time edits raise Changed");

    CHECK(DoMethod(editor, MUIM_PrefsEditor_Use), "Use");
    CHECK(read_text(ZONE_FILE, line, sizeof(line)) > 0 && strcmp(line, "Europe/Berlin") == 0,
        ZONE_FILE " holds %s", line);
    CHECK(applied(7200, "CEST", line, sizeof(line)), "TimeZone applied %s", line);
    n = clock_hour();
    CHECK(n == 14, "clock at hour %ld: noon UTC is 14:00 in Berlin", (long)n);

    /* The WATCH started with no zone still runs and follows a zone file
       it did not write. */
    CHECK(write_text(ZONE_FILE, "America/New_York\n"), "zone file set to America/New_York");
    for (i = 0; i < 90 && !applied(-14400, "EDT", line, sizeof(line)); i++)
        Delay(50);
    CHECK(applied(-14400, "EDT", line, sizeof(line)), "WATCH applied %s after %ld s", line, (long)i);
    n = clock_hour();
    CHECK(n == 8, "clock at hour %ld: 14:00 in Berlin is 08:00 in New York", (long)n);

    DoMethod(editor, MUIM_PrefsEditor_ImportFromDirectory, (IPTR)"ENV:");
    SET(zone, MUIA_Cycle_Active, 0);
    CHECK(DoMethod(editor, MUIM_PrefsEditor_Use), "Use with None");
    lock = Lock(ZONE_FILE, SHARED_LOCK);
    if (lock)
        UnLock(lock);
    CHECK(!lock, "None removed " ZONE_FILE);
    n = clock_day();
    CHECK(n > 17700, "the clock stayed in 2026 (day %ld)", (long)n);

    SET(window, MUIA_Window_Open, FALSE);
    MUI_DisposeObject(app);
    /* The WATCH idles with no zone until the guest stops. */
    return PROBE_RESULT();
}
