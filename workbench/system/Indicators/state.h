/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1 */
#ifndef INDICATOR_STATE_H
#define INDICATOR_STATE_H
#include <stdint.h>
#include <stddef.h>
enum IndicatorKind { IND_BATTERY, IND_NETWORK, IND_BLUETOOTH, IND_SOUND };
#define IND_FIELD_VALUE 1U
#define IND_FIELD_LINK 2U
#define INDICATOR_TEXT 192
#define INDICATOR_LEASE_MS 4000U
struct IndicatorState {
    uint32_t received;
    unsigned value, charging, available, fields;
    char text[INDICATOR_TEXT];
};
/* Provider event: "<0..100> <0|1> <single line>". Invalid events never renew. */
int IndicatorUpdate(struct IndicatorState *state, const char *event, uint32_t now);
int IndicatorApply(struct IndicatorState *state, enum IndicatorKind kind,
                   const char *event, uint32_t now);
int IndicatorTypedGet(const struct IndicatorState *state, enum IndicatorKind kind,
                      const char *command, uint32_t now, char *out, size_t size);
int IndicatorSetParse(enum IndicatorKind kind, const char *command, unsigned *value);
int IndicatorHealthy(const struct IndicatorState *state, uint32_t now);
void IndicatorAnchor(int x, int y, int screenWidth, int screenHeight,
                     int width, int height, int *left, int *top);
#endif
