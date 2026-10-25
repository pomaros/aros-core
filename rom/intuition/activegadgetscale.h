/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License, version 1.1. */
#ifndef INTUITION_ACTIVEGADGETSCALE_H
#define INTUITION_ACTIVEGADGETSCALE_H
#include <intuition/intuition.h>
#include <graphics/rastport.h>
struct GadgetInfo;
struct WindowFitGeometry;
struct IntuitionBase;
/* Embedded transaction state: no allocation, font ownership or client state.
 * The input.device coordinator owns IBase -> screen/layer leases and serializes
 * activation/removal. Window/frame resources remain pinned through rollback. */
struct PreparedActiveGadgetScale
{
    struct GadgetInfo *info;
    struct RastPort *raster;
    struct DrawInfo *oldDrawInfo, *newDrawInfo;
    struct IBox oldDomain, newDomain;
    struct Layer *oldLayer, *newLayer;
    struct RastPort oldRaster, newRaster;
};
BOOL PrepareActiveGadgetScale(struct Window *, struct DrawInfo *,
    const struct WindowFitGeometry *, LONG, LONG, LONG, LONG,
    struct PreparedActiveGadgetScale *, struct IntuitionBase *);
/* Frame -> requester -> fit must already have installed/rolled back. */
void InstallActiveGadgetScale(struct PreparedActiveGadgetScale *, BOOL rollback);
#endif
