/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1 */
#ifndef INDICATOR_WIFI_PANEL_H
#define INDICATOR_WIFI_PANEL_H
#include <exec/types.h>
#include <intuition/classes.h>
struct IndicatorWifi;
Object *IndicatorWifiCreate(struct IndicatorWifi **out);
void IndicatorWifiBind(struct IndicatorWifi *s, Object *app);
void IndicatorWifiStart(struct IndicatorWifi *s);
BOOL IndicatorWifiAction(struct IndicatorWifi *s, IPTR action);
BOOL IndicatorWifiStep(struct IndicatorWifi *s, ULONG now, ULONG signals, BOOL stop, char *event, ULONG size);
ULONG IndicatorWifiSignals(struct IndicatorWifi *s);
BOOL IndicatorWifiIdle(struct IndicatorWifi *s);
BOOL IndicatorWifiCommand(struct IndicatorWifi *s, const char *command, char *answer, ULONG size);
void IndicatorWifiFree(struct IndicatorWifi *s);
#endif
