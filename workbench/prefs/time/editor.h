/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License, version 1.1. */
#ifndef TIME_EDITOR_H
#define TIME_EDITOR_H
#include <libraries/mui.h>
#include <utility/date.h>

#define MUIM_TimeEditor_Tick    (TAG_USER | 0x10590001)
#define MUIM_TimeEditor_Edited  (TAG_USER | 0x10590002)
#define MUIM_TimeEditor_Zone    (TAG_USER | 0x10590003)
#define MUIM_TimeEditor_Month   (TAG_USER | 0x10590004)
#define MUIM_TimeEditor_Year    (TAG_USER | 0x10590005)
#define MUIM_TimeEditor_Clock   (TAG_USER | 0x10590006)

#define TIME_ZONES_MAX 64

struct TimeEditor_DATA
{
    Object *month, *year, *calendar;
    Object *clock, *hour, *minute, *second;
    Object *zone, *zone_info;
    STRPTR *zone_labels;         /* "None", the zones, NULL; AllocVec'd */
    STRPTR *month_labels;        /* twelve names, NULL; same block */
    struct ClockData shown;      /* what the date and time gadgets hold */
    BOOL edited;                 /* the person changed the date or time */
    struct MUI_InputHandlerNode ticker;
    BOOL ticking;
};
#endif
