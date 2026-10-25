/*
    Copyright (C) 1995-2013, The AROS Development Team. All rights reserved.
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Copyright (C) 2001-2003, The MorphOS Development Team. All Rights Reserved.
*/

#include <proto/layers.h>
#include "showhide.h"
#include "intuition_intern.h"
#include "rendercontextbinding.h"
#include "windowcomposition.h"
#include "inputhandler_actions.h"

struct ShowWindowActionMsg
{
    struct IntuiActionMsg    msg;
    struct Window           *window;
    struct Window           *other;
    const struct ScreenRenderContext *expected;
    IPTR generation;
    BOOL performed;
};

static VOID int_showwindow(struct ShowWindowActionMsg *msg,
                           struct IntuitionBase *IntuitionBase);
static VOID int_showboundwindow(struct ShowWindowActionMsg *msg,
                           struct IntuitionBase *IntuitionBase);

/*****************************************************************************

    NAME */
#include <proto/intuition.h>

        AROS_LH2(BOOL, ShowWindow,

/*  SYNOPSIS */
        AROS_LHA(struct Window *, window, A0),
        AROS_LHA(struct Window *, other, A1),

/*  LOCATION */
        struct IntuitionBase *, IntuitionBase, 140, Intuition)

/*  FUNCTION
        Make a window visible. This function does not bring the
        window back into the visible area of the screen but rather
        switches it into visible state.

    INPUTS
        window - The window to affect.
        other - The window to place this window in front of, or
            WINDOW_FRONTMOST / WINDOW_BACKMOST.

    RESULT
        TRUE if the operation was performed or queued successfully,
        FALSE otherwise.

    NOTES
        This function is soure-compatible with AmigaOS v4.
        This function is also present in MorphOS v50, however
        considered private.

    EXAMPLE

    BUGS

    SEE ALSO
        HideWindow()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct ShowWindowActionMsg msg;

    DEBUG_SHOWWINDOW(dprintf("ShowWindow: Window 0x%p\n", window));
    SANITY_CHECKR(window, FALSE)

    msg.window = window;
    msg.other = other;
    msg.expected = NULL;
    msg.generation = 0;
    msg.performed = FALSE;
    ULONG lock = LockIBase(0);
    if (ResourceExisting(window, RESOURCE_WINDOW, IntuitionBase))
    {
        msg.expected = WindowBoundRenderContext(window);
        msg.generation = IW(window)->BoundWindowGeneration;
    }
    else
    {
        UnlockIBase(lock);
        return FALSE;
    }
    UnlockIBase(lock);
    if (msg.expected)
    {
        if (other != NULL) return FALSE;
        /* Explicit WA_RenderContext contract: caller holds no layer locks.
         * Preserve ordinary ShowWindow's async behavior for unbound windows. */
        DoSyncAction((APTR)int_showboundwindow, &msg.msg, IntuitionBase);
        return msg.performed;
    }
    return DoASyncAction((APTR)int_showwindow, &msg.msg, sizeof(msg), IntuitionBase);

    AROS_LIBFUNC_EXIT
} /* ShowWindow */

static VOID int_showwindow(struct ShowWindowActionMsg *msg,
                           struct IntuitionBase *IntuitionBase)
{
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    struct Window  *window = msg->window;
    struct Window  *other = msg->other;
#ifdef CGXSHOWHIDESUPPORT
    struct Library *CGXSystemBase;
    
    if (!ResourceExisting(window, RESOURCE_WINDOW, IntuitionBase)) return;
    
    SANITY_CHECK(window)
    if ((CGXSystemBase = OpenLibrary("cgxsystem.library", 0)))
    {
        CGXShowWindow(window);
        ((struct IntWindow *)(window))->specialflags &= ~SPFLAG_ICONIFIED;
        CloseLibrary(CGXSystemBase);
        ActivateWindow(window);
    }
#else
    struct Screen *screen;
    
    if (!ResourceExisting(window, RESOURCE_WINDOW, IntuitionBase)) return;
    screen = window->WScreen;

    if (!IsWindowVisible(window))
    {
        struct Requester *req;

        LOCK_REFRESH(screen);

        if (BLAYER(window))
        {
            ChangeLayerVisibility(BLAYER(window), TRUE);
        }
        ChangeLayerVisibility(WLAYER(window), TRUE);
        if (WindowCompositionLayer(window) != WLAYER(window))
            ChangeLayerVisibility(WindowCompositionLayer(window), TRUE);

        for (req = window->FirstRequest; req; req = req->OlderRequest)
        {
            ChangeLayerVisibility(req->ReqLayer, TRUE);
        }

        CheckLayers(screen, IntuitionBase);

        UNLOCK_REFRESH(screen);
    }
#endif

    if (other == WINDOW_FRONTMOST)
    {
        WindowToFront(window);
    }
    else if (other == WINDOW_BACKMOST)
    {
        WindowToBack(window);
    }
    else if ((other != window) &&
             ResourceExisting(other, RESOURCE_WINDOW, IntuitionBase) &&
             (other->WScreen == window->WScreen))
    {
        MoveWindowInFrontOf(window, other);
    }
}

/* Same ordering as prepared screen publication: IBase, then LayerInfo. The
 * expected client snapshot/cookie is checked through the actual visibility
 * mutation; a later scale commit delivers the already-subscribed notification. */
static VOID int_showboundwindow(struct ShowWindowActionMsg *msg,
                           struct IntuitionBase *IntuitionBase)
{
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    struct Window *window = msg->window;
    ULONG lock = LockIBase(0);
    msg->performed = FALSE;
    if (ResourceExisting(window, RESOURCE_WINDOW, IntuitionBase) &&
        WindowBoundRenderContext(window) == msg->expected &&
        IW(window)->BoundWindowGeneration == msg->generation)
    {
        struct Screen *screen = window->WScreen;
        LOCK_REFRESH(screen);
        if (BoundScreenRenderContext(msg->expected, screen))
        {
            struct Requester *req;
            if (BLAYER(window)) ChangeLayerVisibility(BLAYER(window), TRUE);
            ChangeLayerVisibility(WLAYER(window), TRUE);
            if (WindowCompositionLayer(window) != WLAYER(window))
                ChangeLayerVisibility(WindowCompositionLayer(window), TRUE);
            for (req = window->FirstRequest; req; req = req->OlderRequest)
                ChangeLayerVisibility(req->ReqLayer, TRUE);
            CheckLayers(screen, IntuitionBase);
            msg->performed = IsWindowVisible(window);
        }
        UNLOCK_REFRESH(screen);
    }
    UnlockIBase(lock);
}
