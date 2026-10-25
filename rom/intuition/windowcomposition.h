/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.
*/
#ifndef INTUITION_WINDOWCOMPOSITION_H
#define INTUITION_WINDOWCOMPOSITION_H

#include <exec/types.h>

struct Window;
struct Screen;
struct BitMap;
struct Layer;
struct Layer_Info;
struct IntuitionBase;
struct WindowComposition;
struct WindowCompositionScale;
struct WindowFitGeometry;
struct LayerGeometryRequest;
struct LayerGeometryBatch;
struct Region;

/* Lifecycle and geometry run on Intuition's serialized action task. Prepare
 * precedes logical layer creation; Activate follows it. Dispose follows logical
 * layer deletion and the caller's exclusion of further client drawing. Aware
 * windows and non-HIDD screens keep their ordinary drawing path. Allocation or
 * presentation-geometry failures return FALSE; never silently disable scaling. */
BOOL WindowCompositionPrepare(struct Window *, struct IntuitionBase *);
struct Layer_Info *WindowCompositionLayerInfo(struct Window *);
struct BitMap *WindowCompositionBitMap(struct Window *);
struct Layer *WindowCompositionLayer(struct Window *);
BOOL WindowCompositionActivate(struct Window *, struct IntuitionBase *, BOOL, ULONG);
BOOL WindowCompositionSync(struct Window *, struct IntuitionBase *);
/* Implemented by the Intuition family/layer integration. Called after a parent
 * commits successfully, outside the parent's screen lock; it recurses only
 * into direct children, never back to the parent. */
BOOL WindowCompositionSyncFamily(struct Window *, struct IntuitionBase *);
BOOL WindowCompositionPresent(struct Window *, struct IntuitionBase *);
BOOL WindowCompositionPresentScreen(struct Screen *, struct IntuitionBase *);
void WindowCompositionDispose(struct Window *, struct IntuitionBase *);
UWORD WindowCompositionFactor(struct Window *);
UWORD ScreenLegacyScaleFactor(struct Screen *);
UWORD ScreenLegacyScaleFactorForScale(struct Screen *, ULONG);
BOOL WindowCompositionFailed(struct Window *);
/* Parent must have its physical layer installed on the same screen. The caller
 * holds the screen Layer_Info lock while reading geometry. Child public origin
 * uses that child's units; the physical proxy retains the exact parent pixel
 * phase, including odd origins. Extents round down to stay inside the parent. */
BOOL WindowCompositionChildPosition(struct Window *, struct Screen *, BOOL,
    LONG, LONG, LONG *, LONG *);
BOOL WindowCompositionChildExtent(struct Window *, struct Screen *, BOOL,
    LONG *, LONG *);
/* Result is window-relative logical coordinates, before GZZ border subtraction.
 * Negative coordinates are floored, including captured drags outside the window. */
BOOL WindowCompositionMouse(struct Window *, LONG, LONG, LONG *, LONG *);
/* Snapshot the inverse input frame. Logical pixel q denotes the physical
 * interval [origin + q*factor, origin + (q+1)*factor - 1]. Invalid frames
 * return FALSE without modifying the caller outputs. */
BOOL WindowCompositionInputFrame(struct Window *, LONG *, LONG *, UWORD *);

/* Scale transactions use detached proxy generations. Prepare owns new shape
 * copies; Pixels consumes the staged logical canvas; Seal takes damageLock
 * after Layers has frozen physical/private stacks. Install is scalar-only.
 * Release must precede the Layers batch release (reverse lock order). */
struct WindowCompositionScale *PrepareWindowCompositionScale(struct Window *,
    ULONG, const struct WindowFitGeometry *, struct Region *,
    struct LayerGeometryRequest *, struct IntuitionBase *);
BOOL PrepareWindowCompositionPixels(struct WindowCompositionScale *,
    struct LayerGeometryBatch *, struct IntuitionBase *);
void SealWindowCompositionScale(struct WindowCompositionScale *);
void InstallWindowCompositionScale(struct WindowCompositionScale *, BOOL);
void ReleaseWindowCompositionScale(struct WindowCompositionScale *,
    struct IntuitionBase *);

#endif
