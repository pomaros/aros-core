/*
    Copyright (C) 1995-2013, The AROS Development Team. All rights reserved.
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Copyright (C) 2001-2003, The MorphOS Development Team. All Rights Reserved.
*/

#include <proto/layers.h>
#include "intuition_intern.h"
#include "windowcomposition.h"
#include "inputhandler_actions.h"

struct MoveWindowInFrontOfActionMsg
{
    struct IntuiActionMsg    msg;
    struct Window           *window;
    struct Window           *behindwindow;
};

static VOID int_movewindowinfrontof(struct MoveWindowInFrontOfActionMsg *msg,
                                    struct IntuitionBase *IntuitionBase);

/*****************************************************************************

    NAME */
#include <proto/intuition.h>

        AROS_LH2(void, MoveWindowInFrontOf,

/*  SYNOPSIS */
        AROS_LHA(struct Window *, window, A0),
        AROS_LHA(struct Window *, behindwindow, A1),

/*  LOCATION */
        struct IntuitionBase *, IntuitionBase, 80, Intuition)

/*  FUNCTION
        Arrange the relative depth of a window.

    INPUTS
        window - the window to reposition
        behindwindow - the window the other one will be brought in front of

    RESULT
        None.

    NOTES

    EXAMPLE

    BUGS

    SEE ALSO
        WindowToFront(), WindowToBack(),
        hyperlayers.library/MoveLayerInFrontOf()

    INTERNALS
        Uses layers.library/MoveLayerInFrontOf().

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct MoveWindowInFrontOfActionMsg msg;

    SANITY_CHECK(window)
    SANITY_CHECK(behindwindow)

    msg.window = window;
    msg.behindwindow = behindwindow;
    DoASyncAction((APTR)int_movewindowinfrontof, &msg.msg, sizeof(msg), IntuitionBase);

    AROS_LIBFUNC_EXIT

} /* MoveWindowInFrontOf */


static VOID int_movewindowinfrontof(struct MoveWindowInFrontOfActionMsg *msg,
                                    struct IntuitionBase *IntuitionBase)
{
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    struct Window       *window = msg->window;
    struct Window       *behindwindow = msg->behindwindow;
    struct Screen       *screen = window->WScreen;
    struct Requester    *req;
    struct Layer        *layer = WLAYER(window);
    
    struct Layer *behindlayer;

    if (!ResourceExisting(window, RESOURCE_WINDOW, IntuitionBase)) return;
    if (!ResourceExisting(behindwindow, RESOURCE_WINDOW, IntuitionBase)) return;
    if ((window == behindwindow) || (window->WScreen != behindwindow->WScreen)) return;

    LOCK_REFRESH(screen);

    /* Presentation layers share the screen LI. Logical layers belonging to
       a composed window live in that window's private LI, and are ordered
       only against its own border/requester layers. */
    behindlayer = WindowCompositionLayer(behindwindow);
    if (behindlayer == WLAYER(behindwindow))
    {
        for (req = behindwindow->FirstRequest; req; req = req->OlderRequest)
            if (req->ReqLayer)
            {
                behindlayer = req->ReqLayer;
                break;
            }
    }

    if (WindowCompositionLayer(window) != layer)
    {
        MoveLayerInFrontOf(WindowCompositionLayer(window), behindlayer);
        if (BLAYER(window)) UpfrontLayer(0, BLAYER(window));
        UpfrontLayer(0, layer);
    }
    else if (BLAYER(window))
    {
        MoveLayerInFrontOf(BLAYER(window), behindlayer);
        MoveLayerInFrontOf(layer, BLAYER(window));
    }
    else
        MoveLayerInFrontOf(layer, behindlayer);

    for (req = window->FirstRequest; req; req = req->OlderRequest)
        if (req->ReqLayer)
            MoveLayerInFrontOf(req->ReqLayer, layer);

    CheckLayers(screen, IntuitionBase);

    UNLOCK_REFRESH(screen);

    NotifyDepthArrangement(window, IntuitionBase);
}
