/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1 */
#include "subscriptions.h"
#include <string.h>
static int port_name(const char *p)
{
    unsigned i;
    if (!p || !*p) return 0;
    for (i=0;p[i];++i) if (i>=63 || !((p[i]>='A' && p[i]<='Z') ||
        (p[i]>='a' && p[i]<='z') || (p[i]>='0' && p[i]<='9') ||
        p[i]=='.' || p[i]=='_' || p[i]=='-')) return 0;
    return 1;
}
int IndicatorSubscribe(struct IndicatorSubscriber *subs, const char *port, unsigned pending)
{
    int slot=-1;
    unsigned i;
    if (!port_name(port)) return -1;
    for (i=0;i<IND_SUBSCRIBERS;++i) if (!strcmp(subs[i].port,port)) { slot=(int)i; break; }
    if (slot<0) for (i=0;i<IND_SUBSCRIBERS;++i)
        if (!subs[i].active && !(pending&(1U<<i))) { slot=(int)i; break; }
    if (slot<0) return -1;
    strcpy(subs[slot].port,port); subs[slot].active=1; subs[slot].dirty=1; subs[slot].failures=0;
    return slot;
}
int IndicatorUnsubscribe(struct IndicatorSubscriber *subs, const char *port)
{
    unsigned i;
    if (!port_name(port)) return 0;
    for (i=0;i<IND_SUBSCRIBERS;++i) if (subs[i].active && !strcmp(subs[i].port,port)) {
        subs[i].active=0; subs[i].dirty=0; return 1;
    }
    return 0;
}
int IndicatorSubscriptionDue(const struct IndicatorSubscriber *sub, uint32_t now, unsigned pending)
{
    return sub->active && !pending && (!sub->tried || (uint32_t)(now-sub->sent)>=250) &&
        (sub->dirty || (uint32_t)(now-sub->sent)>=2000);
}
void IndicatorSubscriptionMissing(struct IndicatorSubscriber *sub)
{
    if (!sub->active) return;
    sub->dirty=1;
    if (++sub->failures>=8) { sub->active=0; sub->dirty=0; }
}
