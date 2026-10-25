/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1 */
#ifndef INDICATOR_BATTERY_SOURCE_H
#define INDICATOR_BATTERY_SOURCE_H
struct IndicatorBattery;
/* Return a fresh battery sample, or zero when unavailable. The caller owns
 * the kernel resource handle. The provider performs no event formatting. */
int IndicatorBatterySnapshot(struct IndicatorBattery *sample, void *kernel_base);
#endif
