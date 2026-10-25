/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: TimeZone - local time from a zone rule, summer time included.
*/

/******************************************************************************

    NAME

        TimeZone

    SYNOPSIS

        ZONE,SAVE/S,SHOW/S,WATCH/S,NOSET/S,QUIET/S

    LOCATION

        C:

    FUNCTION

        Keeps the system clock in local time and the locale preferences'
        GMT offset in step with a time zone rule, so that summer time is
        applied and removed on its dates.

        The zone is a name such as Europe/Berlin, or a rule in the POSIX
        TZ form such as CET-1CEST,M3.5.0,M10.5.0/3. It is read from ZONE,
        else ENV:Sys/timezone, else ENVARC:Sys/timezone.

        With no switch, the command works out the current UTC time, the
        offset the rule gives for it, sets the system clock to local time
        and writes that offset into ENV:Sys/locale.prefs and
        ENVARC:Sys/locale.prefs (building the file from the zone's
        country data when there is none), then tells locale.library.

        UTC comes from the clock as SNTPSync or an earlier TimeZone left
        it (local time plus the locale offset), or, before either ran,
        from the kernel's record of the time it set from the RTC.

    INPUTS

        ZONE   --  the zone name or rule to use, instead of the saved one
        SAVE   --  store ZONE in ENVARC:Sys/timezone and ENV:Sys/timezone
        SHOW   --  report only; change nothing
        WATCH  --  keep running and apply each change of offset when its
                   moment comes (run it in the background); without ZONE
                   it also re-reads ENV:Sys/timezone each minute, so a
                   zone set, changed or removed later takes effect, and
                   it waits for one when none is set
        NOSET  --  update the preferences but leave the clock alone
        QUIET  --  no report

    RESULT

        RETURN_OK; RETURN_WARN when no zone is set; RETURN_ERROR when the
        zone does not parse or the preferences cannot be written.

    EXAMPLE

        TimeZone ZONE=Europe/Berlin SAVE
        TimeZone
        Run <NIL: >NIL: TimeZone WATCH

******************************************************************************/

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/resident.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <devices/timer.h>
#include <libraries/locale.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/locale.h>
#include <proto/timer.h>
#include <proto/kernel.h>

#include <string.h>
#include <stdio.h>

#include "tz_rule.h"

const char version[] = "$VER: TimeZone 1.0 (30.9.2026)";

#define TEMPLATE "ZONE,SAVE/S,SHOW/S,WATCH/S,NOSET/S,QUIET/S"
#define AMIGA_EPOCH 252460800LL            /* 1978-01-01 in Unix seconds */
#define ENV_ZONE "ENV:Sys/timezone"
#define ENVARC_ZONE "ENVARC:Sys/timezone"
#define ENV_PREFS "ENV:Sys/locale.prefs"
#define ENVARC_PREFS "ENVARC:Sys/locale.prefs"
#define ENV_APPLIED "ENV:TimeZone/Applied"
#define ENV_LASTSYNC "ENV:SNTPSync/LastSync"

/* The Apple kernel's record of the clock it set from the RTC
 * (aros/apple/startup.h); absent on other platforms and hosted builds. */
#define KATTR_COUNTER_FREQUENCY 0x83f10005u
#define KATTR_SYSTEM_TIME 0x83f1000du
#define SYSTEM_TIME_VERSION 1u
#define SYSTEM_TIME_RTC_SET 0x1u
struct system_time_record {
    uint32_t version, size, flags;
    int32_t rtc_status;
    uint64_t unix_seconds;
    uint64_t counter_at_set;
};

struct Args {
    STRPTR zone;
    IPTR save, show, watch, noset, quiet;
};

struct Device *TimerBase;
APTR KernelBase;
static struct timerequest *timer_request;
static struct MsgPort *timer_port;
static char zone_text[256];
static unsigned char file_buffer[8192];
static unsigned char country_buffer[4096];

static int open_timer(void)
{
    timer_port = CreateMsgPort();
    if (timer_port == NULL)
        return 0;
    timer_request = (struct timerequest *)CreateIORequest(timer_port, sizeof(*timer_request));
    if (timer_request == NULL)
        return 0;
    if (OpenDevice("timer.device", UNIT_VBLANK, (struct IORequest *)timer_request, 0) != 0) {
        DeleteIORequest((struct IORequest *)timer_request);
        timer_request = NULL;
        return 0;
    }
    TimerBase = timer_request->tr_node.io_Device;
    return 1;
}

static void close_timer(void)
{
    if (timer_request != NULL) {
        CloseDevice((struct IORequest *)timer_request);
        DeleteIORequest((struct IORequest *)timer_request);
    }
    if (timer_port != NULL)
        DeleteMsgPort(timer_port);
}

static LONG read_file(const char *name, unsigned char *buf, LONG cap)
{
    BPTR f = Open(name, MODE_OLDFILE);
    LONG got;
    if (f == BNULL)
        return -1;
    got = Read(f, buf, cap);
    Close(f);
    return got;
}

static int write_file(const char *name, const unsigned char *buf, LONG len)
{
    BPTR f = Open(name, MODE_NEWFILE);
    LONG put;
    if (f == BNULL)
        return -1;
    put = Write(f, buf, len);
    Close(f);
    return put == len ? 0 : -1;
}

static void ensure_dir(const char *name)
{
    BPTR lock = Lock(name, SHARED_LOCK);
    if (lock == BNULL)
        lock = CreateDir(name);
    if (lock != BNULL)
        UnLock(lock);
}

static int read_text(const char *name, char *out, size_t size)
{
    LONG got = read_file(name, (unsigned char *)out, (LONG)size - 1);
    if (got <= 0)
        return -1;
    out[got] = '\0';
    while (got > 0 && (out[got - 1] == '\n' || out[got - 1] == '\r' || out[got - 1] == ' '))
        out[--got] = '\0';
    return got > 0 ? 0 : -1;
}

/* Minutes west of UTC, as the locale preferences hold them now. */
static LONG locale_offset_west(void)
{
    struct Library *LocaleBase = OpenLibrary("locale.library", 51);
    struct Locale *locale;
    LONG minutes = 0;
    if (LocaleBase == NULL)
        return 0;
    locale = OpenLocale(NULL);
    if (locale != NULL) {
        minutes = locale->loc_GMTOffset;
        CloseLocale(locale);
    }
    CloseLibrary(LocaleBase);
    return minutes;
}

static uint64_t read_counter(void)
{
#if defined(__aarch64__)
    uint64_t v;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
#else
    return 0;
#endif
}

/* The kernel's own record: UTC now = the seconds it set plus the counter
 * time elapsed since. 0 when there is no such record. */
static int64_t utc_from_kernel(void)
{
    IPTR p, freq;
    const struct system_time_record *k;
    uint64_t now;
    if (KernelBase == NULL)
        return 0;
    p = KrnGetSystemAttr(KATTR_SYSTEM_TIME);
    freq = KrnGetSystemAttr(KATTR_COUNTER_FREQUENCY);
    if (p == (IPTR)-1 || p == 0 || freq == (IPTR)-1 || freq == 0)
        return 0;
    k = (const struct system_time_record *)p;
    if (k->version != SYSTEM_TIME_VERSION || k->size < sizeof *k ||
            !(k->flags & SYSTEM_TIME_RTC_SET) || k->unix_seconds == 0)
        return 0;
    now = read_counter();
    if (now < k->counter_at_set)
        return 0;
    return (int64_t)k->unix_seconds + (int64_t)((now - k->counter_at_set) / (uint64_t)freq);
}

static int64_t clock_local_now(ULONG *micro)
{
    struct timeval tv;
    GetSysTime(&tv);
    if (micro)
        *micro = tv.tv_micro;
    return (int64_t)tv.tv_secs + AMIGA_EPOCH;
}

/* UTC now. The clock holds local time in the locale's offset once
 * SNTPSync or this command has run; before that, the kernel's record is
 * exact, and the clock is what it left, which was UTC. */
static int64_t utc_now(int *source)
{
    BPTR lock;
    int64_t t;
    lock = Lock(ENV_APPLIED, SHARED_LOCK);
    if (lock == BNULL)
        lock = Lock(ENV_LASTSYNC, SHARED_LOCK);
    if (lock != BNULL) {
        UnLock(lock);
        *source = 'p';
        return clock_local_now(NULL) + (int64_t)locale_offset_west() * 60;
    }
    t = utc_from_kernel();
    if (t != 0) {
        *source = 'k';
        return t;
    }
    *source = 'c';
    return clock_local_now(NULL) + (int64_t)locale_offset_west() * 60;
}

static int set_clock(int64_t local_seconds)
{
    ULONG micro;
    clock_local_now(&micro);
    timer_request->tr_node.io_Command = TR_SETSYSTIME;
    timer_request->tr_time.tv_secs = (ULONG)(local_seconds - AMIGA_EPOCH);
    timer_request->tr_time.tv_micro = micro;
    timer_request->tr_node.io_Flags = IOF_QUICK;
    DoIO((struct IORequest *)timer_request);
    return timer_request->tr_node.io_Error == 0 ? 0 : -1;
}

/* Write the offset into both locale.prefs files and hand the new
 * preferences to locale.library, the way IPrefs does. */
static int store_offset(LONG west, const char *country, char *error, size_t size)
{
    LONG len = read_file(ENV_PREFS, file_buffer, sizeof file_buffer);
    struct Library *LocaleBase;
    struct Locale *fresh;
    if (len <= 0)
        len = read_file(ENVARC_PREFS, file_buffer, sizeof file_buffer);
    if (len > 0 && tz_prefs_patch(file_buffer, (size_t)len, west) != 0)
        len = 0;
    if (len <= 0) {
        char name[128];
        LONG clen;
        /* LOCALE: is assigned by the standard startup; a bare one has
         * only SYS:Locale. */
        snprintf(name, sizeof name, "LOCALE:Countries/%s.country", country ? country : "united_states");
        clen = read_file(name, country_buffer, sizeof country_buffer);
        if (clen <= 0) {
            snprintf(name, sizeof name, "SYS:Locale/Countries/%s.country", country ? country : "united_states");
            clen = read_file(name, country_buffer, sizeof country_buffer);
        }
        if (clen <= 0) {
            snprintf(error, size, "no locale.prefs and no %s", name);
            return -1;
        }
        len = (LONG)tz_prefs_build(file_buffer, sizeof file_buffer, country ? country : "united_states",
                                   country_buffer, (size_t)clen, west);
        if (len == 0) {
            snprintf(error, size, "%s has no country chunk", name);
            return -1;
        }
    }
    ensure_dir("ENV:Sys");
    ensure_dir("ENVARC:Sys");
    if (write_file(ENV_PREFS, file_buffer, len) != 0) {
        snprintf(error, size, "cannot write %s", ENV_PREFS);
        return -1;
    }
    if (write_file(ENVARC_PREFS, file_buffer, len) != 0) {
        snprintf(error, size, "cannot write %s", ENVARC_PREFS);
        return -1;
    }
    LocaleBase = OpenLibrary("locale.library", 51);
    if (LocaleBase != NULL) {
        fresh = OpenLocale(ENV_PREFS);
        if (fresh != NULL) {
            CloseLocale(LocalePrefsUpdate(fresh));
            CloseLocale(fresh);
        }
        CloseLibrary(LocaleBase);
    }
    return 0;
}

/* "<utc> <seconds east> <abbreviation>": the last line is what the
 * desktop widget shows (CEST, CET...). */
static void record_applied(int64_t utc, long east, const char *abbreviation)
{
    char line[64];
    LONG n;
    ensure_dir("ENV:TimeZone");
    n = snprintf(line, sizeof line, "%lld %ld %s\n", (long long)utc, east,
                 abbreviation && abbreviation[0] ? abbreviation : "-");
    if (n > 0)
        write_file(ENV_APPLIED, (const unsigned char *)line, n);
}

static void report(const struct Args *args, const char *zone, const struct tz_rule *rule,
                   int64_t utc, long east, int dst, int source, int set)
{
    char off[16], local[32], next[32] = "";
    int64_t change;
    if (args->quiet)
        return;
    tz_format_offset(east, off, sizeof off);
    tz_format_time(utc + east, local, sizeof local);
    change = tz_next_transition(rule, utc);
    if (change)
        tz_format_time(change, next, sizeof next);
    Printf("[tz] %s %s UTC%s%s; local %s (utc from %s)%s\n",
           (IPTR)zone, (IPTR)(dst ? rule->dst_name : rule->std_name), (IPTR)off,
           (IPTR)(dst ? " summer time" : ""), (IPTR)local,
           (IPTR)(source == 'k' ? "the kernel's RTC record" : source == 'p' ? "the clock and the locale offset" : "the clock"),
           (IPTR)(set ? "; clock set" : ""));
    if (change)
        Printf("[tz] next change at %s UTC\n", (IPTR)next);
}

/* The rule and country of a zone name or POSIX rule; 0 when it parses. */
static int load_zone(const char *text, struct tz_rule *rule, const char **country)
{
    const char *spec;
    *country = NULL;
    if (tz_lookup(text, &spec, country) != 0)
        spec = text;
    return tz_parse(spec, rule);
}

/* Applies the rule at this instant: the locale offset, the clock unless
 * NOSET, and the record the desktop widget reads. */
static int apply(const struct Args *args, const char *zone, const struct tz_rule *rule,
                 const char *country, long *applied_east)
{
    char error[160];
    int64_t utc;
    long east;
    int dst, source, set = 0;

    utc = utc_now(&source);
    east = tz_offset_at(rule, utc, &dst);
    if (store_offset(-(LONG)(east / 60), country, error, sizeof error) != 0) {
        Printf("[tz] %s\n", (IPTR)error);
        return RETURN_ERROR;
    }
    if (!args->noset)
        set = set_clock(utc + east) == 0;
    record_applied(utc, east, dst ? rule->dst_name : rule->std_name);
    report(args, zone, rule, utc, east, dst, source, set);
    *applied_east = east;
    return RETURN_OK;
}

int main(void)
{
    IPTR argv[6] = { 0 };
    struct RDArgs *rda;
    struct Args args;
    struct tz_rule rule;
    const char *country = NULL;
    char watched[sizeof zone_text];
    int64_t utc;
    long east, applied_east = 0;
    int dst, source, have_zone, result = RETURN_OK;
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR window = me->pr_WindowPtr;

    rda = ReadArgs(TEMPLATE, argv, NULL);
    if (rda == NULL) {
        PrintFault(IoErr(), "TimeZone");
        return RETURN_FAIL;
    }
    me->pr_WindowPtr = (APTR)-1;    /* a missing volume is an error, never a requester */
    args.zone = (STRPTR)argv[0];
    args.save = argv[1]; args.show = argv[2]; args.watch = argv[3];
    args.noset = argv[4]; args.quiet = argv[5];

    if (args.zone != NULL) {
        strlcpy(zone_text, (const char *)args.zone, sizeof zone_text);
        have_zone = 1;
    } else
        have_zone = read_text(ENV_ZONE, zone_text, sizeof zone_text) == 0 ||
                    read_text(ENVARC_ZONE, zone_text, sizeof zone_text) == 0;
    if (!have_zone && !args.watch) {
        if (!args.quiet)
            Printf("[tz] no zone set: the clock stays as it is (TimeZone ZONE=Europe/Berlin SAVE)\n");
        me->pr_WindowPtr = window;
        FreeArgs(rda);
        return RETURN_WARN;
    }
    if (have_zone && load_zone(zone_text, &rule, &country) != 0) {
        Printf("[tz] %s is neither a known zone nor a POSIX rule\n", (IPTR)zone_text);
        me->pr_WindowPtr = window;
        FreeArgs(rda);
        return RETURN_ERROR;
    }
    if (have_zone && args.save) {
        char line[260];
        LONG n = snprintf(line, sizeof line, "%s\n", zone_text);
        ensure_dir("ENV:Sys");
        ensure_dir("ENVARC:Sys");
        if (write_file(ENVARC_ZONE, (const unsigned char *)line, n) != 0 ||
                write_file(ENV_ZONE, (const unsigned char *)line, n) != 0) {
            Printf("[tz] cannot save the zone\n");
            me->pr_WindowPtr = window;
            FreeArgs(rda);
            return RETURN_ERROR;
        }
    }
    if (!open_timer()) {
        Printf("[tz] timer.device is not available\n");
        close_timer();
        me->pr_WindowPtr = window;
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    KernelBase = OpenResource("kernel.resource");

    if (have_zone && args.show) {
        utc = utc_now(&source);
        east = tz_offset_at(&rule, utc, &dst);
        report(&args, zone_text, &rule, utc, east, dst, source, 0);
    } else if (have_zone)
        result = apply(&args, zone_text, &rule, country, &applied_east);

    /* WATCH follows the rule's changes and, unless ZONE was given, the zone
     * file itself: a zone set, changed or removed by the Time page takes
     * effect within a minute, and with no zone the clock is left alone. */
    while (args.watch && result == RETURN_OK) {
        Delay(50 * 60);
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
            break;
        if (args.zone == NULL) {
            int now_set = read_text(ENV_ZONE, watched, sizeof watched) == 0;
            if (!now_set) {
                have_zone = 0;
                continue;
            }
            if (!have_zone || strcmp(watched, zone_text) != 0) {
                struct tz_rule next;
                const char *next_country;
                if (load_zone(watched, &next, &next_country) != 0) {
                    if (!args.quiet)
                        Printf("[tz] %s is neither a known zone nor a POSIX rule\n", (IPTR)watched);
                    have_zone = 0;
                    strlcpy(zone_text, watched, sizeof zone_text);
                    continue;
                }
                strlcpy(zone_text, watched, sizeof zone_text);
                rule = next;
                country = next_country;
                have_zone = 1;
                if (apply(&args, zone_text, &rule, country, &applied_east) != RETURN_OK && !args.quiet)
                    Printf("[tz] %s not applied\n", (IPTR)zone_text);
                continue;
            }
        }
        if (!have_zone)
            continue;
        utc = utc_now(&source);
        east = tz_offset_at(&rule, utc, &dst);
        if (east != applied_east)
            apply(&args, zone_text, &rule, country, &applied_east);
    }
    close_timer();
    me->pr_WindowPtr = window;
    FreeArgs(rda);
    return result;
}
