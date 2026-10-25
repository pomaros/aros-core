/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    This file is part of AROS, distributed under the AROS Public License 1.1.
*/
#include "intuition_intern.h"
#include "inputhandler.h"
#include "preparednotifications.h"
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/execlock.h>
#include <proto/intuition.h>

/* Six possible notifications per window: scale, geometry, newsize,
 * unconditional newsize input event, refresh, unconditional refresh input event. Each owns exactly one message
 * or generated event, never an event borrowed from another input producer.
 */
struct PreparedNotification
{
    struct Window *window;
    struct IntuiMessage *message;
    struct GeneratedInputEvent *event;
};
struct PreparedNotifications
{
    struct IIHData *handler;
    ULONG count;
    BOOL filled;
    BOOL enqueued;
    struct PreparedNotification items[1];
};

void PreparedNotificationsRelease(struct PreparedNotifications *batch,
    struct IntuitionBase *IntuitionBase)
{
    ULONG i;
    if (!batch) return;
    for (i = 0; i < batch->count; ++i)
    {
        if (batch->items[i].message)
            FreeIntuiMessage(batch->items[i].message);
        if (batch->items[i].event)
            FreePooled(batch->handler->InputEventMemPool,
                batch->items[i].event, sizeof(struct GeneratedInputEvent));
    }
    FreeVec(batch);
}

static BOOL reserve_notification(struct PreparedNotifications *batch,
    struct Window *window, ULONG message_class, UWORD code, UBYTE event_class,
    struct IntuitionBase *IntuitionBase)
{
    struct PreparedNotification *item = &batch->items[batch->count++];
    item->window = window;
    if (message_class && window->UserPort &&
        (window->IDCMPFlags & message_class))
    {
        item->message = AllocIntuiMessage(window);
        if (!item->message) return FALSE;
        item->message->Class = message_class;
        item->message->Code = code;
        item->message->IAddress = message_class == IDCMP_CHANGESCALE ?
            (APTR)window->WScreen : (APTR)window;
    }
    else if (event_class)
    {
        item->event = AllocPooled(batch->handler->InputEventMemPool,
            sizeof(struct GeneratedInputEvent));
        if (!item->event) return FALSE;
        memclr(item->event, sizeof(struct GeneratedInputEvent));
        item->event->ie.ie_Class = event_class;
        item->event->ie.ie_Code = code;
        item->event->ie.ie_EventAddress = window;
    }
    return TRUE;
}

/* Match WindowNeedsRefresh's queue coalescing while preparation is still
 * fallible. The coordinator holds the layer freeze: a recipient taking the
 * pending refresh cannot complete BeginRefresh until the new damage is live.
 * No queued message is removed or modified here. */
static BOOL refresh_pending(struct Window *window, struct IntuitionBase *IntuitionBase)
{
    struct IntuiMessage *message;
    BOOL found = FALSE;
#if defined(__AROSEXEC_SMP__)
    struct ExecLockBase *ExecLockBase = GetPrivIBase(IntuitionBase)->ExecLockBase;
#endif
    if (!window->UserPort || !(window->IDCMPFlags & IDCMP_REFRESHWINDOW)) return FALSE;
    Forbid();
#if defined(__AROSEXEC_SMP__)
    if (ExecLockBase) ObtainLock(&window->UserPort->mp_SpinLock, SPINLOCK_MODE_READ, 0);
#endif
    ForeachNode(&window->UserPort->mp_MsgList, message)
        if (message->Class == IDCMP_REFRESHWINDOW && message->IAddress == window)
        { found = TRUE; break; }
#if defined(__AROSEXEC_SMP__)
    if (ExecLockBase) ReleaseLock(&window->UserPort->mp_SpinLock, 0);
#endif
    Permit();
    return found;
}

struct PreparedNotifications *PreparedNotificationsPrepare(
    const struct PreparedNotificationSpec *specs, ULONG count,
    struct IntuitionBase *IntuitionBase)
{
    struct PreparedNotifications *batch;
    ULONG i;
    IPTR size, capacity;
    if ((!specs && count) ||
        !GetPrivIBase(IntuitionBase)->InputHandler) return NULL;
    /* Check before multiplying, including on 32-bit targets. */
    if (count > ((ULONG)-1) / 6 || (IPTR)count > (((IPTR)-1 - sizeof(*batch)) /
        sizeof(struct PreparedNotification)) / 6) return NULL;
    capacity = (IPTR)count * 6;
    size = sizeof(*batch) + capacity * sizeof(struct PreparedNotification);
    batch = AllocVec(size, MEMF_PUBLIC | MEMF_CLEAR);
    if (!batch) return NULL;
    batch->handler = (struct IIHData *)
        GetPrivIBase(IntuitionBase)->InputHandler->is_Data;
    if (!batch->handler) goto fail;
    for (i = 0; i < count; ++i)
    {
        struct Window *window = specs[i].window;
        ULONG flags = specs[i].flags;
        if (!window || (flags & ~(PNF_SCALE | PNF_GEOMETRY |
            PNF_NEWSIZE | PNF_REFRESH))) goto fail;
        if ((flags & PNF_SCALE) && !reserve_notification(batch, window,
            IDCMP_CHANGESCALE, specs[i].scale, 0, IntuitionBase)) goto fail;
        if ((flags & PNF_GEOMETRY) && !reserve_notification(batch, window,
            IDCMP_CHANGEWINDOW, CWCODE_MOVESIZE, IECLASS_CHANGEWINDOW,
            IntuitionBase)) goto fail;
        if (flags & PNF_NEWSIZE)
        {
            if (!reserve_notification(batch, window, IDCMP_NEWSIZE, 0,
                IECLASS_SIZEWINDOW, IntuitionBase)) goto fail;
            if (!reserve_notification(batch, window, 0, IECODE_NEWSIZE,
                IECLASS_EVENT, IntuitionBase)) goto fail;
        }
        if ((flags & PNF_REFRESH) && !refresh_pending(window, IntuitionBase))
        {
            if (!reserve_notification(batch, window, IDCMP_REFRESHWINDOW, 0,
                IECLASS_REFRESHWINDOW, IntuitionBase)) goto fail;
            if (!reserve_notification(batch, window, 0, IECODE_REFRESH,
                IECLASS_EVENT, IntuitionBase)) goto fail;
        }
    }
    return batch;
fail:
    PreparedNotificationsRelease(batch, IntuitionBase);
    return NULL;
}

void PreparedNotificationsFill(struct PreparedNotifications *batch,
    struct IntuitionBase *IntuitionBase)
{
    ULONG i, seconds, micros;
    if (!batch || batch->enqueued) return;
    CurrentTime(&seconds, &micros);
    for (i = 0; i < batch->count; ++i)
    {
        struct PreparedNotification *item = &batch->items[i];
        struct Window *window = item->window;
        if (item->message)
        {
            struct IntuiMessage *message = item->message;
            message->Qualifier = batch->handler->ActQualifier;
            if (window->IDCMPFlags & IDCMP_DELTAMOVE)
            {
                message->MouseX = IW(window)->CompositionMouseDeltaX;
                message->MouseY = IW(window)->CompositionMouseDeltaY;
            }
            else
            {
                message->MouseX = window->MouseX;
                message->MouseY = window->MouseY;
            }
            message->Seconds = seconds;
            message->Micros = micros;
        }
        if (item->event)
        {
            item->event->ie.ie_Qualifier = batch->handler->ActQualifier;
            item->event->ie.ie_TimeStamp.tv_secs = seconds;
            item->event->ie.ie_TimeStamp.tv_micro = micros;
        }
    }
    batch->filled = TRUE;
}

void PreparedNotificationsEnqueue(struct PreparedNotifications *batch,
    struct IntuitionBase *IntuitionBase)
{
    ULONG i;
    (void)IntuitionBase;
    if (!batch || !batch->filled || batch->enqueued) return;
    /* Match ordinary SendIntuiMessage's port/close exclusion and bookkeeping,
     * restricted to the prepared transaction notification classes (refresh
     * coalescing was observed during preparation).
     * Unlike its MorphOS dead-task branch, this path cannot redraw or invoke
     * client callbacks. The coordinator has already completed all pixel work.
     */
    Forbid();
    for (i = 0; i < batch->count; ++i)
    {
        struct PreparedNotification *item = &batch->items[i];
        if (item->message)
        {
            struct Window *window = item->window;
            struct IntuiMessage *message = item->message;
            if (message->Qualifier & IEQUALIFIER_REPEAT)
                IW(window)->num_repeatevents++;
            IW(window)->lastmsgsent.tv_secs = message->Seconds;
            IW(window)->lastmsgsent.tv_micro = message->Micros;
            item->message = NULL; /* ownership passes to normal ReplyMsg path */
            if (window->UserPort)
            {
                PutMsg(window->UserPort, &message->ExecMessage);
                window->MessageKey = message;
            }
            else ReplyMsg(&message->ExecMessage);
        }
        if (item->event)
        {
            struct GeneratedInputEvent *event = item->event;
            item->event = NULL;
            AddTail((struct List *)&batch->handler->NewAllocatedInputEventList,
                (struct Node *)event);
            *batch->handler->EndInputEventChain = &event->ie;
            batch->handler->EndInputEventChain = &event->ie.ie_NextEvent;
        }
    }
    batch->enqueued = TRUE;
    Permit();
}
