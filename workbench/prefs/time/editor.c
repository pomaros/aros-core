/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Date and time: the system clock and the named time zone C:TimeZone
    follows. The page's file is SYS/timezone, the zone name; Use sets the
    clock when the date or time was edited, writes or removes the zone and
    runs TimeZone once, so the offset and summer time apply at once; a
    running TimeZone WATCH picks the zone up as well. Save also stores the
    clock in the battery-backed clock through SetClock.
*/
#define MUIMASTER_YES_INLINE_STDARG
#include <stdio.h>
#include <string.h>
#include <devices/timer.h>
#include <exec/memory.h>
#include <dos/dostags.h>
#include <libraries/locale.h>
#include <utility/date.h>
#include <zune/prefseditor.h>
#include <zune/calendar.h>
#include <zune/clock.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/locale.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include "editor.h"
#include "tz_rule.h"
#include LC_LIBDEFS_FILE

#define ZONE_FILE   "SYS/timezone"
#define AMIGA_EPOCH 252460800LL     /* 1978-01-01 in Unix seconds */

static CONST_STRPTR english_months[12] =
{
    "January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December"
};

/* Local time now, from the clock DOS keeps. */
static ULONG local_now(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return ds.ds_Days * 86400 + ds.ds_Minute * 60 + ds.ds_Tick / TICKS_PER_SECOND;
}

static LONG minutes_west(void)
{
    LONG west = 0;
    struct Locale *locale;
    if (LocaleBase && (locale = OpenLocale(NULL)))
    {
        west = locale->loc_GMTOffset;
        CloseLocale(locale);
    }
    return west;
}

/* The zone the cycle selects, "" for none. */
static CONST_STRPTR selected_zone(struct TimeEditor_DATA *data)
{
    IPTR active = XGET(data->zone, MUIA_Cycle_Active);
    return active == 0 ? (CONST_STRPTR)"" : (CONST_STRPTR)data->zone_labels[active];
}

static void show_zone_info(struct TimeEditor_DATA *data)
{
    static char text[200];
    CONST_STRPTR zone = selected_zone(data);
    const char *rule_text, *country;
    struct tz_rule rule;

    if (zone[0] == 0)
    {
        SET(data->zone_info, MUIA_Text_Contents,
            (IPTR)"No zone: the fixed offset of Language and region applies, without summer time");
        return;
    }
    if (tz_lookup(zone, &rule_text, &country) == 0 && tz_parse(rule_text, &rule) == 0)
    {
        int64_t utc = (int64_t)local_now() + AMIGA_EPOCH + minutes_west() * 60LL;
        int dst;
        long east = tz_offset_at(&rule, utc, &dst);
        int64_t change = tz_next_transition(&rule, utc);
        char offset[16], next[32];
        tz_format_offset(east, offset, sizeof(offset));
        if (change)
        {
            tz_format_time(change, next, sizeof(next));
            snprintf(text, sizeof(text), "%s, UTC%s now; next change %s UTC",
                dst ? rule.dst_name : rule.std_name, offset, next);
        }
        else
            snprintf(text, sizeof(text), "%s, UTC%s, no summer time", rule.std_name, offset);
        SET(data->zone_info, MUIA_Text_Contents, (IPTR)text);
    }
}

/* The date and time gadgets show data->shown. */
static void show_time(struct TimeEditor_DATA *data)
{
    NNSET(data->month, MUIA_Cycle_Active, data->shown.month - 1);
    NNSET(data->year, MUIA_String_Integer, data->shown.year);
    NNSET(data->calendar, MUIA_Calendar_Date, (IPTR)&data->shown);
    NNSET(data->clock, MUIA_Clock_Time, (IPTR)&data->shown);
    NNSET(data->hour, MUIA_String_Integer, data->shown.hour);
    NNSET(data->minute, MUIA_String_Integer, data->shown.min);
    NNSET(data->second, MUIA_String_Integer, data->shown.sec);
}

static void select_zone(struct TimeEditor_DATA *data, CONST_STRPTR zone)
{
    ULONG i, active = 0;
    for (i = 1; data->zone_labels[i]; i++)
        if (strcmp((const char *)data->zone_labels[i], (const char *)zone) == 0)
            active = i;
    NNSET(data->zone, MUIA_Cycle_Active, active);
    show_zone_info(data);
}

static Object *number(ULONG id, CONST_STRPTR help, CONST_STRPTR width)
{
    return StringObject,
        StringFrame,
        MUIA_CycleChain, TRUE,
        MUIA_ObjectID, id,
        MUIA_ShortHelp, (IPTR)help,
        MUIA_String_Accept, (IPTR)"0123456789",
        MUIA_FixWidthTxt, (IPTR)width,
    End;
}

IPTR TimeEditor__OM_NEW(Class *CLASS, Object *self, struct opSet *message)
{
    struct TimeEditor_DATA local, *data;
    ULONG i;

    memset(&local, 0, sizeof(local));
    /* The cycles keep pointers to their labels for their whole life. */
    local.zone_labels = AllocVec(sizeof(STRPTR) * (TIME_ZONES_MAX + 2 + 13), MEMF_CLEAR);
    if (!local.zone_labels)
        return 0;
    local.month_labels = local.zone_labels + TIME_ZONES_MAX + 2;
    local.zone_labels[0] = (STRPTR)"None";
    for (i = 0; i < TIME_ZONES_MAX && tz_zone_name(i); i++)
        local.zone_labels[i + 1] = (STRPTR)tz_zone_name(i);
    for (i = 0; i < 12; i++)
        local.month_labels[i] = (STRPTR)english_months[i];
    if (LocaleBase)
    {
        struct Locale *locale = OpenLocale(NULL);
        if (locale)
        {
            for (i = 0; i < 12; i++)
                local.month_labels[i] = (STRPTR)GetLocaleStr(locale, MON_1 + i);
            CloseLocale(locale);
        }
    }

    self = (Object *)DoSuperNewTags(CLASS, self, NULL,
        MUIA_PrefsEditor_Name, (IPTR)"Date and time",
        MUIA_PrefsEditor_Path, (IPTR)ZONE_FILE,
        Child, (IPTR)HGroup,
            Child, (IPTR)VGroup,
                GroupFrameT("Date"),
                Child, (IPTR)HGroup,
                    Child, (IPTR)(local.month = CycleObject,
                        MUIA_CycleChain, TRUE,
                        MUIA_ObjectID, MAKE_ID('T','M','M','O'),
                        MUIA_ShortHelp, (IPTR)"The month of the system date",
                        MUIA_Cycle_Entries, (IPTR)local.month_labels,
                    End),
                    Child, (IPTR)(local.year = number(MAKE_ID('T','M','Y','R'),
                        "The year of the system date", "55555")),
                End,
                Child, (IPTR)(local.calendar = CalendarObject,
                    MUIA_CycleChain, TRUE,
                    MUIA_ObjectID, MAKE_ID('T','M','D','Y'),
                    MUIA_ShortHelp, (IPTR)"The day of the system date",
                End),
            End,
            Child, (IPTR)VGroup,
                GroupFrameT("Time"),
                Child, (IPTR)(local.clock = ClockObject, End),
                Child, (IPTR)HGroup,
                    Child, (IPTR)HSpace(0),
                    Child, (IPTR)(local.hour = number(MAKE_ID('T','M','H','R'),
                        "Hours of the system time, 0 to 23", "555")),
                    Child, (IPTR)CLabel2(":"),
                    Child, (IPTR)(local.minute = number(MAKE_ID('T','M','M','N'),
                        "Minutes of the system time", "555")),
                    Child, (IPTR)CLabel2(":"),
                    Child, (IPTR)(local.second = number(MAKE_ID('T','M','S','C'),
                        "Seconds of the system time", "555")),
                    Child, (IPTR)HSpace(0),
                End,
            End,
        End,
        Child, (IPTR)VGroup,
            GroupFrameT("Time zone"),
            Child, (IPTR)HGroup,
                Child, (IPTR)Label1("Zone"),
                Child, (IPTR)(local.zone = CycleObject,
                    MUIA_CycleChain, TRUE,
                    MUIA_ObjectID, MAKE_ID('T','M','Z','N'),
                    MUIA_ShortHelp, (IPTR)"The named zone whose offset and summer time the clock follows",
                    MUIA_Cycle_Entries, (IPTR)local.zone_labels,
                End),
            End,
            Child, (IPTR)(local.zone_info = TextObject,
                MUIA_Text_Contents, (IPTR)"",
            End),
        End,
        TAG_MORE, (IPTR)message->ops_AttrList);
    if (!self)
    {
        FreeVec(local.zone_labels);
        return 0;
    }

    data = INST_DATA(CLASS, self);
    *data = local;
    Amiga2Date(local_now(), &data->shown);
    show_time(data);
    show_zone_info(data);

    DoMethod(data->month, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime,
        (IPTR)self, 1, MUIM_TimeEditor_Month);
    DoMethod(data->year, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime,
        (IPTR)self, 1, MUIM_TimeEditor_Year);
    DoMethod(data->calendar, MUIM_Notify, MUIA_Calendar_MonthDay, MUIV_EveryTime,
        (IPTR)self, 1, MUIM_TimeEditor_Edited);
    DoMethod(data->hour, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime,
        (IPTR)self, 1, MUIM_TimeEditor_Clock);
    DoMethod(data->minute, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime,
        (IPTR)self, 1, MUIM_TimeEditor_Clock);
    DoMethod(data->second, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime,
        (IPTR)self, 1, MUIM_TimeEditor_Clock);
    DoMethod(data->zone, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime,
        (IPTR)self, 1, MUIM_TimeEditor_Zone);
    return (IPTR)self;
}

IPTR TimeEditor__OM_DISPOSE(Class *CLASS, Object *self, Msg message)
{
    struct TimeEditor_DATA *data = INST_DATA(CLASS, self);
    STRPTR *labels = data->zone_labels;
    IPTR result = DoSuperMethodA(CLASS, self, message);
    FreeVec(labels);
    return result;
}

IPTR TimeEditor__MUIM_Setup(Class *CLASS, Object *self, Msg message)
{
    struct TimeEditor_DATA *data = INST_DATA(CLASS, self);
    if (!DoSuperMethodA(CLASS, self, message))
        return FALSE;
    data->ticker.ihn_Object = self;
    data->ticker.ihn_Flags = MUIIHNF_TIMER;
    data->ticker.ihn_Millis = 1000;
    data->ticker.ihn_Method = MUIM_TimeEditor_Tick;
    DoMethod(_app(self), MUIM_Application_AddInputHandler, (IPTR)&data->ticker);
    data->ticking = TRUE;
    return TRUE;
}

IPTR TimeEditor__MUIM_Cleanup(Class *CLASS, Object *self, Msg message)
{
    struct TimeEditor_DATA *data = INST_DATA(CLASS, self);
    if (data->ticking)
        DoMethod(_app(self), MUIM_Application_RemInputHandler, (IPTR)&data->ticker);
    data->ticking = FALSE;
    return DoSuperMethodA(CLASS, self, message);
}

/* Until the person edits the date or time, the page follows the clock. */
IPTR TimeEditor__MUIM_TimeEditor_Tick(Class *CLASS, Object *self, Msg message)
{
    struct TimeEditor_DATA *data = INST_DATA(CLASS, self);
    if (!data->edited)
    {
        Amiga2Date(local_now(), &data->shown);
        show_time(data);
    }
    return 0;
}

/* Read every date and time gadget into data->shown, valid or corrected. */
IPTR TimeEditor__MUIM_TimeEditor_Edited(Class *CLASS, Object *self, Msg message)
{
    struct TimeEditor_DATA *data = INST_DATA(CLASS, self);
    struct ClockData *day = NULL, check;
    ULONG hour = XGET(data->hour, MUIA_String_Integer);
    ULONG minute = XGET(data->minute, MUIA_String_Integer);
    ULONG second = XGET(data->second, MUIA_String_Integer);

    GET(data->calendar, MUIA_Calendar_Date, &day);
    if (day)
    {
        data->shown.mday = day->mday;
        data->shown.month = day->month;
        data->shown.year = day->year;
    }
    data->shown.hour = hour > 23 ? 23 : hour;
    data->shown.min = minute > 59 ? 59 : minute;
    data->shown.sec = second > 59 ? 59 : second;
    check = data->shown;
    if (!CheckDate(&check))
        data->shown.mday = 1;
    Amiga2Date(Date2Amiga(&data->shown), &data->shown);   /* weekday */
    data->edited = TRUE;
    show_time(data);
    SET(self, MUIA_PrefsEditor_Changed, TRUE);
    return 0;
}

static ULONG month_days(ULONG month, ULONG year)
{
    static const UBYTE days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    BOOL leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : days[month - 1];
}

IPTR TimeEditor__MUIM_TimeEditor_Month(Class *CLASS, Object *self, Msg message)
{
    struct TimeEditor_DATA *data = INST_DATA(CLASS, self);
    data->shown.month = XGET(data->month, MUIA_Cycle_Active) + 1;
    if (data->shown.mday > month_days(data->shown.month, data->shown.year))
        data->shown.mday = month_days(data->shown.month, data->shown.year);
    NNSET(data->calendar, MUIA_Calendar_Date, (IPTR)&data->shown);
    return DoMethod(self, MUIM_TimeEditor_Edited);
}

IPTR TimeEditor__MUIM_TimeEditor_Year(Class *CLASS, Object *self, Msg message)
{
    struct TimeEditor_DATA *data = INST_DATA(CLASS, self);
    ULONG year = XGET(data->year, MUIA_String_Integer);
    data->shown.year = year < 1978 ? 1978 : year > 2099 ? 2099 : year;
    if (data->shown.mday > month_days(data->shown.month, data->shown.year))
        data->shown.mday = month_days(data->shown.month, data->shown.year);
    NNSET(data->calendar, MUIA_Calendar_Date, (IPTR)&data->shown);
    return DoMethod(self, MUIM_TimeEditor_Edited);
}

IPTR TimeEditor__MUIM_TimeEditor_Clock(Class *CLASS, Object *self, Msg message)
{
    return DoMethod(self, MUIM_TimeEditor_Edited);
}

IPTR TimeEditor__MUIM_TimeEditor_Zone(Class *CLASS, Object *self, Msg message)
{
    struct TimeEditor_DATA *data = INST_DATA(CLASS, self);
    show_zone_info(data);
    SET(self, MUIA_PrefsEditor_Changed, TRUE);
    return 0;
}

/* The zone file holds one zone name; empty means no zone. */
IPTR TimeEditor__MUIM_PrefsEditor_ImportFH(Class *CLASS, Object *self,
    struct MUIP_PrefsEditor_ImportFH *message)
{
    struct TimeEditor_DATA *data = INST_DATA(CLASS, self);
    char text[256];
    LONG n = Read(message->fh, text, sizeof(text) - 1);
    if (n < 0)
        return FALSE;
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r' || text[n - 1] == ' '))
        n--;
    text[n] = 0;
    data->edited = FALSE;
    select_zone(data, (CONST_STRPTR)text);
    return TRUE;
}

/* No zone file is a valid state, no zone, not a reason for defaults. */
IPTR TimeEditor__MUIM_PrefsEditor_ImportFromDirectory(Class *CLASS, Object *self,
    struct MUIP_PrefsEditor_ImportFromDirectory *message)
{
    struct TimeEditor_DATA *data = INST_DATA(CLASS, self);
    char path[64];
    BPTR lock;

    strlcpy(path, (const char *)message->directory, sizeof(path));
    AddPart((STRPTR)path, (CONST_STRPTR)ZONE_FILE, sizeof(path));
    if ((lock = Lock((CONST_STRPTR)path, SHARED_LOCK)))
    {
        UnLock(lock);
        return DoSuperMethodA(CLASS, self, (Msg)message);
    }
    if (IoErr() != ERROR_OBJECT_NOT_FOUND)
        return FALSE;
    data->edited = FALSE;
    select_zone(data, (CONST_STRPTR)"");
    return TRUE;
}

IPTR TimeEditor__MUIM_PrefsEditor_ExportFH(Class *CLASS, Object *self,
    struct MUIP_PrefsEditor_ExportFH *message)
{
    struct TimeEditor_DATA *data = INST_DATA(CLASS, self);
    CONST_STRPTR zone = selected_zone(data);
    LONG n = strlen((const char *)zone);
    if (n == 0)
        return TRUE;
    return Write(message->fh, (APTR)zone, n) == n && Write(message->fh, "\n", 1) == 1;
}

IPTR TimeEditor__MUIM_PrefsEditor_SetDefaults(Class *CLASS, Object *self, Msg message)
{
    struct TimeEditor_DATA *data = INST_DATA(CLASS, self);
    data->edited = FALSE;
    select_zone(data, (CONST_STRPTR)"");
    return TRUE;
}

static BOOL set_system_time(ULONG seconds)
{
    struct MsgPort *port = CreateMsgPort();
    struct timerequest *request = port ? (struct timerequest *)CreateIORequest(port, sizeof(*request)) : NULL;
    BOOL done = FALSE;

    if (request && OpenDevice("timer.device", UNIT_VBLANK, &request->tr_node, 0) == 0)
    {
        request->tr_node.io_Command = TR_SETSYSTIME;
        request->tr_time.tv_secs = seconds;
        request->tr_time.tv_micro = 0;
        done = DoIO(&request->tr_node) == 0;
        CloseDevice(&request->tr_node);
    }
    if (request)
        DeleteIORequest(&request->tr_node);
    if (port)
        DeleteMsgPort(port);
    return done;
}

static void run_quietly(CONST_STRPTR command)
{
    BPTR in = Open("NIL:", MODE_OLDFILE), out = Open("NIL:", MODE_NEWFILE);
    if (in && out)
    {
        if (SystemTags(command, SYS_Input, (IPTR)in, SYS_Output, (IPTR)out, TAG_DONE) != -1)
            in = out = BNULL;    /* SystemTags closed them */
    }
    if (in)
        Close(in);
    if (out)
        Close(out);
}

/* Use and Test write ENV: and apply; Save also writes ENVARC: and the
   battery-backed clock. No zone removes the file, so TimeZone stops. */
IPTR TimeEditor__MUIM_PrefsEditor_ExportToDirectory(Class *CLASS, Object *self,
    struct MUIP_PrefsEditor_ExportToDirectory *message)
{
    struct TimeEditor_DATA *data = INST_DATA(CLASS, self);
    BOOL applied = Stricmp(message->directory, (CONST_STRPTR)"ENV:") == 0;
    CONST_STRPTR zone = selected_zone(data);
    char path[64];
    BOOL success = TRUE;

    if (applied && data->edited)
    {
        if (!set_system_time(Date2Amiga(&data->shown)))
            return FALSE;
        data->edited = FALSE;
    }
    if (zone[0] == 0)
    {
        strlcpy(path, (const char *)message->directory, sizeof(path));
        AddPart((STRPTR)path, (CONST_STRPTR)ZONE_FILE, sizeof(path));
        if (!DeleteFile((CONST_STRPTR)path) && IoErr() != ERROR_OBJECT_NOT_FOUND)
            success = FALSE;
    }
    else
        success = DoSuperMethodA(CLASS, self, (Msg)message);
    if (!success)
        return FALSE;
    if (applied && zone[0])
        run_quietly((CONST_STRPTR)"C:TimeZone QUIET");
    if (!applied)
        run_quietly((CONST_STRPTR)"C:SetClock SAVE");
    show_zone_info(data);
    return TRUE;
}
