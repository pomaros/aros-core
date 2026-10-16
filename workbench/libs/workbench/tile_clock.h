/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1.
    SPDX-License-Identifier: APL-1.1
*/
#ifndef WB_TILE_CLOCK_H
#define WB_TILE_CLOCK_H
#define __TIMER_NOLIBBASE__
#include <devices/timer.h>
#include <proto/timer.h>
/* ReadEClock is monotonic; changing the system date cannot revive a frame. */
static ULONG WBTileClock(struct timerequest *request)
{
    struct Device *TimerBase = request->tr_node.io_Device;
    struct EClockVal clock;
    ULONG frequency = ReadEClock(&clock);
    UQUAD ticks = ((UQUAD)clock.ev_hi << 32) | clock.ev_lo;
    if (!frequency) return 0;
    return (ULONG)((ticks / frequency) * 1000 +
                   (ticks % frequency) * 1000 / frequency);
}
#endif
