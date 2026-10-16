/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1 */
#ifndef INDICATOR_SUBSCRIPTIONS_H
#define INDICATOR_SUBSCRIPTIONS_H
#include <stdint.h>
#define IND_SUBSCRIBERS 8
struct IndicatorSubscriber {
    char port[64]; unsigned active,dirty,tried,failures; uint32_t sent;
};
/* A pending bit includes timed-out messages whose ownership has not returned. */
int IndicatorSubscribe(struct IndicatorSubscriber *subs, const char *port, unsigned pending);
int IndicatorUnsubscribe(struct IndicatorSubscriber *subs, const char *port);
int IndicatorSubscriptionDue(const struct IndicatorSubscriber *sub, uint32_t now, unsigned pending);
void IndicatorSubscriptionMissing(struct IndicatorSubscriber *sub);
#endif
