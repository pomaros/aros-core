/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: Time zone rules in the POSIX TZ form, and the locale.prefs
          offset they produce.
*/
#include "tz_rule.h"

#include <string.h>
#include <stdio.h>

static const struct {
    const char *name, *rule, *country;
} zones[] = {
    { "UTC",                 "UTC0",                                  "great_britain" },
    { "Europe/London",       "GMT0BST,M3.5.0/1,M10.5.0",             "great_britain" },
    { "Europe/Dublin",       "GMT0IST,M3.5.0/1,M10.5.0",             "ireland" },
    { "Europe/Lisbon",       "WET0WEST,M3.5.0/1,M10.5.0",            "portugal" },
    { "Europe/Berlin",       "CET-1CEST,M3.5.0,M10.5.0/3",           "germany" },
    { "Europe/Paris",        "CET-1CEST,M3.5.0,M10.5.0/3",           "france" },
    { "Europe/Zurich",       "CET-1CEST,M3.5.0,M10.5.0/3",           "switzerland" },
    { "Europe/Vienna",       "CET-1CEST,M3.5.0,M10.5.0/3",           "austria" },
    { "Europe/Amsterdam",    "CET-1CEST,M3.5.0,M10.5.0/3",           "netherlands" },
    { "Europe/Brussels",     "CET-1CEST,M3.5.0,M10.5.0/3",           "belgium" },
    { "Europe/Madrid",       "CET-1CEST,M3.5.0,M10.5.0/3",           "spain" },
    { "Europe/Rome",         "CET-1CEST,M3.5.0,M10.5.0/3",           "italy" },
    { "Europe/Stockholm",    "CET-1CEST,M3.5.0,M10.5.0/3",           "sweden" },
    { "Europe/Warsaw",       "CET-1CEST,M3.5.0,M10.5.0/3",           "poland" },
    { "Europe/Athens",       "EET-2EEST,M3.5.0/3,M10.5.0/4",         "greece" },
    { "Europe/Helsinki",     "EET-2EEST,M3.5.0/3,M10.5.0/4",         "finland" },
    { "Europe/Moscow",       "MSK-3",                                 "russia" },
    { "America/New_York",    "EST5EDT,M3.2.0,M11.1.0",               "united_states" },
    { "America/Chicago",     "CST6CDT,M3.2.0,M11.1.0",               "united_states" },
    { "America/Denver",      "MST7MDT,M3.2.0,M11.1.0",               "united_states" },
    { "America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0",               "united_states" },
    { "America/Toronto",     "EST5EDT,M3.2.0,M11.1.0",               "canada" },
    { "America/Sao_Paulo",   "<-03>3",                                "brazil" },
    { "Asia/Tokyo",          "JST-9",                                 "japan" },
    { "Asia/Shanghai",       "CST-8",                                 "china" },
    { "Asia/Kolkata",        "IST-5:30",                              "india" },
    { "Australia/Sydney",    "AEST-10AEDT,M10.1.0,M4.1.0/3",         "australia" },
    { "Pacific/Auckland",    "NZST-12NZDT,M9.5.0,M4.1.0/3",          "new_zealand" },
};

const char *tz_zone_name(unsigned index)
{
    return index < sizeof zones / sizeof zones[0] ? zones[index].name : NULL;
}

int tz_lookup(const char *name, const char **rule, const char **country)
{
    size_t i;
    for (i = 0; i < sizeof zones / sizeof zones[0]; i++)
        if (strcmp(zones[i].name, name) == 0) {
            *rule = zones[i].rule;
            *country = zones[i].country;
            return 0;
        }
    return -1;
}

/* ---- parsing ------------------------------------------------------------ */

static int is_alpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static const char *parse_name(const char *p, char *out)
{
    size_t n = 0;
    if (*p == '<') {
        p++;
        while (*p && *p != '>') {
            if (n < TZ_NAME_MAX - 1) out[n++] = *p;
            p++;
        }
        if (*p != '>') return NULL;
        p++;
    } else {
        while (is_alpha(*p)) {
            if (n < TZ_NAME_MAX - 1) out[n++] = *p;
            p++;
        }
    }
    out[n] = '\0';
    return n >= 3 ? p : NULL;
}

static const char *parse_number(const char *p, int *value, int max_digits)
{
    int v = 0, digits = 0;
    while (*p >= '0' && *p <= '9' && digits < max_digits) {
        v = v * 10 + (*p - '0');
        p++; digits++;
    }
    if (digits == 0) return NULL;
    *value = v;
    return p;
}

/* [+|-]hh[:mm[:ss]] as seconds; the sign is the caller's business. */
static const char *parse_hms(const char *p, long *seconds, int allow_sign)
{
    int sign = 1, h, m = 0, s = 0;
    if (allow_sign && (*p == '+' || *p == '-')) {
        if (*p == '-') sign = -1;
        p++;
    }
    p = parse_number(p, &h, 3);
    if (!p || h > 167) return NULL;
    if (*p == ':') {
        p = parse_number(p + 1, &m, 2);
        if (!p || m > 59) return NULL;
        if (*p == ':') {
            p = parse_number(p + 1, &s, 2);
            if (!p || s > 59) return NULL;
        }
    }
    *seconds = sign * ((long)h * 3600 + m * 60 + s);
    return p;
}

static const char *parse_date(const char *p, struct tz_date *d)
{
    int v;
    d->time = 2 * 3600;
    if (*p == 'M') {
        d->form = 'M';
        p = parse_number(p + 1, &d->month, 2);
        if (!p || d->month < 1 || d->month > 12 || *p != '.') return NULL;
        p = parse_number(p + 1, &d->week, 1);
        if (!p || d->week < 1 || d->week > 5 || *p != '.') return NULL;
        p = parse_number(p + 1, &d->day, 1);
        if (!p || d->day > 6) return NULL;
    } else if (*p == 'J') {
        d->form = 'J';
        p = parse_number(p + 1, &v, 3);
        if (!p || v < 1 || v > 365) return NULL;
        d->day = v;
    } else {
        d->form = 'n';
        p = parse_number(p, &v, 3);
        if (!p || v > 365) return NULL;
        d->day = v;
    }
    if (*p == '/') {
        p = parse_hms(p + 1, &d->time, 1);
        if (!p) return NULL;
    }
    return p;
}

int tz_parse(const char *spec, struct tz_rule *out)
{
    const char *p = spec;
    long off;
    if (!spec || !out) return -1;
    memset(out, 0, sizeof *out);
    p = parse_name(p, out->std_name);
    if (!p) return -1;
    p = parse_hms(p, &off, 1);
    if (!p) return -1;
    out->std_offset = -off;             /* POSIX counts west; we count east */
    out->dst_offset = out->std_offset;
    if (*p == '\0') return 0;
    if (*p == ',') return -1;           /* a rule needs a daylight name */
    p = parse_name(p, out->dst_name);
    if (!p) return -1;
    out->has_dst = 1;
    out->dst_offset = out->std_offset + 3600;
    if (*p != ',' && *p != '\0') {
        p = parse_hms(p, &off, 1);
        if (!p) return -1;
        out->dst_offset = -off;
    }
    if (*p == '\0') {
        /* No rule: POSIX says the US rule applies. */
        return tz_parse_rule_default(out);
    }
    if (*p != ',') return -1;
    p = parse_date(p + 1, &out->start);
    if (!p || *p != ',') return -1;
    p = parse_date(p + 1, &out->end);
    if (!p || *p != '\0') return -1;
    return 0;
}

int tz_parse_rule_default(struct tz_rule *out)
{
    out->start.form = 'M'; out->start.month = 3;  out->start.week = 2; out->start.day = 0; out->start.time = 7200;
    out->end.form = 'M';   out->end.month = 11;   out->end.week = 1;   out->end.day = 0;   out->end.time = 7200;
    return 0;
}

/* ---- calendar ----------------------------------------------------------- */

int64_t tz_days_from_civil(int y, int m, int d)
{
    int64_t era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void tz_civil_from_days(int64_t z, int *y, int *m, int *d)
{
    int64_t era, doe, yoe, doy, mp, yy;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    yy = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*m <= 2));
}

static int is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int weekday(int64_t days)      /* 0 = Sunday */
{
    int64_t w = (days + 4) % 7;
    return (int)(w < 0 ? w + 7 : w);
}

/* The day in a year a rule names, as days since 1970-01-01. */
static int64_t rule_day(const struct tz_date *r, int year)
{
    int64_t jan1 = tz_days_from_civil(year, 1, 1);
    if (r->form == 'M') {
        static const int mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        int64_t first = tz_days_from_civil(year, r->month, 1);
        int len = mdays[r->month - 1] + (r->month == 2 && is_leap(year));
        int64_t d = first + ((r->day - weekday(first)) + 7) % 7 + (int64_t)(r->week - 1) * 7;
        while (d >= first + len) d -= 7;
        return d;
    }
    if (r->form == 'J')
        return jan1 + r->day - 1 + (is_leap(year) && r->day >= 60);
    return jan1 + r->day;
}

/* The UTC instant of a transition in a year: the rule's local time, read in
 * the offset in force just before it. */
static int64_t transition_utc(const struct tz_date *r, int year, long offset_before)
{
    return rule_day(r, year) * 86400 + r->time - offset_before;
}

static int in_dst(const struct tz_rule *rule, int64_t utc, int year)
{
    int64_t start = transition_utc(&rule->start, year, rule->std_offset);
    int64_t end = transition_utc(&rule->end, year, rule->dst_offset);
    if (start < end)
        return utc >= start && utc < end;
    return !(utc >= end && utc < start);     /* southern hemisphere */
}

long tz_offset_at(const struct tz_rule *rule, int64_t utc, int *dst)
{
    int y, m, d, on;
    if (!rule->has_dst) {
        if (dst) *dst = 0;
        return rule->std_offset;
    }
    /* The local year may differ from the UTC year at the edges; the rule's
     * own year is the one in which local time falls. */
    tz_civil_from_days((utc + rule->std_offset) / 86400 - ((utc + rule->std_offset) % 86400 < 0), &y, &m, &d);
    on = in_dst(rule, utc, y);
    if (dst) *dst = on;
    return on ? rule->dst_offset : rule->std_offset;
}

int64_t tz_next_transition(const struct tz_rule *rule, int64_t utc)
{
    int y, m, d, i;
    int64_t best = 0;
    if (!rule->has_dst) return 0;
    tz_civil_from_days(utc / 86400 - (utc % 86400 < 0), &y, &m, &d);
    for (i = -1; i <= 1 && !best; i++) {
        int64_t a = transition_utc(&rule->start, y + i, rule->std_offset);
        int64_t b = transition_utc(&rule->end, y + i, rule->dst_offset);
        int64_t lo = a < b ? a : b, hi = a < b ? b : a;
        if (lo > utc) best = lo;
        else if (hi > utc) best = hi;
    }
    return best;
}

void tz_format_offset(long seconds_east, char *out, size_t size)
{
    long a = seconds_east < 0 ? -seconds_east : seconds_east;
    snprintf(out, size, "%c%02ld:%02ld", seconds_east < 0 ? '-' : '+', a / 3600, a / 60 % 60);
}

void tz_format_time(int64_t t, char *out, size_t size)
{
    int y, m, d;
    int64_t days = t / 86400 - (t % 86400 < 0), s = t - days * 86400;
    tz_civil_from_days(days, &y, &m, &d);
    snprintf(out, size, "%04d-%02d-%02d %02d:%02d:%02d", y, m, d,
             (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
}

/* ---- locale.prefs --------------------------------------------------------- */

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put_be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

/* The LCLE chunk's data offset and size inside a FORM PREF, or -1. */
static long find_chunk(const unsigned char *buf, size_t len, const char *id, size_t *size)
{
    size_t at = 12;
    if (len < 12 || memcmp(buf, "FORM", 4) || memcmp(buf + 8, "PREF", 4)) return -1;
    while (at + 8 <= len) {
        size_t n = be32(buf + at + 4);
        if (memcmp(buf + at, id, 4) == 0) {
            if (at + 8 + n > len) return -1;
            *size = n;
            return (long)(at + 8);
        }
        at += 8 + n + (n & 1);
    }
    return -1;
}

int tz_prefs_patch(unsigned char *buf, size_t len, long west)
{
    size_t n;
    long at = find_chunk(buf, len, "LCLE", &n);
    if (at < 0 || n < TZ_LCLE_COUNTRY) return -1;
    put_be32(buf + at + TZ_LCLE_GMTOFFSET, (uint32_t)(int32_t)west);
    put_be32(buf + at + TZ_LCLE_FLAGS,
             be32(buf + at + TZ_LCLE_FLAGS) | (uint32_t)TZ_LCLE_FLAG_GMT_CLOCK);
    return 0;
}

int tz_prefs_read(const unsigned char *buf, size_t len, long *west)
{
    size_t n;
    long at = find_chunk(buf, len, "LCLE", &n);
    if (at < 0 || n < TZ_LCLE_COUNTRY) return -1;
    *west = (int32_t)be32(buf + at + TZ_LCLE_GMTOFFSET);
    return 0;
}

size_t tz_prefs_build(unsigned char *out, size_t cap, const char *region,
                      const unsigned char *country, size_t country_len, long west)
{
    size_t csize, lcle, total, at = 0;
    long cat = find_chunk(country, country_len, "CTRY", &csize);
    if (cat < 0) return 0;
    lcle = TZ_LCLE_COUNTRY + csize;
    total = 12 + 8 + 6 + 8 + lcle + (lcle & 1);
    if (cap < total) return 0;
    memset(out, 0, total);
    memcpy(out, "FORM", 4); put_be32(out + 4, (uint32_t)(total - 8)); memcpy(out + 8, "PREF", 4);
    at = 12;
    memcpy(out + at, "PRHD", 4); put_be32(out + at + 4, 6); at += 8;
    out[at] = 0;                        /* ph_Version = PHV_CURRENT */
    at += 6;
    memcpy(out + at, "LCLE", 4); put_be32(out + at + 4, (uint32_t)lcle); at += 8;
    if (region) {
        size_t rn = strlen(region);
        memcpy(out + at + 16, region, rn < 31 ? rn : 31);
    }
    put_be32(out + at + TZ_LCLE_GMTOFFSET, (uint32_t)(int32_t)west);
    put_be32(out + at + TZ_LCLE_FLAGS, (uint32_t)TZ_LCLE_FLAG_GMT_CLOCK);
    memcpy(out + at + TZ_LCLE_COUNTRY, country + cat, csize);
    return total;
}
