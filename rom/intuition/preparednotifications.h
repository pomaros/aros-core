/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    This file is part of AROS, distributed under the AROS Public License 1.1.
*/
#ifndef INTUITION_PREPAREDNOTIFICATIONS_H
#define INTUITION_PREPAREDNOTIFICATIONS_H

#include <exec/types.h>
struct Window;
struct IntuitionBase;
struct PreparedNotifications;

#define PNF_SCALE     (1UL << 0)
#define PNF_GEOMETRY  (1UL << 1)
#define PNF_NEWSIZE   (1UL << 2)
#define PNF_REFRESH   (1UL << 3)

struct PreparedNotificationSpec
{
    struct Window *window;
    ULONG flags;
    UWORD scale;
};

/* Private Intuition API. The input action coordinator must keep windows,
 * IDCMP subscriptions and ports stable from Prepare through Enqueue/Release.
 * Prepare allocates but never publishes or consumes the input reuse list.
 * Fill samples final mouse/time/qualifier fields without allocation.
 * Enqueue is the irreversible final publication, once only, without callbacks,
 * region work, allocation or frees. Release is required after enqueue as well
 * as on abort; unsent messages are freed, sent messages retain ReplyMsg ownership.
 * NEWSIZE includes the unconditional IECLASS_EVENT/IECODE_NEWSIZE notification.
 */
struct PreparedNotifications *PreparedNotificationsPrepare(
    const struct PreparedNotificationSpec *specs, ULONG count,
    struct IntuitionBase *IntuitionBase);
void PreparedNotificationsFill(struct PreparedNotifications *batch,
    struct IntuitionBase *IntuitionBase);
void PreparedNotificationsEnqueue(struct PreparedNotifications *batch,
    struct IntuitionBase *IntuitionBase);
void PreparedNotificationsRelease(struct PreparedNotifications *batch,
    struct IntuitionBase *IntuitionBase);
#endif
