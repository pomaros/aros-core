/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include "intuition_intern.h"
#include "menutask.h"
#include <proto/intuition.h>
#include <proto/exec.h>

void Characterize(struct Menu *menu);

/* This touches only the caller's detached candidate. All metadata derives
   from the supplied layout snapshot, even if the screen has since changed. */
static BOOL MenuStripContextDimensions(struct Menu *menu,
    const struct ScreenRenderContext *context, BOOL logical)
{
    ULONG count = 0;
    LONG hborder = logical ? context->src_LogicalMenuHBorder : context->src_PhysicalMenuHBorder;
    LONG vborder = logical ? context->src_LogicalMenuVBorder : context->src_PhysicalMenuVBorder;
    for (; menu; menu = menu->NextMenu)
    {
        struct MenuItem *item;
        LONG left = 32767, top = 32767, right = -32768, bottom = -32768;
        if (++count > 4096) return FALSE;
        if (!menu->FirstItem)
        { menu->JazzX = menu->JazzY = menu->BeatX = menu->BeatY = 0; continue; }
        for (item = menu->FirstItem; item; item = item->NextItem)
        {
            LONG r = (LONG)item->LeftEdge + item->Width - 1;
            LONG b = (LONG)item->TopEdge + item->Height - 1;
            if (++count > 4096 || item->Width <= 0 || item->Height <= 0 ||
                r > 32767 || b > 32767) return FALSE;
            if (item->LeftEdge < left) left = item->LeftEdge;
            if (item->TopEdge < top) top = item->TopEdge;
            if (r > right) right = r;
            if (b > bottom) bottom = b;
        }
        left -= hborder; right += hborder; top -= vborder; bottom += vborder;
        if (left < -32768 || top < -32768 || right > 32767 || bottom > 32767)
            return FALSE;
        menu->JazzX = left; menu->JazzY = top;
        menu->BeatX = right; menu->BeatY = bottom;
    }
    return TRUE;
}

/* Install an already laid-out detached strip using that exact immutable
   ScreenRenderContext. logical specifies its font/geometry units; it is
   independent of the owner Window's rendering domain. No context is queried
   here, and ExpectedRenderContext is never changed. A screen-scale race is
   handled when the menu task prepares its destination generation.

   On success an old strip is replaced atomically under MenuLock and may be
   freed by its owner. Failure leaves the installed strip/context untouched.
   Caller owns the Window/screen lifetime and must hold no IBase/layer lock. */
AROS_LH4(BOOL, SetMenuStripContext,
    AROS_LHA(struct Window *, window, A0),
    AROS_LHA(struct Menu *, menu, A1),
    AROS_LHA(const struct ScreenRenderContext *, context, A2),
    AROS_LHA(BOOL, logical, D0),
    struct IntuitionBase *, IntuitionBase, 163, Intuition)
{
    AROS_LIBFUNC_INIT
    SANITY_CHECKR(window, FALSE)
    if (!context || !context->src_LogicalDrawInfo || !context->src_PhysicalDrawInfo ||
        context->src_LogicalDrawInfo->dri_Screen != window->WScreen ||
        context->src_PhysicalDrawInfo->dri_Screen != window->WScreen ||
        context->src_Scale < 100 || context->src_Scale > 400 || context->src_Scale % 25 ||
        context->src_LegacyScaleFactor < 1 || context->src_LegacyScaleFactor > 4)
        return FALSE;
    if (!MenuStripContextDimensions(menu, context, logical) ||
        !EnsureDefaultMenuHandler(IntuitionBase)) return FALSE;
    ObtainSemaphore(&GetPrivIBase(IntuitionBase)->MenuLock);
    Characterize(menu);
    IW(window)->MenuRenderContext = context;
    IW(window)->MenuRenderLogical = !!logical;
    window->MenuStrip = menu;
    ReleaseSemaphore(&GetPrivIBase(IntuitionBase)->MenuLock);
    return TRUE;
    AROS_LIBFUNC_EXIT
}
