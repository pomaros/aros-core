/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1 */
#ifndef INDICATOR_CONSUMERS_H
#define INDICATOR_CONSUMERS_H
#include <stdint.h>
#include <stddef.h>
#define IND_CPU_TOP 3
#define IND_CPU_NAME 32
struct IndicatorConsumer { uint32_t usage; char name[IND_CPU_NAME]; };
struct IndicatorConsumers { struct IndicatorConsumer top[IND_CPU_TOP]; unsigned scanned, truncated; };
struct IndicatorAux { uint32_t received; unsigned available; char text[192]; };
/* Native task.resource adapter; caller opens TaskResBase. */
int IndicatorConsumersRead(struct IndicatorConsumers *result);
void IndicatorConsumerAdd(struct IndicatorConsumers *list, const char *name, uint32_t usage);
int IndicatorConsumersFormat(const struct IndicatorConsumers *list, char *out, size_t size);
int IndicatorAuxUpdate(struct IndicatorAux *aux, const char *text, uint32_t now);
const char *IndicatorAuxText(const struct IndicatorAux *aux, uint32_t now);
#endif
