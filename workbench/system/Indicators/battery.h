/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1 */
#ifndef INDICATOR_BATTERY_H
#define INDICATOR_BATTERY_H
#include <stddef.h>
#include <stdint.h>
struct IndicatorBattery {
    unsigned percent, adapter, charging, capacityValid, currentValid;
    uint32_t remainingMah;
    int32_t currentMa;
};
int IndicatorBatteryEvent(const struct IndicatorBattery *sample, char *output, size_t size);
#endif
