/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1 */
#include "battery_source.h"

/* The virtual platform has no battery device. Never publish a fabricated
 * state when no platform provider can supply a measurement. */
int IndicatorBatterySnapshot(struct IndicatorBattery *sample, void *kernel_base)
{
    (void)sample;
    (void)kernel_base;
    return 0;
}
