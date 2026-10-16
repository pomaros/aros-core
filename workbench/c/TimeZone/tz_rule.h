/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: Time zone rules in the POSIX TZ form, and the locale.prefs
          offset they produce. Pure C, no AROS dependency, so the host
          can test it.
*/
#ifndef TZ_RULE_H
#define TZ_RULE_H

#include <stddef.h>
#include <stdint.h>

#define TZ_NAME_MAX 8

struct tz_date {
    int form;           /* 'M' month.week.day, 'J' Julian without Feb 29, 'n' zero-based day */
    int month, week, day;
    long time;          /* seconds after local midnight, in the time in force before the change */
};

struct tz_rule {
    char std_name[TZ_NAME_MAX];
    char dst_name[TZ_NAME_MAX];
    long std_offset;    /* seconds east of UTC */
    long dst_offset;    /* seconds east of UTC while daylight time is in force */
    int has_dst;
    struct tz_date start, end;
};

/* std offset[dst[offset][,start[/time],end[/time]]]. Returns 0 on success. */
int tz_parse(const char *spec, struct tz_rule *out);
int tz_parse_rule_default(struct tz_rule *out);

/* A zone name such as Europe/Berlin: its rule and the AROS country file
 * that goes with it. Returns 0 when the name is known. */
int tz_lookup(const char *name, const char **rule, const char **country);

/* The offset east of UTC in force at a UTC instant, and whether daylight
 * time is in force then. */
long tz_offset_at(const struct tz_rule *rule, int64_t utc, int *dst);

/* The next UTC instant after utc at which the offset changes; 0 when it
 * never does. */
int64_t tz_next_transition(const struct tz_rule *rule, int64_t utc);

/* Civil date helpers shared with the command's report. */
int64_t tz_days_from_civil(int year, int month, int day);
void tz_civil_from_days(int64_t days, int *year, int *month, int *day);
void tz_format_offset(long seconds_east, char *out, size_t size);   /* "+02:00" */
void tz_format_time(int64_t seconds, char *out, size_t size);       /* "2026-09-30 12:00:00" */

/* locale.prefs is an IFF PREF file whose LCLE chunk is a struct
 * LocalePrefs, big-endian. lp_GMTOffset is minutes west of UTC and sits
 * 348 bytes into the chunk. */
#define TZ_LCLE_GMTOFFSET 348
#define TZ_LCLE_FLAGS 352
/* LOCF_GMT_CLOCK: the hardware clock keeps UTC, as the Apple PMIC does. */
#define TZ_LCLE_FLAG_GMT_CLOCK (1UL << 16)
#define TZ_LCLE_COUNTRY 356

/* Patch lp_GMTOffset in an existing file image. Returns 0 on success, -1
 * when the buffer is not a PREF form with an LCLE chunk of a plausible size. */
int tz_prefs_patch(unsigned char *buf, size_t len, long gmt_offset_west_minutes);

/* Read lp_GMTOffset back from a file image; -1 when absent. */
int tz_prefs_read(const unsigned char *buf, size_t len, long *gmt_offset_west_minutes);

/* Build a locale.prefs image from a country file's CTRY chunk (the bytes
 * of that chunk, as stored) and the offset. Returns the size written, or 0
 * when cap is too small or the chunk is absent. */
size_t tz_prefs_build(unsigned char *out, size_t cap, const char *region_name,
                      const unsigned char *country_file, size_t country_len,
                      long gmt_offset_west_minutes);

#endif
