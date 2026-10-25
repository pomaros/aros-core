/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License, version 1.1. */
#include "intuition_intern.h"
#include "windowfit.h"
#include "windowscale.h"
#include <proto/intuition.h>
#include <proto/exec.h>
#include <dos/dosextens.h>

struct FitWindowLayoutAction
{
    struct IntuiActionMsg msg;
    struct WindowLayoutFit layout;
    BOOL fitted;
};

static void fit_window_layout(struct FitWindowLayoutAction *action,
    struct IntuitionBase *IntuitionBase)
{
    ULONG lock = LockIBase(0);
    struct Window *window = action->layout.window;
    if (ResourceExisting(window, RESOURCE_WINDOW, IntuitionBase))
    {
        struct IntScreen *screen = IS(window->WScreen);
        action->fitted = ApplyPreparedScreenScale(screen, screen->DInfo.dri_Font,
            screen->ScaleBaseFont, screen->Scale, &action->layout, NULL, IntuitionBase);
    }
    UnlockIBase(lock);
}

/****************************************************************************
    NAME
        FitWindowLayout -- Fit an adapted window to measured layout minima.

    SYNOPSIS
        BOOL FitWindowLayout(struct Window *window,
            const struct ScreenRenderContext *context,
            LONG physicalMinWidth, LONG physicalMinHeight);

    FUNCTION
        Synchronously fit a WA_ScaleAware, WA_ScaleGeometry window to physical
        outer minima measured using exactly context. This raises the effective
        minimum and fits the current rectangle within the declared maximum and
        viewport. Canonical scale geometry and remembered user resizes are not
        changed. Minima belong only to this exact screen generation and are
        cleared by a successful screen scale or base-font change; recompute
        them when the context changes. No IDCMP_CHANGESCALE is emitted here.
        Ordinary geometry, size and exposure messages may be delivered.

    RESULT
        TRUE on committed fit, including a coherent unchanged rectangle.
        FALSE on a stale/wrong-screen context, unsupported window, impossible
        minimum, or preparation failure. Failure preserves the old scene,
        resources, canonical intent and effective minima.

    NOTES
        intuition.library V50.19. The caller owns Window/screen lifetime, holds
        no layer/IBase/GadgetLock and must be a Process. Prepare client layout
        resources before calling, then use the returned native dimensions.
        Window, gadgets and input capture retain their identities. Font and
        context pointers are borrowed; this call acquires/transfers no caller
        font ownership. It never changes the initial WA_RenderContext binding.
****************************************************************************/
AROS_LH4(BOOL, FitWindowLayout,
    AROS_LHA(struct Window *, window, A0),
    AROS_LHA(const struct ScreenRenderContext *, context, A1),
    AROS_LHA(LONG, physicalMinWidth, D0),
    AROS_LHA(LONG, physicalMinHeight, D1),
    struct IntuitionBase *, IntuitionBase, 164, Intuition)
{
    AROS_LIBFUNC_INIT
    struct FitWindowLayoutAction action;
    if (!window || !context || physicalMinWidth <= 0 || physicalMinHeight <= 0 ||
        physicalMinWidth > 32767 || physicalMinHeight > 32767 ||
        FindTask(NULL)->tc_Node.ln_Type != NT_PROCESS) return FALSE;
    action.layout = (struct WindowLayoutFit){window, context,
        physicalMinWidth, physicalMinHeight};
    action.fitted = FALSE;
    DoSyncAction((APTR)fit_window_layout, &action.msg, IntuitionBase);
    return action.fitted;
    AROS_LIBFUNC_EXIT
}
