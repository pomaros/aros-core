/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1.
 */
#ifndef POSIXC_LANGINFO_H
#define POSIXC_LANGINFO_H

#include <langinfo.h>
#include <string.h>

/* newlocale and stdc setlocale support C and C.UTF-8. Their calendar,
 * monetary and numeric data are identical; only their codesets differ.
 * Immutable data also keeps an explicit-locale query independent of another
 * thread's global locale and of the lifetime of a temporary format buffer. */
static inline char *__posixc_langinfo(nl_item item, const char *name)
{
    static const char *const days[] = {"Sunday", "Monday", "Tuesday",
        "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char *const abdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *const months[] = {"January", "February", "March", "April",
        "May", "June", "July", "August", "September", "October", "November", "December"};
    static const char *const abmonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (item >= DAY_1 && item <= DAY_7)
        return (char *)days[item - DAY_1];
    if (item >= ABDAY_1 && item <= ABDAY_7)
        return (char *)abdays[item - ABDAY_1];
    if (item >= MON_1 && item <= MON_12)
        return (char *)months[item - MON_1];
    if (item >= ABMON_1 && item <= ABMON_12)
        return (char *)abmonths[item - ABMON_1];
    switch (item)
    {
    case CODESET: return (char *)(name && strcmp(name, "C.UTF-8") == 0 ? "UTF-8" : "US-ASCII");
    case D_T_FMT: return "%a %b %e %H:%M:%S %Y";
    case D_FMT: return "%m/%d/%y";
    case T_FMT: return "%H:%M:%S";
    case T_FMT_AMPM: return "%I:%M:%S %p";
    case AM_STR: return "AM";
    case PM_STR: return "PM";
    case RADIXCHAR: return ".";
    case YESEXPR: return "^[yY]";
    case NOEXPR: return "^[nN]";
    case YESSTR: return "yes";
    case NOSTR: return "no";
    /* The C locale has no currency symbol or thousands separator. Unknown
     * items likewise yield the empty string required by nl_langinfo. */
    default: return "";
    }
}

#endif
