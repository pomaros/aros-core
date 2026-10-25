/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License, version 1.1. */
#include "intuition_intern.h"
#include "activegadgetscale.h"
#include "inputhandler.h"
#include "gadgets.h"
#include "windowfit.h"
#include <proto/graphics.h>
#include <string.h>

BOOL PrepareActiveGadgetScale(struct Window *window, struct DrawInfo *drawinfo,
    const struct WindowFitGeometry *desired, LONG left, LONG top, LONG right, LONG bottom,
    struct PreparedActiveGadgetScale *prepared, struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct IIHData *input;
    struct Gadget *gadget;
    struct GadgetInfo source;
    struct Window metrics;
    struct Requester *requester;
    memset(prepared, 0, sizeof(*prepared));
    if (!IW(window)->ScaleAware || !GetPrivIBase(IntuitionBase)->InputHandler) return TRUE;
    input = GetPrivIBase(IntuitionBase)->InputHandler->is_Data;
    if (!input || !input->ActiveGadget || input->GadgetInfo.gi_Window != window) return TRUE;
    gadget = input->ActiveSysGadget ? input->ActiveSysGadget : input->ActiveGadget;
    if (IS_SCREEN_GADGET(gadget)) return TRUE;
    if (input->GadgetInfo.gi_Screen != window->WScreen ||
        input->GadgetInfo.gi_RastPort != &input->GadgetInfoRastPort ||
        !drawinfo || drawinfo->dri_Screen != window->WScreen || !drawinfo->dri_Font ||
        !desired || desired->width <= 0 || desired->height <= 0 ||
        desired->width > 32767 || desired->height > 32767 ||
        left < 0 || top < 0 || right < 0 || bottom < 0) return FALSE;
    requester = input->GadgetInfo.gi_Requester;
    if (requester)
    {
        struct Requester *found;
        for (found = window->FirstRequest; found && found != requester; found = found->OlderRequest) ;
        if (!found || !requester->ReqLayer) return FALSE;
    }
    else if (IS_REQ_GADGET(gadget)) return FALSE;
    /* Use exactly the normal activation raster routing. Layer/bitmap identities
     * survive the prepared scene transaction; only its geometry changes. */
    source = input->GadgetInfo;
    SET_GI_RPORT(&source, window, requester, gadget);
    if (!source.gi_RastPort || !source.gi_RastPort->Layer || !source.gi_RastPort->BitMap)
        return FALSE;
    /* GetGadgetDomain is pure and reads only these public metric fields. This
     * shadow is never passed to a public Window API or to a client method. */
    metrics = *window;
    metrics.Width = desired->width; metrics.Height = desired->height;
    metrics.BorderLeft = left; metrics.BorderTop = top;
    metrics.BorderRight = right; metrics.BorderBottom = bottom;
    if ((LONG)metrics.BorderLeft != left || (LONG)metrics.BorderTop != top ||
        (LONG)metrics.BorderRight != right || (LONG)metrics.BorderBottom != bottom) return FALSE;
    GetGadgetDomain(gadget, window->WScreen, &metrics, requester, &prepared->newDomain);
    if (prepared->newDomain.Width <= 0 || prepared->newDomain.Height <= 0) return FALSE;
    prepared->info = &input->GadgetInfo;
    prepared->raster = &input->GadgetInfoRastPort;
    prepared->oldDrawInfo = input->GadgetInfo.gi_DrInfo;
    prepared->newDrawInfo = drawinfo;
    prepared->oldDomain = input->GadgetInfo.gi_Domain;
    prepared->oldLayer = input->GadgetInfo.gi_Layer;
    prepared->newLayer = source.gi_RastPort->Layer;
    prepared->oldRaster = input->GadgetInfoRastPort;
    prepared->newRaster = input->GadgetInfoRastPort;
    prepared->newRaster.Layer = source.gi_RastPort->Layer;
    prepared->newRaster.BitMap = source.gi_RastPort->BitMap;
    SetFont(&prepared->newRaster, drawinfo->dri_Font);
    return TRUE;
}
void InstallActiveGadgetScale(struct PreparedActiveGadgetScale *prepared, BOOL rollback)
{
    if (!prepared || !prepared->info) return;
    prepared->info->gi_DrInfo = rollback ? prepared->oldDrawInfo : prepared->newDrawInfo;
    prepared->info->gi_Domain = rollback ? prepared->oldDomain : prepared->newDomain;
    prepared->info->gi_Layer = rollback ? prepared->oldLayer : prepared->newLayer;
    *prepared->raster = rollback ? prepared->oldRaster : prepared->newRaster;
}
